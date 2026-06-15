#include "fuzz_utils.h"
#include <string.h>

#define MAX_ALLOCS 512
#define MAX_MARKERS 16

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // We need enough bytes to initialize the stack and read at least one operation
    if (size < 12) return 0;

    size_t i = 0;
    int step = 0;
    (void)step;

    // Read the stack capacity dynamically (between 256 and 65536 bytes)
    size_t stack_capacity = (fuzz_read_size(data, &i, size) % 65280) + 256;

    EM *em = em_create(256 * 1024); // 256 KB parent arena
    if (!em) return 0;

    Stack *stack = em_stack_create(em, stack_capacity);
    if (!stack) {
        em_destroy(em);
        return 0;
    }

    // Local state tracking to maintain correct LIFO ordering
    void *allocations[MAX_ALLOCS] = {0};
    size_t alloc_sizes[MAX_ALLOCS] = {0};
    size_t alloc_count = 0;

    // Local marker tracking
    StackMarker markers[MAX_MARKERS];
    size_t marker_alloc_counts[MAX_MARKERS] = {0};
    size_t marker_count = 0;

    FUZZ_LOG("\n--- STARTING STACK REPLAY ---\n");
    FUZZ_LOG("Stack Capacity: %zu\n", stack_capacity);

    while (i < size) {
        // Safely extract the operation code
        uint8_t op = fuzz_read_byte(data, &i, size) % 6;
        step++;

        switch (op) {
            case 0: { // ALLOC / ALIGNED ALLOC
                if (alloc_count >= MAX_ALLOCS) break;

                size_t alloc_size = (fuzz_read_size(data, &i, size) % 256) + 1;
                
                // Determine alignment dynamically: 4, 8, 16, 32, 64, or 128
                size_t exponent = (fuzz_read_byte(data, &i, size) % 6) + 2; 
                size_t alignment = (size_t)1 << exponent;

                FUZZ_LOG("[%d] STACK ALLOC: size %zu, align %zu -> ", step, alloc_size, alignment);
                
                void *p = em_stack_alloc_aligned(stack, alloc_size, alignment);
                FUZZ_LOG("%p\n", p);

                if (p) {
                    // Dirty the allocated memory to verify write-safety
                    memset(p, 0xDD, alloc_size);
                    alloc_sizes[alloc_count] = alloc_size;
                    allocations[alloc_count++] = p;
                }
                break;
            }
            case 1: { // VALID LIFO FREE (POP)
                if (alloc_count == 0) break;

                // To maintain strict LIFO, we must always pop the top element
                size_t idx = --alloc_count;
                void *p = allocations[idx];

                FUZZ_LOG("[%d] STACK FREE (LIFO POP): index %zu (%p)\n", step, idx, p);
                em_stack_free(stack, p);
                allocations[idx] = NULL;
                break;
            }
            case 2: { // GET MARKER
                if (marker_count >= MAX_MARKERS) break;

                FUZZ_LOG("[%d] STACK GET MARKER\n", step);
                markers[marker_count] = em_stack_get_marker(stack);
                marker_alloc_counts[marker_count] = alloc_count;
                marker_count++;
                break;
            }
            case 3: { // FREE TO MARKER (ROLLBACK)
                if (marker_count == 0) break;

                // Pick a random active marker to roll back to safely
                size_t idx = fuzz_read_byte(data, &i, size) % marker_count;
                FUZZ_LOG("[%d] STACK ROLLBACK TO MARKER: idx %zu\n", step, idx);

                em_stack_free_to_marker(stack, markers[idx]);

                // Synchronize our local state with the rolled back stack state
                alloc_count = marker_alloc_counts[idx];
                
                // All markers created after the rolled-back state are now invalid
                marker_count = idx; 
                break;
            }
            case 4: { // RESET OR RESET ZERO
                bool use_zero = (fuzz_read_byte(data, &i, size) % 2) == 0;
                FUZZ_LOG("[%d] STACK RESET (zero=%d)\n", step, use_zero);

                if (use_zero) {
                    em_stack_reset_zero(stack);
                } else {
                    em_stack_reset(stack);
                }

                // Invalidate all allocations and markers
                alloc_count = 0;
                marker_count = 0;
                break;
            }
            case 5: { // INJECT CORRUPTIONS (Defensive Policy Testing)
                // This branch tests robustness against API misuse and garbage inputs.
                uint8_t garbage_op = fuzz_read_byte(data, &i, size) % 3;

                if (garbage_op == 0) { // LIFO Violation Free
                    if (alloc_count < 2) break;
                    // Attempt to free an element that is NOT the top element
                    size_t bad_idx = fuzz_read_byte(data, &i, size) % (alloc_count - 1);
                    FUZZ_LOG("[%d] JUNK: LIFO VIOLATION FREE of allocations[%zu]\n", step, bad_idx);
                    em_stack_free(stack, allocations[bad_idx]);
                } 
                else if (garbage_op == 1) { // Corrupted Marker Rollback
                    if (marker_count == 0) break;
                    size_t m_idx = fuzz_read_byte(data, &i, size) % marker_count;
                    StackMarker bad_marker = markers[m_idx];
                    bad_marker.magic ^= 0xAAAA; // Damage the signature
                    
                    FUZZ_LOG("[%d] JUNK: ROLLBACK TO CORRUPTED MARKER\n", step);
                    em_stack_free_to_marker(stack, bad_marker);
                }
                else if (garbage_op == 2) { // Allocation of extreme size
                    FUZZ_LOG("[%d] JUNK: HUGE ALLOC ATTEMPT\n", step);
                    void *p = em_stack_alloc(stack, SIZE_MAX);
                    (void)p;
                }
                break;
            }
        }
    }

    FUZZ_LOG("--- REPLAY FINISHED, DESTROYING EM ---\n");
    em_stack_destroy(stack);
    em_destroy(em);

    return 0;
}
