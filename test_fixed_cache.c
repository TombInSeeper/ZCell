#include "fixed_cache.h"

int main()
{
    fcache_t *fc = fcache_constructor(1000,128,MALLOC);
    
    int i ;
    void* elems[1000];
    for ( i = 0 ; i < fc->size ; ++i) {
        elems[i] = fcache_get(fc);
        assert(elems[i]);
        printf("eid=%u\n", fcache_elem_id(fc , elems[i]));
    }

    void *err = fcache_get(fc);
    assert(err == NULL);

    
    
    fcache_destructor(fc);

    return 0;
}