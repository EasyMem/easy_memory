#define EASY_MEMORY_IMPLEMENTATION
#include "easy_memory.h"
#include "test_utils.h"

#define EM_SIZE (1024)
#define BLOCK_SIZE (32)
#define INITIAL_BLOCKS (10)
#define ADDITIONAL_BLOCKS (5)

void test_same_size_allocation(void) {
    TEST_CASE("Same Size Blocks Allocation Pattern");

    // Create an easy memory instance
    EM *em = em_create(EM_SIZE);
    ASSERT(em != NULL, "EM creation should succeed");

    void *blocks[INITIAL_BLOCKS + ADDITIONAL_BLOCKS] = {0};
    
    TEST_PHASE("Initial allocations");
    // Allocate INITIAL_BLOCKS blocks of the same size
    int allocated = 0;
    for (int i = 0; i < INITIAL_BLOCKS; i++) {
        blocks[allocated] = em_alloc(em, BLOCK_SIZE);
        ASSERT(blocks[allocated] != NULL, "Block allocation should succeed");
        
        // Fill with pattern to verify memory
        fill_memory_pattern(blocks[allocated], BLOCK_SIZE, i);
        ASSERT(verify_memory_pattern(blocks[allocated], BLOCK_SIZE, i), 
               "Memory pattern should be valid");
        
        allocated++;
    }

    #ifdef DEBUG
    printf("Initial state after %d allocations:\n", INITIAL_BLOCKS);
    print_fancy(em, 100);
    print_em(em);
    #endif

    size_t after_initial_tail = free_size_in_tail(em);
    
    TEST_PHASE("Free every second block");
    // Free every second block
    for (int i = 0; i < INITIAL_BLOCKS; i += 2) {
        em_free(blocks[i]);
        blocks[i] = NULL;
    }

    #ifdef DEBUG
    printf("\nState after freeing every second block:\n");
    print_fancy(em, 100);
    print_em(em);
    #endif

    TEST_PHASE("Additional allocations");
    // Try to allocate ADDITIONAL_BLOCKS more blocks
    int additional_allocated = 0;
    for (int i = 0; i < ADDITIONAL_BLOCKS; i++) {
        void *ptr = em_alloc(em, BLOCK_SIZE);
        ASSERT(ptr != NULL, "Additional block allocation should succeed");
        
        #ifdef DEBUG
        print_fancy(em, 100);
        #endif
        // Find empty slot
        for (int j = 0; j < INITIAL_BLOCKS + ADDITIONAL_BLOCKS; j++) {
            if (blocks[j] == NULL) {
                blocks[j] = ptr;
                // Fill with pattern
                fill_memory_pattern(ptr, BLOCK_SIZE, 100 + i);
                ASSERT(verify_memory_pattern(ptr, BLOCK_SIZE, 100 + i),
                       "Additional block memory pattern should be valid");
                additional_allocated++;
                break;
            }
        }
    }

    ASSERT(additional_allocated == ADDITIONAL_BLOCKS, 
           "Should allocate all additional blocks");

    #ifdef DEBUG
    printf("\nFinal state after additional allocations:\n");
    print_fancy(em, 100);
    print_em(em);
    #endif

    // Verify final state
    ASSERT(free_size_in_tail(em) == after_initial_tail,
           "Tail size should be the same as after initial allocations");
    ASSERT(em_get_free_blocks(em) == NULL, "Free block should be NULL");

    
    // Free all remaining blocks
    for (int i = 0; i < INITIAL_BLOCKS + ADDITIONAL_BLOCKS; i++) {
        if (blocks[i] != NULL) {
            em_free(blocks[i]);
            #ifdef DEBUG
            print_fancy(em, 100);
            #endif
        }
    }

    em_destroy(em);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 
    
    test_same_size_allocation();
    
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
} 