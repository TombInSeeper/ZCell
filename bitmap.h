#ifndef BITMAP_H
#define BITMAP_H

#include "malloc.h"

#define _CEIL_DIV(x,y) (((x)+(y)-1)/(y))


typedef struct bitmap_t {
    unsigned int bit_length;
    unsigned int words[0];
} bitmap_t;

#define BITMAP_SHIFT (5) // 2^5 = 32
#define BITMAP_MASK (0x1f) //5'b11111
#define BITMAP_WORD_BITS (32)

static inline bitmap_t * bitmap_constructor( unsigned int bit_length)
{
    unsigned int wsz = _CEIL_DIV(bit_length , BITMAP_WORD_BITS) * sizeof(unsigned int);
    bitmap_t *b = (bitmap_t *)malloc(sizeof(bitmap_t) + wsz);
    return b;
}   

static inline void bitmap_destructor(bitmap_t *b)
{
    free(b);
}
static inline void bitmap_set_bit(bitmap_t *b , unsigned int i)
{
    b->words[i >> BITMAP_SHIFT] |= (1<< (i & BITMAP_MASK));  
}

static inline void bitmap_test_bit(bitmap_t *b , unsigned int i)
{
    b->words[i>>BITMAP_SHIFT] &= ~(1<<(i & BITMAP_MASK));
}

static inline int bitmap_clr_bit(bitmap_t *b , unsigned int i)
{
    return b->words[i>>BITMAP_SHIFT] & (1<<(i & BITMAP_MASK));
}



#endif