#include "fuzz_utils.h"
#include <string.h>

#define MAX_PTRS 512

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0;

    EM *em = em_create(1024 * 1024);
    if (!em) return 0;

    void *ptrs[MAX_PTRS] = {0};
    size_t ptr_count = 0;

    size_t i = 0;
    int step = 0;
    
    FUZZ_LOG("\n--- STARTING REPLAY ---\n");
    FUZZ_VISUALIZE(em);

    while (i < size) {
        uint8_t op = data[i++] % 5;
        step++;

        switch (op) {
            case 0: { // ALLOC
                if (ptr_count >= MAX_PTRS) break;
                size_t alloc_size = fuzz_read_size(data, &i, size);
                
                FUZZ_LOG("[%d] ALLOC: size = %zu -> ", step, alloc_size);
                void *p = em_alloc(em, alloc_size);
                FUZZ_LOG("%p\n", p);
                
                if (p) { memset(p, 0xAA, alloc_size); ptrs[ptr_count++] = p; }
                FUZZ_VISUALIZE(em);
                break;
            }
            case 1:
            case 4: { // FREE (50% of the time)
                if (ptr_count == 0 || i >= size) break;
                size_t idx = data[i++] % ptr_count;
                
                if (ptrs[idx]) {
                    FUZZ_LOG("[%d] FREE: ptrs[%zu] (%p)\n", step, idx, ptrs[idx]);
                    em_free(ptrs[idx]);
                    ptrs[idx] = ptrs[--ptr_count]; 
                }
                FUZZ_VISUALIZE(em);
                break;
            }
            case 2: { // ALLOC_ALIGNED
                if (ptr_count >= MAX_PTRS) break;
                size_t alloc_size = fuzz_read_size(data, &i, size);
                size_t alignment = fuzz_read_align(data, &i, size);
                
                FUZZ_LOG("[%d] ALLOC_ALIGNED: size = %zu, align = %zu -> ", step, alloc_size, alignment);
                void *p = em_alloc_aligned(em, alloc_size, alignment);
                FUZZ_LOG("%p\n", p);
                
                if (p) { memset(p, 0xBB, alloc_size); ptrs[ptr_count++] = p; }
                FUZZ_VISUALIZE(em);
                break;
            }
            case 3: { // SCRATCH ALLOC
                if (ptr_count >= MAX_PTRS) break;
                size_t alloc_size = fuzz_read_size(data, &i, size);
                
                if (!em_get_has_scratch(em)) {
                    FUZZ_LOG("[%d] SCRATCH: size = %zu -> ", step, alloc_size);
                    void *p = em_alloc_scratch(em, alloc_size);
                    FUZZ_LOG("%p\n", p);
                    
                    if (p) { memset(p, 0xCC, alloc_size); ptrs[ptr_count++] = p; }
                }
                FUZZ_VISUALIZE(em);
                break;
            }
        }
    }

    FUZZ_LOG("--- REPLAY FINISHED, DESTROYING EM ---\n");
    em_destroy(em);
    (void)step;
    
    return 0;
}