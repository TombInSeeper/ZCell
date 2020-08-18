#ifndef FIXED_CACHE_H
#define FIXED_CACHE_H

#include "stdint.h"
#include "spdk/env.h"
#include "spdk/util.h"

enum allocator_type {
    MALLOC,
    SPDK_MALLOC,
};

typedef struct fcache_t {
    uint8_t mem_allocator;
    void  *elems;
    unsigned int *ptr_arr;
    unsigned int size;
    unsigned int tail;
} __attribute__((aligned(SPDK_CACHE_LINE_SIZE))) fcache_t;

static inline fcache_t *fcache_constructor(uint32_t cache_sz, uint32_t elem_sz , uint8_t mem_allocator)
{

    fcache_t *f;
    if(mem_allocator == MALLOC) {
        f = (fcache_t *)calloc( 1, sizeof(fcache_t)); 
        assert(f);

        f->elems = malloc(elem_sz *cache_sz); 
        assert(f->elems);

        f->ptr_arr = (unsigned int *)malloc(sizeof(void*)*cache_sz);
        assert(f->ptr_arr);
    }
    else if(mem_allocator == SPDK_MALLOC) {
        f = (fcache_t *)calloc(1 , sizeof(fcache_t));
        assert(f);

        uint32_t align = spdk_u32_is_pow2(elem_sz) ? elem_sz : 0;
        f->elems = spdk_dma_malloc(elem_sz *cache_sz, align , NULL); 
        assert(f->elems);

        f->ptr_arr = (unsigned int *)malloc(sizeof(unsigned int) * cache_sz);
        assert(f->ptr_arr);
    
    } else {
        return NULL;
    }

    f->size = cache_sz;
    int i;
    for (i = 0 ; i < f->size; ++i) {
        f->ptr_arr[i] = i;
    }

}


#endif