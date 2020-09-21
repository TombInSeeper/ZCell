#ifndef BITMAP_H
#define BITMAP_H

#include "malloc.h"
#include "string.h"
#include "stdbool.h"

#define _CEIL_DIV(x,y) (((x)+(y)-1)/(y))

typedef struct bitmap_t {
    unsigned int  bit_length;
    unsigned int  words_length;
    unsigned long long words[0];
} bitmap_t;

#define BITMAP_SHIFT 6  // 2^6 = 64
#define BITMAP_MASK (0x3f) //5'b111111
#define BITMAP_WORD_BITS (64)

static inline bitmap_t * bitmap_constructor( unsigned int bit_length , int init_value) {
    unsigned int words_length = _CEIL_DIV(bit_length , BITMAP_WORD_BITS);
    unsigned int wsz = words_length * sizeof(unsigned long long);
    bitmap_t *b = (bitmap_t *)malloc(sizeof(bitmap_t) + wsz);
    b->bit_length = bit_length;
    b->words_length = words_length;
    if(init_value)
        memset(b->words , 0xff, wsz);
    else 
        memset(b->words , 0x00, wsz);
    return b;
}   
static inline void bitmap_destructor(bitmap_t *b) {
    free(b);
}

static inline void bitmap_reset(bitmap_t *b , int value) {
    if(value)
        memset(b->words , 0xff, b->words_length);
    else 
        memset(b->words , 0x00, b->words_length); 
}

static inline void bitmap_set_bit(bitmap_t *b , unsigned int i) {
    b->words[i >> BITMAP_SHIFT] |= (1<< (i & BITMAP_MASK));  
}



static inline void bitmap_clr_bit(bitmap_t *b , unsigned int i){
     b->words[i>>BITMAP_SHIFT] &= ~(1<<(i & BITMAP_MASK));
}
static inline bool bitmap_get_bit(bitmap_t *b , unsigned int i){
    return (b->words[i>>BITMAP_SHIFT] & (1<<(i & BITMAP_MASK)));
}


// static inline int ffsll(unsigned long long u) {
//     return __builtin_ffsll(u);
// }


static inline int bitmap_next_set(bitmap_t *b , unsigned int bit_hint) {
    uint64_t word_idx = bit_hint >> BITMAP_SHIFT;  
    uint64_t _word_hint_idx = word_idx;
    for ( ; word_idx < b->words_length ; ++word_idx) {
        if(b->words[word_idx]){
            return word_idx << BITMAP_SHIFT + __builtin_ffsll(b->words[word_idx]) - 1;
        }
    }
    word_idx = 0;
    for ( ; word_idx < _word_hint_idx ; ++word_idx) {
        if(b->words[word_idx]){
            return word_idx << BITMAP_SHIFT + __builtin_ffsll(b->words[word_idx]) - 1;
        }
    } 
    return -1;   
}


static inline int bitmap_find_next_set_and_clr(bitmap_t *b , unsigned int bit_hint) {
    int i = bitmap_next_set(b , bit_hint);
    if( i < 0) {
        return -1;
    }
    bitmap_clr_bit(b, i);
    return i;
}

#endif