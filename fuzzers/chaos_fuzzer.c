#include "fuzz_utils.h"
#include <string.h>

#define MAX_PTRS 512
#define MAX_SUBS 4

// Supported sub-allocator types
typedef enum {
    SUB_NONE = 0,
    SUB_NESTED_EM,
    SUB_BUMP,
    SUB_SLAB
} SubType;

// Tracks active sub-allocator contexts
typedef struct {
    void *ptr;     
    SubType type;  
    int32_t padding;
} SubContext;

// Tracks individual allocated pointers and their origin
// to prevent Use-After-Free when a sub-allocator is destroyed.
typedef struct {
    void *p;
    int owner; // -1 for Root EM, 0..3 for subs array index
    int32_t padding;
} FuzzPtr;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Require minimum entropy
    if (size < 32) return 0;

    // Root arena configuration
    EM *root_em = em_create(1024 * 1024 * 2); // 2 MB Root
    if (!root_em) return 0;

    SubContext subs[MAX_SUBS] = {0};
    FuzzPtr ptrs[MAX_PTRS] = {0};
    size_t ptr_count = 0;

    size_t i = 0;
    int step = 0;

    FUZZ_LOG("\n--- STARTING CHAOS REPLAY ---\n");

    while (i < size) {
        // Safe byte reading to prevent out-of-bounds access in the fuzzer itself
        uint8_t op = fuzz_read_byte(data, &i, size) % 6; 
        step++;
        (void)step;

        switch (op) {
            case 0: { // CREATE SUB-ALLOCATOR
                uint8_t sub_idx = fuzz_read_byte(data, &i, size) % MAX_SUBS;
                if (subs[sub_idx].type != SUB_NONE) break; // Slot is occupied

                uint8_t type = (fuzz_read_byte(data, &i, size) % 3) + 1;
                size_t cap = (fuzz_read_size(data, &i, size) % 65536) + 1024;
                
                // Chance to create it in the Scratchpad zone
                bool is_scratch = (fuzz_read_byte(data, &i, size) % 4 == 0) && !em_get_has_scratch(root_em);

                FUZZ_LOG("[%d] CREATE SUB %d (Type %d, Scratch: %d, Cap: %zu)\n", step, sub_idx, type, is_scratch, cap);

                if (type == SUB_NESTED_EM) {
                    subs[sub_idx].ptr = is_scratch ? em_create_scratch(root_em, cap) : em_create_nested(root_em, cap);
                } else if (type == SUB_BUMP) {
                    subs[sub_idx].ptr = is_scratch ? em_bump_create_scratch(root_em, cap) : em_bump_create(root_em, cap);
                } else if (type == SUB_SLAB) {
                    size_t chunk = (fuzz_read_size(data, &i, size) % 256) + 8;
                    subs[sub_idx].ptr = is_scratch ? em_slab_create_scratch(root_em, cap, chunk) : em_slab_create(root_em, cap, chunk);
                }
                
                if (subs[sub_idx].ptr) subs[sub_idx].type = type;
                break;
            }
            case 1: { // DESTROY SUB-ALLOCATOR
                uint8_t sub_idx = fuzz_read_byte(data, &i, size) % MAX_SUBS;
                if (subs[sub_idx].type == SUB_NONE) break;

                FUZZ_LOG("[%d] DESTROY SUB %d\n", step, sub_idx);

                if (subs[sub_idx].type == SUB_NESTED_EM) em_destroy((EM*)subs[sub_idx].ptr);
                else if (subs[sub_idx].type == SUB_BUMP) em_bump_destroy((Bump*)subs[sub_idx].ptr);
                else if (subs[sub_idx].type == SUB_SLAB) em_slab_destroy((Slab*)subs[sub_idx].ptr);

                subs[sub_idx].type = SUB_NONE;
                subs[sub_idx].ptr = NULL;

                // FIX: Remove all dangling pointers associated with this sub-allocator
                // to prevent ASan Use-After-Free crashes in subsequent operations.
                for (size_t k = 0; k < ptr_count; ) {
                    if (ptrs[k].owner == sub_idx) {
                        ptrs[k] = ptrs[--ptr_count]; 
                    } else {
                        k++;
                    }
                }
                break;
            }
            case 2: { // ALLOCATE IN ROOT ARENA
                if (ptr_count >= MAX_PTRS) break;
                size_t alloc_sz = (fuzz_read_size(data, &i, size) % 4096) + 1;
                
                FUZZ_LOG("[%d] ROOT ALLOC: %zu\n", step, alloc_sz);
                void *p = em_alloc(root_em, alloc_sz);
                if (p) { 
                    memset(p, 0x11, alloc_sz); 
                    ptrs[ptr_count++] = (FuzzPtr){p, -1, 0}; 
                }
                break;
            }
            case 3: { // ALLOCATE IN SUB-ARENA
                uint8_t sub_idx = fuzz_read_byte(data, &i, size) % MAX_SUBS;
                if (subs[sub_idx].type == SUB_NONE || ptr_count >= MAX_PTRS) break;

                size_t alloc_sz = (fuzz_read_size(data, &i, size) % 1024) + 1;
                void *p = NULL;

                FUZZ_LOG("[%d] SUB %d ALLOC: %zu\n", step, sub_idx, alloc_sz);

                if (subs[sub_idx].type == SUB_NESTED_EM) {
                    p = em_alloc((EM*)subs[sub_idx].ptr, alloc_sz);
                } else if (subs[sub_idx].type == SUB_BUMP) {
                    p = em_bump_alloc((Bump*)subs[sub_idx].ptr, alloc_sz);
                } else if (subs[sub_idx].type == SUB_SLAB) {
                    p = em_slab_alloc((Slab*)subs[sub_idx].ptr);
                    alloc_sz = 8; // Arbitrary size for memset to avoid slab overflow
                }

                if (p) { 
                    memset(p, 0x22, alloc_sz); 
                    ptrs[ptr_count++] = (FuzzPtr){p, sub_idx, 0}; 
                }
                break;
            }
            case 4: { // FREE MEMORY
                if (ptr_count == 0) break;
                size_t idx = fuzz_read_byte(data, &i, size) % ptr_count;
                int owner = ptrs[idx].owner;
                void *p = ptrs[idx].p;
                
                FUZZ_LOG("[%d] FREE PTR %zu (Owner: %d)\n", step, idx, owner);

                if (owner == -1 || subs[owner].type == SUB_NESTED_EM) {
                    em_free(p);
                    ptrs[idx] = ptrs[--ptr_count]; 
                } 
                else if (subs[owner].type == SUB_SLAB) {
                    em_slab_free((Slab*)subs[owner].ptr, p);
                    ptrs[idx] = ptrs[--ptr_count]; 
                }
                // Individual block freeing is unsupported for Bump allocators.
                // We safely ignore this action.
                break;
            }
            case 5: { // RANDOM BUMP TRIM
                uint8_t sub_idx = fuzz_read_byte(data, &i, size) % MAX_SUBS;
                if (subs[sub_idx].type == SUB_BUMP) {
                    FUZZ_LOG("[%d] TRIM BUMP %d\n", step, sub_idx);
                    em_bump_trim((Bump*)subs[sub_idx].ptr);
                }
                break;
            }
        }
    }

    // Cleanup any surviving sub-allocators to prevent ASan leak reports
    for (int j = 0; j < MAX_SUBS; j++) {
        if (subs[j].type == SUB_NESTED_EM) em_destroy((EM*)subs[j].ptr);
        else if (subs[j].type == SUB_BUMP) em_bump_destroy((Bump*)subs[j].ptr);
        else if (subs[j].type == SUB_SLAB) em_slab_destroy((Slab*)subs[j].ptr);
    }

    em_destroy(root_em);
    return 0;
}