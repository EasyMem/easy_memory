#include "fuzz_utils.h"
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 16) return 0;

    size_t i = 0;
    int step = 0;

    EM *em = em_create(1024 * 1024);
    if (!em) return 0;

    size_t bump_capacity = (fuzz_read_size(data, &i, size) % (512 * 1024)) + 256;
    Bump *bump = NULL;

    bool is_scratch = (data[i++] % 2 == 0);
    
    if (is_scratch) {
        bump = em_bump_create_scratch(em, bump_capacity);
    } else {
        bump = em_bump_create(em, bump_capacity);
    }

    if (!bump) {
        em_destroy(em);
        return 0;
    }

    FUZZ_LOG("\n--- STARTING BUMP REPLAY ---\n");
    FUZZ_LOG("Type: %s, Capacity: %zu\n", is_scratch ? "SCRATCH" : "NORMAL", bump_capacity);
    FUZZ_VISUALIZE(em);

    while (i < size) {
        uint8_t op = data[i++] % 4;
        step++;
        (void)step;

        switch (op) {
            case 0: { // ALLOC
                size_t alloc_sz = (fuzz_read_size(data, &i, size) % 8192) + 1;
                
                FUZZ_LOG("[%d] BUMP ALLOC: %zu -> ", step, alloc_sz);
                void *p = em_bump_alloc(bump, alloc_sz);
                FUZZ_LOG("%p\n", p);
                
                if (p) memset(p, 0xAA, alloc_sz); 
                break;
            }
            case 1: { // ALLOC ALIGNED
                size_t alloc_sz = (fuzz_read_size(data, &i, size) % 8192) + 1;
                size_t align = fuzz_read_align(data, &i, size);
                
                FUZZ_LOG("[%d] BUMP ALLOC ALIGNED: %zu, align %zu -> ", step, alloc_sz, align);
                void *p = em_bump_alloc_aligned(bump, alloc_sz, align);
                FUZZ_LOG("%p\n", p);
                
                if (p) memset(p, 0xBB, alloc_sz);
                break;
            }
            case 2: { // TRIM
                FUZZ_LOG("[%d] BUMP TRIM\n", step);
                em_bump_trim(bump);
                FUZZ_VISUALIZE(em);
                break;
            }
            case 3: { // RESET
                FUZZ_LOG("[%d] BUMP RESET\n", step);
                em_bump_reset(bump);
                break;
            }
        }
    }

    FUZZ_LOG("--- REPLAY FINISHED, DESTROYING BUMP & EM ---\n");
    em_bump_destroy(bump);
    em_destroy(em);
    
    return 0;
}