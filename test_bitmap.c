#include "assert.h"

#include "util/bitmap.h"
#include "util/log.h"
#include "util/chrono.h"



void seq_alloc_perf_test(bitmap_t *b) {
    int i;
    int hint = 0;
    for ( i = 0 ; i < b->bit_length ; ++i) {
        hint = bitmap_find_next_set_and_clr(b, hint);
    }
}

void func_test( ) {
    bitmap_t *b = bitmap_constructor(256,1); 
    assert(bitmap_get_bit(b , 1) == 1 );

    bitmap_clr_bit(b , 1);
    assert(bitmap_get_bit(b , 1) == 0 );

    bitmap_clr_bit(b , 32);
    assert(bitmap_get_bit(b , 32) == 0 );

    assert(bitmap_get_bit(b , 9) == 1 );

    assert(bitmap_find_next_set_and_clr(b,0) == 2);

    bitmap_destructor(b);
}


int main() {
    func_test();
    return 0;
}