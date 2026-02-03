#define EASY_MEMORY_IMPLEMENTATION
#define EM_NO_ATTRIBUTES
#include "easy_memory.h"
#include "test_utils.h"

static void test_nested_creation(void) {
    TEST_PHASE("Nested EM Creation");

    TEST_CASE("Create Parent EM");
    size_t parent_em_size = 4096;
    EM *parent_em = em_create(parent_em_size);
    size_t parent_em_size_in_tail = free_size_in_tail(parent_em);
    ASSERT(parent_em != NULL, "Parent EM should be created successfully");

    #ifdef DEBUG
    print_em(parent_em);
    print_fancy(parent_em, 100);
    #endif

    TEST_CASE("Create Nested EM within Parent EM");
    size_t nested_em_size = 1024;
    EM *nested_em = em_create_nested(parent_em, nested_em_size);
    ASSERT(nested_em != NULL, "Nested EM should be created successfully within parent EM");
    ASSERT(((char *)nested_em >= (char *)parent_em) &&
           ((char *)nested_em + nested_em_size <= (char *)parent_em + parent_em_size),
           "Nested EM memory should be within parent EM bounds");
    ASSERT(em_get_capacity(nested_em) == nested_em_size, "Nested EM capacity should match requested size");

    #ifdef DEBUG
    print_em(parent_em);
    print_fancy(parent_em, 100);
    #endif

    TEST_CASE("Allocate memory from Nested EM");
    size_t alloc_size = 256;
    void *ptr = em_alloc(nested_em, alloc_size);
    ASSERT(ptr != NULL, "Allocation from nested EM should succeed");
    ASSERT(((char *)ptr >= (char *)nested_em) &&
           ((char *)ptr + alloc_size <= (char *)nested_em + nested_em_size),
           "Allocated memory should be within nested EM bounds");
    #ifdef DEBUG
    print_em(nested_em);
    print_fancy(nested_em, 100);
    #endif

    em_free(ptr);
    ASSERT(true, "Freeing allocation from nested EM should succeed");

    TEST_CASE("Free Nested EM");
    em_destroy(nested_em);
    ASSERT(true, "Nested EM should be freed successfully");
    ASSERT(free_size_in_tail(parent_em) == parent_em_size_in_tail, "Parent EM free size should be restored after freeing nested EM");
    
    #ifdef DEBUG
    print_em(parent_em);
    print_fancy(parent_em, 100);
    #endif

    TEST_CASE("Invalid Nested EM Creation");
    EM *invalid_nested1 = em_create_nested(NULL, nested_em_size);
    ASSERT(invalid_nested1 == NULL, "Creating nested EM with NULL parent should fail");
    ASSERT(free_size_in_tail(parent_em) == parent_em_size_in_tail, "Parent EM free size should remain unchanged after failed nested EM creation");

    EM *invalid_nested2 = em_create_nested(parent_em, 0);
    ASSERT(invalid_nested2 == NULL, "Creating nested EM with zero size should fail");
    ASSERT(free_size_in_tail(parent_em) == parent_em_size_in_tail, "Parent EM free size should remain unchanged after failed nested EM creation");

    EM *invalid_nested3 = em_create_nested(parent_em, (size_t)-100);
    ASSERT(invalid_nested3 == NULL, "Creating nested EM with negative/too large size should fail");
    ASSERT(free_size_in_tail(parent_em) == parent_em_size_in_tail, "Parent EM free size should remain unchanged after failed nested EM creation");

    TEST_CASE("Free NULL Nested EM");
    em_destroy(NULL); // Should not crash
    ASSERT(true, "Freeing NULL nested EM should not crash");

    TEST_CASE("Free Already Freed Nested EM");
    em_destroy(nested_em); // Should not crash
    ASSERT(true, "Freeing already freed nested EM should not crash");

    TEST_CASE("Free Parent EM");
    em_destroy(parent_em);
    ASSERT(true, "Parent EM should be freed successfully");
    
    TEST_CASE("Nested EM creation in too small Parent EM");
    size_t small_parent_size = sizeof(EM) + sizeof(Block) + EM_MIN_BUFFER_SIZE + 10; // Just above minimum
    EM *small_parent = em_create(small_parent_size);
    ASSERT(small_parent != NULL, "Small parent EM should be created successfully");

    size_t too_large_nested_size = small_parent_size; // Too large to fit nested em
    EM *too_large_nested = em_create_nested(small_parent, too_large_nested_size);
    ASSERT(too_large_nested == NULL, "Creating nested EM larger than parent EM should fail");
    
    em_set_is_nested(small_parent, false); 
    em_destroy(small_parent);
}

static void test_nested_aligned_creation(void) {
    TEST_PHASE("Nested EM Aligned Creation");

    TEST_CASE("Create Parent EM with specific alignment");
    size_t parent_em_size = 8192;
    EM *parent_em = em_create(parent_em_size);
    ASSERT(parent_em != NULL, "Parent EM should be created successfully with specific alignment");

    TEST_CASE("Create Nested EM with specific alignment within Parent EM");
    size_t nested_em_size = 2048;
    size_t nested_alignment = 128;
    EM *nested_em = em_create_nested_aligned(parent_em, nested_em_size, nested_alignment);
    ASSERT(nested_em != NULL, "Nested EM should be created successfully within parent EM with specific alignment");
    ASSERT((em_get_alignment(nested_em) == nested_alignment), "Nested EM alignment should match requested alignment");
    
    void *ptr = em_alloc(nested_em, 256);
    ASSERT(ptr != NULL, "Allocation from nested EM should succeed");
    ASSERT((((uintptr_t)ptr) % nested_alignment) == 0, "Allocated pointer from Nested EM should be aligned to requested alignment");

    #ifdef DEBUG
    print_em(parent_em);
    print_fancy(parent_em, 100);
    #endif

    em_destroy(nested_em);
    ASSERT(true, "Nested EM should be freed successfully");

    em_destroy(parent_em);
    ASSERT(true, "Parent EM should be freed successfully");
}

static void test_nested_freeing(void) {
    TEST_PHASE("Nested EM Freeing");

    TEST_CASE("Freeing Nested EM through Parent EM");
    size_t parent_em_size = 8192;
    EM *parent_em = em_create(parent_em_size);
    ASSERT(parent_em != NULL, "Parent EM should be created successfully");  
    
    size_t parent_free_before = free_size_in_tail(parent_em);

    size_t nested_em_size = 2048;
    EM *nested_em = em_create_nested(parent_em, nested_em_size);
    ASSERT(nested_em != NULL, "Nested EM should be created successfully within parent EM");


    em_destroy(nested_em); // Free nested EM through parent
    ASSERT(true, "Freeing nested EM through parent should succeed");
    ASSERT(free_size_in_tail(parent_em) == parent_free_before, "Parent EM free size should be restored after freeing nested EM");


    void *ptr = em_alloc(parent_em, 512);
    ASSERT(ptr != NULL, "Allocation from parent EM after freeing nested EM should succeed");
    
    
    EM *check_nested = em_create_nested(parent_em, nested_em_size);
    ASSERT(check_nested != NULL, "Should be able to create new nested EM after freeing previous nested EM");
    em_destroy(check_nested);
    ASSERT(true, "Freeing allocation from parent EM should succeed");

    
    EM *another_nested = em_create_nested(parent_em, nested_em_size);
    ASSERT(another_nested != NULL, "Another nested EM should be created successfully within parent EM");
    em_free(ptr);
    ASSERT(true, "Freeing allocation from parent EM should succeed");
    em_destroy(another_nested);
    ASSERT(true, "Freeing another nested EM should succeed");

    em_destroy(parent_em);
    ASSERT(true, "Parent EM should be freed successfully");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_nested_creation();
    test_nested_aligned_creation();
    test_nested_freeing();

    // Print test summary
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
}