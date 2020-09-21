#include "assert.h"

#include "util/bitmap.h"
#include "util/log.h"
#include "util/chrono.h"
#include "util/uint_test.h"


#define BITMAP_SIZE 65536

void seq_alloc_perf_test(bitmap_t *b) {
    int i;
    int hint = 0;
    uint64_t st = now();
    for ( i = 0 ; i < b->bit_length ; ++i) {
        hint = bitmap_find_next_set_and_clr(b, hint);
    }
    uint64_t end = now();
    log_info("avg_lat =  %lf ns \n",  ( (end - st ) * 1000.0 ) / b->bit_length);
}

void rev_alloc_perf_test(bitmap_t *b) {
    int i;
    int hint = 0;
    for ( i = 0 ; i < b->bit_length - 256 ; ++i) {
        bitmap_clr_bit(b , i);
    }
    uint64_t st = now();
    for ( i = 0 ; i < 256 ; ++i) {
        bitmap_find_next_set_and_clr(b, 0);
    }
    uint64_t end = now();
    log_info("avg_lat =  %lf ns \n",  ( (end - st ) * 1000.0 ) / 256.0);
}


void perf_test() {
    bitmap_t *b = bitmap_constructor(512,1); 
    
    seq_alloc_perf_test(b);
    bitmap_reset(b,1);

    rev_alloc_perf_test(b);
    bitmap_reset(b,1);

    bitmap_destructor(b);
}


void func_test() {
    bitmap_t *b = bitmap_constructor(BITMAP_SIZE,1); 
    ASSERT_EQ(b->bit_length,BITMAP_SIZE);
    ASSERT_EQ(b->words_length,BITMAP_SIZE / 64);

    do { 
        typeof(bitmap_get_bit(b,0)) _v1 = (bitmap_get_bit(b,0));
        typeof(1) _v2 = (1); 
        assert(_v1 == _v2); 
    } while(0);

    ASSERT_EQ(bitmap_get_bit(b,0),1);
    ASSERT_EQ(bitmap_get_bit(b,1),1);
    ASSERT_EQ(bitmap_get_bit(b,32),1);
    ASSERT_EQ(bitmap_get_bit(b,33),1);
    ASSERT_EQ(bitmap_get_bit(b,63),1);
    ASSERT_EQ(bitmap_get_bit(b,64),1);
    ASSERT_EQ(bitmap_get_bit(b,127),1);
    ASSERT_EQ(bitmap_get_bit(b,128),1);
    ASSERT_EQ(bitmap_get_bit(b,255),1);

    bitmap_set_bit(b,1);
    ASSERT_EQ(bitmap_get_bit(b,1),1);

    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 0);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 1);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 2);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 3);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 4);

    bitmap_set_bit(b,2);
    ASSERT_EQ(bitmap_find_next_set_and_clr(b,0), 2);

    bitmap_destructor(b);
    log_info("Pass!\n");
}


int main() {
    func_test();
    perf_test();
    return 0;
}