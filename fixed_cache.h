#ifndef FIXED_CACHE_H
#define FIXED_CACHE_H

#include "stdint.h"
#include "stdio.h"
#include "spdk/env.h"
#include "spdk/util.h"

enum allocator_type {
    MALLOC,
    SPDK_MALLOC,
};

typedef struct fcache_t {
    uint8_t mem_allocator;
    void  *elems;
    void  *elems_tail;
    unsigned int *ptr_arr;
    unsigned int elem_size;
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

        f->ptr_arr = (unsigned int *)malloc(sizeof(unsigned int)*cache_sz);
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
    f->elem_size = elem_sz;
    f->elems_tail = (char*)(f->elems) + (f->elem_size * f->size);
    f->mem_allocator = mem_allocator;
    int i;
    for (i = 0 ; i < f->size; ++i) {
        f->ptr_arr[i] = i;
    }

    return f;

}

static inline void fcache_destructor(fcache_t * fc)
{
    printf("fcache_destructor elems\n");
    if(fc->mem_allocator == SPDK_MALLOC) {
        spdk_free(fc->elems);
    } else if (fc->mem_allocator == MALLOC) {
        free(fc->elems);
    } else {
        return;
    }
    printf("fcache_destructor ptr_arr\n");
    free(fc->ptr_arr);
    printf("fcache_destructor fc\n");

    free(fc);
}

static inline uint32_t fcache_id_get(fcache_t *fc) {
    unsigned int _tail = fc->tail;
    if(_tail == fc->size) {
        return -1;
    }
    uint32_t pr_id  = fc->ptr_arr[fc->tail++];
    return pr_id;
}

static inline void fcache_id_put(fcache_t *fc , uint32_t idx) {
    uint32_t _tail = fc->tail;
    if(_tail == 0) {
        return;
    }
    if ( 0 <= idx && idx < fc->size) {
        fc->ptr_arr[--fc->tail] = idx;
    }
}

static inline void* fcache_get(fcache_t *fc)
{
    unsigned int _tail = fc->tail;
    if(_tail == fc->size) {
        return NULL;
    }
    uint32_t pr_id  = fc->ptr_arr[fc->tail++];
    return (char*)fc->elems + (pr_id * fc->elem_size);
}

static inline void fcache_put(fcache_t *fc , void* elem)
{
    unsigned int _tail = fc->tail;
    if(_tail == 0) {
        return;
    }
    uint32_t idx = ( (char*)(elem) - (char*)(fc->elems) ) / (fc->elem_size);
    if ( 0 <= idx && idx < fc->size) {
        fc->ptr_arr[--fc->tail] = idx;
    }
}

static inline uint32_t fcache_elem_id(fcache_t *fc , void *elem)
{
    uint32_t idx = ( (char*)(elem) - (char*)(fc->elems) ) / (fc->elem_size);
    if ( 0 <= idx && idx < fc->size) {
        return idx;
    }
    else {
        return -1;
    }
}

static inline void* fcahe_id_elem(fcache_t *fc , uint32_t id) {
    return (char*)fc->elems + (id * fc->elem_size);
}

static inline bool fcache_in(fcache_t *fc , void *ptr) {

    return (uintptr_t)ptr >= (uintptr_t)fc->elems && 
            (uintptr_t)ptr < (uintptr_t)fc->elems + (fc->elem_size * fc->size); 
}

#endif