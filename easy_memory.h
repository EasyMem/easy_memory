#ifndef EASY_MEMORY_H
#define EASY_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#if defined(_MSVC_VER)
#include <intrin.h>
#endif


#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || defined(__cplusplus)
#   include <assert.h>
#   define EM_STATIC_ASSERT(cond, msg) static_assert(cond, #msg)
#else
#   define EM_STATIC_ASSERT_HELPER(cond, line) typedef char static_assertion_at_line_##line[(cond) ? 1 : -1]
#   define EM_STATIC_ASSERT(cond, msg) EM_STATIC_ASSERT_HELPER(cond, __LINE__)
#endif

    
#ifdef DEBUG
    #include <assert.h>
    #define EM_ASSERT(cond) assert(cond)
#else
    #if defined(__GNUC__) || defined(__clang__)
        #define EM_ASSERT(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
    #elif defined(_MSVC_VER)
        #define EM_ASSERT(cond) __assume(cond)
    #else
        #define EM_ASSERT(cond) ((void)0)
    #endif
#endif


#ifndef EM_POISON_BYTE
#   define EM_POISON_BYTE 0xDD
#endif


#if defined(EM_NO_POISONING)
#   if defined(EM_POISONING)
#       undef EM_POISONING
#   endif
#elif defined(DEBUG) && !defined(EM_POISONING)
#   define EM_POISONING
#endif


#ifndef EM_MIN_BUFFER_SIZE
    // Default minimum buffer size for the single block.
#   define EM_MIN_BUFFER_SIZE 16 
#endif
EM_STATIC_ASSERT(EM_MIN_BUFFER_SIZE > 0, "MIN_BUFFER_SIZE must be a positive value to prevent creation of useless zero-sized free blocks.");

#define EM_DEFAULT_ALIGNMENT 16 // Default memory alignment


#if defined(__GNUC__) || defined(__clang__)
    #define MIN_EXPONENT (__builtin_ctz(sizeof(uintptr_t)))
#else
    #define MIN_EXPONENT ( \
        (sizeof(uintptr_t) == 4) ? 2 : \
        (sizeof(uintptr_t) == 8) ? 3 : \
        4                              \
    )
#endif


#define MAX_ALIGNMENT ((size_t)(256 << MIN_EXPONENT))
#define MIN_ALIGNMENT ((size_t)sizeof(uintptr_t))

// size_and_alignment field masks
#define ALIGNMENT_MASK     ((uintptr_t)7)
#define SIZE_MASK         (~(uintptr_t)7)

// prev field masks
#define IS_FREE_FLAG       ((uintptr_t)1)
#define COLOR_FLAG         ((uintptr_t)2) 
#define PREV_MASK         (~(uintptr_t)3)

// tail field masks
#define IS_DYNAMIC_FLAG    ((uintptr_t)1)
#define IS_NESTED_FLAG     ((uintptr_t)2)
#define TAIL_MASK         (~(uintptr_t)3)

// free_blocks field masks
#define IS_PADDING         ((uintptr_t)1)
#define HAS_SCRATCH_FLAG   ((uintptr_t)2)
#define FREE_BLOCKS_MASK  (~(uintptr_t)3)


#define RED false
#define BLACK true

#define BLOCK_MIN_SIZE (sizeof(Block) + EM_MIN_BUFFER_SIZE)
#define EM_MIN_SIZE (sizeof(EM) + BLOCK_MIN_SIZE)


#define block_data(block) ((void *)((char *)(block) + sizeof(Block)))

// Structure type declarations for memory management
typedef struct Block Block;
typedef struct EM    EM;
typedef struct Bump  Bump;

/*
 * Memory block structure
 * Represents a chunk of memory and metadata for its management within the EM memory system
 */
struct Block {
    size_t size_and_alignment;   // Size of the data block.
    Block *prev;                 // Pointer to the previous block in the global list, also stores flags via pointer tagging.

    union {
        struct {
            Block *left_free;     // Left child in red-black tree
            Block *right_free;    // Right child in red-black tree
        } free;
        struct {
            EM *em;               // Pointer to the EM instance that allocated this block
            uintptr_t magic;      // Magic number for validation random pointer
        } occupied;
    } as;
};

/*
 * Bump allocator structure
 * A simple allocator that allocates memory linearly from a pre-allocated block
 */
struct Bump {
    union {
        Block block_representation;  // Block representation for compatibility
        struct {
            size_t capacity;         // Total capacity of the bump allocator
            Block *prev;             // Pointer to the previous block in the global list, need for compatibility with block struct layout
            EM *em;                  // Pointer to the EM instance that allocated this block
            size_t offset;           // Current offset for allocations within the bump allocator
        } self;
    } as;
};

EM_STATIC_ASSERT((sizeof(Bump) == sizeof(Block)), Size_mismatch_between_Bump_and_Block);

/*
 * Easy Memory structure
 * Manages a pool of memory, block allocation, and block states
 */
struct EM {
    union {
        Block block_representation;         // Block representation for compatibility
        struct {
            size_t capacity_and_alignment;  // Total capacity of the easy memory
            Block *prev;                    // Pointer to the previous block in the global list, need for compatibility with block struct layout
            Block *tail;                    // Pointer to the last block in the global list, also stores is_dynamic flag via pointer tagging
            Block *free_blocks;             // Pointer to the tree of free blocks
        } self;
    } as;
};

EM_STATIC_ASSERT((sizeof(Bump) == sizeof(Block)), Size_mismatch_between_Bump_and_Block);


#ifdef DEBUG
#include <stdio.h>
#include <math.h>
void print_em(EM *em);
void print_fancy(EM *em, size_t bar_size);
void print_llrb_tree(Block *node, int depth);
#endif // DEBUG


// EM specific functions
// EM creation functions
#ifndef EM_NO_MALLOC
EM *em_create(size_t size);
EM *em_create_aligned(size_t size, size_t alignment);
#endif // EM_NO_MALLOC

EM *em_create_static(void *memory, size_t size);
EM *em_create_static_aligned(void *memory, size_t size, size_t alignment);

EM *em_create_nested(EM *parent_em, size_t size);
EM *em_create_nested_aligned(EM *parent_em, size_t size, size_t alignment);

EM *em_create_scratch(EM *parent_em, size_t size);
EM *em_create_scratch_aligned(EM *parent_em, size_t size, size_t alignment);

// EM reset and destroy functions
void em_reset(EM *em);
void em_reset_zero(EM *em);
void em_destroy(EM *em);
void em_free_scratch(EM *em); // Optional free scratch memory function. Try not to use it. 
//                               Recommended to call em_free or dedicated em_destroy_* for specific objects. 
//                               Yes, EM know what blocks\sub-allocators are scratch and can free\destroy them properly. 

// Allocation functions
void *em_alloc(EM *em, size_t size);
void *em_alloc_aligned(EM *em, size_t size, size_t alignment);

void *em_alloc_scratch(EM *em, size_t size);
void *em_alloc_scratch_aligned(EM *em, size_t size, size_t alignment);

// Calloc function
void *em_calloc(EM *em, size_t nmemb, size_t size);
void em_free(void *data);



// Bump allocator specific functions
Bump *em_create_bump(EM *em, size_t size);
void *em_bump_alloc(Bump *bump, size_t size);
void *em_bump_alloc_aligned(Bump *bump, size_t size, size_t alignment);
void em_bump_trim(Bump *bump);
void em_bump_reset(Bump *bump);
void em_bump_destroy(Bump *bump);



#ifdef EASY_MEMORY_IMPLEMENTATION

/*
 * Helper function to Align up
 * Rounds up the given size to the nearest multiple of alignment
 */
static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/*
 * Helper function to Align down
 * Rounds down the given size to the nearest multiple of alignment
 */
static inline size_t align_down(size_t size, size_t alignment) {
    return size & ~(alignment - 1);
}

/*
 * Helper function to find minimum exponent of a number
 * Returns the position of the least significant set bit
 */
static inline size_t min_exponent_of(size_t num) {
    if (num == 0) return 0; // Undefined for zero, return 0 as a safe default

    // Use compiler built-ins if available for efficiency
    #if defined(__GNUC__) || defined(__clang__)
        return __builtin_ctz(num);
    #elif defined(_MSVC_VER)
        unsigned long index;
        #if defined(_M_X64) || defined(_M_ARM64)
            _BitScanForward64(&index, num);
        #else
            _BitScanForward(&index, num);
        #endif
        return index;
    #else
        size_t s = num;
        size_t zeros = 0;
        while ((s >>= 1) != 0) ++zeros;
        return zeros;
    #endif
}

/*
 * Get alignment from block
 * Extracts the alignment information stored in the block's size_and_alignment field
 */
static inline size_t get_alignment(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_alignment' called on NULL block");

    size_t exponent = (block->size_and_alignment & ALIGNMENT_MASK) + MIN_EXPONENT; // Extract exponent and adjust by MIN_EXPONENT
    size_t alignment = (size_t)1 << (exponent); // Calculate alignment as power of two

    return alignment;
}

/*
 * Set alignment for block
 * Updates the alignment information in the block's size_and_alignment field
 * Valid alignment range (power of two):
 *  -- 32 bit system: [4 ... 512]
 *  -- 64 bit system: [8 ... 1024]
 */
static inline void set_alignment(Block *block, size_t alignment) {
    EM_ASSERT((block != NULL)                      && "Internal Error: 'set_alignment' called on NULL block");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'set_alignment' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'set_alignment' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'set_alignment' called on too big alignment");

    /*
     * How does that work?
     * Alignment is always a power of two, so instead of storing the alignment directly and wasting full 4-8 bytes, we can represent it as 2^n.
     * Since minimum alignment is 2^MIN_EXPONENT, we can store only the exponent minus MIN_EXPONENT in 3 bits(value 0-7).
     * For example:
     *  - On 32-bit system (MIN_EXPONENT = 2):
     *       Alignment 4     ->  2^2  ->  2-2  ->  stored as 0
     *       Alignment 8     ->  2^3  ->  3-2  ->  stored as 1
     *       Alignment 16    ->  2^4  ->  4-2  ->  stored as 2
     *       ... and so on up to
     *       Alignment 512   ->  2^9  ->  9-2  ->  stored as 7
     * 
     *  - On 64-bit system (MIN_EXPONENT = 3):
     *       Alignment 8     ->  2^3  ->  3-3  ->  stored as 0
     *       Alignment 16    ->  2^4  ->  4-3  ->  stored as 1
     *       Alignment 32    ->  2^5  ->  5-3  ->  stored as 2
     *       ... and so on up to 
     *       Alignment 1024  -> 2^10  -> 10-3  ->  stored as 7
     * This way, we efficiently use only 3 bits to cover the full range alignments that could be potentially used within the size_and_alignment field.
    */ 
    
    size_t exponent = min_exponent_of(alignment >> MIN_EXPONENT); // Calculate exponent from alignment

    size_t spot = block->size_and_alignment & ALIGNMENT_MASK; // Preserve current alignment bits
    block->size_and_alignment = block->size_and_alignment ^ spot; // Clear current alignment bits

    block->size_and_alignment = block->size_and_alignment | exponent;  // Set new alignment bits
}



/*
 * Get size from block
 * Extracts the size information stored in the block's size_and_alignment field
 */
static inline size_t get_size(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_size' called on NULL block");

    return block->size_and_alignment >> 3;
}

/*
 * Set size for block
 * Updates the size information in the block's size_and_alignment field
 * Valid size range (limited by 3-bit reserved for alignment/flags):
 *  -- 32-bit system: [0] U [EM_MIN_BUFFER_SIZE ... 512 MiB] (2^29 bytes)
 *  -- 64-bit system: [0] U [EM_MIN_BUFFER_SIZE ... 2 EiB]   (2^61 bytes)
 */
static inline void set_size(Block *block, size_t size) {
    EM_ASSERT((block != NULL)      && "Internal Error: 'set_size' called on NULL block");
    EM_ASSERT((size <= SIZE_MASK)  && "Internal Error: 'set_size' called on too big size");

    /*
     * Why size limit?
     * Since we utilize 3 bits of size_and_alignment field for alignment/flags, we have the remaining bits available for size.
     * 
     * On 32-bit systems, size_t is 4 bytes (32 bits), so we have 29 bits left for size (32 - 3 = 29).
     * This gives us a maximum size of 2^29 - 1 = 536,870,911 bytes (approximately 512 MiB).
     * In 32-bit systems, where maximum addressable memory in user space is 2-3 GiB, this limitation is acceptable.
     * Bigger size is not practical since we cannot allocate a contiguous memory block that **literally** 30%+ of all accessible memory, 
     *  malloc is extremely likely to return NULL due to heap fragmentation.
     * Like, what you even gonna do with 1GB of contiguous memory, when even all operating system use ~1-2GB?
     * Play "Bad Apple" 8K 120fps via raw frames?
     * 
     * On the other hand,
     * 
     * On 64-bit systems, size_t is 8 bytes (64 bits), so we have 61 bits left for size (64 - 3 = 61).
     * This gives us a maximum size of 2^61 - 1 = 2,305,843,009,213,693,951 bytes (approximately 2 EiB).
     * In 64-bit systems, this limitation is practically non-existent since current hardware and OS limitations are far below this threshold.
     * 
     * Conclusion: This limitation is a deliberate trade-off that avoids any *real* constraints on both 32-bit and 64-bit systems while optimizing memory usage.
    */

    size_t alignment_piece = block->size_and_alignment & ALIGNMENT_MASK; // Preserve current alignment bits
    block->size_and_alignment = (size << 3) | alignment_piece; // Set new size while preserving alignment bits
}



/*
 * Get pointer to prev block from given block
 * Extracts the previous block pointer stored in the block's prev field
 */
static inline Block *get_prev(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_prev' called on NULL block");

    return (Block *)((uintptr_t)block->prev & PREV_MASK); // Clear flag bits to get actual pointer
}

/*
 * Set pointer to prev block for given block
 * Updates the previous block pointer in the block's prev field
 */
static inline void set_prev(Block *block, void *ptr) {
    EM_ASSERT((block != NULL) && "Internal Error: 'set_prev' called on NULL block");

    /*
     * Why pointer tagging?
     * Classic approach would be to have separate fields for pointer and flags, 
     *  but that would increase the size of the Block struct by an additional 8-16 bytes if kept separate,
     *  or by 4-8 bytes if we use bitfields.
     * Any of this ways would bloat the Block struct size dramatically, especially when we have tons of blocks in the easy memory.
     * 
     * Instead, by knowing that pointers are always aligned to at least 4 bytes (on 32-bit systems) or 8 bytes (on 64-bit systems),
     *  we can utilize that free space in the 2-3 least significant bits of the pointer to store our flags.
     * But because we want to have 32-bit support as well, we can only safely use 2 bits for flags,
     *  since on 32-bit systems, pointers are aligned to at least 4 bytes, meaning the last 2 bits are always zero.
     * 
     * This way, we can store our flags without increasing the size of the Block struct at all.
    */
    
    uintptr_t flags_tips = (uintptr_t)block->prev & ~PREV_MASK; // Preserve flag bits
    block->prev = (Block *)((uintptr_t)ptr | flags_tips); // Set new pointer while preserving flag bits
}



/*
 * Get is_free flag from block
 * Extracts the is_free flag stored in the block's prev field
 */
static inline bool get_is_free(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_is_free' called on NULL block");

    return (uintptr_t)block->prev & IS_FREE_FLAG; // Check the is_free flag bit
}

/*
 * Set is_free flag for block
 * Updates the is_free flag in the block's prev field
 */
static inline void set_is_free(Block *block, bool is_free) {
    EM_ASSERT((block != NULL) && "Internal Error: 'set_is_free' called on NULL block");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 1st least significant bit to store is_free flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(block->prev); // Get current pointer with flags
    if (is_free) {
        int_ptr |= IS_FREE_FLAG;  // Set the is_free flag bit
    }
    else {
        int_ptr &= ~IS_FREE_FLAG; // Clear the is_free flag bit
    }
    block->prev = (Block *)int_ptr; // Update the prev field with new flags
}



/*
 * Get color flag from block
 * Extracts the color flag stored in the block's prev field
 */
static inline bool get_color(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_color' called on NULL block");

    return ((uintptr_t)block->prev & COLOR_FLAG); // Check the color flag bit
}

/*
 * Set color flag for block
 * Updates the color flag in the block's prev field
 */
static inline void set_color(Block *block, bool color) {
    EM_ASSERT((block != NULL) && "Internal Error: 'set_color' called on NULL block");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 2nd least significant bit to store color flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(block->prev); // Get current pointer with flags
    if (color) {
        int_ptr |= COLOR_FLAG; // Set the color flag bit
    }
    else {
        int_ptr &= ~COLOR_FLAG; // Clear the color flag bit
    }
    block->prev = (Block *)int_ptr; // Update the prev field with new flags
}



/*
 * Get left child from block
 * Extracts the left child pointer stored in the block's as.free.left_free field
 */
static inline Block *get_left_tree(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_left_tree' called on NULL block");

    return block->as.free.left_free; // Return left child pointer
}

/*
 * Set left child for block
 * Updates the left child pointer in the block's as.free.left_free field
 */
static inline void set_left_tree(Block *parent_block, Block *left_child_block) {
    EM_ASSERT((parent_block != NULL) && "Internal Error: 'set_left_tree' called on NULL parent_block");

    parent_block->as.free.left_free = left_child_block; // Set left child pointer
}



/*
 * Get right child from block
 * Extracts the right child pointer stored in the block's as.free.right_free field
 */
static inline Block *get_right_tree(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_right_tree' called on NULL block");

    return block->as.free.right_free; // Return right child pointer
}

/*
 * Set right child for block
 * Updates the right child pointer in the block's as.free.right_free field
 */
static inline void set_right_tree(Block *parent_block, Block *right_child_block) {
    EM_ASSERT((parent_block != NULL) && "Internal Error: 'set_right_tree' called on NULL parent_block");

    parent_block->as.free.right_free = right_child_block; // Set right child pointer
}



/*
 * Get magic number from block
 * Extracts the magic number stored in the block's as.occupied.magic field
 */
static inline uintptr_t get_magic(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_magic' called on NULL block");

    return block->as.occupied.magic; // Return magic number
}

/*
 * Set magic number for block
 * Updates the magic number in the block's as.occupied.magic field
 */
static inline void set_magic(Block *block, void *user_ptr) {
    EM_ASSERT((block != NULL)    && "Internal Error: 'set_magic' called on NULL block");
    EM_ASSERT((user_ptr != NULL) && "Internal Error: 'set_magic' called on NULL user_ptr");

    /*
     * Why use magic and XOR with user pointer?
     * 
     * easy memory's main goal is to provide a simple external API, and 'free' is one of the most critical functions.
     * by allowing users to pass only the user pointer to 'free', we need a way to verify that the pointer is valid and was indeed allocated by our easy memory.
     * Using a magic number helps us achieve this by providing a unique identifier for each allocated block.
     * 
     * But storing a fixed magic number can be predictable and potentially exploitable.
     * By XORing the magic number with the user pointer, we create a unique magic value for each allocation.
     * This makes it significantly harder for an attacker to guess or forge valid magic numbers,
     *  enhancing the security and integrity of the memory management system.
    */

    block->as.occupied.magic = (uintptr_t)0xDEADBEEF ^ (uintptr_t)user_ptr; // Set magic number using XOR with user pointer
}

/*
 * Validate magic number for block
 * Checks if the magic number in the block matches the expected value based on the user pointer
 */
static inline bool is_valid_magic(const Block *block, const void *user_ptr) {
    EM_ASSERT((block != NULL)    && "Internal Error: 'is_valid_magic' called on NULL block");
    EM_ASSERT((user_ptr != NULL) && "Internal Error: 'is_valid_magic' called on NULL user_ptr");

    return ((get_magic(block) ^ (uintptr_t)user_ptr) == (uintptr_t)0xDEADBEEF); // Validate magic number by XORing with user pointer
}



/*
 * Get easy memory instace from block
 * Extracts the easy memory pointer stored in the block's as.occupied.em field
 */
static inline EM *get_em(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_em' called on NULL block");
    return block->as.occupied.em; // Return easy memory pointer
}

/*
 * Set easy memory for block
 * Updates the easy memory pointer in the block's as.occupied.em field
 */
static inline void set_em(Block *block, EM *em) {
    EM_ASSERT((block != NULL) && "Internal Error: 'set_em' called on NULL block");
    EM_ASSERT((em != NULL)    && "Internal Error: 'set_em' called on NULL em");
    block->as.occupied.em = em; // Set easy memory pointer
}



/*
 * Check if block is scratch block
 * Determines if the block is a scratch block based on its color and free status
 */
static inline bool get_is_in_scratch(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_is_in_scratch' called on NULL block");

    /*
     * Why are we sure that this 100% scratch block?
     * Because occupied blocks are always red.
     * So combination of occupied + black gives us unique state that we use to identify scratch block.
    */

    return (!get_is_free(block) && get_color(block) == BLACK);
}

/*
 * Set block as scratch or non-scratch
 * Updates the block's status to be a scratch block or not based on the is_scratch parameter
 */
static inline void set_is_in_scratch(Block *block, bool is_scratch) {
    EM_ASSERT((block != NULL) && "Internal Error: 'set_is_in_scratch' called on NULL block");

    /*
     * Why does that work?
     * Because occupied blocks are always red.
     * So combination of occupied + black gives us unique state that we use to identify scratch block.
    */

    set_is_free(block, !is_scratch); // Set free status based on scratch status
    if (is_scratch) {
        set_color(block, BLACK); // Set color to BLACK for scratch blocks
    }
    else {
        set_color(block, RED);   // Set color to RED for non-scratch blocks
    }
}





/*
 * get tail block from easy memory
 * Extracts the tail block pointer in the easy memory's as.self.tail field
 */
static inline Block *em_get_tail(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_tail' called on NULL easy memory");

    return (Block *)((uintptr_t)em->as.self.tail & TAIL_MASK);
}

/*
 * Set tail block for easy memory
 * Updates the tail block pointer in the easy memory's as.self.tail field
 */
static inline void em_set_tail(EM *em, Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'em_set_tail' called on NULL block");
    EM_ASSERT((em != NULL)    && "Internal Error: 'em_set_tail' called on NULL easy memory");

    /* 
     * See 'set_prev' for explanation of pointer tagging.
     * In this case we store is_dynamic and is_nested flags in the tail pointer.
    */

    uintptr_t flags_tips = (uintptr_t)em->as.self.tail & ~TAIL_MASK; // Preserve flag bits
    em->as.self.tail = (Block *)((uintptr_t)block | flags_tips); // set new pointer while preserving flag bits
}



/*
 * Get is_dynamic flag from easy memory
 * Extracts the is_dynamic flag stored in the easy memory's as.self.tail field
 */
static inline bool em_get_is_dynamic(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_is_dynamic' called on NULL easy memory");

    return ((uintptr_t)em->as.self.tail & IS_DYNAMIC_FLAG); // Check the is_dynamic flag bit
}

/*
 * Set is_dynamic flag for easy memory
 * Updates the is_dynamic flag in the easy memory's as.self.tail field
 */
static inline void em_set_is_dynamic(EM *em, bool is_dynamic) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_set_is_dynamic' called on NULL easy memory");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 1st least significant bit to store is_dynamic flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(em->as.self.tail); // Get current pointer with flags
    if (is_dynamic) {
        int_ptr |= IS_DYNAMIC_FLAG; // Set the is_dynamic flag bit
    }
    else {
        int_ptr &= ~IS_DYNAMIC_FLAG; // Clear the is_dynamic flag bit
    }
    em->as.self.tail = (Block *)int_ptr; // Update the tail field with new flags
}



/*
 * Get is easy memory nested
 * Retrieves the is_nested flag of the easy memory pointer
 */
static inline bool em_get_is_nested(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_is_nested' called on NULL easy memory");
    
    return ((uintptr_t)em->as.self.tail & IS_NESTED_FLAG);
}

/*
 * Set is easy memory nested
 * Updates the is_nested flag of the easy memory pointer
 */
static inline void em_set_is_nested(EM *em, bool is_nested) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_set_is_nested' called on NULL easy memory");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 2st least significant bit to store is_nested flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(em->as.self.tail);  // Get current pointer with flags
    if (is_nested) {
        int_ptr |= IS_NESTED_FLAG; // Set the is_nested flag bit
    }
    else {
        int_ptr &= ~IS_NESTED_FLAG; // Clear the is_nested flag bit
    }
    em->as.self.tail = (Block *)int_ptr; // Update the tail field with new flags
}



/*
 * Get has_padding flag from easy memory
 * Extracts the has_padding flag stored in the easy memory's as.self.free_blocks field
 */
static inline bool em_get_padding_bit(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_padding_bit' called on NULL easy memory");

    return ((uintptr_t)em->as.self.free_blocks & IS_PADDING); // Check the is_padding flag bit
}

/*
 * Set has_padding flag for easy memory
 * Updates the has_padding flag in the easy memory's as.self.free_blocks field
 */
static inline void em_set_padding_bit(EM *em, bool has_padding) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_set_padding_bit' called on NULL easy memory");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 1st least significant bit to store has_padding flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(em->as.self.free_blocks); // Get current pointer with flags
    if (has_padding) {
        int_ptr |= IS_PADDING; // Set the is_padding flag bit
    }
    else {
        int_ptr &= ~IS_PADDING; // Clear the is_padding flag bit
    }
    em->as.self.free_blocks = (Block *)int_ptr; // Update the free_blocks field with new flags
}



/*
 * Get has_scratch flag from easy memory
 * Extracts the has_scratch flag stored in the easy memory's as.self.free_blocks field
 */
static inline bool em_get_has_scratch(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_has_scratch' called on NULL easy memory");

    return ((uintptr_t)em->as.self.free_blocks & HAS_SCRATCH_FLAG); // Check the is_scratch flag bit
}

/*
 * Set has_scratch flag for easy memory
 * Updates the has_scratch flag in the easy memory's as.self.free_blocks field
 */
static inline void em_set_has_scratch(EM *em, bool has_scratch) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_set_has_scratch' called on NULL easy memory");

    /*
     * See 'set_prev' for explanation of pointer tagging.
     * Here we use 2nd least significant bit to store has_scratch flag. 
    */

    uintptr_t int_ptr = (uintptr_t)(em->as.self.free_blocks); // Get current pointer with flags
    if (has_scratch) {
        int_ptr |= HAS_SCRATCH_FLAG; // Set the has_scratch flag bit
    }
    else {
        int_ptr &= ~HAS_SCRATCH_FLAG; // Clear the has_scratch flag bit
    }
    em->as.self.free_blocks = (Block *)int_ptr; // Update the free_blocks field with new flags
}



/*
 * Get free blocks tree from easy memory
 * Extracts the pointer to the root of the free blocks tree stored in the easy memory's as.self.free_blocks field
 */
static inline Block *em_get_free_blocks(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_free_blocks' called on NULL easy memory");

    return (Block *)((uintptr_t)em->as.self.free_blocks & FREE_BLOCKS_MASK); // select only pointer bits
}

/*
 * Set free blocks tree for easy memory
 * Updates the pointer to the root of the free blocks tree in the easy memory's as.self.free_blocks field
 */
static inline void em_set_free_blocks(EM *em, Block *block) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_set_free_blocks' called on NULL easy memory");

    /* 
     * See 'set_prev' for explanation of pointer tagging.
     * In this case we store padding_bit and has_scratch flags in the free_blocks pointer.
    */

    uintptr_t flags_tips = (uintptr_t)em->as.self.free_blocks & ~FREE_BLOCKS_MASK; // Preserve flag bits
    em->as.self.free_blocks = (Block *)((uintptr_t)block | flags_tips); // set new pointer while preserving flag bits
}



/*
 * Get capacity from easy memory
 * Extracts the capacity information stored in the easy memory's as.block_representation field
 */
static inline size_t em_get_capacity(const EM *em) {
    EM_ASSERT(em != NULL && "Internal Error: 'em_get_capacity' called on NULL easy memory");

    /*
     * What is happening here?
     * By design, the Easy Memory struct if fully ABI compatible with Block struct, 
     *  we can safely use dedicated Block functions by treating EM as a Block.
    */

    return get_size(&(em->as.block_representation));
}

/*
 * Set capacity for easy memory
 * Updates the capacity information in the easy memory's as.block_representation field
 */
static inline void em_set_capacity(EM *em, size_t size) {
    EM_ASSERT((em != NULL)                            && "Internal Error: 'em_set_capacity' called on NULL easy memory");
    EM_ASSERT(((size == 0 || size >= BLOCK_MIN_SIZE)) && "Internal Error: 'em_set_capacity' called on too small size");
    EM_ASSERT((size <= SIZE_MASK)                     && "Internal Error: 'em_set_capacity' called on too big size");

    /*
     * What is happening here?
     * By design, the Easy Memory struct if fully ABI compatible with Block struct, 
     *  we can safely use dedicated Block functions by treating EM as a Block.
    */

    set_size(&(em->as.block_representation), size);
}



/*
 * Get alignment from easy memory
 * Extracts the alignment information stored in the easy memory's as.block_representation field
 */
static inline size_t em_get_alignment(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_alignment' called on NULL easy memory");
    /*
     * What is happening here?
     * By design, the Easy Memory struct if fully ABI compatible with Block struct, 
     *  we can safely use dedicated Block functions by treating EM as a Block.
    */

    return get_alignment(&(em->as.block_representation));
}

/*
 * Set alignment for easy memory
 * Updates the alignment information in the easy memory's as.block_representation field
 */
static inline void em_set_alignment(EM *em, size_t alignment) {
    EM_ASSERT((em != NULL)                         && "Internal Error: 'em_set_alignment' called on NULL easy memory");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'em_set_alignment' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'em_set_alignment' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'em_set_alignment' called on too big alignment");
    
    /*
     * What is happening here?
     * By design, the Easy Memory struct if fully ABI compatible with Block struct, 
     *  we can safely use dedicated Block functions by treating EM as a Block.
    */

    set_alignment(&(em->as.block_representation), alignment);
}



/*
 * Get first block in easy memory
 * Calculates the pointer to the first block in the easy memory based on its alignment
 */
static inline Block *em_get_first_block(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_first_block' called on NULL easy memory");
    
    /*
     * What is happening here?
     * Since the easy memory_c uses alignment for blocks, the first block may not be located immediately after the Easy Memory struct.
     * For example(on 64-bit systems), if the easy memory alignment is 32 bytes, and the size of Easy Memory(EM) struct is 32 bytes (4 machine words),
     *  malloc or stack allocation may return an address with 8 byte alignment, so to have the userdata of first block aligned to desired 32 bytes,
     *  we need to calculate padding after the EM struct that, after adding Block size metadata, reaches the next 32-byte aligned address.
     * 
     * Easy Memory does that calculation automatically while created, to ensure alignment requirements are met.
     * 
     * To find the first block, we need to calculate its address based on the easy memory's alignment.
    */

    size_t align = em_get_alignment(em); // Get easy memory alignment
    uintptr_t raw_start = (uintptr_t)em + sizeof(EM); // Calculate raw start address of the first block

    uintptr_t aligned_start = align_up(raw_start + sizeof(Block), align) - sizeof(Block); // Align the start address to the easy memory's alignment
    
    return (Block *)aligned_start;
}





/*
 * Get easy memory from bump
 * Extracts the easy memory pointer stored in the block's as.occupied.em field
 */
static inline EM *bump_get_em(const Bump *bump) {
    EM_ASSERT((bump != NULL) && "Internal Error: 'bump_get_em' called on NULL bump");

    return get_em(&(bump->as.block_representation)); // Return pointer to the parent easy memory
}

/*
 * Set easy memory for bump
 * Updates the easy memory pointer in the block's as.occupied.em field
 */
static inline void bump_set_em(Bump *bump, EM *em) {
    EM_ASSERT((bump != NULL)  && "Internal Error: 'bump_set_em' called on NULL bump");
    EM_ASSERT((em != NULL)    && "Internal Error: 'bump_set_em' called on NULL easy memory");
    set_em(&(bump->as.block_representation), em); // Set pointer to the parent easy memory;
}



/*
 * Get offset from bump
 * Extracts the offset stored in the bump's as.self.offset field
 */
static inline size_t bump_get_offset(const Bump *bump) {
    EM_ASSERT((bump != NULL) && "Internal Error: 'bump_get_offset' called on NULL bump");

    return bump->as.self.offset;
}

/*
 * Set offset for bump
 * Updates the offset in the bump's as.self.offset field
 */
static inline void bump_set_offset(Bump *bump, size_t offset) {
    EM_ASSERT((bump != NULL)  && "Internal Error: 'bump_set_offset' called on NULL bump");

    bump->as.self.offset = offset;
}



/*
 * Get capacity of Bump
 * Extracts the size information stored in the bump's as.block_representation field
 */
static inline size_t bump_get_capacity(const Bump *bump) {
    EM_ASSERT((bump != NULL) && "Internal Error: 'bump_get_capacity' called on NULL bump");

    return get_size(&(bump->as.block_representation));
}

/*
 * Set capacity for Bump
 * Updates the size information in the bump's as.block_representation field
 */
static inline void bump_set_capacity(Bump *bump, size_t size) {
    EM_ASSERT((bump != NULL)  && "Internal Error: 'bump_set_capacity' called on NULL bump");

    return set_size(&(bump->as.block_representation), size);
}





/*
 * Get free size in tail block of easy memory
 * Calculates the amount of free space available in the tail block of the easy memory
 */
static inline size_t free_size_in_tail(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'free_size_in_tail' called on NULL easy memory");
   
    Block *tail = em_get_tail(em); // Get the tail block of the easy memory 
    if (!tail || !get_is_free(tail)) return 0;  // If tail is NULL or not free, that means no free space in tail

    size_t occupied_relative_to_em = (uintptr_t)tail + sizeof(Block) + get_size(tail) - (uintptr_t)em;
    
    size_t em_capacity = em_get_capacity(em);

    if (em_get_has_scratch(em)) {
        uintptr_t raw_end = (uintptr_t)em + em_capacity;
        uintptr_t aligned_end = align_down(raw_end, MIN_ALIGNMENT);

        size_t *stored_size_ptr = (size_t*)(aligned_end - sizeof(uintptr_t));

        em_capacity -= *stored_size_ptr; // Reduce capacity by scratch size
    }

    return em_capacity - occupied_relative_to_em; // Calculate free size in tail
}

/*
 * Get next block from given block (unsafe)
 * Calculates the pointer to the next block based on the current block's size
 * Note: This function does not perform any boundary checks, it just does pointer arithmetic.
 */
static inline Block *next_block_unsafe(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'next_block_unsafe' called on NULL block");

    return (Block *)((uintptr_t)block_data(block) + get_size(block)); // Calculate next block address based on current block's size
}

/*
 * Check if block is within easy memory bounds
 * Verifies if the given block is within the easy memory's capacity
 */
static inline bool is_block_within_em(const EM *em, const Block *block) {
    EM_ASSERT((em != NULL)    && "Internal Error: 'is_block_within_em' called on NULL easy memory");
    EM_ASSERT((block != NULL) && "Internal Error: 'is_block_within_em' called on NULL block");
 
    return ((uintptr_t)block >= (uintptr_t)em_get_first_block(em)) &&
           ((uintptr_t)block <  (uintptr_t)(em) + em_get_capacity(em)); // Check if block address is within easy memory bounds
}

/*
 * Check if block is in active part of easy memory
 * Verifies if the given block is within the active part of the easy memory (not in free tail)
 */
static inline bool is_block_in_active_part(const EM *em, const Block *block) {
    EM_ASSERT((em != NULL)    && "Internal Error: 'is_block_in_active_part' called on NULL easy memory");
    EM_ASSERT((block != NULL) && "Internal Error: 'is_block_in_active_part' called on NULL block");
    
    if (!is_block_within_em(em, block)) return false;

    return ((uintptr_t)block <= (uintptr_t)em_get_tail(em)); // Check if block address is within active part of easy memory
}

/*
 * Get next block from given block (safe)
 * Calculates the pointer to the next block if it is within the easy memory bounds
 * Returns NULL if the next block is out of easy memory bounds
 */
static inline Block *next_block(const EM *em, const Block *block) {
    EM_ASSERT((em != NULL)    && "Internal Error: 'next_block' called on NULL easy memory");
    EM_ASSERT((block != NULL) && "Internal Error: 'next_block' called on NULL block");
    
    Block *next_block = next_block_unsafe(block);
    if (!is_block_in_active_part(em, next_block)) return NULL; // Check if next block is within easy memory bounds
    return next_block;
}

/* 
 * Block creation functions
 * Functions to initialize new blocks within the easy memory
 */
static inline Block *create_block(void *point) {
    EM_ASSERT((point != NULL) && "Internal Error: 'create_block' called on NULL pointer");

    // Initialize block metadata
    Block *block = (Block *)point;
    set_size(block, 0);
    set_prev(block, NULL);
    set_is_free(block, true);
    set_color(block, RED);
    set_left_tree(block, NULL);
    set_right_tree(block, NULL);

    return block;
}

/*
 * Create next block in easy memory
 * Initializes a new block following the given previous block within the easy memory
 */
static inline Block *create_next_block(EM *em, Block *prev_block) {
    EM_ASSERT((em != NULL)         && "Internal Error: 'create_next_block' called on NULL easy memory");
    EM_ASSERT((prev_block != NULL) && "Internal Error: 'create_next_block' called on NULL prev_block");
    
    Block *next_block = NULL;
    if (is_block_within_em(em, prev_block)) {
        next_block = next_block_unsafe(prev_block);
        
        // Safety check - next block already exists
        if (is_block_in_active_part(em, next_block) && get_prev(next_block) == prev_block) return NULL;
    }
    // LCOV_EXCL_START
    else {
        // Safety check - prev_block is out of easy memory bounds
        EM_ASSERT(false && "Internal Error: 'create_next_block' called with prev_block out of easy memory bounds");
    }
    // LCOV_EXCL_STOP

    next_block = create_block(next_block);
    set_prev(next_block, prev_block);
    
    return next_block;
}

/*
 * Merge source into target
 * Source must be physically immediately after target.
 */
static inline void merge_blocks_logic(EM *em, Block *target, Block *source) {
    EM_ASSERT((em != NULL)      && "Internal Error: 'merge_blocks_logic' called on NULL easy memory");
    EM_ASSERT((target != NULL)  && "Internal Error: 'merge_blocks_logic' called on NULL target");
    EM_ASSERT((source != NULL)  && "Internal Error: 'merge_blocks_logic' called on NULL source");
    EM_ASSERT((next_block_unsafe(target) == source) && "Internal Error: 'merge_blocks_logic' called with non-adjacent blocks");

    size_t new_size = get_size(target) + sizeof(Block) + get_size(source);
    set_size(target, new_size);

    Block *following = next_block(em, target);
    if (following) {
        set_prev(following, target);
    }
}





/*
 * Rotate left
 * Used to balance the LLRB tree
 */
Block *rotateLeft(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'rotateLeft' called on NULL current_block");
    
    Block *x = get_right_tree(current_block);
    set_right_tree(current_block, get_left_tree(x));
    set_left_tree(x, current_block);

    set_color(x, get_color(current_block));
    set_color(current_block, RED);

    return x;
}

/*
 * Rotate right
 * Used to balance the LLRB tree
 */
Block *rotateRight(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'rotateRight' called on NULL current_block");
    
    Block *x = get_left_tree(current_block);
    set_left_tree(current_block, get_right_tree(x));
    set_right_tree(x, current_block);

    set_color(x, get_color(current_block));
    set_color(current_block, RED);

    return x;
}

/*
 * Flip colors
 * Used to balance the LLRB tree
 */
void flipColors(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'flipColors' called on NULL current_block");
    
    set_color(current_block, RED);
    set_color(get_left_tree(current_block), BLACK);
    set_color(get_right_tree(current_block), BLACK);
}

/*
 * Check if block is red
 * Helper function to check if a block is red in the LLRB tree
 */
static inline bool is_red(Block *block) {
    if (block == NULL) return false;
    return get_color(block) == RED;
}

/*
 * Balance LLRB tree
 * Balances the LLRB tree after insertions or deletions
 */
static Block *balance(Block *h) {
    EM_ASSERT((h != NULL) && "Internal Error: 'balance' called on NULL block");

    if (is_red(get_right_tree(h))) 
        h = rotateLeft(h);
    
    if (is_red(get_left_tree(h)) && is_red(get_left_tree(get_left_tree(h)))) 
        h = rotateRight(h);
    
    if (is_red(get_left_tree(h)) && is_red(get_right_tree(h))) 
        flipColors(h);

    return h;
}

/*
 * Insert block into LLRB tree
 * Inserts a new free block into the LLRB tree based on size, alignment, and address
 */
static Block *insert_block(Block *h, Block *new_block) {
    EM_ASSERT((new_block != NULL) && "Internal Error: 'insert_block' called on NULL new_block");

    /*
     * Logic Overview:
     * This function utilizes a "Triple-Key" insertion strategy to keep the free-block tree
     * highly optimized for subsequent "Best-Fit" searches:
     * 
     * 1. Primary Key: Size. 
     *    The tree is sorted primarily by block size. This allows the allocator to find 
     *    a block that fits the requested size in O(log n) time.
     * 
     * 2. Secondary Key: Alignment Quality (CTZ - Count Trailing Zeros).
     *    If two blocks have the same size, we sort them by the "quality" of their data pointer 
     *    alignment. Blocks with higher alignment (more trailing zeros) are placed in the 
     *    right sub-tree. This clusters "cleaner" addresses together, helping the search 
     *    algorithm find high-alignment blocks (e.g., 64-byte aligned) faster.
     * 
     * 3. Tertiary Key: Raw Address.
     *    If both size and alignment quality are identical, the raw memory address is used 
     *    as a final tie-breaker to ensure every node in the tree is unique and the 
     *    ordering is deterministic.
    */

    if (h == NULL) return new_block;

    size_t h_size = get_size(h);
    size_t new_size = get_size(new_block);

    if (new_size < h_size) {
        set_left_tree(h, insert_block(get_left_tree(h), new_block));
    } 
    else if (new_size > h_size) {
        set_right_tree(h, insert_block(get_right_tree(h), new_block));
    } 
    else {
        size_t h_quality = min_exponent_of((uintptr_t)block_data(h));
        size_t new_quality = min_exponent_of((uintptr_t)block_data(new_block));

        if (new_quality < h_quality) {
            set_left_tree(h, insert_block(get_left_tree(h), new_block));
        }
        else if (new_quality > h_quality) {
            set_right_tree(h, insert_block(get_right_tree(h), new_block));
        }
        else {
            if ((uintptr_t)new_block > (uintptr_t)h)
                set_left_tree(h, insert_block(get_left_tree(h), new_block));
            else
                set_right_tree(h, insert_block(get_right_tree(h), new_block));
        }
    }

    return balance(h);
}

/*
 * Find best fit block in LLRB tree
 * Searches the LLRB tree for the best fitting free block that can accommodate the requested size and alignment
 *
 * Strategy: 
 *   The tree is ordered primarily by size, and secondarily by "address quality" (CTZ).
 *   We aim to find the smallest block that satisfies: block_size >= requested_size + alignment_padding.
 *   Performance: O(log n)
 */
static Block *find_best_fit(Block *root, size_t size, size_t alignment, Block **out_parent) {
    EM_ASSERT((size > 0)          && "Internal Error: 'find_best_fit' called on too small size");
    EM_ASSERT((size <= SIZE_MASK) && "Internal Error: 'find_best_fit' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'find_best_fit' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'find_best_fit' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'find_best_fit' called on too big alignment");
    
    if (root == NULL) return NULL;

    Block *best = NULL;
    Block *best_parent = NULL;
    Block *current = root;
    Block *current_parent = NULL;

    while (current != NULL) {
        size_t current_size = get_size(current);
        
        /* 
         * CASE 1: Block is physically too small.
         * Since the tree is sorted by size (left < current < right), 
         * all blocks in the left sub-tree are guaranteed to be even smaller.
         * We MUST search the right sub-tree.
        */
        if (current_size < size) {
            current_parent = current;
            current = get_right_tree(current);
            continue;
        }

        uintptr_t data_ptr = (uintptr_t)block_data(current);
        uintptr_t aligned_ptr = align_up(data_ptr, alignment);
        size_t padding = aligned_ptr - data_ptr;

        /* 
         * CASE 2: Block is large enough to fit size + padding.
         * It is a valid candidate. We record it and then try to find an even 
         * smaller (better) block in the left sub-tree.
        */
        if (current_size >= size + padding) {
            // Potential best fit found. 
            // We keep the smallest block that can satisfy the request.
            if (best == NULL || current_size < get_size(best)) {
                best_parent = current_parent;
                best = current;
            }

            // Look for a smaller block in the left sub-tree.
            current_parent = current;
            current = get_left_tree(current);
        }

        /* 
         * CASE 3: Block is large enough on its own, but insufficient after padding.
         * This means the address of this block is "poorly aligned" for the request.
         * Since our tree sorts same-sized blocks by "address quality" (right has more trailing zeros),
         * we go RIGHT to find a block of the same or larger size with better alignment properties.
        */
        else {
            current_parent = current;
            current = get_right_tree(current);
        }
    }

    if (out_parent) *out_parent = best_parent;
    return best;
}

/*
 * Detach block from LLRB tree (fast version)
 * Removes a block from the LLRB tree without rebalancing
 * Note: Uses pragmatic BST deletion with a single balance pass at the root.
 */
static void detach_block_fast(Block **tree_root, Block *target, Block *parent) {
    EM_ASSERT((tree_root != NULL) && "Internal Error: 'detach_block_fast' called on NULL tree_root");
    EM_ASSERT((target != NULL)    && "Internal Error: 'detach_block_fast' called on NULL target");

    Block *replacement = NULL;
    Block *left_child = get_left_tree(target);
    Block *right_child = get_right_tree(target);

    if (!right_child) {
        replacement = left_child;
    } else if (!left_child) {
        replacement = right_child;
    } else {
        Block *min_parent = target;
        Block *min_node = right_child;
        while (get_left_tree(min_node)) {
            min_parent = min_node;
            min_node = get_left_tree(min_node);
        }
        if (min_parent != target) {
            set_left_tree(min_parent, get_right_tree(min_node));
            set_right_tree(min_node, right_child);
        }
        set_left_tree(min_node, left_child);
        replacement = min_node;
    }

    if (parent == NULL) {
        *tree_root = replacement;
    } else {
        if (get_left_tree(parent) == target)
            set_left_tree(parent, replacement);
        else
            set_right_tree(parent, replacement);
    }

    set_left_tree(target, NULL);
    set_right_tree(target, NULL);
    set_color(target, RED);
    
    if (*tree_root) *tree_root = balance(*tree_root);
}

/*
 * Find and detach block
 * High-level internal function that searches for the best fit and removes it from the tree.
 * Returns the detached block or NULL if no suitable block was found.
 */
static Block *find_and_detach_block(Block **tree_root, size_t size, size_t alignment) {
    EM_ASSERT((size > 0)          && "Internal Error: 'find_and_detach_block' called on too small size");
    EM_ASSERT((size <= SIZE_MASK) && "Internal Error: 'find_and_detach_block' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'find_and_detach_block' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'find_and_detach_block' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'find_and_detach_block' called on too big alignment");
    
    if (*tree_root == NULL) return NULL;

    Block *parent = NULL;
    Block *best = find_best_fit(*tree_root, size, alignment, &parent);

    if (best) {
        detach_block_fast(tree_root, best, parent);
    }

    return best;
}

/*
 * Detach a specific block by its pointer
 * Finds the parent of the given block using Triple-Key logic and detaches it.
 */
static void detach_block_by_ptr(Block **tree_root, Block *target) {
    EM_ASSERT((tree_root != NULL) && "Internal Error: 'detach_block_by_ptr' called on NULL tree_root");
    EM_ASSERT((target != NULL) && "Internal Error: 'detach_block_by_ptr' called on NULL target");

    Block *parent = NULL;
    Block *current = *tree_root;

    size_t target_size = get_size(target);
    size_t target_quality = min_exponent_of((uintptr_t)block_data(target));

    while (current != NULL && current != target) {
        parent = current;
        size_t current_size = get_size(current);

        if (target_size < current_size) {
            current = get_left_tree(current);
        } else if (target_size > current_size) {
            current = get_right_tree(current);
        } else {
            size_t current_quality = min_exponent_of((uintptr_t)block_data(current));
            if (target_quality < current_quality) {
                current = get_left_tree(current);
            } else if (target_quality > current_quality) {
                current = get_right_tree(current);
            } else {
                if ((uintptr_t)target > (uintptr_t)current)
                    current = get_left_tree(current);
                else
                    current = get_right_tree(current);
            }
        }
    }

    if (current == target) {
        detach_block_fast(tree_root, target, parent);
    }
}

static void em_free_block_full(EM *em, Block *block);
/*
 * Split block
 * Splits a larger free block into two blocks if it is significantly larger than needed
 * The remainder block is added back to the free blocks tree
 */
static inline void split_block(EM *em, Block *block, size_t needed_size) {
    size_t full_size = get_size(block);
    
    if (full_size > needed_size && full_size - needed_size >= BLOCK_MIN_SIZE) {
        set_size(block, needed_size);

        Block *remainder = create_block(next_block_unsafe(block)); 
        set_prev(remainder, block);
        set_size(remainder, full_size - needed_size - sizeof(Block));
        
        Block *following = next_block(em, remainder);
        if (following) {
            set_prev(following, remainder);
        }

        em_free_block_full(em, remainder);
    }
}

/*
 * Get the easy memory that owns this block
 * Uses neighbor-borrowing or the LSB Padding Detector to find the header.
 */
static inline EM *get_parent_em(Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_parent_em' called on NULL block");

    if (get_is_in_scratch(block)) {
        return (EM *)get_prev(block); 
    }

    Block *prev = block;
    
    /*
     * Logic: Physical Neighbor Walkback
     * We traverse the 'prev' pointers, which point to physical neighbors in memory.
     * We are looking for a block that can tell us who its owner is.
    */
    while (get_prev(prev) != NULL) {
        prev = get_prev(prev);

        /* 
         * We found an occupied block. But wait!
         * Because EM and Block are ABI-compatible, a nested easy memory 
         * LOOKS like an occupied block to its parent. 
         * We must check the 'IS_NESTED' flag to ensure we don't accidentally 
         * treat a nested EM as a simple block.
        */
        if (!get_is_free(prev) && !em_get_is_nested((EM*)(void *)(prev))) {
            return get_em(prev);
        }

        // If it's a nested EM or a free block, we keep walking back.
    }

    /*
     * Logic: Terminal Case - The First Block
     * If we reach the start (prev == NULL), we are at the very beginning of 
     * the easy memory segment. We check the word immediately preceding the block.
     * To get more understanding whats going on go to 'em_new_static_custom'
     * function. 
    */
    uintptr_t *detector_spot = (uintptr_t *)((char *)prev - sizeof(uintptr_t));
    uintptr_t val = *detector_spot;
    
    if (val & 1) return (EM *)((char *)prev - (val >> 1));

    return (EM *)((char *)prev - sizeof(EM));
}





/*
 * Free block (full version)
 * Frees a block of memory and merges with adjacent free blocks if possible
 */
static void em_free_block_full(EM *em, Block *block) {
    EM_ASSERT((em != NULL)    && "Internal Error: 'em_free_block_full' called on NULL em");
    EM_ASSERT((block != NULL) && "Internal Error: 'em_free_block_full' called on NULL block");

    #ifdef EM_POISONING
    memset(block_data(block), EM_POISON_BYTE, get_size(block));
    #endif

    if (get_is_in_scratch(block)) {
        em_free_scratch(em);
        return;
    }

    set_is_free(block, true);
    set_left_tree(block, NULL);
    set_right_tree(block, NULL);
    set_color(block, RED);

    Block *tail = em_get_tail(em);
    Block *prev = get_prev(block);
    
    Block *result_to_tree = block;
    
    // If block is tail, just set its size to 0
    if (block == tail) {
        set_size(block, 0);
        result_to_tree = NULL;
    }
    else {
        Block *next = next_block(em, block);

        // If next block is tail, just set its size to 0 and update tail pointer
        if (next == tail) {
            set_size(block, 0);
            em_set_tail(em, block);
            result_to_tree = NULL; 
        } 
        // Merge with next block if it is free
        else if (next && get_is_free(next)) {
            Block *free_blocks_root = em_get_free_blocks(em);
            detach_block_by_ptr(&free_blocks_root, next);
            em_set_free_blocks(em, free_blocks_root);
            merge_blocks_logic(em, block, next);
            result_to_tree = block;
        }
    }

    // Merge with previous block if it is free
    if (prev && get_is_free(prev)) {
        Block *free_blocks_root = em_get_free_blocks(em);
        detach_block_by_ptr(&free_blocks_root, prev);
        em_set_free_blocks(em, free_blocks_root);

        // If we merged with tail before, just update tail pointer
        if (result_to_tree == NULL) {
            set_size(prev, 0);
            em_set_tail(em, prev);
        } 
        // Else, merge previous with current result
        else {
            merge_blocks_logic(em, prev, result_to_tree);
            result_to_tree = prev;
        }
    }

    // Insert the resulting free block back into the free blocks tree
    if (result_to_tree != NULL) {
        Block *free_blocks_root = em_get_free_blocks(em);
        free_blocks_root = insert_block(free_blocks_root, result_to_tree);
        em_set_free_blocks(em, free_blocks_root);
    }
}

/*
 * Allocate memory in free blocks of easy memory ()full version)
 * Attempts to allocate a block of memory of given size and alignment from the free blocks tree of the easy memory
 * Returns pointer to allocated memory or NULL if allocation fails
 */
static void *alloc_in_free_blocks(EM *em, size_t size, size_t alignment) {
    EM_ASSERT((em != NULL)                         && "Internal Error: 'alloc_in_free_blocks' called on NULL easy memory");
    EM_ASSERT((size > 0)                           && "Internal Error: 'alloc_in_free_blocks' called on too small size");
    EM_ASSERT((size <= SIZE_MASK)                  && "Internal Error: 'alloc_in_free_blocks' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'alloc_in_free_blocks' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'alloc_in_free_blocks' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'alloc_in_free_blocks' called on too big alignment");

    Block *root = em_get_free_blocks(em);
    Block *block = find_and_detach_block(&root, size, alignment);
    em_set_free_blocks(em, root);
    
    if (!block) return NULL;
    
    set_is_free(block, false);
    
    uintptr_t data_ptr = (uintptr_t)block_data(block);
    uintptr_t aligned_ptr = align_up(data_ptr, alignment);
    size_t padding = aligned_ptr - data_ptr;

    size_t total_needed = padding + size;
    size_t aligned_needed = align_up(total_needed, sizeof(uintptr_t)); 
    
    split_block(em, block, aligned_needed);

    if (padding > 0) {
        uintptr_t *spot_before = (uintptr_t *)(aligned_ptr - sizeof(uintptr_t));
        *spot_before = (uintptr_t)block ^ aligned_ptr;
    }

    set_em(block, em);
    set_magic(block, (void *)aligned_ptr);
    set_color(block, RED);

    return (void *)aligned_ptr;
}

/*
 * Allocate memory in tail block of easy memory (full version)
 * Attempts to allocate a block of memory of given size and alignment in the tail block of the easy memory
 * Returns pointer to allocated memory or NULL if allocation fails
 */
static void *alloc_in_tail_full(EM *em, size_t size, size_t alignment) {
    EM_ASSERT((em != NULL)                         && "Internal Error: 'alloc_in_tail_full' called on NULL easy memory");
    EM_ASSERT((size > 0)                           && "Internal Error: 'alloc_in_tail_full' called on too small size");
    EM_ASSERT((size <= SIZE_MASK)                  && "Internal Error: 'alloc_in_tail_full' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'alloc_in_tail_full' called on invalid alignment");
    EM_ASSERT((alignment >= MIN_ALIGNMENT)         && "Internal Error: 'alloc_in_tail_full' called on too small alignment");
    EM_ASSERT((alignment <= MAX_ALIGNMENT)         && "Internal Error: 'alloc_in_tail_full' called on too big alignment");
    if (free_size_in_tail(em) < size) return NULL;  // Quick check to avoid unnecessary calculations
    
    /*
     * Allocation in tail may seem simple at first glance, but there are several edge cases to consider:
     * 1. Alignment padding before user data:
     *      If the required alignment is greater than the easy memory's alignment, 
     *       we need to calculate the padding needed before the user data to satisfy the alignment requirement.
     *      If calculated padding is so big that it by itself can contain a whole minimal block(BLOCK_MIN_SIZE) or more,
     *       we need to create the new block. It will allow us to reuse that, in other case wasted, memory if needed.
     * 2. Alignment padding after user data:
     *      After allocating the requested size, we need to check if there is enough space left in the tail block to create a new free block.
     *      This may require additional alignment padding after the user data to ensure that the data pointer of the next block 
     *       is aligned according to the easy memory's alignment.
     * 3. Minimum block size:
     *      We need to ensure that any new blocks created (either before or after the user data) meet the minimum block size requirement.
     * 4. Insufficient space:
     *      If there is not enough space in the tail block to satisfy the allocation request (including any necessary padding), we must return NULL.
    */

    Block *tail = em_get_tail(em);
    EM_ASSERT((tail != NULL)      && "Internal Error: alloc_in_tail_full' called on NULL tail");
    EM_ASSERT((get_is_free(tail)) && "Internal Error: alloc_in_tail_full' called on non free tail");

    // Calculate padding needed for alignment before user data
    uintptr_t raw_data_ptr = (uintptr_t)block_data(tail);
    uintptr_t aligned_data_ptr = align_up(raw_data_ptr, alignment);
    size_t padding = aligned_data_ptr - raw_data_ptr;

    size_t minimal_needed_block_size = padding + size;

    size_t free_space = free_size_in_tail(em);
    if (minimal_needed_block_size > free_space) return NULL;

    // If alignment padding is bigger than easy memory alignment, 
    //  it may be possible to create a new block before user data
    if (alignment > em_get_alignment(em) && padding > 0) {
        if (padding >= BLOCK_MIN_SIZE) {
            set_size(tail, padding - sizeof(Block));
            Block *free_blocks_root = em_get_free_blocks(em);
            free_blocks_root = insert_block(free_blocks_root, tail);
            em_set_free_blocks(em, free_blocks_root);

            Block *new_tail = create_next_block(em, tail);
            em_set_tail(em, new_tail);
            tail = new_tail;
            padding = 0;
        }
    }

    minimal_needed_block_size = padding + size;

    free_space = free_size_in_tail(em);
    if (minimal_needed_block_size > free_space) return NULL;

    // Check if we can allocate with end padding for next block
    size_t final_needed_block_size = minimal_needed_block_size;
    if (free_space - minimal_needed_block_size >= BLOCK_MIN_SIZE) {
        uintptr_t raw_data_end_ptr = aligned_data_ptr + size;
        uintptr_t aligned_data_end_ptr = align_up(raw_data_end_ptr + sizeof(Block), em_get_alignment(em)) - sizeof(Block);
        size_t end_padding = aligned_data_end_ptr - raw_data_end_ptr;
    
        size_t full_needed_block_size = minimal_needed_block_size + end_padding;
        if (free_space - full_needed_block_size >= BLOCK_MIN_SIZE) {
            final_needed_block_size = full_needed_block_size;
        } else {
            // we ignore coverage for this line cose it`s have very low chance to happen in real usage
            // and it effectivly not change anything in logic
            final_needed_block_size = free_space; // LCOV_EXCL_LINE
        }
    } else {
        final_needed_block_size = free_space;
    } 
    
    /*
    * Why we sure that padding >= sizeof(uintptr_t) here?
    * 
    * Since we are allocating aligned memory, the alignment is always a power of two and at least sizeof(uintptr_t).
    * Therefore, any padding in 'padding' variable will be always 0 or powers of 2 with sizeof(uintptr_t) as minimum.
    */
   
    // Store pointer to block metadata before user data for deallocation if there is padding
    if (padding > 0) {
        uintptr_t *spot_before_user_data = (uintptr_t *)(aligned_data_ptr - sizeof(uintptr_t));
        *spot_before_user_data = (uintptr_t)tail ^ aligned_data_ptr;
    }

    // Finalize tail block as occupied
    set_size(tail, final_needed_block_size);
    set_is_free(tail, false);
    set_magic(tail, (void *)aligned_data_ptr);
    set_color(tail, RED);
    set_em(tail, em);

    // If there is remaining free space, create a new free block
    if (free_space != final_needed_block_size) {
        Block *new_tail = create_next_block(em, tail);
        em_set_tail(em, new_tail);
    }

    return (void *)aligned_data_ptr;
}





/*
 * Free scratch memory in easy memory
 * Marks the scratch memory as free
 */
void em_free_scratch(EM *em) {
    if (!em || !em_get_has_scratch(em)) return;

    em_set_has_scratch(em, false);
    // Yeah it is that simple
}

/*
 * Free a block of memory in the easy memory
 * Marks the block as free, merges it with adjacent free blocks if possible,
 * and updates the free block list
 */
void em_free(void *data) {
    if (!data) return;
    if ((uintptr_t)data % sizeof(uintptr_t) != 0) return;

    Block *block = NULL;

    /*
     * Retrieve block metadata from user data pointer
     * We have two possible scenarios:
     * 1. There is no alignment padding before user data:
     *      In this case, we can directly calculate the block pointer by subtracting the size of Block
     *      from the user data pointer.
     * 2. There is alignment padding before user data:
     *      In this case, we stored the block pointer XORed with user data pointer just before the user data.
     *      We can retrieve it and XOR it back with user data pointer to get the original block pointer.
     * 
     * Thanks to Block struct having magic value as last field, we can validate which scenario is correct by just checking
     *  whether the XORed value before the user data matches the expected value.
    */

    uintptr_t *spot_before_user_data = (uintptr_t *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)0xDEADBEEF) {
        block = (Block *)(void *)((char *)data - sizeof(Block));
    }
    else {
        if ((uintptr_t)check % sizeof(uintptr_t) != 0) return;
        block = (Block *)check;
    }
    
    EM_ASSERT((block != NULL) && "Internal Error: 'em_free' detected NULL block");
    
    // If block size is bigger than SIZE_MASK, it's invalid
    if (get_size(block) > SIZE_MASK) return;
    // If block is already free, it's invalid
    if (get_is_free(block)) return;
    // If magic is invalid, it's invalid
    if (!is_valid_magic(block, data)) return;
    
    EM *em = get_em(block);
    
    EM_ASSERT((em != NULL) && "Internal Error: 'em_free' detected block with NULL em");

    if (!is_block_within_em(em, block)) return;

    em_free_block_full(em, block);
}

/*
 * Allocate memory in the easy memory with custom alignment
 * Returns NULL if there is not enough space
 */
void *em_alloc_aligned(EM *em, size_t size, size_t alignment) {
    if (!em || size == 0 || size > em_get_capacity(em)) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT || alignment > MAX_ALIGNMENT) return NULL;

    // Trying to allocate in free blocks first
    void *result = alloc_in_free_blocks(em, size, alignment);
    if (result) return result;

    if (free_size_in_tail(em) == 0) return NULL;
    return alloc_in_tail_full(em, size, alignment);
}

/*
 * Allocate memory in the easy memory with default alignment
 * Returns NULL if there is not enough space
 */
void *em_alloc(EM *em, size_t size) {
    if (!em) return NULL;
    return em_alloc_aligned(em, size, em_get_alignment(em));
}

/*
 * Allocate scratch memory in the physical end of easy memory with custom alignment
 * Returns NULL if there is not enough space or scratch memory is already allocated
 */
void *em_alloc_scratch_aligned(EM *em, size_t size, size_t alignment) {
    if (!em || size == 0 || em_get_has_scratch(em) || size > em_get_capacity(em)) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT || alignment > MAX_ALIGNMENT) return NULL;
    if (size > free_size_in_tail(em)) return NULL;

    uintptr_t raw_end_of_em = (uintptr_t)em + em_get_capacity(em);
    uintptr_t end_of_em = raw_end_of_em;
    end_of_em = align_down(end_of_em, MIN_ALIGNMENT);
    
    end_of_em -= sizeof(uintptr_t);
    uintptr_t scratch_size_spot = end_of_em;

    uintptr_t scratch_data_spot = end_of_em - size;
    scratch_data_spot = align_down(scratch_data_spot, alignment);

    uintptr_t block_metadata_spot = scratch_data_spot - sizeof(Block);

    Block *tail = em_get_tail(em);
    EM_ASSERT((tail != NULL)      && "Internal Error: 'em_alloc_scratch_aligned' called on NULL tail");
    EM_ASSERT((get_is_free(tail)) && "Internal Error: 'em_alloc_scratch_aligned' called on non free tail");

    if (block_metadata_spot < (uintptr_t)tail + sizeof(Block) + get_size(tail)) return NULL;

    size_t scratch_size = scratch_size_spot - scratch_data_spot;

    Block *scratch_block = create_block((void *)block_metadata_spot);
    set_size(scratch_block, scratch_size);
    set_is_free(scratch_block, false);
    set_magic(scratch_block, (void *)scratch_data_spot);
    set_em(scratch_block, em);
    set_is_in_scratch(scratch_block, true);
    
    uintptr_t *size_spot = (uintptr_t *)scratch_size_spot;
    *size_spot = raw_end_of_em - block_metadata_spot;

    em_set_has_scratch(em, true);

    return (void *)scratch_data_spot;
}

/*
 * Allocate scratch memory in the physical end of easy memory with default alignment
 * Returns NULL if there is not enough space or scratch memory is already allocated
 */
void *em_alloc_scratch(EM *em, size_t size) {
    if (!em) return NULL;
    return em_alloc_scratch_aligned(em, size, em_get_alignment(em));
}

/*
 * Allocate zero-initialized memory in the easy memory
 * Returns NULL if there is not enough space or overflow is detected
 */
void *em_calloc(EM *em, size_t nmemb, size_t size) {
    if (!em) return NULL;

    if (nmemb > 0 && (SIZE_MAX / nmemb) < size) {
        return NULL; // Overflow detected
    }

    size_t total_size = nmemb * size;
    void *ptr = em_alloc(em, total_size);
    if (ptr) {
        memset(ptr, 0, total_size); // Zero-initialize the allocated memory
    }
    return ptr;
}

/*
 * Create a static easy memory
 * Initializes an easy memory using preallocated memory and sets up the first block
 * Returns NULL if the provided size is too small, memory is NULL or size is negative
 */
EM *em_create_static_aligned(void *memory, size_t size, size_t alignment) {
    if (!memory || size < EM_MIN_SIZE || size > SIZE_MASK) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT|| alignment > MAX_ALIGNMENT) return NULL;

    uintptr_t raw_addr = (uintptr_t)memory;
    uintptr_t aligned_addr = align_up(raw_addr, MIN_ALIGNMENT);
    size_t em_padding = aligned_addr - raw_addr; 

    if (size < em_padding + sizeof(EM) + BLOCK_MIN_SIZE) return NULL;
    
    EM *em = (EM *)aligned_addr;

    /*
     * Magic LSB Padding Detector
     *
     *What is this for?
     * One of the core goals of easy_memory is Zero-Cost Parent Tracking. We need to find 
     * the 'EM' header starting from a 'Block' pointer (especially the first block) 
     * without storing an explicit 8-byte 'parent' pointer in every single block.
     *
     * In a nested or static easy_memory, there is often a gap (padding) between the 
     * end of the 'EM' struct and the start of the first 'Block' due to 
     * alignment requirements. Instead of wasting this space, we use it to store 
     * a "back-link" offset to the EM header.
     *
     * Why/How are we sure there is enough space?
     * 1. Structural Invariants: Both 'EM' and 'Block' structures are designed 
     *    to be multiples of the machine word (sizeof(uintptr_t)).
     * 2. Alignment Logic: Since 'alignment' is a power of two and is at least 
     *    sizeof(uintptr_t), any gap created by 'align_up' will also be 
     *    a multiple of the machine word.
     * 3. The Condition: If 'aligned_block_start' is greater than the end of the 
     *    easy_memory structure, the difference is guaranteed to be at least 4 bytes 
     *    (on 32-bit) or 8 bytes (on 64-bit). This is exactly the space needed 
     *    to store our tagged uintptr_t offset.
     *
     * The Detection Trick
     * We store the offset shifted left by 1, with the Least Significant Bit (LSB) 
     * set to 1 (e.g., (offset << 1) | 1). 
     * Why? Because the last field of an 'EM' struct is 'free_blocks' (a pointer). 
     * Valid pointers are always word-aligned (even numbers). By checking the LSB, 
     * we can instantly distinguish between:
     *   - 0: We are looking at the 'free_blocks' pointer (EM is immediately adjacent).
     *   - 1: We are looking at our custom padding offset (EM is 'offset' bytes away).
    */

    uintptr_t aligned_block_start = align_up(aligned_addr + sizeof(Block) + sizeof(EM), alignment) - sizeof(Block);
    Block *block = create_block((void *)(aligned_block_start));

    if (aligned_block_start > (aligned_addr + sizeof(EM))) {
        uintptr_t offset = aligned_block_start - (uintptr_t)em;
        uintptr_t *detector_spot = (uintptr_t *)(aligned_block_start - sizeof(uintptr_t));
        *detector_spot = (offset << 1) | 1;
    }

    em_set_alignment(em, alignment);
    em_set_capacity(em, size - em_padding);
    
    em_set_free_blocks(em, NULL);
    em_set_has_scratch(em, false);
    em_set_padding_bit(em, false); // enforce zero to be sure detention logic work correctly
    
    em_set_tail(em, block);
    em_set_is_dynamic(em, false);
    em_set_is_nested(em, false);

    return em;
}

/*
 * Create a static easy memory with default alignment
 * Initializes an easy memory using preallocated memory with default alignment
 * Returns NULL if the provided size is too small, memory is NULL or size is negative
 */
EM *em_create_static(void *memory, size_t size) {
    return em_create_static_aligned(memory, size, EM_DEFAULT_ALIGNMENT);
}

#ifndef EM_NO_MALLOC
/*
 * Create a easy memory with custom alignment in heap
 * Allocates memory for the easy memory and initializes it with the specified size and alignment
 * Returns NULL if the provided size is too small, memory allocation fails or size is negative
 */
EM *em_create_aligned(size_t size, size_t alignment) {
    if (size < BLOCK_MIN_SIZE || size > SIZE_MASK) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT|| alignment > MAX_ALIGNMENT) return NULL;

    void *data = malloc(size + sizeof(EM) + alignment);
    if (!data) return NULL;
    
    EM *em = em_create_static_aligned(data, size + sizeof(EM), alignment);

    if (!em) {
        // LCOV_EXCL_START
        free(data);
        return NULL;
        // LCOV_EXCL_STOP
    }

    em_set_is_dynamic(em, true);

    return em;
}

/*
 * Create a easy memory with default alignment in heap
 * Allocates memory for the easy memory and initializes it with the specified size and default alignment
 * Returns NULL if the provided size is too small, memory allocation fails or size is negative
 */
EM *em_create(size_t size) {
    return em_create_aligned(size, EM_DEFAULT_ALIGNMENT);
}
#endif // EM_NO_MALLOC

/*
 * Destroy the easy memory
 * Deallocates the memory used by the easy memory if it was allocated in heap
 * Can be safely called with static easy memories (no operation in that case)
 */
void em_destroy(EM *em) {
    if (!em) return;
    if (em_get_is_nested(em)) {
        EM *parent = get_parent_em((Block *)em);
        em_free_block_full(parent, (Block *)em); 
        return;
    }

    #ifndef EM_NO_MALLOC
    if (em_get_is_dynamic(em)) {
        free(em);
    }
    #endif // EM_NO_MALLOC
}

/*
 * Reset the easy memory
 * Clears the easy memory's blocks and resets it to the initial state without freeing memory
 */
void em_reset(EM *em) {
    if (!em) return;

    Block *first_block = em_get_first_block(em);

    // Reset first block
    set_size(first_block, 0);
    set_prev(first_block, NULL);
    set_is_free(first_block, true);
    set_color(first_block, RED);
    set_left_tree(first_block, NULL);
    set_right_tree(first_block, NULL);

    // Reset easy memory metadata
    em_set_free_blocks(em, NULL);
    em_set_tail(em, first_block);
    em_set_has_scratch(em, false);
}

/*
 * Reset the easy memory and set its tail to zero
 * clears the easy memory's blocks and resets it to the initial state with zeroing all the memory
 */
void em_reset_zero(EM *em) {
    if (!em) return;
    em_reset(em); // Reset easy memory
    memset(block_data(em_get_tail(em)), 0, free_size_in_tail(em)); // Set tail to zero
}

/*
 * Create a nested easy memory with custom alignment
 * Allocates memory for a nested easy memory from a parent easy memory and initializes it
 * Returns NULL if the parent easy memory is NULL, requested size is too small, or allocation fails
 */
EM *em_create_nested_aligned(EM *parent_em, size_t size, size_t alignment) {
    if (!parent_em || size < BLOCK_MIN_SIZE || size > SIZE_MASK) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT|| alignment > MAX_ALIGNMENT) return NULL;
    
    void *data = em_alloc(parent_em, size);  // Allocate memory from the parent easy memory
    if (!data) return NULL;

    Block *block = NULL;

    uintptr_t *spot_before_user_data = (uintptr_t *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)0xDEADBEEF) {
        block = (Block *)(void *)((char *)data - sizeof(Block));
    }
    // LCOV_EXCL_START
    else {
        block = (Block *)check;
    }
    // LCOV_EXCL_STOP

    EM *em = em_create_static_aligned((void *)block, size, alignment);
    em_set_is_nested(em, true); // Mark the easy memory as nested

    return em;
}

/*
 * Create a nested easy memory with alignment of parent easy memory
 * Allocates memory for a nested easy memory from a parent easy memory and initializes it
 * Returns NULL if the parent easy memory is NULL, requested size is too small, or allocation fails
 */
EM *em_create_nested(EM *parent_em, size_t size) {
    if (!parent_em || size < BLOCK_MIN_SIZE || size > SIZE_MASK) return NULL;

    return em_create_nested_aligned(parent_em, size, em_get_alignment(parent_em));
}

/*
 * Create a scratch nested easy memory with custom alignment
 * Allocates scratch memory for a nested easy memory from a parent easy memory and initializes it
 * Returns NULL if the parent easy memory is NULL, requested size is too small, or allocation fails
 */
EM *em_create_scratch_aligned(EM *parent_em, size_t size, size_t alignment) {
    if (!parent_em || em_get_has_scratch(parent_em) || size < BLOCK_MIN_SIZE || size > SIZE_MASK) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT|| alignment > MAX_ALIGNMENT) return NULL;
    
    void *data = em_alloc_scratch_aligned(parent_em, size, alignment);  // Allocate memory from the parent easy memory scratch
    if (!data) return NULL;

    Block *block = (Block *)(void *)((char *)data - sizeof(Block)); // Scratch block is always with no padding before user data
    set_prev(block, parent_em); // Scratch block has no previous block so we use prev pointer to store parent EM pointer

    EM *em = em_create_static_aligned((void *)block, size, alignment);
    em_set_is_nested(em, true); // Mark the easy memory as nested

    return em;
}

/*
 * Create a scratch nested easy memory with alignment of parent easy memory
 * Allocates scratch memory for a nested easy memory from a parent easy memory and initializes it
 * Returns NULL if the parent easy memory is NULL, requested size is too small, or allocation fails
 */
EM *em_create_scratch(EM *parent_em, size_t size) {
    if (!parent_em) return NULL;
    return em_create_scratch_aligned(parent_em, size, em_get_alignment(parent_em));
}





/*
 * Create a bump allocator
 * Initializes a bump allocator within a parent easy memory
 * Returns NULL if the parent easy memory is NULL, requested size is too small, or allocation fails
 */
Bump *em_create_bump(EM *parent_em, size_t size) {
    if (!parent_em) return NULL;
    if (size > SIZE_MASK || size < EM_MIN_BUFFER_SIZE) return NULL;  // Check for minimal reasonable size
    
    void *data = em_alloc(parent_em, size);  // Allocate memory from the parent easy memory
    if (!data) return NULL;

    Block *block = NULL;

    uintptr_t *spot_before_user_data = (uintptr_t *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)0xDEADBEEF) {
        block = (Block *)(void *)((char *)data - sizeof(Block));
    }
    // LCOV_EXCL_START
    else {
        block = (Block *)check;
    }
    // LCOV_EXCL_STOP
    
    Bump *bump = (Bump *)((void *)block);  // just cast allocated Block to Bump

    bump_set_em(bump, parent_em);
    bump_set_offset(bump, sizeof(Bump));

    return bump;
}

/*
 * Allocate memory from a bump allocator
 * Returns a pointer to the allocated memory or NULL if allocation fails
 * May return NOT aligned pointer
 */
void *em_bump_alloc(Bump *bump, size_t size) {
    if (!bump) return NULL;
    
    size_t offset = bump_get_offset(bump);
    if (size == 0 || size >= (bump_get_capacity(bump) - offset + sizeof(Bump))) return NULL;

    void *memory = (char *)bump + offset;
    bump_set_offset(bump, offset + size);

    return memory;
}

/*
 * Allocate aligned memory from a bump allocator
 * Returns a pointer to the allocated memory or NULL if allocation fails
 */
void *em_bump_alloc_aligned(Bump *bump, size_t size, size_t alignment) {
    if (!bump) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < MIN_ALIGNMENT|| alignment > MAX_ALIGNMENT) return NULL;
    if (size == 0) return NULL;

    uintptr_t current_ptr = (uintptr_t)bump + bump_get_offset(bump);
    uintptr_t aligned_ptr = align_up(current_ptr, alignment);
    size_t padding = aligned_ptr - current_ptr;

    if ((size_t)size > SIZE_MAX - padding) return NULL;

    size_t total_size = padding + size;

    size_t offset = bump_get_offset(bump);
    if ((size_t)total_size >= (bump_get_capacity(bump) - offset + sizeof(Bump))) return NULL;

    bump_set_offset(bump, offset + total_size);

    return (void *)aligned_ptr;
}

/*
 * Trim a bump allocator
 * Trims the bump allocator and return free part back to easy memory
 */
void em_bump_trim(Bump *bump) {
    if (!bump) return;

    EM *parent = bump_get_em(bump);
    size_t parent_align = em_get_alignment(parent);
    uintptr_t bump_addr = (uintptr_t)bump;
    
    uintptr_t current_end = bump_addr + bump_get_offset(bump);
    uintptr_t next_data_aligned = align_up(current_end + sizeof(Block), parent_align);

    uintptr_t remainder_addr = next_data_aligned - sizeof(Block);

    size_t new_payload_size = remainder_addr - ((uintptr_t)bump + sizeof(Block));

    if (bump_get_capacity(bump) > new_payload_size) 
        split_block(parent, (Block*)bump, new_payload_size);
}

/*
 * Reset a bump allocator
 * Resets the bump allocator's offset to the beginning
 */
void em_bump_reset(Bump *bump) {
    if (!bump) return;
    
    bump_set_offset(bump, sizeof(Bump));
}

/*
 * Destroy a bump allocator
 * Returns memory back to parent easy memory
 */
void em_bump_destroy(Bump *bump) {
    if (!bump) return;

    em_free_block_full(bump_get_em(bump), (Block *)(void *)bump);
}


#ifdef DEBUG

#ifdef USE_WPRINT
    #include <wchar.h>
    #define PRINTF wprintf
    #define T(str) L##str
#else
    #include <stdio.h>
    #define PRINTF printf
    #define T(str) str
#endif

/*
 * Helper function to print LLRB tree structure
 * Recursively prints the tree with indentation to show hierarchy
 */
void print_llrb_tree(Block *node, int depth) {
    if (node == NULL) return;
    
    // Print right subtree first (to display tree horizontally)
    print_llrb_tree(get_right_tree(node), depth + 1);
    
    // Print current node with indentation
    for (int i = 0; i < depth; i++) PRINTF(T("    "));
    PRINTF(T("Block: %p, Size: %lu %i\n"),
        node,
        get_size(node),
        get_color(node));
    
    // Print left subtree
    print_llrb_tree(get_left_tree(node), depth + 1);
}

/*
 * Print easy memory details
 * Outputs the current state of the easy memory and its blocks, including free blocks
 * Useful for debugging and understanding memory usage
 */
void print_em(EM *em) {
    if (!em) return;
    PRINTF(T("Easy Memory: %p\n"), em);
    PRINTF(T("EM Full Size: %lu\n"), em_get_capacity(em) + sizeof(EM));
    PRINTF(T("EM Data Size: %lu\n"), em_get_capacity(em));
    PRINTF(T("EM Alignment: %lu\n"), em_get_alignment(em));
    PRINTF(T("Data: %p\n"), (void *)((char *)em + sizeof(EM)));
    PRINTF(T("Tail: %p\n"), em_get_tail(em));
    PRINTF(T("Free Blocks: %p\n"), em_get_free_blocks(em));
    PRINTF(T("Free Size in Tail: %lu\n"), free_size_in_tail(em));
    PRINTF(T("\n"));

    size_t occupied_data = 0;
    size_t occupied_meta = 0;
    size_t len = 0;

    Block *block = em_get_first_block(em);
    while (block != NULL) {
        occupied_data += get_size(block);
        occupied_meta += sizeof(Block);
        len++;
        PRINTF(T("  Block: %p\n"), block);
        PRINTF(T("  Block Full Size: %lu\n"), get_size(block) + sizeof(Block));
        PRINTF(T("  Block Data Size: %lu\n"), get_size(block));
        PRINTF(T("  Is Free: %d\n"), get_is_free(block));
        PRINTF(T("  Data Pointer: %p\n"), block_data(block));
        if (!get_is_free(block)) {
            PRINTF(T("  Magic: 0x%lx\n"), get_magic(block));
            PRINTF(T("  EM: %p\n"), get_em(block));
        }
        else {
            PRINTF(T("  Left Free: %p\n"), get_left_tree(block));
            PRINTF(T("  Right Free: %p\n"), get_right_tree(block));
        }
        PRINTF(T("  Color: %s\n"), get_color(block) ? "BLACK": "RED");
        PRINTF(T("  Next: %p\n"), next_block(em, block));
        PRINTF(T("  Prev: %p\n"), get_prev(block));
        PRINTF(T("\n"));
        block = next_block(em, block);
    }

    PRINTF(T("Easy Memory Free Blocks\n"));

    Block *free_block = em_get_free_blocks(em);
    if (free_block == NULL) PRINTF(T("  None\n"));
    else {
        print_llrb_tree(free_block, 0);
    }
    PRINTF(T("\n"));

    PRINTF(T("EM occupied data size: %lu\n"), occupied_data);
    PRINTF(T("EM occupied meta size: %lu + %lu\n"), occupied_meta, sizeof(EM));
    PRINTF(T("EM occupied full size: %lu + %lu\n"), occupied_data + occupied_meta, sizeof(EM));
    PRINTF(T("EM block count: %lu\n"), len);
}

/*
 * Print a fancy visualization of the easy memory
 * Displays a bar chart of the easy memory's usage, including free blocks, occupied data, and metadata
 * Uses ANSI escape codes to colorize the visualization
 */
void print_fancy(EM *em, size_t bar_size) {
    if (!em) return;
    
    size_t total_size = em_get_capacity(em);

    PRINTF(T("\nEasy Memory Visualization [%zu bytes]\n"), total_size + sizeof(EM));
    PRINTF(T("┌"));
    for (int i = 0; i < (int)bar_size; i++) PRINTF(T("─"));
    PRINTF(T("┐\n│"));

    // Size of one segment of visualization in bytes
    double segment_size = (double)(total_size / bar_size);
    
    // Iterate through each segment of visualization
    for (int i = 0; i < (int)bar_size; i++) {
        // Calculate the start and end positions of the segment in memory
        size_t segment_start = (size_t)(i * segment_size);
        size_t segment_end = (size_t)((i + 1) * segment_size);
        
        // Determine which data type prevails in this segment
        char segment_type = ' '; // Empty by default
        size_t max_overlap = 0;
        
        // Check easy memory metadata
        size_t em_meta_end = sizeof(EM);
        if (segment_start < em_meta_end) {
            size_t overlap = segment_start < em_meta_end ? 
                (em_meta_end > segment_end ? segment_end - segment_start : em_meta_end - segment_start) : 0;
            if (overlap > max_overlap) {
                max_overlap = overlap;
                segment_type = '@'; // Easy memory metadata
            }
        }
        
        // Check each block
        size_t current_pos = 0;
        Block *current = (Block *)((char *)em + sizeof(EM));
        
        while (current) {
            // Position of block metadata
            size_t block_meta_start = current_pos;
            size_t block_meta_end = block_meta_start + sizeof(Block);
            
            // Check intersection with block metadata
            if (segment_start < block_meta_end && segment_end > block_meta_start) {
                size_t overlap = (segment_end < block_meta_end ? segment_end : block_meta_end) - 
                             (segment_start > block_meta_start ? segment_start : block_meta_start);
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    segment_type = '@'; // Block metadata
                }
            }
            
            // Position of block data
            size_t block_data_start = block_meta_end;
            size_t block_data_end = block_data_start + get_size(current);
            
            // Check intersection with block data
            if (segment_start < block_data_end && segment_end > block_data_start) {
                // Calculate end point of overlap
                size_t overlap_end = segment_end;
                if (segment_end > block_data_end) {
                    overlap_end = block_data_end;
                }

                // Calculate start point of overlap
                size_t overlap_start = segment_start;
                if (segment_start < block_data_start) {
                    overlap_start = block_data_start;
                }

                size_t overlap = overlap_end - overlap_start;
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    segment_type = get_is_free(current) ? ' ' : '#'; // Free or occupied block
                }
            }
            
            current_pos = block_data_end;
            current = next_block(em, current);
        }

        // Check tail free memory
        if (free_size_in_tail(em) > 0) {
            size_t tail_start = total_size - free_size_in_tail(em);
            if (segment_start < total_size && segment_end > tail_start) {
                size_t overlap = (segment_end < total_size ? segment_end : total_size) - 
                               (segment_start > tail_start ? segment_start : tail_start);
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    segment_type = '-'; // Free tail
                }
            }
        }
        
        // Display the corresponding symbol with color
        if (segment_type == '@') {
            PRINTF(T("\033[43m@\033[0m")); // Yellow for metadata
        } else if (segment_type == '#') {
            PRINTF(T("\033[41m#\033[0m")); // Red for occupied blocks
        } else if (segment_type == ' ') {
            PRINTF(T("\033[42m=\033[0m")); // Green for free blocks
        } else if (segment_type == '-') {
            PRINTF(T("\033[40m.\033[0m")); // Black for empty space
        }
    }

    PRINTF(T("│\n└"));
    for (int i = 0; i < (int)bar_size; i++) PRINTF(T("─"));
    PRINTF(T("┘\n"));

    PRINTF(T("Legend: "));
    PRINTF(T("\033[43m @ \033[0m - Used Meta blocks, "));
    PRINTF(T("\033[41m # \033[0m - Used Data blocks, "));
    PRINTF(T("\033[42m   \033[0m - Free blocks, "));
    PRINTF(T("\033[40m   \033[0m - Empty space\n\n"));
}
#endif // DEBUG

#endif // EASY_MEMORY_IMPLEMENTATION

#ifdef __cplusplus
} // extern "C"
#endif

#endif // EASY_MEMORY_H
