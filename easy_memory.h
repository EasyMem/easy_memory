#ifndef EASY_MEMORY_H
#define EASY_MEMORY_H

/*
 * Easy Memory Allocator (easy_memory.h)
 * A lightweight, efficient memory allocator for C programs.
 * Features:
    - Dynamic and static memory arenas
    - Nested arenas for hierarchical memory management
    - Bump allocator for fast linear allocations
    - Scratchpad allocations for temporary memory usage
    - Free block management using Left-Leaning Red-Black (LLRB) trees
 * Configurable via macros for assertions, poisoning, and static linkage.
 * Suitable for embedded systems, game development, and performance-critical applications.
 * Author: gooderfreed
 * License: MIT
*/

/*
 * ============================================================================
 *  CONFIGURATION QUICK REFERENCE
 * ============================================================================
 *  Define these macros before including this header to customize behavior.
 *
 *  SAFETY & VERIFICATION:
 *    #define EM_SAFETY_POLICY <N> // 0: CONTRACT (Design-by-Contract), 1: DEFENSIVE (Fault-Tolerant) [Default]
 *    #define DEBUG                // Enables assertions and auto-enables poisoning
 *    #define EM_ASSERT_STAYS      // Forces assertions to remain active even in Release builds
 *    #define EM_ASSERT_PANIC      // Assertions call abort() (Hardened Release)
 *    #define EM_ASSERT_OPTIMIZE   // Assertions are optimization hints (Danger!)
 *    #define EM_ASSERT(cond)      // Override with custom assertion logic
 *
 *  MEMORY POISONING:
 *    #define EM_POISONING         // Force ENABLE poisoning (even in Release)
 *    #define EM_NO_POISONING      // Force DISABLE poisoning (even in Debug)
 *    #define EM_POISON_BYTE 0xDD  // Custom byte pattern for freed memory
 *
 *  SYSTEM & LINKAGE:
 *    #define EM_NO_MALLOC         // Disable stdlib dependencies (Bare Metal mode)
 *    #define EM_STATIC            // Make all functions static (Private linkage)
 *    #define EM_RESTRICT          // Override 'restrict' keyword definition
 *    #define EM_RESTRICT          // Manual override for 'restrict' keyword definition
 *    #define EM_NO_ATTRIBUTES     // Disable all compiler-specific attributes
 *
 *  TUNING:
 *    #define EM_MAGIC <value>         // Custom magic number for block validation
 *    #define EM_DEFAULT_ALIGNMENT 16  // Global alignment baseline
 *    #define EM_MIN_BUFFER_SIZE   16  // Minimum split block size
 * ============================================================================
*/

/*
 * Configuration: C++ Compatibility Wrapper
 * Ensures the header can be included in both C and C++ projects without linkage issues.
*/
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

// Structure type forward declarations
typedef struct Block Block;
typedef struct EM    EM;
typedef struct Bump  Bump;

#ifdef _MSC_VER
#include <intrin.h>
#endif

/*
 * Configuration: Static Assertions
 * 
 * Behavior depends on defined macros:
 * 1. C11 or C++11 and above:
 *    Uses standard static_assert.
 * 
 * 2. Pre-C11/C++11:
 *    Uses a typedef trick to create a compile-time error on failure.
*/
#define EM_CONCAT_INTERNAL(a, b) a##b
#define EM_CONCAT(a, b) EM_CONCAT_INTERNAL(a, b)

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || defined(__cplusplus)
#   include <assert.h>
#   define EM_STATIC_ASSERT(cond, msg) static_assert(cond, #msg)
#else
#   define EM_STATIC_ASSERT(cond, msg) \
        typedef char EM_CONCAT(static_assertion_at_line_, __LINE__)[(cond) ? 1 : -1]
#endif

/*
 * Configuration: EMDEF Macro
 * Controls the linkage of the Easy Memory functions.
 * 
 * Behavior depends on defined macros:
 * 1. EM_STATIC:
 *    Functions are declared as static, limiting their visibility to the current translation unit.
 * 
 * 2. Default (None of the above):
 *    Functions are declared as extern, allowing linkage across multiple translation units.
*/ 
#ifndef EMDEF
#   ifdef EM_STATIC
#       define EMDEF static
#   else
#       define EMDEF extern
#   endif
#endif

/*
 * Configuration: Assertions
 * 
 * Behavior depends on defined macros:
 * 1. DEBUG or EM_ASSERT_STAYS: 
 *    Standard C assert(). Aborts and prints file/line on failure.
 * 
 * 2. EM_ASSERT_PANIC:
 *    Hardened Release. Calls abort() on failure. 
 *    Recommended for security-critical environments to prevent heap exploitation.
 * 
 * 3. EM_ASSERT_OPTIMIZE:
 *    Performance Release. Uses compiler hints (__builtin_unreachable/__assume).
 *    WARNING: Invokes Undefined Behavior if the condition is false. 
 *    Use only if you are 100% sure about invariants.
 * 
 * 4. Default (None of the above):
 *    No-op. Assertions are compiled out completely. Safe and fast.
*/
#ifndef EM_ASSERT
#   if defined(DEBUG) || defined(EM_ASSERT_STAYS)
#       include <assert.h>
#       define EM_ASSERT(cond) assert(cond)
#   elif defined(EM_ASSERT_PANIC)
#       include <stdlib.h>
#       define EM_ASSERT(cond) do { if (!(cond)) abort(); } while(0)
#   elif defined(EM_ASSERT_OPTIMIZE)
#       if defined(__GNUC__) || defined(__clang__)
#           define EM_ASSERT(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
#       elif defined(_MSC_VER)
#           define EM_ASSERT(cond) __assume(cond)
#       else
#           define EM_ASSERT(cond) ((void)0)
#       endif
#   else
        // Default Release: Safe No-op
#       define EM_ASSERT(cond) ((void)0)
#   endif
#endif

/*
 * Configuration: EM_RESTRICT Macro
 * Defines the restrict qualifier for pointer parameters to indicate non-aliasing.
 * 
 * Behavior depends on defined macros:
 * 1. C99 or C++ (with compiler support):
 *    Uses standard restrict or compiler-specific equivalents.
 * 
 * 2. Pre-C99/C++ (without compiler support):
 *    Defined as empty, effectively disabling the restrict qualifier.
*/
#ifndef EM_RESTRICT
#   if defined(__cplusplus)
#       if defined(_MSC_VER)
#           define EM_RESTRICT __restrict
#       elif defined(__GNUC__) || defined(__clang__)
#           define EM_RESTRICT __restrict__
#       else
#           define EM_RESTRICT
#       endif
#   elif defined(_MSC_VER)
#       define EM_RESTRICT __restrict
#   elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#       define EM_RESTRICT restrict
#   else
#       define EM_RESTRICT
#   endif
#endif

/*
 * Configuration: Safety Policies
 * 
 * Defines the methodology for handling invariant violations and API misuse. 
 * This allows the developer to choose how the library responds to errors.
 *
 * EM_POLICY_CONTRACT (0):
 *   - Philosophy: Post-conditions and invariants are treated as a contract.
 *   - Behavior: Checks are delegated to the EM_ASSERT mechanism.
 *   - Outcome: The final behavior (whether checks are compiled out, lead to a panic, 
 *     or stay in release via EM_ASSERT_STAYS) is determined entirely by the 
 *     configured Assertion Strategy.
 *   - Best for: Fine-grained control over debugging and performance.
 *
 * EM_POLICY_DEFENSIVE (1) [DEFAULT]:
 *   - Philosophy: Runtime resilience and fault-tolerance.
 *   - Behavior: Performs explicit 'if' checks in both Debug and Release.
 *   - Outcome: Gracefully returns NULL or exits the function on violation.
 *   - Best for: Production environments where the program must survive misuse 
 *     without hard crashes.
*/
#define EM_POLICY_CONTRACT  0
#define EM_POLICY_DEFENSIVE 1

#ifndef EM_SAFETY_POLICY
#   define EM_SAFETY_POLICY EM_POLICY_DEFENSIVE
#endif

/*
 * Internal Safety Macros
 *
 * EM_CHECK   - Used in functions returning values (e.g., pointers, size_t).
 * EM_CHECK_V - Used in void functions (e.g., em_free, em_destroy).
 *
 * These macros adapt to the chosen EM_SAFETY_POLICY, providing either 
 * a fail-fast assertion or a graceful runtime exit.
*/
#if EM_SAFETY_POLICY == EM_POLICY_CONTRACT
#   define EM_CHECK(cond, ret, msg) EM_ASSERT((cond) && msg)
#   define EM_CHECK_V(cond, msg)    EM_ASSERT((cond) && msg)
#else
#   define EM_CHECK(cond, ret, msg) do { if (!(cond)) return (ret); } while(0)
#   define EM_CHECK_V(cond, msg)    do { if (!(cond)) return;       } while(0)
#endif

/*
 * Configuration: Force Disable Attributes
 * Disables all compiler-specific attributes, regardless of compiler support.
 * 
*/
#ifndef EM_NO_ATTRIBUTES
#   if defined(EASY_MEMORY_IMPLEMENTATION) && defined(EM_STATIC)
#       define EM_NO_ATTRIBUTES
#   endif
#endif

/*
 * Configuration: Compiler Attributes
 * Adds compiler-specific attributes to functions for optimization and correctness hints.
 * 
 * Behavior depends on defined macros:
 * 1. EM_NO_ATTRIBUTES:
 *    Disables all attributes, regardless of compiler support.
 * 
 * 2. GCC or Clang:
 *    Uses __attribute__ syntax. Takes index arguments, ignores name arguments.
 * 
 * 3. MSVC:
 *    Uses __declspec and SAL annotations. Takes name arguments, ignores index arguments.
 *    Requires <sal.h>.
 * 
 * 4. Other Compilers:
 *    Attributes are defined as empty.
 */
#if defined(_MSC_VER)
#   include <sal.h>
#endif

#if defined(EM_NO_ATTRIBUTES)
#   define EM_ATTR_MALLOC
#   define EM_ATTR_WARN_UNUSED
#   define EM_ATTR_ALLOC_SIZE(idx, name)
#   define EM_ATTR_ALLOC_SIZE2(idx1, idx2, name1, name2)
#elif defined(__GNUC__) || defined(__clang__)
    // GCC / Clang
#   define EM_ATTR_MALLOC __attribute__((malloc))
#   define EM_ATTR_WARN_UNUSED __attribute__((warn_unused_result))
    // GCC uses argument INDICES (1-based)
#   define EM_ATTR_ALLOC_SIZE(idx, name) __attribute__((alloc_size(idx)))
#   define EM_ATTR_ALLOC_SIZE2(idx1, idx2, name1, name2) __attribute__((alloc_size(idx1, idx2)))
#elif defined(_MSC_VER)
    // MSVC (Windows)
#   define EM_ATTR_MALLOC __declspec(restrict) _Ret_maybenull_
#   define EM_ATTR_WARN_UNUSED _Check_return_
    // MSVC uses argument NAMES.
    // We use _Post_writable_byte_size_ to tell static analysis how much memory is valid.
#   define EM_ATTR_ALLOC_SIZE(idx, name) _Post_writable_byte_size_(name)
    // For calloc-like logic, we can multiply names directly in SAL
#   define EM_ATTR_ALLOC_SIZE2(idx1, idx2, name1, name2) _Post_writable_byte_size_((name1) * (name2))
#else
    // Unknown Compiler
#   define EM_ATTR_MALLOC
#   define EM_ATTR_WARN_UNUSED
#   define EM_ATTR_ALLOC_SIZE(idx, name)
#   define EM_ATTR_ALLOC_SIZE2(idx1, idx2, name1, name2)
#endif

/*
 * Configuration: Memory Poisoning
 * Helps detect use-after-free and memory corruption bugs by filling freed memory with a known pattern.
 * 
 * Behavior depends on defined macros:
 * 1. EM_NO_POISONING:
 *    Disables all poisoning features, regardless of build type.
 * 
 * 2. DEBUG (without EM_NO_POISONING):
 *    Enables poisoning in debug builds for maximum safety.
 * 
 * 3. Default (Release without EM_NO_POISONING):
 *    Disables poisoning to maximize performance.
*/
#ifdef EM_NO_POISONING
#   if defined(EM_POISONING)
#       undef EM_POISONING
#   endif
#elif defined(DEBUG) && !defined(EM_POISONING)
#   define EM_POISONING
#endif

/*
 * Configuration: Poison Byte
 * Byte value used to fill freed memory when poisoning is enabled.
 * Default is 0xDD, but can be customized by defining EM_POISON_BYTE before including this header.
*/
#ifndef EM_POISON_BYTE
#   define EM_POISON_BYTE 0xDD
#endif
EM_STATIC_ASSERT((EM_POISON_BYTE >= 0x00) && (EM_POISON_BYTE <= 0xFF), "EM_POISON_BYTE must be a valid byte value (0x00 to 0xFF).");

/*
 * Configuration: Minimum Buffer Size
 * Defines the minimum size of the usable memory buffer within a block.
 * This prevents creation of useless zero-sized free blocks.
 * Default is 16 bytes, but can be customized by defining EM_MIN_BUFFER_SIZE before including this header.
*/
#ifndef EM_MIN_BUFFER_SIZE
#   define EM_MIN_BUFFER_SIZE 16 
#endif
EM_STATIC_ASSERT(EM_MIN_BUFFER_SIZE > 0, "MIN_BUFFER_SIZE must be a positive value to prevent creation of useless zero-sized free blocks.");

/*
 * Configuration: Magic Number
 * Unique identifier used to validate memory blocks and detect corruption.
 * Default values are chosen based on pointer size to ensure uniqueness.
 * Can be customized by defining EM_MAGIC before including this header.
*/
#ifndef EM_MAGIC
#   if UINTPTR_MAX > 0xFFFFFFFF
#       define EM_MAGIC 0xDEADBEEFDEADBEEFULL
#   elif UINTPTR_MAX > 0xFFFF
#       define EM_MAGIC 0xDEADBEEFUL
#   else
#       define EM_MAGIC 0xBEEFU
#   endif
#endif
EM_STATIC_ASSERT((EM_MAGIC != 0), "EM_MAGIC must be a non-zero value to ensure effective block validation.");

/*
 * Constant: Minimum Exponent
 * Used to calculate minimum and maximum alignment limits based on pointer size.
*/
#if defined(__GNUC__) || defined(__clang__)
#   define EMMIN_EXPONENT (__builtin_ctz(sizeof(uintptr_t)))
#else
#   define EMMIN_EXPONENT ( \
        (sizeof(uintptr_t) == 2) ? 2 : \
        (sizeof(uintptr_t) == 4) ? 2 : \
        (sizeof(uintptr_t) == 8) ? 3 : \
        4                              \
    )
#endif

/*
 * Constant: Maximum Alignment Limit
 * Maximum alignment is 512 on 32-bit systems and 1024 on 64-bit systems.
*/
#define EMMAX_ALIGNMENT ((size_t)(256 << EMMIN_EXPONENT))

/*
 * Constant: Minimum Alignment Limit
 * Minimum alignment is based on the size of uintptr_t.
*/
#define EMMIN_ALIGNMENT ((size_t)sizeof(uintptr_t))

/*
 * Configuration: Default Alignment
 * Defines the default memory alignment for the easy memory allocator.
 * Default is 16 bytes, but can be customized by defining EM_DEFAULT_ALIGNMENT before including this header.
*/
#ifndef EM_DEFAULT_ALIGNMENT
#   define EM_DEFAULT_ALIGNMENT 16
#endif
EM_STATIC_ASSERT((EM_DEFAULT_ALIGNMENT & (EM_DEFAULT_ALIGNMENT - 1)) == 0, "EM_DEFAULT_ALIGNMENT must be a power of two.");
EM_STATIC_ASSERT(EM_DEFAULT_ALIGNMENT >= EMMIN_ALIGNMENT, "EM_DEFAULT_ALIGNMENT must be at least EMMIN_ALIGNMENT.");
EM_STATIC_ASSERT(EM_DEFAULT_ALIGNMENT <= EMMAX_ALIGNMENT, "EM_DEFAULT_ALIGNMENT must be at most EMMAX_ALIGNMENT.");


/*
 * Constant: Alignment Mask
 * Mask to extract alignment bits from size_and_alignment field.
*/
#define EMALIGNMENT_MASK     ((uintptr_t)7)

/*
 * Constant: Size Mask
 * Mask to extract size bits from size_and_alignment field.
*/
#define EMSIZE_MASK         (~(uintptr_t)7)



/*
 * Constant: IS_FREE Mask
 * Mask to check if a block is free.
*/
#define EMIS_FREE_FLAG       ((uintptr_t)1)

/*
 * Constant: COLOR Mask
 * Mask to check the color of a block in the red-black tree.
*/
#define EMCOLOR_FLAG         ((uintptr_t)2)

/*
 * Constant: Previous Block Mask
 * Mask to extract the previous block pointer from prev field.
*/
#define EMPREV_MASK         (~(uintptr_t)3)



/*
 * Constant: Is Dynamic Mask
 * Mask to check if the EM is dynamically allocated.
*/
#define EMIS_DYNAMIC_FLAG    ((uintptr_t)1)

/*
 * Constant: Is Nested Mask
 * Mask to check if the EM is a nested EM.
*/
#define EMIS_NESTED_FLAG     ((uintptr_t)2)

/*
 * Constant: Tail Block Mask
 * Mask to extract the tail block pointer from tail field.
*/
#define EMTAIL_MASK         (~(uintptr_t)3)



/*
 * Constant: Padding Mask
 * Mask used to ensure Zero in least significant bit of `free_blocks` pointer.
 * Needed for `Magic LSB Padding Detector` trick to work properly.
 * For more info, see comment in `em_create_static_aligned` function.
*/
#define EMIS_PADDING         ((uintptr_t)1)

/*
 * Constant: Has Scratch Mask
 * Mask to check if the EM scratchpad slot currently in use.
*/
#define EMHAS_SCRATCH_FLAG   ((uintptr_t)2)

/*
 * Constant: Free Blocks Mask
 * Mask to extract the free blocks tree pointer from free_blocks field.
*/
#define EMFREE_BLOCKS_MASK  (~(uintptr_t)3)



/*
 * Constant: EM Color Definitions
 * Defines the color values for blocks in the red-black tree.
*/
#define EMRED   false
#define EMBLACK true

/*
 * Constant: Minimum Block Size
 * The minimum size required to create a valid EM instance.
*/
#define EMBLOCK_MIN_SIZE (sizeof(Block) + EM_MIN_BUFFER_SIZE)

/*
 * Constant: Minimum EM Size
 * The minimum size required to create a valid EM instance.
*/
#define EMMIN_SIZE       (sizeof(EM) + EMBLOCK_MIN_SIZE)

/*
 * Constant: Maximum EM Size
 * The maximum size allowed for an EM instance to prevent overflow issues.
*/
#define EMMAX_SIZE (SIZE_MAX >> 3)

/*
 * Macro: Block Data Pointer
 * Calculates the pointer to the usable data area of a block.
*/
#define block_data(block) ((void *)((uintptr_t)(block) + sizeof(Block)))

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

EM_STATIC_ASSERT(offsetof(Bump, as.self.capacity) == offsetof(Block, size_and_alignment), 
    Bump_capacity_offset_mismatch);
EM_STATIC_ASSERT(offsetof(Bump, as.self.prev) == offsetof(Block, prev), 
    Bump_prev_offset_mismatch);
EM_STATIC_ASSERT(offsetof(Bump, as.self.em) == offsetof(Block, as.occupied.em), 
    Bump_em_offset_mismatch);
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
            Block *free_blocks;             // Pointer to the tree of free blocks and scratchpad usage flag via pointer tagging
        } self;
    } as;
};

EM_STATIC_ASSERT(offsetof(EM, as.self.capacity_and_alignment) == offsetof(Block, size_and_alignment), 
    EM_capacity_offset_mismatch);
EM_STATIC_ASSERT(offsetof(EM, as.self.prev) == offsetof(Block, prev), 
    EM_prev_offset_mismatch);
EM_STATIC_ASSERT(offsetof(EM, as.self.tail) == offsetof(Block, as.occupied.em), 
    EM_tail_offset_mismatch);
EM_STATIC_ASSERT((sizeof(EM) == sizeof(Block)), Size_mismatch_between_Bump_and_Block);



/* 
 * ======================================================================================
 * Public API Declarations
 * ======================================================================================
*/

#ifdef DEBUG
#include <stdio.h>
#include <math.h>
EMDEF void print_em(EM *em);
EMDEF void print_fancy(EM *em, size_t bar_size);
EMDEF void print_llrb_tree(Block *node, int depth);
#endif // DEBUG


// --- EM Creation (Dynamic) ---
#ifndef EM_NO_MALLOC
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED
EM *em_create(size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED 
EM *em_create_aligned(size_t size, size_t alignment);
#endif // EM_NO_MALLOC

// --- EM Creation (Static) ---
EMDEF EM_ATTR_WARN_UNUSED 
EM *em_create_static(void *EM_RESTRICT memory, size_t size);

EMDEF EM_ATTR_WARN_UNUSED 
EM *em_create_static_aligned(void *EM_RESTRICT memory, size_t size, size_t alignment);

// --- EM Creation (Nested & Scratch) ---
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED 
EM *em_create_nested(EM *EM_RESTRICT parent_em, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED 
EM *em_create_nested_aligned(EM *EM_RESTRICT parent_em, size_t size, size_t alignment);
 
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED
EM *em_create_scratch(EM *EM_RESTRICT parent_em, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED 
EM *em_create_scratch_aligned(EM *EM_RESTRICT parent_em, size_t size, size_t alignment);

// --- Lifecycle & Reset ---
EMDEF void em_reset(EM *EM_RESTRICT em);
EMDEF void em_reset_zero(EM *EM_RESTRICT em);
EMDEF void em_destroy(EM *em);


// --- Allocation Core ---
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size) 
void *em_alloc(EM *EM_RESTRICT em, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size) 
void *em_alloc_aligned(EM *EM_RESTRICT em, size_t size, size_t alignment);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size) 
void *em_alloc_scratch(EM *EM_RESTRICT em, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size) 
void *em_alloc_scratch_aligned(EM *EM_RESTRICT em, size_t size, size_t alignment);

// --- Calloc ---
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE2(2, 3, nmemb, size) 
void *em_calloc(EM *EM_RESTRICT em, size_t nmemb, size_t size);

// --- Free ---
EMDEF void em_free(void *data);



// --- Bump Allocator ---
EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED
Bump *em_bump_create(EM *EM_RESTRICT em, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size)
void *em_bump_alloc(Bump *EM_RESTRICT bump, size_t size);

EMDEF EM_ATTR_MALLOC EM_ATTR_WARN_UNUSED EM_ATTR_ALLOC_SIZE(2, size) 
void *em_bump_alloc_aligned(Bump *EM_RESTRICT bump, size_t size, size_t alignment);

EMDEF void em_bump_trim(Bump *EM_RESTRICT bump);
EMDEF void em_bump_reset(Bump *EM_RESTRICT bump);
EMDEF void em_bump_destroy(Bump *bump);

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
    // EM_FORCE_GENERIC needed for testing fallback generic implementation
    #if (defined(__GNUC__) || defined(__clang__)) && !defined(EM_FORCE_GENERIC)
        #if UINTPTR_MAX > 0xFFFFFFFF
            return (size_t)__builtin_ctzll((unsigned long long)num);
        #else
            return (size_t)__builtin_ctz((unsigned int)num);
        #endif
    #elif defined(_MSC_VER) && !defined(EM_FORCE_GENERIC)
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
        while ((s & 1) == 0) { s >>= 1; zeros++; }
        return zeros;
    #endif
}

/*
 * Get alignment from block
 * Extracts the alignment information stored in the block's size_and_alignment field
 */
static inline size_t get_alignment(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_alignment' called on NULL block");

    size_t exponent = (block->size_and_alignment & EMALIGNMENT_MASK) + EMMIN_EXPONENT; // Extract exponent and adjust by EMMIN_EXPONENT
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
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)         && "Internal Error: 'set_alignment' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)         && "Internal Error: 'set_alignment' called on too big alignment");

    /*
     * How does that work?
     * Alignment is always a power of two, so instead of storing the alignment directly and wasting full 4-8 bytes, we can represent it as 2^n.
     * Since minimum alignment is 2^EMMIN_EXPONENT, we can store only the exponent minus EMMIN_EXPONENT in 3 bits(value 0-7).
     * For example:
     *  - On 32-bit system (EMMIN_EXPONENT = 2):
     *       Alignment 4     ->  2^2  ->  2-2  ->  stored as 0
     *       Alignment 8     ->  2^3  ->  3-2  ->  stored as 1
     *       Alignment 16    ->  2^4  ->  4-2  ->  stored as 2
     *       ... and so on up to
     *       Alignment 512   ->  2^9  ->  9-2  ->  stored as 7
     * 
     *  - On 64-bit system (EMMIN_EXPONENT = 3):
     *       Alignment 8     ->  2^3  ->  3-3  ->  stored as 0
     *       Alignment 16    ->  2^4  ->  4-3  ->  stored as 1
     *       Alignment 32    ->  2^5  ->  5-3  ->  stored as 2
     *       ... and so on up to 
     *       Alignment 1024  -> 2^10  -> 10-3  ->  stored as 7
     * This way, we efficiently use only 3 bits to cover the full range alignments that could be potentially used within the size_and_alignment field.
    */ 
    
    size_t exponent = min_exponent_of(alignment >> EMMIN_EXPONENT); // Calculate exponent from alignment

    size_t spot = block->size_and_alignment & EMALIGNMENT_MASK; // Preserve current alignment bits
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
    EM_ASSERT((block != NULL)        && "Internal Error: 'set_size' called on NULL block");
    EM_ASSERT((size <= EMMAX_SIZE)   && "Internal Error: 'set_size' called on too big size");

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

    size_t alignment_piece = block->size_and_alignment & EMALIGNMENT_MASK; // Preserve current alignment bits
    block->size_and_alignment = (size << 3) | alignment_piece; // Set new size while preserving alignment bits
}



/*
 * Get pointer to prev block from given block
 * Extracts the previous block pointer stored in the block's prev field
 */
static inline Block *get_prev(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_prev' called on NULL block");

    return (Block *)((uintptr_t)block->prev & EMPREV_MASK); // Clear flag bits to get actual pointer
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
    
    uintptr_t flags_tips = (uintptr_t)block->prev & ~EMPREV_MASK; // Preserve flag bits
    block->prev = (Block *)((uintptr_t)ptr | flags_tips); // Set new pointer while preserving flag bits
}



/*
 * Get is_free flag from block
 * Extracts the is_free flag stored in the block's prev field
 */
static inline bool get_is_free(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_is_free' called on NULL block");

    return (uintptr_t)block->prev & EMIS_FREE_FLAG; // Check the is_free flag bit
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
        int_ptr |= EMIS_FREE_FLAG;  // Set the is_free flag bit
    }
    else {
        int_ptr &= ~EMIS_FREE_FLAG; // Clear the is_free flag bit
    }
    block->prev = (Block *)int_ptr; // Update the prev field with new flags
}



/*
 * Get color flag from block
 * Extracts the color flag stored in the block's prev field
 */
static inline bool get_color(const Block *block) {
    EM_ASSERT((block != NULL) && "Internal Error: 'get_color' called on NULL block");

    return ((uintptr_t)block->prev & EMCOLOR_FLAG); // Check the color flag bit
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
        int_ptr |= EMCOLOR_FLAG; // Set the color flag bit
    }
    else {
        int_ptr &= ~EMCOLOR_FLAG; // Clear the color flag bit
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

    block->as.occupied.magic = (uintptr_t)EM_MAGIC ^ (uintptr_t)user_ptr; // Set magic number using XOR with user pointer
}

/*
 * Validate magic number for block
 * Checks if the magic number in the block matches the expected value based on the user pointer
 */
static inline bool is_valid_magic(const Block *block, const void *user_ptr) {
    EM_ASSERT((block != NULL)    && "Internal Error: 'is_valid_magic' called on NULL block");
    EM_ASSERT((user_ptr != NULL) && "Internal Error: 'is_valid_magic' called on NULL user_ptr");

    return ((get_magic(block) ^ (uintptr_t)user_ptr) == (uintptr_t)EM_MAGIC); // Validate magic number by XORing with user pointer
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

    return (!get_is_free(block) && get_color(block) == EMBLACK);
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
        set_color(block, EMBLACK); // Set color to BLACK for scratch blocks
    }
    // LCOV_EXCL_START
    else {
        set_color(block, EMRED);   // Set color to RED for non-scratch blocks
    }
    // LCOV_EXCL_STOP
}





/*
 * get tail block from easy memory
 * Extracts the tail block pointer in the easy memory's as.self.tail field
 */
static inline Block *em_get_tail(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_tail' called on NULL easy memory");

    return (Block *)((uintptr_t)em->as.self.tail & EMTAIL_MASK);
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

    uintptr_t flags_tips = (uintptr_t)em->as.self.tail & ~EMTAIL_MASK; // Preserve flag bits
    em->as.self.tail = (Block *)((uintptr_t)block | flags_tips); // set new pointer while preserving flag bits
}



/*
 * Get is_dynamic flag from easy memory
 * Extracts the is_dynamic flag stored in the easy memory's as.self.tail field
 */
static inline bool em_get_is_dynamic(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_is_dynamic' called on NULL easy memory");

    return ((uintptr_t)em->as.self.tail & EMIS_DYNAMIC_FLAG); // Check the is_dynamic flag bit
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
        int_ptr |= EMIS_DYNAMIC_FLAG; // Set the is_dynamic flag bit
    }
    else {
        int_ptr &= ~EMIS_DYNAMIC_FLAG; // Clear the is_dynamic flag bit
    }
    em->as.self.tail = (Block *)int_ptr; // Update the tail field with new flags
}



/*
 * Get is easy memory nested
 * Retrieves the is_nested flag of the easy memory pointer
 */
static inline bool em_get_is_nested(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_is_nested' called on NULL easy memory");
    
    return ((uintptr_t)em->as.self.tail & EMIS_NESTED_FLAG);
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
        int_ptr |= EMIS_NESTED_FLAG; // Set the is_nested flag bit
    }
    else {
        int_ptr &= ~EMIS_NESTED_FLAG; // Clear the is_nested flag bit
    }
    em->as.self.tail = (Block *)int_ptr; // Update the tail field with new flags
}



/*
 * Get has_padding flag from easy memory
 * Extracts the has_padding flag stored in the easy memory's as.self.free_blocks field
 */
static inline bool em_get_padding_bit(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_padding_bit' called on NULL easy memory");

    return ((uintptr_t)em->as.self.free_blocks & EMIS_PADDING); // Check the is_padding flag bit
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
    // LCOV_EXCL_START
        int_ptr |= EMIS_PADDING; // Set the is_padding flag bit
    // LCOV_EXCL_STOP
    }
    else {
        int_ptr &= ~EMIS_PADDING; // Clear the is_padding flag bit
    }
    em->as.self.free_blocks = (Block *)int_ptr; // Update the free_blocks field with new flags
}



/*
 * Get has_scratch flag from easy memory
 * Extracts the has_scratch flag stored in the easy memory's as.self.free_blocks field
 */
static inline bool em_get_has_scratch(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_has_scratch' called on NULL easy memory");

    return ((uintptr_t)em->as.self.free_blocks & EMHAS_SCRATCH_FLAG); // Check the is_scratch flag bit
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
        int_ptr |= EMHAS_SCRATCH_FLAG; // Set the has_scratch flag bit
    }
    else {
        int_ptr &= ~EMHAS_SCRATCH_FLAG; // Clear the has_scratch flag bit
    }
    em->as.self.free_blocks = (Block *)int_ptr; // Update the free_blocks field with new flags
}



/*
 * Get free blocks tree from easy memory
 * Extracts the pointer to the root of the free blocks tree stored in the easy memory's as.self.free_blocks field
 */
static inline Block *em_get_free_blocks(const EM *em) {
    EM_ASSERT((em != NULL) && "Internal Error: 'em_get_free_blocks' called on NULL easy memory");

    return (Block *)((uintptr_t)em->as.self.free_blocks & EMFREE_BLOCKS_MASK); // select only pointer bits
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

    uintptr_t flags_tips = (uintptr_t)em->as.self.free_blocks & ~EMFREE_BLOCKS_MASK; // Preserve flag bits
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
    EM_ASSERT((em != NULL)                              && "Internal Error: 'em_set_capacity' called on NULL easy memory");
    EM_ASSERT(((size == 0 || size >= EMBLOCK_MIN_SIZE)) && "Internal Error: 'em_set_capacity' called on too small size");
    EM_ASSERT((size <= EMMAX_SIZE)                      && "Internal Error: 'em_set_capacity' called on too big size");

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
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)         && "Internal Error: 'em_set_alignment' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)         && "Internal Error: 'em_set_alignment' called on too big alignment");
    
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

    set_size(&(bump->as.block_representation), size);
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
        uintptr_t aligned_end = align_down(raw_end, EMMIN_ALIGNMENT);

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
    
    Block *block = (Block *)point;
    
    block->size_and_alignment = 0;
    block->prev = NULL;

    // Initialize block metadata
    set_size(block, 0);
    set_prev(block, NULL);
    set_is_free(block, true);
    set_color(block, EMRED);
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
    
    if (!is_block_within_em(em, prev_block)) {
        EM_ASSERT(false && "Internal Error: prev_block out of bounds");
        return NULL;
    }

    Block *nb = next_block_unsafe(prev_block);

    if (is_block_in_active_part(em, nb) && get_prev(nb) == prev_block) return NULL;

    Block *final_block = create_block(nb);
    set_prev(final_block, prev_block);
    
    return final_block;
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
static inline Block *rotateLeft(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'rotateLeft' called on NULL current_block");
    
    Block *x = get_right_tree(current_block);
    set_right_tree(current_block, get_left_tree(x));
    set_left_tree(x, current_block);

    set_color(x, get_color(current_block));
    set_color(current_block, EMRED);

    return x;
}

/*
 * Rotate right
 * Used to balance the LLRB tree
 */
static inline Block *rotateRight(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'rotateRight' called on NULL current_block");
    
    Block *x = get_left_tree(current_block);
    set_left_tree(current_block, get_right_tree(x));
    set_right_tree(x, current_block);

    set_color(x, get_color(current_block));
    set_color(current_block, EMRED);

    return x;
}

/*
 * Flip colors
 * Used to balance the LLRB tree
 */
static inline void flipColors(Block *current_block) {
    EM_ASSERT((current_block != NULL) && "Internal Error: 'flipColors' called on NULL current_block");
    
    set_color(current_block, EMRED);
    set_color(get_left_tree(current_block), EMBLACK);
    set_color(get_right_tree(current_block), EMBLACK);
}

/*
 * Check if block is red
 * Helper function to check if a block is red in the LLRB tree
 */
static inline bool is_red(Block *block) {
    if (block == NULL) return false;
    return get_color(block) == EMRED;
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
    EM_ASSERT((size > 0)                           && "Internal Error: 'find_best_fit' called on too small size");
    EM_ASSERT((size <= EMMAX_SIZE)                 && "Internal Error: 'find_best_fit' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'find_best_fit' called on invalid alignment");
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)       && "Internal Error: 'find_best_fit' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)       && "Internal Error: 'find_best_fit' called on too big alignment");
    
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
    set_color(target, EMRED);
    
    if (*tree_root) *tree_root = balance(*tree_root);
}

/*
 * Find and detach block
 * High-level internal function that searches for the best fit and removes it from the tree.
 * Returns the detached block or NULL if no suitable block was found.
 */
static Block *find_and_detach_block(Block **tree_root, size_t size, size_t alignment) {
    EM_ASSERT((size > 0)                           && "Internal Error: 'find_and_detach_block' called on too small size");
    EM_ASSERT((size <= EMMAX_SIZE)                 && "Internal Error: 'find_and_detach_block' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'find_and_detach_block' called on invalid alignment");
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)       && "Internal Error: 'find_and_detach_block' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)       && "Internal Error: 'find_and_detach_block' called on too big alignment");
    
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
    
    if (full_size > needed_size && full_size - needed_size >= EMBLOCK_MIN_SIZE) {
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
    uintptr_t *detector_spot = (uintptr_t *)(void *)((char *)prev - sizeof(uintptr_t));
    uintptr_t val = *detector_spot;
    
    if (val & 1) return (EM *)(void *)((char *)prev - (val >> 1));

    return (EM *)(void *)((char *)prev - sizeof(EM));
}





/*
 * Free scratch memory in easy memory
 * Marks the scratch memory as free
 */
static void em_free_scratch(EM *em, Block *scratch_block) {
    if (!em || !em_get_has_scratch(em)) return;

    em_set_has_scratch(em, false);

    Block *tail = em_get_tail(em);

    if (get_size(tail) != 0) {
        set_color(scratch_block, EMRED);
        set_is_free(scratch_block, true);
        set_prev(scratch_block, tail);
        em_set_tail(em, scratch_block);
        set_size(scratch_block, 0);
    }
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
        em_free_scratch(em, block);
        return;
    }

    set_is_free(block, true);
    set_left_tree(block, NULL);
    set_right_tree(block, NULL);
    set_color(block, EMRED);

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
    EM_ASSERT((size <= EMMAX_SIZE)                 && "Internal Error: 'alloc_in_free_blocks' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0) && "Internal Error: 'alloc_in_free_blocks' called on invalid alignment");
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)       && "Internal Error: 'alloc_in_free_blocks' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)       && "Internal Error: 'alloc_in_free_blocks' called on too big alignment");

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
    set_color(block, EMRED);

    return (void *)aligned_ptr;
}

/*
 * Allocate memory in tail block of easy memory (full version)
 * Attempts to allocate a block of memory of given size and alignment in the tail block of the easy memory
 * Returns pointer to allocated memory or NULL if allocation fails
 */
static void *alloc_in_tail_full(EM *em, size_t size, size_t alignment) {
    EM_ASSERT((em != NULL)                           && "Internal Error: 'alloc_in_tail_full' called on NULL easy memory");
    EM_ASSERT((size > 0)                             && "Internal Error: 'alloc_in_tail_full' called on too small size");
    EM_ASSERT((size <= EMMAX_SIZE)                   && "Internal Error: 'alloc_in_tail_full' called on too big size");
    EM_ASSERT(((alignment & (alignment - 1)) == 0)   && "Internal Error: 'alloc_in_tail_full' called on invalid alignment");
    EM_ASSERT((alignment >= EMMIN_ALIGNMENT)         && "Internal Error: 'alloc_in_tail_full' called on too small alignment");
    EM_ASSERT((alignment <= EMMAX_ALIGNMENT)         && "Internal Error: 'alloc_in_tail_full' called on too big alignment");
    if (free_size_in_tail(em) < size) return NULL;  // Quick check to avoid unnecessary calculations
    
    /*
     * Allocation in tail may seem simple at first glance, but there are several edge cases to consider:
     * 1. Alignment padding before user data:
     *      If the required alignment is greater than the easy memory's alignment, 
     *       we need to calculate the padding needed before the user data to satisfy the alignment requirement.
     *      If calculated padding is so big that it by itself can contain a whole minimal block(EMBLOCK_MIN_SIZE) or more,
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
        if (padding >= EMBLOCK_MIN_SIZE) {
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
    size_t final_needed_block_size;
    if (free_space - minimal_needed_block_size >= EMBLOCK_MIN_SIZE) {
        uintptr_t raw_data_end_ptr = aligned_data_ptr + size;
        uintptr_t aligned_data_end_ptr = align_up(raw_data_end_ptr + sizeof(Block), em_get_alignment(em)) - sizeof(Block);
        size_t end_padding = aligned_data_end_ptr - raw_data_end_ptr;
    
        size_t full_needed_block_size = minimal_needed_block_size + end_padding;
        if (free_space - full_needed_block_size >= EMBLOCK_MIN_SIZE) {
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
    set_color(tail, EMRED);
    set_em(tail, em);

    // If there is remaining free space, create a new free block
    if (free_space != final_needed_block_size) {
        Block *new_tail = create_next_block(em, tail);
        if (new_tail) em_set_tail(em, new_tail);
        else set_size(tail, free_space);
    }

    return (void *)aligned_data_ptr;
}





/*
 * Deallocate a memory block
 *
 * Returns a previously allocated block of memory to its parent Easy Memory instance.
 * The function automatically identifies the block type (standard or scratch) and 
 * performs coalescing (merging) with adjacent free blocks to mitigate fragmentation.
 *
 * Parameters:
 *   - data: Pointer to the memory area to be freed. Must be a pointer previously 
 *           returned by any em_alloc_* or em_calloc function.
 * 
 * Performance: 
 *   - O(1) Constant Time if freeing the most recent allocation (the tail block).
 *   - O(log n) if the block is in the middle of the heap (requires LLRB tree manipulation).
 * 
 * Safety & Behavior:
 *   The function's response to invalid input is governed by the EM_SAFETY_POLICY:
 *
 *   - EM_POLICY_CONTRACT:
 *       Passing a NULL pointer or a pointer not managed by this library is a 
 *       violation of the API contract. This will trigger an EM_ASSERT. If assertions 
 *       are disabled in release builds, this results in Undefined Behavior.
 *   - EM_POLICY_DEFENSIVE:
 *       The function performs robust runtime validation. If 'data' is NULL or 
 *       if the block metadata is corrupted/invalid, the function will safely 
 *       return without performing any operations, preventing a crash.
 *
 * Note: Once freed, the 'data' pointer becomes invalid and should not be accessed. 
 * If EM_POISONING is enabled, the memory area will be filled with EM_POISON_BYTE.
 */
EMDEF void em_free(void *data) {
    EM_CHECK_V((data != NULL),                             "Internal Error: 'em_free' called on NULL pointer");
    EM_CHECK_V(((uintptr_t)data % sizeof(uintptr_t) == 0), "Internal Error: 'em_free' called on unaligned pointer");

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

    uintptr_t *spot_before_user_data = (uintptr_t *)(void *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)EM_MAGIC) {
        block = (Block *)(void *)((char *)data - sizeof(Block));
    }
    else {
        EM_CHECK_V(((uintptr_t)check % sizeof(uintptr_t) == 0), "Internal Error: 'em_free' detected corrupted block metadata");
        block = (Block *)check;
    }

    #if EM_SAFETY_POLICY == EM_POLICY_DEFENSIVE
        EM_ASSERT((block != NULL) && "Internal Error: 'em_free' detected NULL block");
        
        EM_CHECK_V((get_size(block) <= EMMAX_SIZE), "Internal Error: 'em_free' detected block with invalid size");
        EM_CHECK_V((is_valid_magic(block, data)),    "Internal Error: 'em_free' detected block with invalid magic");
        EM *em = get_em(block);
        
        EM_ASSERT((em != NULL) && "Internal Error: 'em_free' detected block with NULL em");

        EM_CHECK_V((is_block_within_em(em, block)), "Internal Error: 'em_free' detected block outside of its easy memory");
    #else
        EM *em = get_em(block); 
    #endif
        
    EM_CHECK_V((!get_is_free(block)), "Internal Error: 'em_free' called on already freed block");    

    em_free_block_full(em, block);
}

/*
 * Allocate memory with custom alignment
 *
 * Attempts to find or create a contiguous block of memory within the arena that 
 * satisfies both the requested size and alignment constraints.
 *
 * Performance:
 *   - O(1) Fast-Path: Sequential allocations from the tail block.
 *   - O(log n) Fallback: Best-fit search in the LLRB tree for fragmented memory.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: 1 byte (internally padded to EM_MIN_BUFFER_SIZE, default 16).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: The current free space of the instance minus sizeof(Block) 
 *     for metadata. In any case, it cannot exceed the Physical Max
 *
 * Parameters:
 *   - em:        Pointer to the Easy Memory instance.
 *   - size:      Number of bytes to allocate (must be > 0 and not exceed instance capacity).
 *   - alignment: Boundary (power of two, within supported range).
 *
 * Returns:
 *   A pointer to the aligned memory block, or NULL if the allocation fails.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: Triggers EM_ASSERT on NULL 'em', zero size, or 
 *     invalid alignment.
 *   - EM_POLICY_DEFENSIVE: Returns NULL on any invalid input or if an internal 
 *     integer overflow is detected during padding calculation.
 */
EMDEF void *em_alloc_aligned(EM *EM_RESTRICT em, size_t size, size_t alignment) {
    EM_CHECK((em != NULL),                         NULL, "Internal Error: 'em_alloc_aligned' called on NULL easy memory");
    EM_CHECK((size > 0),                           NULL, "Internal Error: 'em_alloc_aligned' called on too small size");
    EM_CHECK((size <= em_get_capacity(em)),        NULL, "Internal Error: 'em_alloc_aligned' called on too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_alloc_aligned' called on invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT),       NULL, "Internal Error: 'em_alloc_aligned' called on too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT),       NULL, "Internal Error: 'em_alloc_aligned' called on too big alignment");

    // Trying to allocate in free blocks first
    void *result = alloc_in_free_blocks(em, size, alignment);
    if (result) return result;

    if (free_size_in_tail(em) == 0) return NULL;
    return alloc_in_tail_full(em, size, alignment);
}

/*
 * Allocate memory with default alignment
 *
 * A convenience wrapper for em_alloc_aligned that uses the arena's baseline 
 * alignment (configured during instance creation).
 *
 * Performance:
 *   - O(1) Constant Time (Fast-Path): Sequential allocations from the tail block.
 *   - O(log n) Fallback: Best-fit search in the LLRB tree for fragmented memory.
 *
 * Alignment Requirements:
 *   - Uses the default alignment of the EM instance.
 *
 * Capacity Limits:
 *   - Minimum: 1 byte (internally padded to EM_MIN_BUFFER_SIZE, default 16).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: The current free space of the instance minus sizeof(Block) 
 *     for metadata. In any case, it cannot exceed the Physical Max
 *
 * Parameters:
 *   - em:   Pointer to the Easy Memory instance.
 *   - size: Bytes to allocate (must not exceed instance capacity).
 *
 * Returns:
 *   - Pointer to the aligned memory, or NULL on failure.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if 'em' is NULL or 'size' is 0 or out of range.
 *   - EM_POLICY_DEFENSIVE: 
 *       Returns NULL if 'em' is NULL, 'size' is 0, or if the arena is exhausted.
 */
EMDEF void *em_alloc(EM *EM_RESTRICT em, size_t size) {
    EM_CHECK((em != NULL), NULL, "Internal Error: 'em_alloc' called on NULL easy memory");

    return em_alloc_aligned(em, size, em_get_alignment(em));
}

/*
 * Allocate scratch memory at the physical end of the instance [IN PROGRESS]
 *
 * This function provides a specialized mechanism to dynamically "bite off" memory 
 * from the very end (highest addresses) of the memory pool.
 *
 * Performance: 
 *   O(1) Constant Time. Since it simply calculates the address relative to the 
 *   total capacity and avoids tree searching, it is the fastest allocation method.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: 1 byte (internally padded to EM_MIN_BUFFER_SIZE, default 16).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: The current free space of the instance minus sizeof(Block) 
 *     for metadata. In any case, it cannot exceed the Physical Max
 * 
 * Rationale:
 *   Scratch allocation is designed to solve the "Short-lived vs. Long-lived" object 
 *   problem. By dynamically taking memory from the opposite end of the pool, 
 *   temporary buffers (e.g., workspaces, loaders) do not interfere with the 
 *   main heap topology, preserving large contiguous chunks for long-lived data.
 *
 * Mechanism:
 *   There is no pre-reserved "scratch slot". The function calculates a position 
 *   at the extreme tail of the instance's total capacity, effectively shrinking 
 *   the available space for standard allocations from the top down.
 * 
 * Deallocation (Crucial):
 *   It is STRONGLY RECOMMENDED to use the standard 'em_free()' function to 
 *   release this memory. The library is designed to automatically detect 
 *   scratch blocks and correctly update the instance state upon deallocation.
 *
 * Constraints:
 *   - Only ONE active scratchpad allocation is allowed at a time per any 
 *     Easy Memory instance.
 *   - The scratchpad must be released (em_free) to reclaim the memory 
 *     and allow a new scratch allocation.
 *
 * Parameters:
 *   - em:        Pointer to the Easy Memory instance.
 *   - size:      Size of the scratch buffer.
 *   - alignment: Alignment boundary (power of two).
 *
 * Returns:
 *   A pointer to the scratch buffer at the end of the pool, or NULL on failure.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT:
 *       Breaching the following triggers EM_ASSERT:
 *       - 'em' is NULL or a scratchpad is already active.
 *       - 'size' is 0 or too large.
 *       - 'alignment' is invalid.
 *
 *   - EM_POLICY_DEFENSIVE:
 *       Returns NULL if:
 *       - A scratchpad is already in use.
 *       - There is not enough free space between the standard tail and the 
 *         end of the pool to fit the requested size and metadata.
 *       - Any parameter is invalid.
 */
EMDEF void *em_alloc_scratch_aligned(EM *EM_RESTRICT em, size_t size, size_t alignment) {
    EM_CHECK((em != NULL)                        , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on NULL easy memory");
    EM_CHECK((size > 0)                          , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on too small size");
    EM_CHECK((!em_get_has_scratch(em))           , NULL,"Internal Error: 'em_alloc_scratch_aligned' called when scratch already allocated");
    EM_CHECK((size <= em_get_capacity(em))       , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL,"Internal Error: 'em_alloc_scratch_aligned' called on invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on too big alignment");
    EM_CHECK((size <= free_size_in_tail(em))     , NULL,"Internal Error: 'em_alloc_scratch_aligned' called on too big size for scratch");

    uintptr_t raw_end_of_em = (uintptr_t)em + em_get_capacity(em);
    uintptr_t end_of_em = raw_end_of_em;
    end_of_em = align_down(end_of_em, EMMIN_ALIGNMENT);
    
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
 * Allocate scratch memory with default alignment [IN PROGRESS]
 *
 * A convenience wrapper for em_alloc_scratch_aligned that uses the arena's 
 * baseline alignment (configured during instance creation). 
 *
 * Performance:
 *   - O(1) Constant Time. This is the fastest allocation method as it 
 *     dynamically "bites off" memory from the extreme physical end of the pool.
 *
 * Alignment Requirements:
 *   - Uses the default alignment of the EM instance.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: 1 byte (internally padded to machine-word boundary).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: The free space at the tail end of the arena minus sizeof(Block) 
 *     for metadata and an additional machine-word for internal size tracking.
 *
 * Constraints:
 *   - Only ONE active scratchpad allocation per any Easy Memory instance.
 *   - You must free (em_free) the current scratchpad before allocating a new one.
 *
 * Parameters:
 *   - em:   Pointer to the Easy Memory instance.
 *   - size: Bytes to allocate.
 *
 * Returns:
 *   - Pointer to the scratch buffer at the end of the pool, or NULL on failure.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if 'em' is NULL, a scratch is already active, 
 *       или 'size' некорректен.
 *   - EM_POLICY_DEFENSIVE: 
 *       Returns NULL if 'em' is NULL, scratch is already active, or 
 *       the tail area cannot fit the request + overhead.
 */
EMDEF void *em_alloc_scratch(EM *EM_RESTRICT em, size_t size) {
    EM_CHECK((em != NULL), NULL, "Internal Error: 'em_alloc_scratch' called on NULL easy memory"); 

    return em_alloc_scratch_aligned(em, size, em_get_alignment(em));
}

/*
 * Allocate zero-initialized memory for an array
 *
 * Allocates a block of memory for an array of 'nmemb' elements of 'size' bytes 
 * each and initializes all bytes in the allocated storage to zero.
 *
 * Performance:
 *   Equivalent to em_alloc() plus a memset() operation.
 * 
 * Alignment Requirements:
 *   - Uses the default alignment of the EM instance.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: 1 byte (internally padded to EM_MIN_BUFFER_SIZE, default 16).
 *   - Maximum: 512 MiB (32-bit) or 2 EiB (64-bit).
 *   - Note: The function performs a safe overflow check (nmemb * size) before allocation.
 *
 * Parameters:
 *   - em:    Pointer to the Easy Memory instance.
 *   - nmemb: Number of elements in the array.
 *   - size:  Size of each element in bytes.
 *
 * Returns:
 *   A pointer to the zero-initialized memory, or NULL if the allocation fails 
 *   or if the multiplication (nmemb * size) results in an integer overflow.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT:
 *       Triggers EM_ASSERT if 'em' is NULL, if parameters are zero, or if 
 *       an integer overflow is detected.
 *
 *   - EM_POLICY_DEFENSIVE:
 *       Safely returns NULL on invalid parameters or detected integer overflow.
 *       The multiplication check is performed BEFORE any allocation attempt.
 */
EMDEF void *em_calloc(EM *EM_RESTRICT em, size_t nmemb, size_t size) {
    EM_CHECK((em != NULL),                 NULL, "Internal Error: 'em_calloc' called on NULL easy memory");
    EM_CHECK((nmemb > 0),                  NULL, "Internal Error: 'em_calloc' called on zero nmemb");
    EM_CHECK((size > 0),                   NULL, "Internal Error: 'em_calloc' called on zero size");
    EM_CHECK(((SIZE_MAX / nmemb) >= size), NULL, "Internal Error: 'em_calloc' detected size overflow");

    size_t total_size = nmemb * size;
    void *ptr = em_alloc(em, total_size);
    if (ptr) {
        memset(ptr, 0, total_size); // Zero-initialize the allocated memory
    }
    return ptr;
}

/*
 * Initialize an Easy Memory instance over a static buffer
 *
 * Transforms a raw pre-allocated block of memory into a fully functional arena.
 * This is the primary initialization function for bare-metal, stack-allocated, 
 * or shared memory environments.
 *
 * Flexible Input Handling:
 *   The function is designed to handle "crooked" or unaligned input pointers. 
 *   It will automatically shift its internal starting position to the nearest 
 *   required machine-word boundary. 
 *   Note: This internal alignment shift slightly reduces the usable capacity 
 *   from the provided total 'size'.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: ~48 bytes (32-bit) or ~80 bytes (64-bit). 
 *     Calculated as: sizeof(EM) + sizeof(Block) + EM_MIN_BUFFER_SIZE.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: Total 'size' minus internal alignment padding, sizeof(EM) 
 *     header, and the first block's metadata (sizeof(Block)).
 *
 * Parameters:
 *   - memory:    Pointer to the start of the buffer (alignment is handled internally).
 *   - size:      Total size of the buffer in bytes.
 *   - alignment: Baseline alignment for all future allocations in this instance.
 *
 * Returns:
 *   A pointer to the initialized EM header within the provided buffer, 
 *   or NULL if the usable area (after self-alignment) is below the minimum threshold.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT:
 *       Triggers EM_ASSERT on NULL memory or if size is outside [Min..Max] range.
 *
 *   - EM_POLICY_DEFENSIVE:
 *       Gracefully returns NULL if input parameters are invalid or if the 
 *       buffer cannot satisfy the initialization overhead.
 */
EMDEF EM *em_create_static_aligned(void *EM_RESTRICT memory, size_t size, size_t alignment) {
    EM_CHECK((memory != NULL)                    , NULL, "Internal Error: 'em_create_static_aligned' called with NULL memory");
    EM_CHECK((size >= EMMIN_SIZE)                , NULL, "Internal Error: 'em_create_static_aligned' called with too small size");
    EM_CHECK((size <= EMMAX_SIZE)                , NULL, "Internal Error: 'em_create_static_aligned' called with too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_create_static_aligned' called with invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL, "Internal Error: 'em_create_static_aligned' called with too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL, "Internal Error: 'em_create_static_aligned' called with too big alignment");

    uintptr_t raw_addr = (uintptr_t)memory;
    uintptr_t aligned_addr = align_up(raw_addr, EMMIN_ALIGNMENT);
    size_t em_padding = aligned_addr - raw_addr; 

    if (size < em_padding + sizeof(EM) + EMBLOCK_MIN_SIZE) return NULL;
    
    EM *em = (EM *)aligned_addr;

    // Initialize all fields to zero/NULL
    em->as.self.capacity_and_alignment = 0;
    em->as.self.prev = NULL;
    em->as.self.tail = NULL;
    em->as.self.free_blocks = NULL;

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
 * Create a static Easy Memory instance with default alignment over a static buffer
 *
 * A convenience wrapper for em_create_static_aligned using the arena's 
 * baseline alignment (default 16 bytes).
 *
 * Performance:
 *   - O(1) Constant Time.
 *
 * Alignment Requirements:
 *   - Fixed at 16 bytes (unless EM_DEFAULT_ALIGNMENT is overridden).
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum: ~48 bytes (32-bit) or ~80 bytes (64-bit).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: Total 'size' minus internal alignment padding, sizeof(EM), 
 *     and sizeof(Block).
 *
 * Parameters:
 *   - memory: Raw pointer to the buffer (internal alignment shift is applied).
 *   - size:   Total size of the provided buffer in bytes.
 *
 * Returns:
 *   - Pointer to the initialized EM instance, or NULL on failure.
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies and limits as em_create_static_aligned.
 */
EMDEF EM *em_create_static(void *EM_RESTRICT memory, size_t size) {
    EM_CHECK((memory != NULL), NULL, "Internal Error: 'em_create_static' called with NULL memory");

    return em_create_static_aligned(memory, size, EM_DEFAULT_ALIGNMENT);
}

#ifndef EM_NO_MALLOC
/*
 * Create a dynamic Easy Memory instance on the heap
 *
 * Allocates a contiguous block from the system heap (via malloc) and 
 * initializes it as a dynamic Easy Memory arena.
 *
 * Performance:
 *   - O(1) + system malloc() overhead.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Minimum Usable: 16 bytes (EM_MIN_BUFFER_SIZE).
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Usable Max: Equal to the requested 'size'. 
 *     Note: The actual system memory consumption will be higher due to the 
 *     EM header (sizeof(EM)) and alignment overhead.
 *
 * Parameters:
 *   - size:      The requested usable capacity of the arena.
 *   - alignment: Baseline alignment for all future allocations (power of two).
 *
 * Returns:
 *   - Pointer to the new EM instance, or NULL if system malloc fails or 
 *     if (size + overhead) causes an integer overflow.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if size is out of range or overhead calculation overflows.
 *   - EM_POLICY_DEFENSIVE: 
 *       Performs an unconditional overflow check and returns NULL if the 
 *       request is mathematically impossible to satisfy.
 */
EMDEF EM *em_create_aligned(size_t size, size_t alignment) {
    size_t overhead = sizeof(EM) + alignment;
    EM_CHECK((size <= SIZE_MAX - overhead)       , NULL, "Internal Error: 'em_create_aligned' size overflow");
    EM_CHECK((size >= EMBLOCK_MIN_SIZE)          , NULL, "Internal Error: 'em_create_aligned' called with too small size");
    EM_CHECK((size <= EMMAX_SIZE)                , NULL, "Internal Error: 'em_create_aligned' called with too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_create_aligned' called with invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL, "Internal Error: 'em_create_aligned' called with too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL, "Internal Error: 'em_create_aligned' called with too big alignment");

    void *data = malloc(size + overhead);
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
 * Create a dynamic Easy Memory instance with default alignment
 *
 * A convenience wrapper for em_create_aligned using the baseline 
 * default alignment (16 bytes).
 *
 * Performance:
 *   - O(1) + system malloc() overhead.
 *
 * Alignment Requirements:
 *   - Fixed at 16 bytes (unless EM_DEFAULT_ALIGNMENT is overridden).
 *
 * Capacity Limits:
 *   - Minimum Usable: 16 bytes.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit).
 *
 * Parameters:
 *   - size: The requested usable capacity of the arena.
 *
 * Returns:
 *   - Pointer to the new EM instance, or NULL on failure.
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies and overflow checks as em_create_aligned.
 */
EMDEF EM *em_create(size_t size) {
    return em_create_aligned(size, EM_DEFAULT_ALIGNMENT);
}
#endif // EM_NO_MALLOC

/*
 * Destroy an Easy Memory instance
 *
 * Reclaims all resources associated with the arena and terminates its lifecycle.
 *
 * Performance:
 *   - Static/Dynamic: O(1) constant time.
 *   - Nested: O(N), where N is the number of physically adjacent free blocks 
 *     and nested arenas located immediately BEFORE the target instance. 
 *     Rationale: The library performs a "Walkback" search to find the parent 
 *     header without storing explicit parent pointers in every block.
 *     Note: Typically N is very small (1 to 3), ensuring near-instant operation.
 *
 * Mechanism (Context-Aware):
 *   1. Nested Instance: Identifies the parent arena by traversing preceding 
 *      physical neighbors, then returns its entire block to that parent.
 *   2. Dynamic Instance: Releases the heap buffer via the system free() call.
 *   3. Static Instance: No-op. The EM metadata is discarded, but the buffer 
 *      remains intact for raw memory access.
 *
 * Lifecycle & Stability:
 *   - After destruction, the 'em' pointer and ALL pointers allocated from 
 *     it become invalid (Use-After-Free risk).
 *   - Destroying a parent instance effectively invalidates all its nested 
 *     children, as their underlying storage is reclaimed.
 *
 * Parameters:
 *   - em: Pointer to the Easy Memory instance to be destroyed.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: Triggers EM_ASSERT if 'em' is NULL.
 *   - EM_POLICY_DEFENSIVE: Safely returns without action if 'em' is NULL.
 */
EMDEF void em_destroy(EM *em) {
    EM_CHECK_V((em != NULL), "Internal Error: 'em_destroy' called on NULL easy memory");

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
 * Reset the Easy Memory instance
 *
 * Wipes all internal metadata and returns the arena to its initial state 
 * without releasing the underlying memory.
 *
 * Performance: 
 *   - O(1) Constant Time. Only internal flags and the tail pointer are reset.
 *
 * Capacity & Alignment:
 *   - Baseline alignment and total capacity remain unchanged.
 *
 * Parameters:
 *   - em: Pointer to the Easy Memory instance to be reset.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: Triggers EM_ASSERT if 'em' is NULL.
 *   - EM_POLICY_DEFENSIVE: Safely returns if 'em' is NULL.
 *
 * Note: After a reset, ALL previously allocated pointers from this 
 * instance become invalid, though the memory is not physically cleared 
 * (unless using em_reset_zero).
 */
EMDEF void em_reset(EM *EM_RESTRICT em) {
    EM_CHECK_V((em != NULL), "Internal Error: 'em_reset' called on NULL easy memory");

    Block *first_block = em_get_first_block(em);

    // Reset first block
    set_size(first_block, 0);
    set_prev(first_block, NULL);
    set_is_free(first_block, true);
    set_color(first_block, EMRED);
    set_left_tree(first_block, NULL);
    set_right_tree(first_block, NULL);

    // Reset easy memory metadata
    em_set_free_blocks(em, NULL);
    em_set_tail(em, first_block);
    em_set_has_scratch(em, false);
}

/*
 * Reset the instance and zero-initialize memory
 *
 * Performs an O(1) metadata reset followed by a full-span zeroing of the 
 * usable capacity.
 *
 * Performance: 
 *   - O(N) Linear Time, where N is the total usable capacity of the arena.
 *
 * Parameters:
 *   - em: Pointer to the Easy Memory instance to be reset and zeroed.
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies as em_reset.
 */
EMDEF void em_reset_zero(EM *EM_RESTRICT em) {
    EM_CHECK_V((em != NULL), "Internal Error: 'em_reset_zero' called on NULL easy memory");

    em_reset(em); // Reset easy memory
    memset(block_data(em_get_tail(em)), 0, free_size_in_tail(em)); // Set tail to zero
}

/*
 * Create a nested Easy Memory instance with custom alignment
 *
 * Carves out a block from a parent arena and initializes it as an 
 * independent child arena.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   The EM and Block structures are strictly ABI compatible. The nested 
 *   instance hijacks the parent's existing block header to store its own 
 *   EM metadata. This ensures that 100% of the allocated payload is 
 *   directly usable for child allocations, with zero additional overhead 
 *   for the arena header itself.
 *
 * Performance:
 *   - Allocation from parent: O(1) (tail) or O(log n) (tree).
 *   - Initialization: O(1) Constant Time.
 *
 * Zero-Cost Parent Tracking:
 *   - No explicit parent pointers are stored. The child arena identifies 
 *     its parent using the "Physical Neighbor Walkback" strategy, 
 *     traversing preceding blocks to find the owner. This saves 
 *     8-16 bytes of metadata per instance.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Usable Max: Exactly the 'size' requested from the parent. 
 *     The child gains full access to the parent's allocated payload area.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit).
 *
 * Parameters:
 *   - parent_em: Pointer to the active parent instance.
 *   - size:      The desired usable capacity of the nested arena.
 *   - alignment: Baseline alignment for the child's future allocations.
 *
 * Returns:
 *   - Pointer to the child EM instance, or NULL on failure.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: Triggers EM_ASSERT on NULL parent or invalid size/alignment.
 *   - EM_POLICY_DEFENSIVE: Returns NULL on invalid input or if parent is exhausted.
 */
EMDEF EM *em_create_nested_aligned(EM *EM_RESTRICT parent_em, size_t size, size_t alignment) {
    EM_CHECK((parent_em != NULL)                 , NULL, "Internal Error: 'em_create_nested_aligned' called with NULL parent easy memory");
    EM_CHECK((size >= EMBLOCK_MIN_SIZE)          , NULL, "Internal Error: 'em_create_nested_aligned' called with too small size");
    EM_CHECK((size <= EMMAX_SIZE)                , NULL, "Internal Error: 'em_create_nested_aligned' called with too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_create_nested_aligned' called with invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL, "Internal Error: 'em_create_nested_aligned' called with too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL, "Internal Error: 'em_create_nested_aligned' called with too big alignment");

    void *data = em_alloc(parent_em, size);  // Allocate memory from the parent easy memory
    if (!data) return NULL;

    Block *block = NULL;

    uintptr_t *spot_before_user_data = (uintptr_t *)(void *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)EM_MAGIC) {
        block = (Block *)(void *)((char *)data - sizeof(Block));
    }
    // LCOV_EXCL_START
    else {
        block = (Block *)check;
    }
    // LCOV_EXCL_STOP

    Block *prev = get_prev(block);

    EM *em = em_create_static_aligned((void *)block, size, alignment);
    em_set_is_nested(em, true); // Mark the easy memory as nested
    set_prev(block, prev);      // Restore the previous block link

    return em;
}

/*
 * Create a nested Easy Memory instance with default alignment
 *
 * Carves out a block from a parent arena and initializes it as an 
 * independent child arena (nested instance) using the parent's alignment.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   The EM and Block structures are strictly ABI compatible. The nested 
 *   instance hijacks the parent's existing block header to store its own 
 *   EM metadata. This ensures that 100% of the allocated payload is 
 *   directly usable for child allocations, with zero additional overhead 
 *   for the arena header itself.
 *
 * Performance:
 *   - Allocation from parent: O(1) Constant Time (if from tail) or 
 *     O(log n) (if from LLRB tree).
 *   - Initialization: O(1) Constant Time.
 *
 * Zero-Cost Parent Tracking:
 *   - No explicit parent pointers are stored in the hierarchy. The child 
 *     arena identifies its parent header using the "Physical Neighbor 
 *     Walkback" strategy, traversing preceding blocks to find the owner. 
 *     This saves 8-16 bytes of metadata per nested instance.
 *
 * Alignment Requirements:
 *   - Uses the default alignment of the parent EM instance.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Usable Max: Exactly the 'size' requested from the parent. 
 *     The child gains full access to the parent's allocated payload area.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Minimum: ~48 bytes (32-bit) or ~80 bytes (64-bit).
 *
 * Parameters:
 *   - parent_em: Pointer to the active parent instance.
 *   - size:      The desired usable capacity of the nested arena.
 *
 * Returns:
 *   - Pointer to the child EM instance, or NULL if the parent cannot fit 
 *     the requested block or if parameters are invalid.
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies and overflow checks as em_create_nested_aligned.
 */
EMDEF EM *em_create_nested(EM *EM_RESTRICT parent_em, size_t size) {
    EM_CHECK((parent_em != NULL), NULL, "Internal Error: 'em_create_nested' called with NULL parent easy memory");

    return em_create_nested_aligned(parent_em, size, em_get_alignment(parent_em));
}

/*
 * Create a scratch nested Easy Memory instance with custom alignment [IN PROGRESS]
 *
 * A specialized nested arena that is dynamically "bitten off" from the 
 * extreme physical end (highest addresses) of the parent instance.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   The EM and Block structures are strictly ABI compatible. The scratch 
 *   instance hijacks the parent's block header to store its own EM metadata. 
 *   No additional memory is wasted on a separate arena header.
 *
 * Instant Parent Tracking (O(1)):
 *   Unlike standard nested arenas that use "Walkback" search, a scratch 
 *   instance stores an explicit link to its parent in the 'prev' field of 
 *   its header. Since scratch blocks are terminal and isolated, this 
 *   repurposing of the 'prev' pointer ensures O(1) parent access with 
 *   zero additional metadata overhead.
 *
 * Performance: 
 *   - Allocation: O(1) Constant Time (Bites off from the tail).
 *   - Initialization: O(1) Constant Time.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit) or [8..1024] bytes (64-bit).
 *
 * Capacity Limits:
 *   - Usable Max: Exactly the 'size' requested from the parent's tail.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit).
 *
 * Constraints:
 *   - Only ONE active scratchpad allocation per any Easy Memory instance.
 *   - Current scratch arena must be destroyed before creating a new one.
 *
 * Parameters:
 *   - parent_em: Pointer to the active parent instance.
 *   - size:      Total capacity to carve out from the parent's end.
 *   - alignment: Baseline alignment for the scratch arena's allocations.
 *
 * Returns:
 *   - Pointer to the scratch EM instance, or NULL if the parent's tail 
 *     is insufficient or a scratch is already active.
 */
EMDEF EM *em_create_scratch_aligned(EM *EM_RESTRICT parent_em, size_t size, size_t alignment) {
    EM_CHECK((parent_em != NULL)                 , NULL, "Internal Error: 'em_create_scratch_aligned' called with NULL parent easy memory");
    EM_CHECK((!em_get_has_scratch(parent_em))    , NULL, "Internal Error: 'em_create_scratch_aligned' called when scratch already allocated in parent");
    EM_CHECK((size >= EMBLOCK_MIN_SIZE)          , NULL, "Internal Error: 'em_create_scratch_aligned' called with too small size");
    EM_CHECK((size <= EMMAX_SIZE)                , NULL, "Internal Error: 'em_create_scratch_aligned' called with too big size");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_create_scratch_aligned' called with invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL, "Internal Error: 'em_create_scratch_aligned' called with too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL, "Internal Error: 'em_create_scratch_aligned' called with too big alignment");
        
    void *data = em_alloc_scratch_aligned(parent_em, size, alignment);  // Allocate memory from the parent easy memory scratch
    if (!data) return NULL;

    Block *block = (Block *)(void *)((char *)data - sizeof(Block)); // Scratch block is always with no padding before user data
    
    EM *em = em_create_static_aligned((void *)block, size, alignment);
    em_set_is_nested(em, true); // Mark the easy memory as nested
    set_color(&(em->as.block_representation), EMBLACK);  // Scratch block is always black to highlight its special status
    set_prev(&(em->as.block_representation), parent_em); // Scratch block has no previous block so we use prev pointer to store parent EM pointer

    return em;
}

/*
 * Create a scratch nested Easy Memory instance with default alignment [IN PROGRESS]
 *
 * A convenience wrapper for em_create_scratch_aligned that uses the 
 * parent instance's baseline alignment. 
 *
 * This function dynamically "bites off" memory from the extreme physical end 
 * (highest addresses) of the parent instance.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   The EM and Block structures are strictly ABI compatible. The scratch 
 *   instance hijacks the parent's block header to store its own EM metadata. 
 *   This ensures that 100% of the allocated scratch payload is directly 
 *   usable for allocations, with zero additional overhead for the arena header.
 *
 * Instant Parent Tracking (O(1)):
 *   Unlike standard nested arenas that use "Walkback" search, a scratch 
 *   instance stores an explicit link to its parent in the 'prev' field of 
 *   its header. Since scratch blocks are terminal and isolated, this 
 *   repurposing of the 'prev' pointer ensures O(1) parent access with 
 *   zero additional metadata overhead.
 *
 * Performance: 
 *   - Allocation: O(1) Constant Time (Bites off from the parent's tail).
 *   - Initialization: O(1) Constant Time.
 *
 * Alignment Requirements:
 *   - Uses the default alignment of the parent EM instance.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Usable Max: Exactly the 'size' requested from the parent's tail area.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit), limited by bit-packing.
 *   - Minimum: ~48 bytes (32-bit) or ~80 bytes (64-bit).
 *
 * Constraints:
 *   - Only ONE active scratchpad allocation (raw or arena) per any 
 *     Easy Memory instance.
 *   - You must destroy the current scratch arena before creating a new one.
 *
 * Parameters:
 *   - parent_em: Pointer to the active parent instance.
 *   - size:      Total capacity to carve out from the parent's end.
 *
 * Returns:
 *   - Pointer to the scratch EM instance, or NULL if the parent's tail 
 *     is insufficient or a scratch is already active.
 */
EMDEF EM *em_create_scratch(EM *EM_RESTRICT parent_em, size_t size) {
    EM_CHECK((parent_em != NULL), NULL, "Internal Error: 'em_create_scratch' called with NULL parent easy memory");

    return em_create_scratch_aligned(parent_em, size, em_get_alignment(parent_em));
}





/*
 * Create a bump allocator
 *
 * Initializes a linear (bump) allocator within a parent Easy Memory instance. 
 * This is ideal for high-speed, temporary allocations where all memory is 
 * released at once.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   The Bump and Block structures are strictly ABI compatible. The bump 
 *   allocator hijacks the parent's block header to store its metadata (offset, 
 *   capacity, and parent link). No extra memory is wasted on the allocator's 
 *   own management structure.
 *
 * Performance:
 *   - Creation: O(1) (from tail) or O(log n) (from tree) of parent arena.
 *   - Allocation: O(1) Constant Time (linear pointer shift).
 *
 * Instant Parent Tracking (O(1)):
 *   The bump allocator stores a direct pointer to its parent EM instance within 
 *   its hijacked header, ensuring O(1) complexity for destruction and trimming.
 *
 * Capacity Limits:
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit).
 *   - Usable Max: Exactly the 'size' requested from the parent. 
 *     Note: The internal offset starts after the header (sizeof(Bump)).
 *   - Minimum: 16 bytes (EM_MIN_BUFFER_SIZE).
 *
 * Parameters:
 *   - parent_em: Pointer to the active parent instance.
 *   - size:      Total capacity to carve out from the parent.
 *
 * Returns:
 *   - Pointer to the Bump allocator instance, or NULL on failure.
 */
EMDEF Bump *em_bump_create(EM *EM_RESTRICT parent_em, size_t size) {
    EM_CHECK((parent_em != NULL),          NULL, "Internal Error: 'em_bump_create' called with NULL parent easy memory");
    EM_CHECK((size <= EMMAX_SIZE),         NULL, "Internal Error: 'em_bump_create' called with too big size");
    EM_CHECK((size >= EM_MIN_BUFFER_SIZE), NULL, "Internal Error: 'em_bump_create' called with too small size");

    void *data = em_alloc(parent_em, size);  // Allocate memory from the parent easy memory
    if (!data) return NULL;

    Block *block = NULL;

    uintptr_t *spot_before_user_data = (uintptr_t *)(void *)((char *)data - sizeof(uintptr_t));
    uintptr_t check = *spot_before_user_data ^ (uintptr_t)data;
    if (check == (uintptr_t)EM_MAGIC) {
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
 *
 * Performs a lightning-fast linear allocation by advancing an internal offset.
 *
 * Performance:
 *   - O(1) Constant Time. The fastest possible allocation (single addition).
 *
 * Alignment:
 *   - This basic version returns memory with NO additional alignment. 
 *     The result depends on the current offset state. Use em_bump_alloc_aligned 
 *     for strict boundary requirements.
 *
 * Capacity Limits:
 *   - Maximum: Remaining free space within the bump instance.
 *
 * Parameters:
 *   - bump: Pointer to the active bump allocator.
 *   - size: Bytes to allocate.
 *
 * Returns:
 *   - Pointer to the allocated memory.
 *   - Returns NULL if the allocator is exhausted or if 'size' is 0.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if 'bump' is NULL or 'size' is 0.
 *   - EM_POLICY_DEFENSIVE: 
 *       Returns NULL if 'bump' is NULL, 'size' is 0, or capacity is exceeded.
 */
EMDEF void *em_bump_alloc(Bump *EM_RESTRICT bump, size_t size) {
    EM_CHECK((bump != NULL), NULL, "Internal Error: 'em_bump_alloc' called on NULL bump allocator");
    EM_CHECK((size > 0),     NULL, "Internal Error: 'em_bump_alloc' called with zero size");

    size_t offset = bump_get_offset(bump);
    if (size >= (bump_get_capacity(bump) - offset + sizeof(Bump))) return NULL;

    void *memory = (char *)bump + offset;
    bump_set_offset(bump, offset + size);

    return memory;
}

/*
 * Allocate aligned memory from a bump allocator
 *
 * Performs a linear allocation while ensuring the returned pointer satisfies 
 * the specified alignment boundary.
 *
 * Performance:
 *   - O(1) Constant Time.
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Capacity Limits:
 *   - Maximum: Remaining free space within the bump instance.
 *   - Physical Max: 512 MiB (32-bit) or 2 EiB (64-bit).
 *
 * Parameters:
 *   - bump:      Pointer to the active bump allocator.
 *   - size:      Bytes to allocate.
 *   - alignment: Boundary (power of two, within supported range).
 *
 * Returns:
 *   - Aligned pointer to the allocated memory.
 *   - Returns NULL if the allocator is exhausted, if 'size' is 0, if 'alignment' 
 *     is invalid, or if an integer overflow is detected during padding calculation.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT on NULL 'bump', zero 'size', invalid alignment, 
 *       or integer overflow.
 *   - EM_POLICY_DEFENSIVE: 
 *       Returns NULL on any invalid input, detected overflow, or exhaustion.
 */
EMDEF void *em_bump_alloc_aligned(Bump *EM_RESTRICT bump, size_t size, size_t alignment) {
    EM_CHECK((bump != NULL)                      , NULL, "Internal Error: 'em_bump_alloc_aligned' called on NULL bump allocator");
    EM_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'em_bump_alloc_aligned' called with invalid alignment");
    EM_CHECK((alignment >= EMMIN_ALIGNMENT)      , NULL, "Internal Error: 'em_bump_alloc_aligned' called with too small alignment");
    EM_CHECK((alignment <= EMMAX_ALIGNMENT)      , NULL, "Internal Error: 'em_bump_alloc_aligned' called with too big alignment");
    EM_CHECK((size > 0)                          , NULL, "Internal Error: 'em_bump_alloc_aligned' called with zero size");

    uintptr_t current_ptr = (uintptr_t)bump + bump_get_offset(bump);
    uintptr_t aligned_ptr = align_up(current_ptr, alignment);
    size_t padding = aligned_ptr - current_ptr;

    EM_CHECK(((size_t)size <= SIZE_MAX - padding), NULL, "Internal Error: 'em_bump_alloc_aligned' size overflow");


    size_t total_size = padding + size;

    size_t offset = bump_get_offset(bump);
    if ((size_t)total_size >= (bump_get_capacity(bump) - offset + sizeof(Bump))) return NULL;

    bump_set_offset(bump, offset + total_size);

    return (void *)aligned_ptr;
}

/*
 * Trim a bump allocator
 *
 * Calculates the unused portion of the bump allocator and returns it 
 * to the parent Easy Memory instance.
 *
 * Performance:
 *   - O(N), where N is the number of physically preceding free blocks/arenas 
 *     in the parent (due to the Physical Neighbor Walkback used for merging).
 *
 * Rationale:
 *   Allows "reserving" a large chunk for temporary work and then "giving back" 
 *   what wasn't actually used, preventing memory waste.
 *
 * Parameters:
 *   - bump: Pointer to the bump allocator to be trimmed.
 *
 * Safety & Behavior:
 *   - Subject to Safety Policies. Requires a valid 'bump' instance.
 */
EMDEF void em_bump_trim(Bump *EM_RESTRICT bump) {
    EM_CHECK_V((bump != NULL), "Internal Error: 'em_bump_trim' called on NULL bump allocator");

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
 *
 * Wipes the internal offset, allowing the allocator to reuse its entire 
 * capacity from the beginning.
 *
 * Performance:
 *   - O(1) Constant Time. Only the internal offset field is updated.
 *
 * Mechanism:
 *   The function sets the internal offset back to the start (immediately 
 *   following the Bump header). It does NOT physically clear the memory 
 *   content; it only marks the entire capacity as available for new allocations.
 *
 * Parameters:
 *   - bump: Pointer to the active bump allocator to be reset.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if 'bump' is NULL.
 *   - EM_POLICY_DEFENSIVE: 
 *       Safely returns without action if 'bump' is NULL.
 *
 * Note: After a reset, ALL pointers previously allocated from this 
 * bump instance become logically invalid and will be overwritten 
 * by subsequent allocations.
 */
EMDEF void em_bump_reset(Bump *EM_RESTRICT bump) {
    EM_CHECK_V((bump != NULL), "Internal Error: 'em_bump_reset' called on NULL bump allocator");
    
    bump_set_offset(bump, sizeof(Bump));
}

/*
 * Destroy a bump allocator
 *
 * Reclaims the entire memory block used by the bump allocator and 
 * returns it to the parent Easy Memory instance.
 *
 * Zero-Overhead Header (ABI Compatibility):
 *   Since the Bump and Block structures are strictly ABI compatible, the 
 *   destruction process treats the allocator as a standard memory block.
 *
 * Performance:
 *   - O(N), where N is the number of physically adjacent free blocks 
 *     and nested arenas located immediately BEFORE the bump instance 
 *     in the parent's memory.
 *   - Rationale: The library performs a "Physical Neighbor Walkback" 
 *     to identify the parent header and perform coalescing (merging).
 *
 * Parameters:
 *   - bump: Pointer to the bump allocator to be destroyed.
 *
 * Safety & Behavior:
 *   - EM_POLICY_CONTRACT: 
 *       Triggers EM_ASSERT if 'bump' is NULL.
 *   - EM_POLICY_DEFENSIVE: 
 *       Safely returns without action if 'bump' is NULL.
 *
 * Note: After this call, the 'bump' pointer and ALL memory pointers 
 * previously allocated from it become invalid (Use-After-Free risk).
 */
EMDEF void em_bump_destroy(Bump *bump) {
    EM_CHECK_V((bump != NULL), "Internal Error: 'em_bump_destroy' called on NULL bump allocator");

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
EMDEF void print_llrb_tree(Block *node, int depth) {
    if (node == NULL) return;
    
    // Print right subtree first (to display tree horizontally)
    print_llrb_tree(get_right_tree(node), depth + 1);
    
    // Print current node with indentation
    for (int i = 0; i < depth; i++) PRINTF(T("    "));
    PRINTF(T("Block: %p, Size: %zu %i\n"),
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
EMDEF void print_em(EM *em) {
    if (!em) return;
    PRINTF(T("Easy Memory: %p\n"), em);
    PRINTF(T("EM Full Size: %zu\n"), em_get_capacity(em));
    PRINTF(T("EM Data Size: %zu\n"), em_get_capacity(em) - sizeof(EM));
    PRINTF(T("EM Alignment: %zu\n"), em_get_alignment(em));
    PRINTF(T("Data: %p\n"), (void *)((char *)em + sizeof(EM)));
    PRINTF(T("Tail: %p\n"), em_get_tail(em));
    PRINTF(T("Free Blocks: %p\n"), em_get_free_blocks(em));
    PRINTF(T("Free Size in Tail: %zu\n"), free_size_in_tail(em));
    
    if (em_get_has_scratch(em)) {
        // Calculate Scratchpad location and size
        uintptr_t raw_end = (uintptr_t)em + em_get_capacity(em);
        uintptr_t aligned_end = align_down(raw_end, EMMIN_ALIGNMENT);
        size_t *stored_size_ptr = (size_t*)(aligned_end - sizeof(uintptr_t));
        
        size_t total_scratch_size = *stored_size_ptr;
        uintptr_t header_addr = raw_end - total_scratch_size;
        Block *scratch_block = (Block *)header_addr;

        PRINTF(T("Scratchpad: PRESENT\n"));
        PRINTF(T("  Address: %p\n"), scratch_block);
        PRINTF(T("  Full Size: %zu\n"), total_scratch_size);
        PRINTF(T("  Data Size: %zu\n"), get_size(scratch_block));
    } else {
        PRINTF(T("Scratchpad: NONE\n"));
    }
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
        PRINTF(T("  Block Full Size: %zu\n"), get_size(block) + sizeof(Block));
        PRINTF(T("  Block Data Size: %zu\n"), get_size(block));
        PRINTF(T("  Is Free: %d\n"), get_is_free(block));
        PRINTF(T("  Data Pointer: %p\n"), block_data(block));
        if (!get_is_free(block)) {
            PRINTF(T("  Magic: 0x%zx\n"), get_magic(block));
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

    PRINTF(T("EM occupied data size: %zu\n"), occupied_data);
    PRINTF(T("EM occupied meta size: %zu + %zu\n"), occupied_meta, sizeof(EM));
    PRINTF(T("EM occupied full size: %zu + %zu\n"), occupied_data + occupied_meta, sizeof(EM));
    PRINTF(T("EM block count: %zu\n"), len);
}

/*
 * Print a fancy visualization of the easy memory
 * Displays a bar chart of the easy memory's usage
 * Uses ANSI escape codes to colorize the visualization
 * Legend:
 *   - Yellow (@): Metadata (EM header and block headers)
 *   - Red (#): Occupied blocks
 *   - Green (=): Free blocks
 *   - Blue (S): Scratchpad area
 *   - Black (.): Empty space (unallocated)
 */
EMDEF void print_fancy(EM *em, size_t bar_size) {
    if (!em) return;
    
    // total_size includes the EM header and the entire managed buffer
    size_t total_size = em_get_capacity(em);

    PRINTF(T("\nEasy Memory Visualization [%zu bytes]\n"), total_size);
    PRINTF(T("┌"));
    for (int i = 0; i < (int)bar_size; i++) PRINTF(T("─"));
    PRINTF(T("┐\n│"));

    // --- 1. PRE-CALCULATIONS ---

    // A. Scratchpad Offset
    // Determine where the scratchpad starts relative to the EM base address
    size_t scratch_offset = total_size; 
    if (em_get_has_scratch(em)) {
        uintptr_t raw_end = (uintptr_t)em + total_size;
        uintptr_t aligned_end = align_down(raw_end, EMMIN_ALIGNMENT);
        size_t *stored_size_ptr = (size_t*)(aligned_end - sizeof(uintptr_t));
        uintptr_t header_addr = raw_end - *stored_size_ptr;
        
        if (header_addr >= (uintptr_t)em) {
             scratch_offset = header_addr - (uintptr_t)em;
        }
    }

    // B. First Block Offset
    // Calculate offset to detect initial alignment padding
    Block *first_block = em_get_first_block(em);
    size_t first_block_offset = (uintptr_t)first_block - (uintptr_t)em;

    // --- 2. RENDERING ---

    double segment_size = (double)total_size / (double)bar_size;
    
    for (int i = 0; i < (int)bar_size; i++) {
        size_t segment_start = (size_t)(i * segment_size);
        size_t segment_end = (size_t)((i + 1) * segment_size);
        
        // CORRECTION: High-Zoom Levels
        // If the scale is so detailed that start == end (less than 1 byte per pixel),
        // force the window to be at least 1 byte to verify the content.
        if (segment_end <= segment_start) {
            segment_end = segment_start + 1;
        }

        // PRIORITY: Scratchpad (Blue)
        if (segment_start >= scratch_offset) {
            PRINTF(T("\033[44mS\033[0m")); 
            continue; 
        }

        char segment_type = '-'; // Default: Black (Void/Unknown)
        size_t max_overlap = 0;
        
        // 1. EM Header (Yellow)
        // From 0 to sizeof(EM)
        if (segment_start < sizeof(EM)) {
             size_t overlap = (segment_end < sizeof(EM) ? segment_end : sizeof(EM)) - segment_start;
             if (overlap > max_overlap) {
                 max_overlap = overlap;
                 segment_type = '@';
             }
        }

        // 2. Alignment Padding (Red/Occupied)
        // From end of EM Header to Start of First Block.
        // If first_block_offset > sizeof(EM), there is a gap used for alignment.
        if (first_block_offset > sizeof(EM)) {
            size_t pad_start = sizeof(EM);
            size_t pad_end = first_block_offset;
            
            if (segment_start < pad_end && segment_end > pad_start) {
                size_t overlap_end = (segment_end < pad_end) ? segment_end : pad_end;
                size_t overlap_start = (segment_start > pad_start) ? segment_start : pad_start;
                size_t overlap = overlap_end - overlap_start;
                
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    segment_type = '#'; // Treat padding as occupied space
                }
            }
        }
        
        // 3. Blocks (Loop)
        size_t current_pos = first_block_offset; 
        Block *current = first_block;
        
        while (current) {
            // Block Meta
            size_t block_meta_end = current_pos + sizeof(Block);
            if (segment_start < block_meta_end && segment_end > current_pos) {
                size_t overlap = (segment_end < block_meta_end ? segment_end : block_meta_end) - 
                             (segment_start > current_pos ? segment_start : current_pos);
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    segment_type = '@'; 
                }
            }
            
            // Block Data
            size_t block_len = get_size(current);
            size_t block_data_start = block_meta_end;
            size_t block_data_end = block_data_start + block_len;
            
            if (segment_start < block_data_end && segment_end > block_data_start) {
                size_t overlap_end = (segment_end > block_data_end) ? block_data_end : segment_end;
                size_t overlap_start = (segment_start < block_data_start) ? block_data_start : segment_start;
                size_t overlap = overlap_end - overlap_start;
                
                if (overlap > max_overlap) {
                    // If block is Free (including Tail) - draw Green, otherwise Red
                    max_overlap = overlap;
                    segment_type = get_is_free(current) ? ' ' : '#'; 
                }
            }
            
            current_pos = block_data_end;
            current = next_block(em, current);
            if (current_pos > segment_end) break; // Optimization
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
        } else if (segment_type == 'S') { 
            PRINTF(T("\033[44mS\033[0m")); // Blue
        }
    }

    PRINTF(T("│\n└"));
    for (int i = 0; i < (int)bar_size; i++) PRINTF(T("─"));
    PRINTF(T("┘\n"));
    
    PRINTF(T("\nLegend: "));
    PRINTF(T("\033[43m @ \033[0m - Used Meta blocks, "));
    PRINTF(T("\033[41m # \033[0m - Used Data blocks, "));
    PRINTF(T("\033[42m = \033[0m - Free blocks, "));
    PRINTF(T("\033[44m S \033[0m - Scratch block, "));
    PRINTF(T("\033[40m . \033[0m - Empty space\n\n"));
}
#endif // DEBUG

#endif // EASY_MEMORY_IMPLEMENTATION

#ifdef __cplusplus
} // extern "C"
#endif

#endif // EASY_MEMORY_H
