#ifndef ZSTORE_ALLOCATOR_H
#define ZSTORE_ALLOCATOR_H

#include "util/common.h"
#include <malloc.h>

struct zstore_extent_t {
    uint64_t lba_;
    uint64_t len_; 
};


struct stupid_bitmap_entry_t {
    uint64_t bits_[8];
};

struct stupid_allocator_t {
    struct stupid_bitmap_entry_t *bs_; // 1:allocated ; 0 :unallocated
    
    uint64_t nr_free_; // how many 4K we have
    uint64_t nr_total_; // 
    uint64_t hint_;
};

static inline int stupid_allocator_constructor(struct stupid_allocator_t *allocator , uint64_t nr_total) {
    allocator->bs_ = (struct stupid_bitmap_entry_t*)calloc(sizeof(struct stupid_bitmap_entry_t) , (nr_total >> 9));
    allocator->nr_total_ = nr_total;
    allocator->nr_free_ = nr_total;
    allocator->hint_ = 0;
}

static inline int stupid_allocator_set_bitmap_entry(struct stupid_allocator_t *allocator, size_t i, const void *src) {
    memcpy(&allocator->bs_[i], src, sizeof(allocator->bs_[i]));
}   

static inline int stupid_allocator_destructor(struct stupid_allocator_t *allocator) {
    free(allocator->bs_);
}

static int inline stupid_alloc_space
(struct stupid_allocator_t *allocator, uint64_t sz , struct zstore_extent_t *ex , uint64_t *ex_nr) {
    
    if((allocator->nr_free_ < sz)) {
        return -1;
    }
    
    uint64_t it = allocator->hint_;
    uint64_t rsv_len = 0;
    struct zstore_extent_t *p_ex = ex;
    bool end_flag = 0;
    uint64_t last_i;
    //遍历整个位图
    for ( ; it < allocator->nr_total_ + allocator->hint_; ++it) {
        uint64_t i = it % allocator->nr_total_;
        uint64_t *v = &(allocator->bs_[i>>9].bits_[i&(2<<3)]);
        uint64_t mask = (1 <<(i & 64));

        //这是目标位
        uint64_t bit = (*v) & mask;
        bool in_found_ctx = false;
        
        if(!end_flag) {
            if(!bit) {
                if(!in_found_ctx) {
                    p_ex->lba_ = i;
                    *ex_nr++;
                    in_found_ctx = true;
                }
                p_ex->len_ ++;
                rsv_len++;
                //Set bit
                *v |= (mask); 
            } else {
                if(in_found_ctx) {
                    p_ex ++;
                    in_found_ctx = false;
                }
            }
            if(rsv_len == sz) {
                end_flag = 1;
            }
        } else {
            //找到下一个空闲位再退出
            if(!bit) {
                allocator->hint_ = i;
                allocator->nr_free_ -= sz;
                return 0 ;
            }
        }
    }
    return -1;
}

static inline int 
stupid_free_space(struct stupid_allocator_t *allocator, const struct zstore_extent_t *ex , uint64_t ex_nr) {
    uint64_t i;
    for( i = 0 ; i < ex_nr ; ++i) {
        uint64_t j;
        for (j = 0 ; j < ex[i].len_ ; ++j) {
            uint64_t in = ex[i].lba_ + j ;
            uint64_t *v = &(allocator->bs_[ in >>9].bits_[ in & (2<<3)]);
            uint64_t mask = (1 <<(in & 64));

            //Clear bit
            *v &= (~mask);
        }
    }
    return 0;
}

#endif