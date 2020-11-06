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
    uint64_t nr_entrys_;
    uint64_t hint_;
};

static inline int stupid_allocator_constructor(struct stupid_allocator_t *allocator , uint64_t nr_total) {
    allocator->bs_ = (struct stupid_bitmap_entry_t*)calloc((nr_total >> 9) , sizeof(struct stupid_bitmap_entry_t));
    allocator->nr_total_ = nr_total;
    allocator->nr_free_ = 0;
    allocator->nr_entrys_ = (FLOOR_ALIGN(nr_total , 512)) >> 9;
    allocator->hint_ = 0;
    return 0;
}

static inline int stupid_allocator_init_bitmap_entry(struct stupid_allocator_t *allocator, size_t entry_index, const void *src) {
    memcpy(&allocator->bs_[entry_index], src, sizeof(allocator->bs_[entry_index]));
    size_t i = entry_index << 9;
    size_t total_free = 0;
    size_t end = ((entry_index + 1) << 9) > allocator->nr_total_ ?
        allocator->nr_total_ : ((entry_index + 1) << 9);

    for ( ; i < end ; ) {
        uint64_t *v = &(allocator->bs_[i>>9].bits_[(i & 511) >> 6]);
        uint64_t mask = (1ULL <<(i & 63));
        if (!(*v)) {
            total_free += 64;
            i += 64;
        } else {
            //这是目标位
            uint64_t bit = (*v) & mask;         
            if(!bit) {
                total_free++;
                i++;
            }
        }
    }
    allocator->nr_free_ += total_free;
    return 0;
}   

static inline int stupid_allocator_destructor(struct stupid_allocator_t *allocator) {
    free(allocator->bs_);
    return 0;
}

static inline void dump_bitmap(const char* prefix, uint64_t v) {
    uint64_t i;
    printf("%s[0:63]{" , prefix);
    for ( i = 0 ; i < 64 ; ++i) {
        if ( v & ( 1ULL << i)) {
            printf("1");
        } else {
            printf("0");
        }
    }
    printf("}\n");
}

static inline void dump_extent(struct zstore_extent_t *ze , uint64_t nr) {
    size_t i;
    for ( i = 0 ; i < nr ; ++i) {
        printf("{%lu,%lu}" , ze[i].lba_, ze[i].len_);
    }
    printf("\n");
}

static inline int stupid_alloc_space
(struct stupid_allocator_t *allocator, uint64_t sz , struct zstore_extent_t *ex , uint64_t *ex_nr) {
    
    if((allocator->nr_free_ < sz)) {
        return -1;
    }    
    uint64_t it = allocator->hint_;
    uint64_t rsv_len = 0;
    struct zstore_extent_t *p_ex = ex;
    bool end_flag = 0;
    bool in_found_ctx = false;

    // uint64_t last_i;
    //遍历整个位图
    *ex_nr = 0;
    for ( ; it < allocator->nr_total_ + allocator->hint_; ++it) {
        uint64_t i = it % allocator->nr_total_;
        // uint64_t mod_mask = (1ULL<<9) - 1;
        // printf("Tgt v: bs[%lu], bits_[%lu] , bit[%lu]\n", i >> 9, (i & 511) >> 6, i & 63);
        uint64_t *v = &(allocator->bs_[i>>9].bits_[(i & 511) >> 6]);
        uint64_t mask = (1ULL <<(i & 63));
        //这是目标位
        uint64_t bit = (*v) & mask;        
        if(!end_flag) {
            if(!bit) {
                if(!in_found_ctx) {
                    // printf("New extent\n");
                    p_ex->lba_ = i;
                    p_ex->len_ = 0;
                    *ex_nr = *ex_nr + 1;
                    in_found_ctx = true;
                }
                p_ex->len_++;
                rsv_len++;
                //Set bit
                *v |= (mask); 
                // dump_bitmap("v",*v);
                // dump_extent(p_ex , 1);
            } else {
                if(in_found_ctx) {
                    // printf("??\n");
                    p_ex++;
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
    uint64_t total_len = 0;
    for( i = 0 ; i < ex_nr ; ++i) {
        uint64_t j;
        for (j = 0 ; j < ex[i].len_ ; ++j) {
            uint64_t in = ex[i].lba_ + j ;
            uint64_t *v = &(allocator->bs_[in >> 9].bits_[ (in & 511) >> 6 ]);
            uint64_t mask = (1ULL <<(in & 63));
            total_len ++;
            //Clear bit
            *v &= (~mask);
        }
    }
    allocator->nr_free_+= total_len;
    return 0;
}

#endif