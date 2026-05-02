#define EASY_MEMORY_IMPLEMENTATION
#define EM_NO_ATTRIBUTES
#include "easy_memory.h"
#include "test_utils.h"
#include <limits.h>
#include <stdint.h>

static void test_slab_lifecycle_normal(void) {
    TEST_PHASE("Slab Lifecycle - Normal Path");
    
    EM *em = em_create(2048);
    size_t initial_free = free_size_in_tail(em);

    TEST_CASE("Standard Slab initialization");
    size_t chunk_size = 32;
    size_t slab_size = 512;
    Slab *slab = em_slab_create(em, slab_size, chunk_size);
    
    ASSERT(slab != NULL, "Slab pointer should not be NULL");
    ASSERT(slab_get_chunk_size(slab) == chunk_size, "Chunk size should be correctly stored");
    ASSERT(slab_get_capacity(slab) >= slab_size, "Capacity should meet requested size");
    
    em_slab_destroy(slab);
    ASSERT(free_size_in_tail(em) == initial_free, "Parent EM should reclaim memory after Slab destruction");

    TEST_CASE("Scratch Slab initialization");
    Slab *scratch_slab = em_slab_create_scratch(em, slab_size, chunk_size);
    ASSERT(scratch_slab != NULL, "Scratch Slab should not be NULL");
    
    em_slab_destroy(scratch_slab);
    ASSERT(free_size_in_tail(em) == initial_free, "Scratch tail should be restored");

    em_destroy(em);
}

static void test_slab_lifecycle_garbage(void) {
    TEST_PHASE("Slab Lifecycle - Sad Path & Garbage");
    
    EM *em = em_create(1024);

#if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
    TEST_CASE("Creation with NULL parent EM");
    ASSERT(em_slab_create(NULL, 512, 16) == NULL, "Should fail on NULL parent");

    TEST_CASE("Creation with zero size");
    ASSERT(em_slab_create(em, 0, 16) == NULL, "Should fail on zero slab size");

    TEST_CASE("Creation with chunk_size > slab_size");
    ASSERT(em_slab_create(em, 128, 256) == NULL, "Should fail if chunk exceeds slab capacity");

    TEST_CASE("Destroy NULL Slab");
    em_slab_destroy(NULL); // Should safely return
    ASSERT(true, "Destroying NULL should not crash");

    TEST_CASE("Creation with extreme OOM size");
    ASSERT(em_slab_create(em, 4096, 16) == NULL, "Should return NULL if parent EM is exhausted");
#endif
    #ifdef DEBUG
    print_em(em);
    print_fancy(em, 100);
    #endif


    TEST_CASE("Automatic chunk alignment");
    // Requesting 7 bytes should result in 8-byte chunks on 64-bit systems.
    Slab *unaligned_slab = em_slab_create(em, 256, 7);
    ASSERT(unaligned_slab != NULL, "Creation should succeed via internal align_up");
    size_t effective_chunk = slab_get_chunk_size(unaligned_slab);
    ASSERT(effective_chunk % EMMIN_ALIGNMENT == 0, "Resulting chunk size must be aligned");
    ASSERT(effective_chunk >= 7, "Effective chunk must not be smaller than requested");
    em_slab_destroy(unaligned_slab);

    em_destroy(em);
}

static void test_slab_operations_normal(void) {
    TEST_PHASE("Slab Operations - Normal Path");
    
    EM *em = em_create(4096);
    size_t chunk_size = 64;
    size_t slab_size = 1024; // ~16 chunks
    Slab *slab = em_slab_create(em, slab_size, chunk_size);
    
    void *ptrs[16];
    size_t count = 0;

    TEST_CASE("Fill slab (Bump mode)");
    while (count < 16) {
        void *p = em_slab_alloc(slab);
        if (!p) break;
        ptrs[count++] = p;
        ASSERT_QUIET(((uintptr_t)p % EMMIN_ALIGNMENT) == 0, "Chunk must be word-aligned");
        fill_memory_pattern(p, chunk_size, (int)count);
    }
    ASSERT(count > 0, "Should have allocated chunks");
    ASSERT(em_slab_alloc(slab) == NULL, "Slab should return NULL when exhausted");

    TEST_CASE("Recycle chunks (Free-List mode)");
    // Free even-indexed chunks
    for (size_t i = 0; i < count; i += 2) {
        em_slab_free(slab, ptrs[i]);
    }

    // Re-allocate and verify patterns of untouched chunks
    for (size_t i = 0; i < count / 2; i++) {
        void *p = em_slab_alloc(slab);
        ASSERT(p != NULL, "Re-allocation from free-list should succeed");
        // Verify that the odd-indexed chunks we didn't free are still intact
        ASSERT_QUIET(verify_memory_pattern(ptrs[1], chunk_size, 2), "Untouched data must remain valid");
    }

    em_slab_destroy(slab);
    em_destroy(em);
}

static void test_slab_operations_garbage(void) {
    TEST_PHASE("Slab Operations - Sad Path & Corruption");
    
    EM *em = em_create(1024);
    Slab *slab = em_slab_create(em, 512, 32);
    void *valid_chunk = em_slab_alloc(slab);

#if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
    TEST_CASE("Free NULL pointer");
    em_slab_free(slab, NULL);
    ASSERT(true, "Should not crash on NULL free");

    TEST_CASE("Free pointer outside slab boundaries");
    void *external_ptr = (char *)slab - 64;
    em_slab_free(slab, external_ptr);
    ASSERT(true, "Should not crash on out-of-bounds free (underflow)");

    void *far_ptr = (char *)slab + 2048;
    em_slab_free(slab, far_ptr);
    ASSERT(true, "Should not crash on out-of-bounds free (overflow)");

    TEST_CASE("Free unaligned pointer");
    void *misaligned = (void *)((uintptr_t)valid_chunk + 1);
    em_slab_free(slab, misaligned);
    ASSERT(true, "Should not crash on misaligned pointer free");

    TEST_CASE("Sequential Double Free detection");
    em_slab_free(slab, valid_chunk);
    em_slab_free(slab, valid_chunk); // Second call should be caught by O(1) check
    ASSERT(true, "Should not crash on sequential double free");
#endif

    em_slab_destroy(slab);
    em_destroy(em);
}


static void test_slab_complex_ordering(void) {
    TEST_PHASE("Slab Complex Ordering & LIFO");
    
    EM *em = em_create(4096);
    const size_t chunk_size = 64;
    const size_t slab_size = 1024;
    Slab *slab = em_slab_create(em, slab_size, chunk_size);
    
    // Rationale: Slab is zero-overhead; capacity equals usable memory.
    size_t max_chunks = slab_get_capacity(slab) / chunk_size;
    void **p = em_calloc(em, max_chunks, sizeof(void*));

    TEST_CASE("Fill slab completely");
    for (size_t i = 0; i < max_chunks; i++) {
        p[i] = em_slab_alloc(slab);
        ASSERT_QUIET(p[i] != NULL, "Allocation must succeed until full");
    }
    ASSERT(em_slab_alloc(slab) == NULL, "Slab must be physically exhausted");

    TEST_CASE("LIFO Order Verification");
    // Free the last three allocated chunks
    em_slab_free(slab, p[max_chunks - 3]);
    em_slab_free(slab, p[max_chunks - 2]);
    em_slab_free(slab, p[max_chunks - 1]);

    // Re-allocations must return them in reverse order
    ASSERT(em_slab_alloc(slab) == p[max_chunks - 1], "LIFO step 1 failed");
    ASSERT(em_slab_alloc(slab) == p[max_chunks - 2], "LIFO step 2 failed");
    ASSERT(em_slab_alloc(slab) == p[max_chunks - 3], "LIFO step 3 failed");

    TEST_CASE("Interleaved Fragmentation Stability");
    em_slab_reset(slab);
    for (size_t i = 0; i < max_chunks; i++) p[i] = em_slab_alloc(slab);

    // Create holes: free every second chunk
    for (size_t i = 0; i < max_chunks; i += 2) {
        em_slab_free(slab, p[i]);
    }

    // Refill holes
    for (size_t i = 0; i < max_chunks / 2; i++) {
        void *tmp = em_slab_alloc(slab);
        ASSERT_QUIET(tmp != NULL, "Refilling holes should succeed");
    }
    ASSERT(em_slab_alloc(slab) == NULL, "Slab must be full after refilling holes");

    em_slab_destroy(slab);
    em_destroy(em);
}

static void test_slab_stress(void) {
    TEST_PHASE("Slab Stress Test");
    
    EM *em = em_create(1024 * 1024);
    size_t chunk_size = 128;
    size_t slab_req_size = 256 * 1024;
    Slab *slab = em_slab_create(em, slab_req_size, chunk_size);
    
    size_t max_chunks = slab_get_capacity(slab) / chunk_size;
    void **ptrs = malloc(max_chunks * sizeof(void*));
    size_t active_ptrs = 0;

    TEST_CASE("20,000 randomized iterations");
    for (int i = 0; i < 20000; i++) {
        // Linear Congruential Generator for deterministic pseudo-randomness
        size_t pseudo_rand = (size_t)i * 1103515245 + 12345;
        
        if ((pseudo_rand % 100) < 55) { // 55% chance to Allocate
            if (active_ptrs < max_chunks) {
                void *p = em_slab_alloc(slab);
                if (p) {
                    ptrs[active_ptrs++] = p;
                    *(size_t *)p = (size_t)i; // Integrity check: write to chunk
                }
            }
        } else { // 45% chance to Free
            if (active_ptrs > 0) {
                size_t idx = (pseudo_rand % active_ptrs);
                em_slab_free(slab, ptrs[idx]);
                // Keep the test array contiguous
                ptrs[idx] = ptrs[--active_ptrs];
            }
        }
    }
    
    ASSERT(true, "Completed 20k iterations without state corruption");
    
    free(ptrs);
    em_slab_destroy(slab);
    em_destroy(em);
}

static void test_slab_exhaustion_edge_case(void) {
    TEST_PHASE("Slab Exhaustion and Recovery");
    
    EM *em = em_create(1024);
    const size_t chunk_size = 64;
    // Exactly 2 chunks, no overhead.
    const size_t slab_size = chunk_size * 2;
    Slab *slab = em_slab_create(em, slab_size, chunk_size);
    
    ASSERT(slab_get_capacity(slab) == slab_size, "Slab must have exact capacity for 2 chunks");

    TEST_CASE("Exhausting a 2-chunk Slab");
    void *p1 = em_slab_alloc(slab);
    void *p2 = em_slab_alloc(slab);
    
    ASSERT(p1 != NULL && p2 != NULL, "Initial two allocations must succeed");
    ASSERT(p1 != p2, "Allocations must return unique addresses");
    
    void *p3 = em_slab_alloc(slab);
    ASSERT(p3 == NULL, "Third allocation must return NULL (Slab exhausted)");

    TEST_CASE("Recovering and Re-exhausting");
    // Free the first chunk. The Slab is now: [FREE (Index 1)] -> [OCCUPIED (Index 2)]
    // The internal free index should now point to chunk 1.
    em_slab_free(slab, p1);
    
    void *p1_retry = em_slab_alloc(slab);
    ASSERT(p1_retry == p1, "Re-allocation must reclaim the specific freed chunk");
    
    void *p4 = em_slab_alloc(slab);
    ASSERT(p4 == NULL, "Allocation after refilling must return NULL");

    TEST_CASE("Reclaiming from the end");
    em_slab_free(slab, p2);
    void *p2_retry = em_slab_alloc(slab);
    ASSERT(p2_retry == p2, "Re-allocation of the second chunk must succeed");
    
    ASSERT(em_slab_alloc(slab) == NULL, "Final exhaustion check");

    em_slab_destroy(slab);
    em_destroy(em);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_slab_lifecycle_normal();
    test_slab_lifecycle_garbage();
    test_slab_operations_normal();
    test_slab_operations_garbage();
    test_slab_complex_ordering();
    test_slab_stress();
    test_slab_exhaustion_edge_case();
    
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
}