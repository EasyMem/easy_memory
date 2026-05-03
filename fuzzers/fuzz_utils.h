#ifndef FUZZ_UTILS_H
#define FUZZ_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define EM_ASSERT_PANIC 
#define EASY_MEMORY_IMPLEMENTATION
#include "../easy_memory.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifdef EM_FUZZ_DEBUG
    #define FUZZ_LOG(...) printf(__VA_ARGS__)
    #define FUZZ_VISUALIZE(em) do { \
        print_fancy(em, 60); \
        printf("\n======================================================\n"); \
    } while(0)
#else
    #define FUZZ_LOG(...)
    #define FUZZ_VISUALIZE(em)
#endif

static inline size_t fuzz_read_size(const uint8_t *data, size_t *offset, size_t max_size) {
    if (*offset + 1 >= max_size) return 1; // Fallback
    size_t val = (size_t)data[*offset] | ((size_t)data[*offset + 1] << 8);
    *offset += 2;
    return val;
}

static inline uint8_t fuzz_read_byte(const uint8_t *data, size_t *offset, size_t max_size) {
    if (*offset >= max_size) return 0;
    return data[(*offset)++];
}

static inline size_t fuzz_read_align(const uint8_t *data, size_t *offset, size_t max_size) {
    if (*offset >= max_size) return 16;
    size_t align_shift = (size_t)(data[(*offset)++] % 7) + 4;
    return (size_t)1 << align_shift;
}

#endif // FUZZ_UTILS_H
