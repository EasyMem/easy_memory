#define EASY_MEMORY_IMPLEMENTATION
#include "easy_memory.h"
#include "test_utils.h"
#include <limits.h>
#include <stdint.h>

#include <limits.h>

#ifndef SSIZE_MAX
#define SSIZE_MAX (SIZE_MAX / 2)
#endif

void test_min_exponent(void) {
    TEST_PHASE("min_exponent_of Function");

    EM_ASSERT(min_exponent_of(0) == 0);

    TEST_CASE("Min exponent of powers of two");
    ASSERT(min_exponent_of(1) == 0, "Min exponent of 1 should be 0");
    ASSERT(min_exponent_of(2) == 1, "Min exponent of 2 should be 1");
    ASSERT(min_exponent_of(8) == 3, "Min exponent of 8 should be 3");
    ASSERT(min_exponent_of(16) == 4, "Min exponent of 16 should be 4");
    ASSERT(min_exponent_of(32) == 5, "Min exponent of 32 should be 5");

    TEST_CASE("Min exponent of non-powers of two");
    ASSERT(min_exponent_of(3) == 0, "Min exponent of 3 should be 0"); 
    ASSERT(min_exponent_of(5) == 0, "Min exponent of 5 should be 0"); 
    ASSERT(min_exponent_of(6) == 1, "Min exponent of 6 should be 1"); 
    ASSERT(min_exponent_of(10) == 1, "Min exponent of 10 should be 1");
    ASSERT(min_exponent_of(12) == 2, "Min exponent of 12 should be 2");
}

void test_invalid_allocations(void) {
    TEST_PHASE("Invalid Allocation Scenarios");

    // Create an EM
    EM *em = em_create(1024);
    ASSERT(em != NULL, "EM creation should succeed");

    TEST_CASE("Zero size allocation");
    void *zero_size = em_alloc(em, 0);
    ASSERT(zero_size == NULL, "Zero size allocation should return NULL");

    TEST_CASE("Negative size allocation");
    void *negative_size = em_alloc(em, -1);
    ASSERT(negative_size == NULL, "Negative size allocation should return NULL");

    TEST_CASE("NULL EM allocation");
    void *null_em = em_alloc(NULL, 32);
    ASSERT(null_em == NULL, "Allocation with NULL EM should return NULL");

    TEST_CASE("Free NULL pointer");
    em_free(NULL); // Should not crash
    ASSERT(true, "Free NULL pointer should not crash");

    TEST_CASE("Free invalid pointer");
    Block fake_block = {0};
    fake_block.as.occupied.magic = (uintptr_t)(&fake_block + 1) ^ (uintptr_t)1;  // 0xFF is an invalid magic number
    void *fake_data = (char*)&fake_block + sizeof(Block);
    em_free(fake_data); // Should not crash
    ASSERT(true, "Free invalid pointer should not crash");
    
    TEST_CASE("Free pointer from different EM");
    EM *another_em = em_create(1024);
    void *ptr = em_alloc(another_em, 32);
    em_free(ptr); // Should not crash
    em_destroy(another_em);

    TEST_CASE("Free already freed pointer");
    void *ptr2 = em_alloc(em, 32);
    em_free(ptr2);
    em_free(ptr2); // Should not crash
    ASSERT(true, "Free already freed pointer should not crash");

    TEST_CASE("Allocation larger than EM size");
    void *huge_allocation = em_alloc(em, 2048);
    ASSERT(huge_allocation == NULL, "Allocation larger than EM size should fail");

    em_destroy(em);
}

void test_invalid_em_creation(void) {
    TEST_PHASE("Invalid EM Creation Scenarios");

    TEST_CASE("Zero size EM");
    EM *zero_size_em = em_create(0);
    ASSERT(zero_size_em == NULL, "Zero size EM creation should fail");

    TEST_CASE("Negative size EM");
    EM *negative_size_em = em_create(-1);
    ASSERT(negative_size_em == NULL, "Negative size EM creation should fail");

    TEST_CASE("Very large size EM");
    #if SIZE_MAX > 0xFFFFFFFF
        EM *large_size_em = em_create(SSIZE_MAX);
        ASSERT(large_size_em == NULL, "Very large size EM creation should fail on 64-bit systems");
    #else
        printf("[INFO] Skipping SSIZE_MAX allocation test on 32-bit system.\n");
    #endif
    
    TEST_CASE("NULL memory for static EM");
    EM *null_memory_em = em_create_static(NULL, 1024);
    ASSERT(null_memory_em == NULL, "Static EM with NULL memory should fail");

    TEST_CASE("Negative size for static EM");
    void *mem = malloc(1024);
    EM *negative_static_em = em_create_static(mem, -1);
    ASSERT(negative_static_em == NULL, "Static EM with negative size should fail");
    free(mem);

    TEST_CASE("Free NULL EM");
    em_destroy(NULL); // Should not crash
    ASSERT(true, "Free NULL EM should not crash");

    TEST_CASE("Reset NULL EM");
    em_reset(NULL); // Should not crash
    ASSERT(true, "Reset NULL EM should not crash");
}

void test_boundary_conditions(void) {
    TEST_PHASE("Boundary Conditions");

    TEST_CASE("EM size just above minimum");
    size_t min_size = EM_MIN_SIZE;
    EM *min_size_em = em_create(min_size);
    ASSERT(min_size_em != NULL, "EM with minimum valid size should succeed");
    em_destroy(min_size_em);

    TEST_CASE("EM size just below minimum");
    EM *below_min_em = em_create(min_size - 1 - sizeof(EM));
    ASSERT(below_min_em == NULL, "EM with size below minimum should fail");

    TEST_CASE("Static EM with minimum size");
    void *min_memory = malloc(min_size);
    EM *min_static_em = em_create_static(min_memory, min_size);
    ASSERT(min_static_em != NULL, "Static EM with minimum valid size should succeed");
    free(min_memory);

    TEST_CASE("Static EM with size below minimum");
    void *small_memory_bc = malloc(min_size - 1);
    EM *small_static_em_bc = em_create_static(small_memory_bc, min_size - 1);
    ASSERT(small_static_em_bc == NULL, "Static EM with size below minimum should fail");
    free(small_memory_bc);

    TEST_CASE("Tail allocation leaving fragment smaller than block header");
    size_t em_size_frag = 1024;
    EM *em_frag = em_create(em_size_frag);
    ASSERT(em_frag != NULL, "EM creation for fragmentation test should succeed");

    // Calculate initial free size in tail
    size_t initial_tail_free = free_size_in_tail(em_frag);
    ASSERT(initial_tail_free > sizeof(Block), "Initial tail should have space");

    // Calculate allocation size to leave a small fragment
    size_t fragment_size = sizeof(Block) / 2; // Must be > 0 and < sizeof(Block)
    if (fragment_size == 0) fragment_size = 1; // Ensure it's at least 1
    size_t alloc_size_frag = initial_tail_free - fragment_size;
    ASSERT(alloc_size_frag > 0, "Calculated alloc size must be positive");

    // Allocate the block
    void *block_frag = em_alloc(em_frag, alloc_size_frag);
    ASSERT(block_frag != NULL, "Allocation leaving small fragment should succeed");

    // Check that the 'else' branch in alloc_in_tail was taken
    ASSERT(free_size_in_tail(em_frag) == 0, "Tail free size should be 0 after small fragment alloc");

    em_destroy(em_frag);
}

void test_full_em_allocation(void) {
    TEST_PHASE("Allocation in Full EM");

    // Create an EM with minimal valid size
    // Size = EM metadata + one Block metadata + minimal usable buffer
    size_t min_valid_size = BLOCK_MIN_SIZE + EM_DEFAULT_ALIGNMENT;
    EM *em = em_create(min_valid_size);
    ASSERT(em != NULL, "EM creation with minimal size should succeed");
    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif // DEBUG

    TEST_CASE("Allocate block filling the entire initial tail");
    size_t avail = free_size_in_tail(em);
    // Try to allocate exactly the minimum buffer size available
    void *first_block = em_alloc(em, avail);
    ASSERT(first_block != NULL, "Allocation of the first block should succeed");
    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif // DEBUG

    // At this point, the free list should be empty, and the tail should have 0 free space.
    ASSERT(em_get_free_blocks(em) == NULL, "Free block list should be empty after filling allocation");
    ASSERT(free_size_in_tail(em) == 0, "Free size in tail should be 0 after filling allocation");

    TEST_CASE("Attempt allocation when no space is left");
    void *second_block = em_alloc(em, 1); // Attempt to allocate just one more byte
    ASSERT(second_block == NULL, "Allocation should fail when no space is left");

    em_free(first_block);

    em_destroy(em);
}

void test_custom_alignment_alloc(void) {
    TEST_PHASE("Custom Alignment Allocation");

    size_t custom_alignment = 32;
    size_t em_size = 5000;
    EM *em = em_create(em_size); // Create with default alignment 16
    ASSERT(em != NULL, "EM creation with default alignment should succeed");
    ASSERT(em_get_alignment(em) == EM_DEFAULT_ALIGNMENT, "EM alignment should match default alignment");
    
    TEST_CASE("Enforce exact alignment value");
    Block *first_block = em_get_first_block(em);
    uintptr_t first_block_addr = (uintptr_t)first_block;
    size_t shift_size = 16;
    uintptr_t next_user_data = first_block_addr + 2 * sizeof(Block) + shift_size;
    if ((next_user_data % 32) == 0) {
        shift_size += 16;
    }
    void *shift = em_alloc(em, shift_size);

    TEST_CASE("Allocate block with custom alignment");
    size_t alloc_size = 128;
    void *block = em_alloc_aligned(em, alloc_size, custom_alignment);
    ASSERT(block != NULL, "Custom aligned allocation should succeed");
    ASSERT(((uintptr_t)block % custom_alignment) == 0, "Allocated block should be aligned to custom alignment");
    uintptr_t *spot_before_user_data = (uintptr_t *)((char *)block - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)block;
    ASSERT(check != (uintptr_t)0xDEADBEEF, "Block should have alignment padding");
    ASSERT(check % sizeof(uintptr_t) == 0, "Retrieved block pointer should be properly aligned");
    Block *block_meta = (Block *)check;
    ASSERT(block_meta != NULL, "Retrieved block metadata should not be NULL");
    ASSERT(get_em(block_meta) == em, "Block`s EM pointer should match the allocating EM");
    em_free(block);

    TEST_CASE("Allocate another block with custom alignment after fragmentation");
    void *temp_ptr = em_alloc(em, 256); // Allocate some more to fragment the EM
    ASSERT(temp_ptr != NULL, "Temporary allocation should succeed");
    void *temp_ptr2 = em_alloc(em, 64);
    ASSERT(temp_ptr2 != NULL, "Second temporary allocation should succeed");

    em_free(temp_ptr);

    void *block2 = em_alloc_aligned(em, alloc_size, custom_alignment);
    ASSERT(block2 != NULL, "Custom aligned allocation after fragmentation should succeed");
    ASSERT(((uintptr_t)block2 % custom_alignment) == 0, "Allocated block should be aligned to custom alignment");
    spot_before_user_data = (uintptr_t *)((char *)block2 - sizeof(uintptr_t));
    check = *spot_before_user_data ^ (uintptr_t)block2;
    ASSERT(check != (uintptr_t)0xDEADBEEF, "Block should have alignment padding");
    ASSERT(check % sizeof(uintptr_t) == 0, "Retrieved block pointer should be properly aligned");
    
    Block *block2_meta = (Block *)check;
    ASSERT(block2_meta != NULL, "Retrieved block2 metadata should not be NULL");
    ASSERT(get_em(block2_meta) == em, "Block2`s EM pointer should match the allocating EM");
    em_free(block2);
    
    em_free(temp_ptr2);

    em_free(shift);

    em_destroy(em);
}

void test_static_em_creation(void) {
    TEST_PHASE("Static EM Creation");

    TEST_CASE("Valid static EM creation");
    size_t static_em_size = 2048;
    void *static_memory = malloc(static_em_size);
    EM *static_em = em_create_static(static_memory, static_em_size);
    ASSERT(static_em != NULL, "Static EM creation with valid memory should succeed");

    TEST_CASE("Allocation from static EM");
    void *alloc1 = em_alloc(static_em, 512);
    ASSERT(alloc1 != NULL, "Allocation from static EM should succeed");

    void *alloc2 = em_alloc(static_em, 1024);
    ASSERT(alloc2 != NULL, "Second allocation from static EM should succeed");

    void *alloc3 = em_alloc(static_em, 1024); // This should fail
    ASSERT(alloc3 == NULL, "Allocation exceeding static EM capacity should fail");

    em_destroy(static_em);

    free(static_memory);
}

void test_freeing_invalid_blocks(void) {
    TEST_PHASE("Freeing Invalid Blocks");

    // Create an EM
    EM *em = em_create(1024);
    ASSERT(em != NULL, "EM creation should succeed");

    TEST_CASE("Freeing a pointer not allocated by the EM");
    struct {
        uintptr_t fake_backlink;
        int data;
    } stack_obj;

    stack_obj.fake_backlink = (uintptr_t)&stack_obj.data ^ 1;
    stack_obj.data = 42;

    em_free(&stack_obj.data); // Should safe return
    ASSERT(true, "Freeing stack variable should not crash");

    TEST_CASE("Freeing a pointer with valid magic number");
    Block fake_block = {0};
    fake_block.as.occupied.magic = 0xDEAFBEEF; // Valid magic number
    void *fake_data = (char*)&fake_block + sizeof(Block);
    em_free(fake_data); // Should not crash
    ASSERT(true, "Freeing block with valid magic number should not crash");

    em_reset(em);
    Block *first_block = em_get_first_block(em);
    Block *free_blocks = em_get_free_blocks(em);

    TEST_CASE("Freeing a pointer with invalid alignment");
    void *misaligned_ptr = (void *)((uintptr_t)em + 1);
    em_free(misaligned_ptr); // Should not crash
    ASSERT(true, "Freeing misaligned pointer should not crash");
    ASSERT(em_get_first_block(em) == first_block, "First block should remain unchanged after misaligned free");
    ASSERT(em_get_free_blocks(em) == free_blocks, "Free blocks should remain unchanged after misaligned free");


    TEST_CASE("Freeing a pointer from a different em");
    EM *another_em = em_create(1024);
    void *ptr = em_alloc(another_em, 32);
    em_free(ptr); // Should not crash
    ASSERT(true, "Freeing block from different em should not crash");
    em_destroy(another_em);

    em_destroy(em);
}

void test_calloc() {
    TEST_PHASE("EM Calloc Functionality");

    // Create an EM
    EM *em = em_create(1024);
    ASSERT(em != NULL, "EM creation should succeed");
    
    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    TEST_CASE("Calloc a block and verify zero-initialization");
    size_t num_elements = 10;
    size_t element_size = sizeof(int);
    int *array = (int *)em_calloc(em, num_elements, element_size);
    ASSERT(array != NULL, "Calloc should succeed");

    // Verify all elements are zero
    bool all_zero = true;
    for (size_t i = 0; i < num_elements; i++) {
        if (array[i] != 0) {
            all_zero = false;
            break;
        }
    }
    ASSERT(all_zero, "All elements in calloced array should be zero");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    em_free(array);
    ASSERT(true, "Freeing calloced block should succeed");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    TEST_PHASE("Calloc with overflow in size calculation");
    size_t large_nmemb = SIZE_MAX / 2;
    size_t large_size = 3;

    void *p_overflow = em_calloc(em, large_nmemb, large_size);
    ASSERT(p_overflow == NULL, "Calloc with true overflow should return NULL");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    int *null_em_array = (int *)em_calloc(NULL, 10, sizeof(int));
    ASSERT(null_em_array == NULL, "Calloc with NULL em should return NULL");
    
    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    int *zero_nmemb_array = (int *)em_calloc(em, 0, sizeof(int));
    ASSERT(zero_nmemb_array == NULL, "Calloc with zero nmemb should return NULL");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    em_destroy(em);
    em = em_create(1000);

    void *almost_full = em_alloc(em, 751); // Fill up the em
    ASSERT(almost_full != NULL, "Allocation to nearly fill em should succeed");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    void *tail = em_alloc(em, 152);
    ASSERT(tail != NULL, "Allocation to fill em should succeed");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    em_destroy(em);
}

void test_em_reset_zero(void) {
    TEST_PHASE("EM Reset Zero");

    TEST_CASE("Setup and dirtying memory");
    size_t em_size = 4096;
    EM *em = em_create(em_size);
    size_t em_init_free_size = free_size_in_tail(em);
    ASSERT(em != NULL, "Dynamic em creation should succeed");

    size_t data_size = 256;
    unsigned char *ptr1 = (unsigned char *)em_alloc(em, data_size);
    ASSERT(ptr1 != NULL, "Allocation 1 should succeed");
    
    memset(ptr1, 0xAA, data_size);
    ASSERT(ptr1[0] == 0xAA && ptr1[data_size - 1] == 0xAA, "Memory should be writable");

    unsigned char *ptr2 = (unsigned char *)em_alloc(em, data_size);
    ASSERT(ptr2 != NULL, "Allocation 2 should succeed");
    memset(ptr2, 0xBB, data_size);

    TEST_CASE("Execute reset_zero");
    em_reset_zero(em);
    ASSERT(free_size_in_tail(em) > 0, "EM should have free space after reset_zero");
    ASSERT(free_size_in_tail(em) == em_init_free_size, "EM free size should be reset to initial state");

    TEST_CASE("Verify memory zeroing");

    int is_zero_1 = 1;
    for (size_t i = 0; i < data_size; i++) {
        if (ptr1[i] != 0) {
            is_zero_1 = 0;
            break;
        }
    }
    ASSERT(is_zero_1, "Memory at ptr1 should be strictly zeroed");

    int is_zero_2 = 1;
    for (size_t i = 0; i < data_size; i++) {
        if (ptr2[i] != 0) {
            is_zero_2 = 0;
            break;
        }
    }
    ASSERT(is_zero_2, "Memory at ptr2 (tail) should be strictly zeroed");

    TEST_CASE("Verify EM state reset");
    unsigned char *new_ptr = (unsigned char *)em_alloc(em, data_size);
    ASSERT(new_ptr != NULL, "Re-allocation after reset should succeed");
    ASSERT(new_ptr == ptr1, "Allocator should reset tail to the beginning");
    ASSERT(new_ptr[0] == 0, "New allocation should point to the zeroed memory");

    em_destroy(em);
}


// --- Alignment Abstraction Layer ---
#define TEST_BASE_ALIGNMENT 4096

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
    // C11 Standard
    #include <stdalign.h>
    #define ALIGN_PREFIX(N) alignas(N)
    #define ALIGN_SUFFIX(N)
    #define HAS_NATIVE_ALIGN 1

#elif defined(_MSC_VER)
    // MSVC
    #define ALIGN_PREFIX(N) __declspec(align(N))
    #define ALIGN_SUFFIX(N)
    #define HAS_NATIVE_ALIGN 1

#elif defined(__GNUC__) || defined(__clang__)
    // GCC / Clang (Extension)
    #define ALIGN_PREFIX(N)
    #define ALIGN_SUFFIX(N) __attribute__((aligned(N)))
    #define HAS_NATIVE_ALIGN 1

#else
    // C99 Fallback (No native support)
    #define ALIGN_PREFIX(N)
    #define ALIGN_SUFFIX(N)
    #define HAS_NATIVE_ALIGN 0
#endif


#if HAS_NATIVE_ALIGN
    #define BUFFER_OVERHEAD 0
#else
    #define BUFFER_OVERHEAD TEST_BASE_ALIGNMENT
#endif


static ALIGN_PREFIX(TEST_BASE_ALIGNMENT) 
char master_test_buffer[16384 + BUFFER_OVERHEAD] 
ALIGN_SUFFIX(TEST_BASE_ALIGNMENT);


static void* get_exact_alignment_ptr(size_t offset) {
    uintptr_t raw = (uintptr_t)master_test_buffer;
    uintptr_t base = (raw + (TEST_BASE_ALIGNMENT - 1)) & ~((uintptr_t)TEST_BASE_ALIGNMENT - 1);

    return (void*)(base + offset);
}


static size_t get_buffer_size(void *start) {
    uintptr_t raw = (uintptr_t)master_test_buffer;
    uintptr_t end = raw + 16384;
    uintptr_t aligned_start = (uintptr_t)start;

    return (end - aligned_start);
}


static size_t count_blocks_in_em(EM *em) {
    size_t count = 0;
    Block *current = em_get_first_block(em);
    while (current != NULL) {
        count++;
        current = next_block(em, current);
    }
    return count;
}


void test_alignment_alloc(void) {
    void *buffer = get_exact_alignment_ptr(8);
    size_t size = get_buffer_size(buffer);

    ASSERT(((uintptr_t)(buffer) % 8 == 0),   "Allocation should     be   8-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 16 != 0),  "Allocation should not be  16-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 32 != 0),  "Allocation should not be  32-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 64 != 0),  "Allocation should not be  64-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 128 != 0), "Allocation should not be 128-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 256 != 0), "Allocation should not be 256-byte aligned");
    ASSERT(((uintptr_t)(buffer) % 512 != 0), "Allocation should not be 512-byte aligned");
    
    TEST_PHASE("Test alignment requirements with base 8-byte aligned EM");

    // ---------------------------------------------------------
    TEST_CASE("CASE 1: ReqAlign = 8 (Ideal)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        
        void *p1 = alloc_in_tail_full(em, 50, 8);
        ASSERT(p1 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p1 % 8 == 0, "Allocation should be properly 8-byte aligned");

        Block *tail = em_get_first_block(em);
        uintptr_t expected_data = (uintptr_t)tail + sizeof(Block);
        
        ASSERT((uintptr_t)p1 == expected_data, "Should correspond to zero padding");
        ASSERT(count_blocks_in_em(em) == 2, "No split should happen, only one block allocated in EM");
    }

    // ---------------------------------------------------------
    TEST_CASE("CASE 2: ReqAlign = 16 (Small Shift / XOR Link)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        Block *initial_first_block = em_get_tail(em);
        
        void *p2 = alloc_in_tail_full(em, 50, 16);
        ASSERT(p2 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p2 % 16 == 0, "Allocation should be properly 16-byte aligned");
        
        uintptr_t raw_data = (uintptr_t)initial_first_block + sizeof(Block);
        size_t padding = (uintptr_t)p2 - raw_data;
        ASSERT(padding == 8, "Padding should be exactly 8 bytes");
   
        ASSERT(em_get_first_block(em) == initial_first_block, "First block should not change (no split)");
        ASSERT(count_blocks_in_em(em) == 2, "No split should happen, only one block allocated in EM");
    }

    // ---------------------------------------------------------
    TEST_CASE("CASE 3: ReqAlign = 128 (Big Shift / Split)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        
        void *p3 = alloc_in_tail_full(em, 50, 128);
        
        ASSERT(p3 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p3 % 128 == 0, "Allocation should be properly 128-byte aligned");

        Block *new_first_block = em_get_first_block(em);
        ASSERT(new_first_block != (Block *)((char *)p3 - sizeof(Block)), "First block pointer MUST change (split happened)");
        ASSERT(count_blocks_in_em(em) == 3, "Split should happen, two blocks allocated in EM");
    }

    
    TEST_PHASE("Test Tail Absorption (Fill remaining space)");

    // ---------------------------------------------------------
    TEST_CASE("CASE 4: ReqAlign = 8 (Ideal + Absorb Tail)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        size_t capacity = free_size_in_tail(em);
        void *p4 = alloc_in_tail_full(em, capacity, 8);
        
        ASSERT(p4 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p4 % 8 == 0, "Allocation should be properly 8-byte aligned");

        ASSERT(count_blocks_in_em(em) == 1, "Should absorb tail, leaving 1 block total");
        ASSERT(free_size_in_tail(em) == 0, "Free space should be 0");
    }

    // ---------------------------------------------------------
    TEST_CASE("CASE 5: ReqAlign = 16 (Small Shift + Absorb Tail)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        size_t total_free = free_size_in_tail(em);
        
        size_t padding = 8;
        size_t alloc_size = total_free - padding;

        void *p5 = alloc_in_tail_full(em, alloc_size, 16);
        
        ASSERT(p5 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p5 % 16 == 0, "Alignment check");
        
        ASSERT(count_blocks_in_em(em) == 1, "Should absorb tail with internal padding, 1 block total");
        ASSERT(free_size_in_tail(em) == 0, "Free space should be 0");
    }

    // ---------------------------------------------------------
    TEST_CASE("CASE 6: ReqAlign = 128 (Big Shift/Split + Absorb Tail)");
    {
        EM *em = em_create_static_aligned(buffer, size, 8);
        size_t total_free = free_size_in_tail(em);
        
        size_t padding = 103;
        
        size_t alloc_size = total_free - padding;

        void *p6 = alloc_in_tail_full(em, alloc_size, 128);
        
        ASSERT(p6 != NULL, "Alloc should succeed");
        ASSERT((uintptr_t)p6 % 128 == 0, "Alignment check");

        ASSERT(count_blocks_in_em(em) == 2, "Split happened + Tail absorbed = 2 blocks total");
        ASSERT(free_size_in_tail(em) == 0, "Free space should be 0");
        
        #ifdef DEBUG
        print_em(em);
        #endif
    }
}

void test_static_em_detector_coverage(void) {
    TEST_CASE("Force Magic LSB Detector coverage");

    size_t alignment = 64; 
    size_t total_size = 1024;
    char raw_memory[2048];

    uintptr_t base = align_up((uintptr_t)raw_memory, alignment);

    void *bad_ptr = (void *)(base + 1);

    EM *em = em_create_static_aligned(bad_ptr, total_size, alignment);

    ASSERT(em != NULL, "EM should be created");

    Block *first = em_get_first_block(em);
    uintptr_t *detector_spot = (uintptr_t *)((char *)first - sizeof(uintptr_t));
    ASSERT((*detector_spot & 1) == 1, "Magic LSB Detector should be set");
}

void test_tail_alloc_edge_case_deterministic(void) {
    TEST_CASE("Tail Allocation Edge Case - Deterministic");
    char raw[512];
    void *mem = (void*)align_up((uintptr_t)raw, 64);
    EM *em = em_create_static_aligned(mem, 256, 16);

    size_t target_remainder = BLOCK_MIN_SIZE + 12;
    size_t initial_free = free_size_in_tail(em);
    em_alloc(em, initial_free - target_remainder);
    void *p2 = em_alloc(em, 4);

    ASSERT(p2 != NULL, "This should trigger the 'final_needed_block_size = free_space' branch");
}

void test_scratch_allocation_and_freeing(void) {
    TEST_PHASE("Scratch EM Allocation and Freeing");

    TEST_CASE("Create EM and allocate scratch EM");
    EM *em = em_create(2048);
    ASSERT(em != NULL, "EM creation should succeed");
    size_t free_in_tail = free_size_in_tail(em);

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    size_t scratch_size = 512;
    void *ptr = em_alloc_scratch(em, scratch_size);
    ASSERT(ptr != NULL, "Scratch allocationshould succeed");
    ASSERT(free_size_in_tail(em) <= free_in_tail - scratch_size, "EM free size should decrease after scratch allocation");

    #ifdef DEBUG
    print_fancy(em, 100);
    #endif

    TEST_CASE("Try to allocate too large chunk");
    void *large_ptr = em_alloc(em, 2048-512);
    ASSERT(large_ptr == NULL, "Oversized allocation should fail");

    TEST_CASE("Allocate small chunk after scratch");
    void *small_ptr = em_alloc(em, 128);
    ASSERT(small_ptr != NULL, "Small allocation should succeed after scratch allocation");

    em_free(small_ptr);

    TEST_CASE("Free scratch block");
    em_free(ptr);
    ASSERT(free_size_in_tail(em) == free_in_tail, "EM free size should restore after freeing scratch block");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    TEST_CASE("Try allocate big chunk after freeing scratch");
    void *large_ptr2 = em_alloc(em, (2048-512));
    ASSERT(large_ptr2 != NULL, "Big allocation should succeed");

    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif

    TEST_CASE("Allocation with alignment");
    void *aligned_ptr = em_alloc_scratch_aligned(em, 128, 64);
    ASSERT(aligned_ptr != NULL, "Aligned scratch allocation should succeed");
    ASSERT(((uintptr_t)aligned_ptr % 64) == 0, "Aligned scratch allocation should meet alignment requirement");
    
    #ifdef DEBUG
    print_fancy(em, 100);
    print_em(em);
    #endif
    
    em_free(aligned_ptr);

    em_destroy(em);
}

void test_invalid_scratch_allocation(void) {
    TEST_PHASE("Invalid Scratch Allocation Scenarios");

    // Create an EM
    EM *em = em_create(1024);
    ASSERT(em != NULL, "EM creation should succeed");

    TEST_CASE("Zero size scratch allocation");
    void *zero_size = em_alloc_scratch(em, 0);
    ASSERT(zero_size == NULL, "Zero size scratch allocation should return NULL");

    TEST_CASE("Negative size scratch allocation");
    void *negative_size = em_alloc_scratch(em, -1);
    ASSERT(negative_size == NULL, "Negative size scratch allocation should return NULL");

    TEST_CASE("NULL EM scratch allocation");
    void *null_em = em_alloc_scratch(NULL, 32);
    ASSERT(null_em == NULL, "Scratch allocation with NULL EM should return NULL");

    TEST_CASE("Scratch allocation larger than EM size");
    void *huge_scratch_allocation = em_alloc_scratch(em, 2048);
    ASSERT(huge_scratch_allocation == NULL, "Scratch allocation larger than EM size should fail");

    TEST_CASE("Alignment larger than MAX_ALIGNMENT");
    void *bad_align_alloc = em_alloc_scratch_aligned(em, 32, 32 + MAX_ALIGNMENT);
    ASSERT(bad_align_alloc == NULL, "Scratch allocation with alignment larger than MAX_ALIGNMENT should fail");

    TEST_CASE("Invalid alignment (not power of two)");
    void *invalid_align_alloc = em_alloc_scratch_aligned(em, 32, 3);
    ASSERT(invalid_align_alloc == NULL, "Scratch allocation with invalid alignment should fail");

    em_destroy(em);
}

void test_scratch_em_creation_and_freeing(void) {
    TEST_PHASE("Scratch EM Creation and Freeing");

    TEST_CASE("Create scratch EM from valid EM");
    EM *em = em_create(2048);
    ASSERT(em != NULL, "EM creation should succeed");
    size_t initial_free = free_size_in_tail(em);

    size_t scratch_em_size = 512;
    EM *scratch_em = em_create_scratch(em, scratch_em_size);
    ASSERT(scratch_em != NULL, "Scratch EM creation should succeed");
    ASSERT(free_size_in_tail(em) <= 2048 - scratch_em_size, "EM free size should decrease after scratch EM creation");

    TEST_CASE("Allocate from scratch EM");
    void *scratch_alloc = em_alloc(scratch_em, 256);
    ASSERT(scratch_alloc != NULL, "Allocation from scratch EM should succeed");

    TEST_CASE("Free scratch EM");
    em_destroy(scratch_em);
    ASSERT(free_size_in_tail(em) == initial_free, "EM free size should restore after freeing scratch EM");

    TEST_CASE("Attempt to create oversized scratch EM");
    EM *oversized_scratch_em = em_create_scratch(em, 4096);
    ASSERT(oversized_scratch_em == NULL, "Oversized scratch EM creation should fail");

    TEST_CASE("Attempt to create scratch EM from NULL EM");
    EM *null_scratch_em = em_create_scratch(NULL, 256);
    ASSERT(null_scratch_em == NULL, "Scratch EM creation from NULL EM should fail");

    TEST_CASE("Attempt to create scratch EM with zero size");
    EM *zero_size_scratch_em = em_create_scratch(em, 0);
    ASSERT(zero_size_scratch_em == NULL, "Scratch EM creation with zero size should fail");

    TEST_CASE("Attempt to create scratch EM with negative size");
    EM *negative_size_scratch_em = em_create_scratch(em, -1);
    ASSERT(negative_size_scratch_em == NULL, "Scratch EM creation with negative size should fail");

    TEST_CASE("Attempt to create scratch EM with custom alignment");
    EM *custom_align_scratch_em = em_create_scratch_aligned(em, 256, 32);
    ASSERT(custom_align_scratch_em != NULL, "Scratch EM creation with custom alignment should succeed");
    em_destroy(custom_align_scratch_em);

    TEST_CASE("Attempt to create scratch EM with invalid alignment");
    EM *invalid_align_scratch_em = em_create_scratch_aligned(em, 256, 3);
    ASSERT(invalid_align_scratch_em == NULL, "Scratch EM creation with invalid alignment should fail");

    em_destroy(em);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_min_exponent();
    test_invalid_allocations();
    test_invalid_em_creation();
    test_boundary_conditions();
    test_full_em_allocation();
    test_custom_alignment_alloc();
    test_static_em_creation();
    test_freeing_invalid_blocks();
    test_calloc();
    test_em_reset_zero();
    test_alignment_alloc();
    test_static_em_detector_coverage();
    test_tail_alloc_edge_case_deterministic();
    test_scratch_allocation_and_freeing();
    test_invalid_scratch_allocation();
    test_scratch_em_creation_and_freeing();
    
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
} 