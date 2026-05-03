#include "fuzz_utils.h"
#include <string.h>

#define MAX_CHUNKS 1024

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 12) return 0;

    size_t i = 0;
    int step = 0;

    size_t slab_capacity = (fuzz_read_size(data, &i, size) % 65280) + 256; 
    size_t chunk_size = (fuzz_read_size(data, &i, size) % 1016) + 8;

    EM *em = em_create(1024 * 1024);
    if (!em) return 0;

    Slab *slab = em_slab_create(em, slab_capacity, chunk_size);
    if (!slab) {
        em_destroy(em);
        return 0;
    }

    void *chunks[MAX_CHUNKS] = {0};
    size_t chunk_count = 0;

    FUZZ_LOG("\n--- STARTING SLAB REPLAY ---\n");
    FUZZ_LOG("Slab Capacity: %zu, Chunk Size: %zu\n", slab_capacity, chunk_size);

    while (i < size) {
        uint8_t op = data[i++] % 4;
        step++;
        (void)step;

        switch (op) {
            case 0:
            case 1: { // ALLOC
                if (chunk_count >= MAX_CHUNKS) break;
                
                FUZZ_LOG("[%d] SLAB ALLOC -> ", step);
                void *p = em_slab_alloc(slab);
                FUZZ_LOG("%p\n", p);
                
                if (p) {
                    memset(p, 0xDD, chunk_size); 
                    chunks[chunk_count++] = p;
                }
                break;
            }
            case 2: { // FREE
                if (chunk_count == 0 || i >= size) break;
                size_t idx = data[i++] % chunk_count;
                
                if (chunks[idx]) {
                    FUZZ_LOG("[%d] SLAB FREE: chunks[%zu] (%p)\n", step, idx, chunks[idx]);
                    em_slab_free(slab, chunks[idx]);
                    chunks[idx] = chunks[--chunk_count]; 
                }
                break;
            }
            case 3: { // RESET
                FUZZ_LOG("[%d] SLAB RESET\n", step);
                em_slab_reset(slab);
                chunk_count = 0;
                break;
            }
        }
    }

    FUZZ_LOG("--- REPLAY FINISHED, DESTROYING EM ---\n");
    em_slab_destroy(slab);
    em_destroy(em);
    
    return 0;
}