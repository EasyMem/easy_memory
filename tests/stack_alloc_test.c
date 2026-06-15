#define EASY_MEMORY_IMPLEMENTATION
#define EM_NO_ATTRIBUTES
#include "easy_memory.h"
#include "test_utils.h"
#include <limits.h>
#include <stdint.h>

static void test_stack_lifecycle_normal(void) {
    TEST_PHASE("Stack Lifecycle - Normal Path");
    
    EM *em = em_create(2048);
    size_t initial_free = free_size_in_tail(em);

    TEST_CASE("Standard Stack initialization");
    size_t stack_size = 128;
    Stack *stack = em_stack_create(em, stack_size);
    
    ASSERT(stack != NULL, "Stack pointer should not be NULL");
    ASSERT(stack_get_capacity(stack) >= stack_size, "Capacity should meet requested size");
    ASSERT(stack_get_em(stack) == em, "Parent EM should be correctly stored");
    ASSERT(stack_get_meta_index(stack) == 0, "Initial metadata index should be 0");
    
    ASSERT(stack_get_meta_type(stack) == 0, "Metadata type should be 0 (uint8_t) for small capacity");
    
    em_stack_destroy(stack);
    ASSERT(free_size_in_tail(em) == initial_free, "Parent EM should reclaim memory after Stack destruction");

    TEST_CASE("Stack Metadata Type Scaling");
    Stack *medium_stack = em_stack_create(em, 512);
    ASSERT(medium_stack != NULL, "Medium stack pointer should not be NULL");
    ASSERT(stack_get_meta_type(medium_stack) == 1, "Metadata type should be 1 (uint16_t) for medium capacity");
    
    em_stack_destroy(medium_stack);
    ASSERT(free_size_in_tail(em) == initial_free, "Parent EM should reclaim memory after medium Stack destruction");

    TEST_CASE("Scratch Stack initialization");
    Stack *scratch_stack = em_stack_create_scratch(em, stack_size);
    ASSERT(scratch_stack != NULL, "Scratch Stack should not be NULL");
    ASSERT(stack_get_em(scratch_stack) == em, "Parent EM of scratch stack should be correctly stored");
    ASSERT(em_get_has_scratch(em) == true, "Parent EM scratch flag should be active");
    
    em_stack_destroy(scratch_stack);
    ASSERT(em_get_has_scratch(em) == false, "Parent EM scratch flag should be inactive");
    ASSERT(free_size_in_tail(em) == initial_free, "Scratch tail should be restored");

    em_destroy(em);
}

static void test_stack_lifecycle_garbage(void) {
    TEST_PHASE("Stack Lifecycle - Sad Path & Garbage");
    
    EM *em = em_create(1024);

#if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
    TEST_CASE("Creation with NULL parent EM");
    ASSERT(em_stack_create(NULL, 128) == NULL, "Should fail on NULL parent");
    ASSERT(em_stack_create_scratch(NULL, 128) == NULL, "Should fail on NULL parent for scratch creation");

    TEST_CASE("Creation with zero size");
    ASSERT(em_stack_create(em, 0) == NULL, "Should fail on zero stack size");

    TEST_CASE("Creation with size too small");
    ASSERT(em_stack_create(em, 2) == NULL, "Should fail if requested size is below EM_MIN_BUFFER_SIZE");

    TEST_CASE("Creation with extreme OOM size");
    ASSERT(em_stack_create(em, 4096) == NULL, "Should return NULL if parent EM is exhausted");
    ASSERT(em_stack_create(em, SIZE_MAX) == NULL, "Should return NULL on size integer overflow");

    TEST_CASE("Scratch Stack conflict");
    Stack *scratch1 = em_stack_create_scratch(em, 128);
    ASSERT(scratch1 != NULL, "First scratch allocation should succeed");

    Stack *scratch2 = em_stack_create_scratch(em, 128);
    ASSERT(scratch2 == NULL, "Second scratch allocation must fail while another scratch is active");

    em_stack_destroy(scratch1);

    TEST_CASE("Destroy NULL Stack");
    em_stack_destroy(NULL);
    ASSERT(true, "Destroying NULL Stack should not crash");
#endif

    em_destroy(em);
}

static void test_stack_operations_normal(void) {
    TEST_PHASE("Stack Operations - Normal Path");

    EM *em = em_create(2048);
    size_t stack_size = 512;
    Stack *stack = em_stack_create(em, stack_size);

    void *ptrs[8];
    size_t count = 0;

    TEST_CASE("Sequential allocations with LIFO ordering");
    while (count < 4) {
        void *p = em_stack_alloc(stack, 64);
        ASSERT(p != NULL, "Allocation should succeed");
        ptrs[count++] = p;
        
        ASSERT_QUIET(((uintptr_t)p % EMMIN_ALIGNMENT) == 0, "Payload must be word-aligned");
        fill_memory_pattern(p, 64, (int)count);
    }

    // Since metadata grows from start and payloads grow backward from the end,
    // subsequent allocations should return strictly decreasing memory addresses.
    for (size_t i = 1; i < count; i++) {
        ASSERT_QUIET((uintptr_t)ptrs[i] < (uintptr_t)ptrs[i - 1], "Addresses must decrease sequentially");
    }

    // Verify written data remains valid and untouched
    for (size_t i = 0; i < count; i++) {
        ASSERT_QUIET(verify_memory_pattern(ptrs[i], 64, (int)(i + 1)), "Data integrity check failed");
    }

    TEST_CASE("Strict LIFO deallocation (popping)");
    // Pop elements in exact reverse order of allocation
    for (size_t i = count; i-- > 0;) {
        size_t prev_index = stack_get_meta_index(stack);
        em_stack_free(stack, ptrs[i]);
        ASSERT_QUIET(stack_get_meta_index(stack) == prev_index - 1, "Meta index must decrement after free");
        
#ifdef EM_POISONING
        // Check if the memory was poisoned upon deallocation
        ASSERT_QUIET(verify_memory_pattern(ptrs[i], 64, EM_POISON_BYTE), "Freed memory must be poisoned");
#endif
    }
    ASSERT(stack_get_meta_index(stack) == 0, "Stack must be empty after popping all elements");

    // Reset the stack to start aligned allocation tests
    em_stack_reset(stack);

    TEST_CASE("Custom alignment allocations");
    size_t alignments[] = {16, 32, 64, 128};
    for (size_t i = 0; i < 4; i++) {
        size_t align = alignments[i];
        void *p = em_stack_alloc_aligned(stack, 32, align);
        ASSERT(p != NULL, "Aligned allocation should succeed");
        ASSERT_QUIET(((uintptr_t)p % align) == 0, "Payload must satisfy requested custom alignment");
    }

    // Reset the stack to perform exhaustion test
    em_stack_reset(stack);

    TEST_CASE("Stack exhaustion");
    size_t capacity = stack_get_capacity(stack);
    size_t allocated_total = 0;
    
    while (true) {
        void *p = em_stack_alloc(stack, 32);
        if (!p) {
            break;
        }
        allocated_total += 32;
        ASSERT_QUIET(allocated_total <= capacity, "Allocated size cannot exceed capacity");
    }

    // Squeeze the remaining bytes with 1-byte allocations until absolutely full.
    // This handles differences in header sizes and alignments across 16, 32, and 64-bit systems.
    while (em_stack_alloc(stack, 1) != NULL) {
        // Keep squeezing
    }

    // Once exhausted, any further allocations must return NULL
    ASSERT(em_stack_alloc(stack, 1) == NULL, "Stack allocation should return NULL when exhausted");

    em_stack_destroy(stack);
    em_destroy(em);
}

static void test_stack_operations_garbage(void) {
#if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
    TEST_PHASE("Stack Operations - Sad Path & Garbage");

    EM *em = em_create(1024);
    Stack *stack = em_stack_create(em, 512);

    void *valid_ptr = em_stack_alloc(stack, 32);

    TEST_CASE("Allocation on NULL stack");
    ASSERT(em_stack_alloc(NULL, 16) == NULL, "Should return NULL on NULL stack");
    ASSERT(em_stack_alloc_aligned(NULL, 16, 16) == NULL, "Should return NULL on NULL stack with custom alignment");

    TEST_CASE("Allocation of zero size");
    ASSERT(em_stack_alloc(stack, 0) == NULL, "Should return NULL on zero size");
    ASSERT(em_stack_alloc_aligned(stack, 0, 16) == NULL, "Should return NULL on zero size with custom alignment");

    TEST_CASE("Allocation with invalid custom alignments");
    // Alignments must be powers of two
    ASSERT(em_stack_alloc_aligned(stack, 16, 3) == NULL, "Should fail on non-power-of-two alignment");
    ASSERT(em_stack_alloc_aligned(stack, 16, 15) == NULL, "Should fail on non-power-of-two alignment");
    
    // Check below minimum limit
    if (EMMIN_ALIGNMENT > 1) {
        ASSERT(em_stack_alloc_aligned(stack, 16, EMMIN_ALIGNMENT / 2) == NULL, "Should fail if alignment is too small");
    }

    // Check above maximum limit
    ASSERT(em_stack_alloc_aligned(stack, 16, EMMAX_ALIGNMENT * 2) == NULL, "Should fail if alignment is too large");

    TEST_CASE("Allocation with size larger than capacity");
    size_t capacity = stack_get_capacity(stack);
    ASSERT(em_stack_alloc(stack, capacity + 1) == NULL, "Should return NULL on allocation larger than capacity");
    ASSERT(em_stack_alloc(stack, SIZE_MAX) == NULL, "Should return NULL on overflow size");

    TEST_CASE("Freeing on NULL inputs");
    em_stack_free(NULL, valid_ptr);
    em_stack_free(stack, NULL);
    ASSERT(true, "Deallocating on NULL inputs should not crash");

    TEST_CASE("Freeing on empty stack");
    em_stack_reset(stack);
    em_stack_free(stack, valid_ptr);
    ASSERT(stack_get_meta_index(stack) == 0, "Meta index must remain 0 after illegal pop on empty stack");

    TEST_CASE("LIFO violation detection");
    void *p1 = em_stack_alloc(stack, 32);
    void *p2 = em_stack_alloc(stack, 32);

    // Attempting to free p1 first (which violates LIFO as p2 is the current head)
    size_t prev_index = stack_get_meta_index(stack);
    em_stack_free(stack, p1);
    ASSERT(stack_get_meta_index(stack) == prev_index, "Deallocating non-head pointer must be ignored");

    // Clean up correctly
    em_stack_free(stack, p2);
    em_stack_free(stack, p1);
    ASSERT(stack_get_meta_index(stack) == 0, "Stack must be successfully emptied using correct LIFO order");

    em_stack_destroy(stack);
    em_destroy(em);
#endif
}

static void test_stack_markers_normal(void) {
    TEST_PHASE("Stack Markers - Normal Path");

    EM *em = em_create(2048);
    Stack *stack = em_stack_create(em, 512);

    void *p1 = em_stack_alloc(stack, 32);
    fill_memory_pattern(p1, 32, 0x11);

    TEST_CASE("Get and rollback to stack markers");
    // Snapshot state after first allocation
    StackMarker marker1 = em_stack_get_marker(stack);

    void *p2 = em_stack_alloc(stack, 32);
    fill_memory_pattern(p2, 32, 0x22);
    void *p3 = em_stack_alloc(stack, 32);
    fill_memory_pattern(p3, 32, 0x33);

    // Snapshot state after three allocations
    StackMarker marker2 = em_stack_get_marker(stack);

    void *p4 = em_stack_alloc(stack, 32);
    fill_memory_pattern(p4, 32, 0x44);

    // Roll back to marker2 (this should release p4)
    em_stack_free_to_marker(stack, marker2);
    ASSERT(stack_get_meta_index(stack) == 3, "Stack index should revert to 3");

#ifdef EM_POISONING
    // Ensure the rolled-back region is poisoned
    ASSERT_QUIET(verify_memory_pattern(p4, 32, EM_POISON_BYTE), "Rolled back block must be poisoned");
#endif

    // Verify we can re-allocate on the freed space
    void *p4_retry = em_stack_alloc(stack, 32);
    ASSERT(p4_retry == p4, "Re-allocation must reclaim the freed space");

    // Roll back to marker1 (releasing p2, p3, p4_retry)
    em_stack_free_to_marker(stack, marker1);
    ASSERT(stack_get_meta_index(stack) == 1, "Stack index should revert to 1");

#ifdef EM_POISONING
    // Verify both released blocks are poisoned
    ASSERT_QUIET(verify_memory_pattern(p2, 32, EM_POISON_BYTE), "Rolled back blocks must be poisoned");
    ASSERT_QUIET(verify_memory_pattern(p3, 32, EM_POISON_BYTE), "Rolled back blocks must be poisoned");
#endif

    em_stack_destroy(stack);
    em_destroy(em);
}

static void test_stack_markers_garbage(void) {
#if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
    TEST_PHASE("Stack Markers - Sad Path & Garbage");

    EM *em = em_create(1024);
    Stack *stackA = em_stack_create(em, 256);
    Stack *stackB = em_stack_create(em, 256);

    TEST_CASE("NULL stack or NULL marker operations");
    // Requesting marker on NULL stack should return a zeroed marker structure
    StackMarker null_marker = em_stack_get_marker(NULL);
    ASSERT(null_marker.index == 0 && null_marker.magic == 0, "NULL stack marker must be zeroed");

    // Reverting with NULL should safely do nothing
    em_stack_free_to_marker(NULL, null_marker);
    ASSERT(true, "Rollback on NULL stack should safely return");

    TEST_CASE("Alien marker protection");
    em_stack_alloc(stackA, 32);
    StackMarker markerA = em_stack_get_marker(stackA);

    em_stack_alloc(stackB, 32);
    em_stack_alloc(stackB, 32);
    size_t index_before = stack_get_meta_index(stackB);

    // Attempting to apply Stack A's marker to Stack B (cross-contamination check)
    em_stack_free_to_marker(stackB, markerA);
    ASSERT(stack_get_meta_index(stackB) == index_before, "Alien marker must be detected and ignored");

    TEST_CASE("Corrupted marker magic protection");
    StackMarker corrupt_marker = em_stack_get_marker(stackB);
    corrupt_marker.magic ^= 0xDEAD; // corrupt the cryptographic validation signature

    em_stack_free_to_marker(stackB, corrupt_marker);
    ASSERT(stack_get_meta_index(stackB) == index_before, "Corrupted marker signature must be ignored");

    TEST_CASE("Forward marker index protection");
    StackMarker future_marker = em_stack_get_marker(stackB);
    // Artificially modify index to point to a future state (index > current_index)
    future_marker.index ^= (size_t)10; 

    em_stack_free_to_marker(stackB, future_marker);
    ASSERT(stack_get_meta_index(stackB) == index_before, "Rollback to future index must be ignored");

    em_stack_destroy(stackB);
    em_stack_destroy(stackA);
    em_destroy(em);
#endif
}

static void test_stack_resets(void) {
    TEST_PHASE("Stack Resets - Standard & Zero");

    EM *em = em_create(1024);
    size_t capacity = 256;
    Stack *stack = em_stack_create(em, capacity);

    TEST_CASE("Standard stack reset");
    void *p1 = em_stack_alloc(stack, 32);
    void *p2 = em_stack_alloc(stack, 32);
    fill_memory_pattern(p1, 32, 0xAA);
    fill_memory_pattern(p2, 32, 0xBB);

    em_stack_reset(stack);
    ASSERT(stack_get_meta_index(stack) == 0, "Reset must set metadata index to 0");

    // Standard reset only resets metadata index, letting us overwrite dirty memory
    void *p1_new = em_stack_alloc(stack, 32);
    ASSERT(p1_new == p1, "Standard reset must allow re-allocation from the start");

    TEST_CASE("Stack reset with zero-initialization");
    void *p2_new = em_stack_alloc(stack, 32);
    fill_memory_pattern(p1_new, 32, 0xCC);
    fill_memory_pattern(p2_new, 32, 0xDD);

    em_stack_reset_zero(stack);
    ASSERT(stack_get_meta_index(stack) == 0, "Reset-zero must set metadata index to 0");

    // Verify the entire physical payload area has been strictly cleared
    void *payload_start = (void *)((char *)stack + sizeof(Stack));
    size_t payload_capacity = stack_get_capacity(stack);
    ASSERT(verify_memory_pattern(payload_start, payload_capacity, 0x00), "Entire payload area must be zeroed");

    em_stack_destroy(stack);
    em_destroy(em);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_stack_lifecycle_normal();
    test_stack_lifecycle_garbage();
    test_stack_operations_normal();
    test_stack_operations_garbage();
    test_stack_markers_normal();
    test_stack_markers_garbage();
    test_stack_resets();
    
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
}
