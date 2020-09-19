#include "assert.h"

#include "util/bitmap.h"
#include "util/log.h"
#include "util/chrono.h"

void ASSERT_EQ(unsigned int exp1, unsigned int exp2) {
    log_info("expect value = %u, true value = %u \n", exp1, exp2);
    assert(exp1 == exp2);
}


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

    bitmap_set_bit(b , 1);
    assert(bitmap_get_bit(b , 1) == 1 );

    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 0);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 1);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 2);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 3);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 4);

    bitmap_destructor(b);

    log_info("Pass!\n");
}


int main() {
    func_test();
    return 0;
}