#define EASY_MEMORY_IMPLEMENTATION
#define EM_NO_ATTRIBUTES
#include "easy_memory.h"
#include "test_utils.h"

static void test_bump_creation(void) {
    TEST_CASE("Bump Allocator Creation");

    TEST_PHASE("Create Bump Allocator within EM");
    size_t em_size = 1024;
    EM *em = em_create(em_size);
    ASSERT(em != NULL, "EM should be created successfully");

    #ifdef DEBUG
    print_em(em);
    print_fancy(em, 100);
    #endif

    size_t bump_size = 256;
    Bump *bump = em_create_bump(em, bump_size);
    ASSERT(bump != NULL, "Bump allocator should be created successfully within the EM");
    #ifdef DEBUG
    print_em(em);
    print_fancy(em, 100);
    #endif

    ASSERT(bump_get_capacity(bump) == bump_size, "Bump allocator capacity should match requested size");
    ASSERT(bump_get_em(bump) == em, "Bump allocator should reference the parent EM");
    ASSERT(bump_get_offset(bump) == sizeof(Bump), "Bump allocator offset should be initialized correctly");

    em_bump_destroy(bump);
    #ifdef DEBUG
    em_free((char *)bump + sizeof(Bump));
    print_em(em);
    #endif

    bump = em_create_bump(em, 0);
    ASSERT(bump == NULL, "Bump allocator creation with zero size should fail");
    if (bump) em_bump_destroy(bump);

    bump = em_create_bump(em, 10);
    ASSERT(bump == NULL, "Bump creation with too small positive size should fail");
    if (bump) em_bump_destroy(bump);

    bump = em_create_bump(NULL, 100);
    ASSERT(bump == NULL, "Bump allocator creation with NULL EM should fail");
    if (bump) em_bump_destroy(bump);

    bump = em_create_bump(em, 2000); // Larger than em size
    ASSERT(bump == NULL, "Bump allocator creation with size larger than EM should fail");
    if (bump) em_bump_destroy(bump);

    bump = em_create_bump(em, em_size - sizeof(EM) - sizeof(Block));
    ASSERT(bump != NULL, "Bump allocator with size of all EM should be created successfully");

    em_bump_destroy(bump);

    em_bump_destroy(NULL); // Should not crash
    ASSERT(true, "Freeing NULL bump allocator should not crash");

    em_bump_reset(NULL); // Should not crash
    ASSERT(true, "Resetting NULL bump allocator should not crash");

    em_destroy(em);
}

static void test_bump_allocation(void) {
    TEST_CASE("Bump Allocator Allocation");

    size_t em_size = 2048;
    EM *em = em_create(em_size);
    ASSERT(em != NULL, "EM should be created successfully");

    size_t bump_size = 512;
    Bump *bump = em_create_bump(em, bump_size);
    ASSERT(bump != NULL, "Bump allocator should be created successfully within the EM");

    TEST_PHASE("Allocate memory from Bump Allocator");

    em_bump_alloc(NULL, 100); // Should not crash
    ASSERT(true, "Allocating from NULL bump allocator should not crash");

    size_t alloc_size1 = 100;
    void *ptr1 = em_bump_alloc(bump, alloc_size1);
    ASSERT(ptr1 != NULL, "First allocation from bump allocator should succeed");

    size_t alloc_size2 = 200;
    void *ptr2 = em_bump_alloc(bump, alloc_size2);
    ASSERT(ptr2 != NULL, "Second allocation from bump allocator should succeed");

    ASSERT((char *)ptr2 == (char *)ptr1 + alloc_size1, "Second allocation should be contiguous after first");

    size_t alloc_size3 = 300; // Exceeds remaining space
    void *ptr3 = em_bump_alloc(bump, alloc_size3);
    ASSERT(ptr3 == NULL, "Allocation exceeding bump allocator capacity should fail");
    
    TEST_PHASE("Reset Bump Allocator");
    em_bump_reset(bump);
    ASSERT(bump_get_offset(bump) == sizeof(Bump), "Bump allocator offset should be reset correctly");
    ASSERT(bump_get_capacity(bump) == bump_size, "Bump allocator capacity should remain unchanged after reset");

    TEST_PHASE("Allocate aligned memory from Bump Allocator");
    size_t alloc_size4 = 50;
    size_t alignment4 = 3;
    void *ptr4 = em_bump_alloc_aligned(bump, alloc_size4, alignment4);
    ASSERT(ptr4 == NULL, "Aligned allocation with non-power-of-two alignment should fail");

    size_t alloc_size5 = 50;
    size_t alignment5 = 64;
    void *ptr5 = em_bump_alloc_aligned(bump, alloc_size5, alignment5);
    ASSERT(ptr5 != NULL, "Aligned allocation from bump allocator should succeed");
    ASSERT(((uintptr_t)ptr5 % alignment5) == 0, "Allocated pointer should be correctly aligned");

    size_t alloc_size6 = 450; // Exceeds remaining space
    void *ptr6 = em_bump_alloc_aligned(bump, alloc_size6, alignment5);
    ASSERT(ptr6 == NULL, "Aligned allocation exceeding bump allocator capacity should fail");
    
    em_bump_reset(bump);
    
    size_t alloc_size7 = 0;
    void *ptr7 = em_bump_alloc_aligned(bump, alloc_size7, alignment5);
    ASSERT(ptr7 == NULL, "Aligned allocation with zero size should fail");

    size_t alloc_size8 = 100;
    size_t alignment8 = (size_t)-1;
    void *ptr8 = em_bump_alloc_aligned(bump, alloc_size8, alignment8);
    ASSERT(ptr8 == NULL, "Aligned allocation with over the top alignment should fail");

    size_t alloc_size9 = bump_size;
    size_t alignment9 = 16;
    void *ptr9 = em_bump_alloc_aligned(bump, alloc_size9, alignment9);
    ASSERT(ptr9 == NULL, "Aligned allocation that exactly matches bump capacity should fail");

    em_bump_reset(bump);
    void *ptr10 = em_bump_alloc(bump, SIZE_MAX); // Cast for warning suppression if needed
    ASSERT(ptr10 == NULL, "Huge allocation must fail gracefully");

    TEST_PHASE("Free Bump Allocator");
    em_bump_destroy(bump);
    em_destroy(em);
}

#define NUM_ALLOCS 100
static void test_bump_hard_usage(void) {
    TEST_PHASE("Bump Integrity / Hard Usage");
    EM *em = em_create(5000);
    Bump *bump = em_create_bump(em, 4096);
    void *ptrs[NUM_ALLOCS];
    size_t sizes[NUM_ALLOCS];
    
    for(int i=0; i<NUM_ALLOCS; i++) {
        sizes[i] = (size_t)(10 + (i % 20));
        ptrs[i] = em_bump_alloc(bump, sizes[i]);
        
        ASSERT_QUIET(ptrs[i] != NULL, "Stress test allocation");
        fill_memory_pattern(ptrs[i], sizes[i], i);
    }
    
    for(int i=0; i<NUM_ALLOCS; i++) {
        ASSERT_QUIET(verify_memory_pattern(ptrs[i], sizes[i], i), "Pattern verification failed for block");
    }
    
    check_pointers_integrity(ptrs, sizes, NUM_ALLOCS);
    
    em_bump_destroy(bump);
    em_destroy(em);
}

#define BLOCK_FROM_DATA(ptr) ((Block *)((uintptr_t)(ptr) - sizeof(Block)))

static void test_bump_trim(void) {
    TEST_CASE("Bump Trim Scenarios");

    // ---------------------------------------------------------
    TEST_PHASE("1. Trim NULL");
    em_bump_trim(NULL);
    ASSERT(1, "bump_trim(NULL) should not crash");

    // ---------------------------------------------------------
    TEST_PHASE("2. Trim when not enough space (No-op)");
    {
        EM *em = em_create(4096);
        Bump *bump = em_create_bump(em, 100); 
        printf("capacity: %zu\n", bump_get_capacity(bump));

        em_bump_alloc(bump, 90);
        printf("free space after alloc: %zu\n", bump_get_capacity(bump) - bump_get_offset(bump) + sizeof(Bump));
        
        size_t old_capacity = bump_get_capacity(bump);
        em_bump_trim(bump);
        
        ASSERT(bump_get_capacity(bump) == old_capacity, "Capacity should not change if remaining space is too small");
        
        em_destroy(em);
    }

    // ---------------------------------------------------------
    TEST_PHASE("3. Trim with plenty of space (Tail Merge Scenario)");
    {
        EM *em = em_create(2048);
        Bump *bump = em_create_bump(em, 1024);
        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif

        void *ptr = em_bump_alloc(bump, 64);
        void *old_tail = em_get_tail(em);
        
        em_bump_trim(bump);

        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif
        
        size_t aligned_ptr = align_up((uintptr_t)ptr + 64, EM_DEFAULT_ALIGNMENT);
        size_t expected_cap = aligned_ptr - (uintptr_t)bump - sizeof(Bump);
        
        ASSERT(bump_get_capacity(bump) == expected_cap, "Capacity should shrink to fit used data");
        
        Block *em_tail = em_get_tail(em);
        ASSERT((void*)em_tail < (void*)old_tail, "EM tail should point to the trimmed bump");
        
        em_destroy(em);
    }

    // ---------------------------------------------------------
    TEST_PHASE("4. Trim when space is JUST enough (Boundary check)");
    {
        EM *em = em_create(2048);

        Bump *bump = em_create_bump(em, 64);
        
        size_t alloc_size = 64 - sizeof(Block) - EM_DEFAULT_ALIGNMENT;
        em_bump_alloc(bump, alloc_size);
        
        em_bump_trim(bump);
        
        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif

        ASSERT(bump_get_capacity(bump) == alloc_size, "Trim should work on exact boundary condition");
        
        em_destroy(em);
    }

    // ---------------------------------------------------------
    TEST_PHASE("5. Trim when right neighbor is OCCUPIED");
    {
        EM *em = em_create(2048);
        
        // [Bump (1024)] -> [Block C (Occupied)]
        Bump *bump = em_create_bump(em, 1024);
        void *data_c = em_alloc(em, 64);
        Block *block_c = BLOCK_FROM_DATA(data_c);
        
        em_bump_alloc(bump, 64);
        
        em_bump_trim(bump);

        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif
        
        Block *new_free = get_prev(block_c);
        ASSERT(new_free != (Block*)bump, "New block should be inserted between Bump and C");
        ASSERT(get_is_free(new_free), "Inserted block should be free");
        ASSERT(get_size(new_free) > 0, "Inserted block should have size");
        
        ASSERT(get_prev(new_free) == (Block*)bump, "New free block should point back to bump");
        
        em_destroy(em);
    }

    // ---------------------------------------------------------
    TEST_PHASE("6. Trim when right neighbor is FREE (Merge Right)");
    {
        EM *em = em_create(2048);
        
        // [Bump (1024)] -> [Block B (Free)] -> [Block C (Occupied)]
        Bump *bump = em_create_bump(em, 1024);
        void *data_b = em_alloc(em, 256);
        void *data_c = em_alloc(em, 64);
        
        em_free(data_b);
        Block *block_b = BLOCK_FROM_DATA(data_b);
        size_t old_b_size = get_size(block_b);
        
        em_bump_alloc(bump, 64);
        
        em_bump_trim(bump);

        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif
        
        Block *next_after_bump = next_block(em, (Block*)bump);
        ASSERT(get_is_free(next_after_bump), "Next block should be free");
        ASSERT(get_size(next_after_bump) > old_b_size, "Free block should have grown due to merge");
        
        em_free(data_c);
        em_destroy(em);
    }

    // ---------------------------------------------------------
    TEST_PHASE("7. Trim when space is large (Offset Alignment check)");
    {
        EM *em = em_create(2048);
        Bump *bump = em_create_bump(em, 100);
        
        em_bump_alloc(bump, 1);
        
        em_bump_trim(bump);

        #ifdef DEBUG
        print_em(bump_get_em(bump));
        print_fancy(bump_get_em(bump), 101);
        #endif

        ASSERT(bump_get_capacity(bump) == 16, "Trim should align capacity up");
        
        em_destroy(em);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_bump_creation();
    test_bump_allocation();
    test_bump_hard_usage();
    test_bump_trim();

    // Print test summary
    print_test_summary();
    
    // Return non-zero exit code if any tests failed
    return tests_failed > 0 ? 1 : 0;
}
