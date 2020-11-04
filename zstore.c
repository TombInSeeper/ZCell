#include "util/common.h"

#include "util/bitmap.h"

//For meta data storage
//
#include "pm.h"

//For data storage
#include "spdk/bdev.h"
#include "spdk/util.h"

#include "zstore.h"

#define PAGE_ALIGN 4096
#define PAGE_ALIGN_SHIFT 12


#define container_of(ptr,type,member) SPDK_CONTAINEROF(ptr,type,member)

//Queue declare
#define tailq_head(name,type) TAILQ_HEAD(name,type)
#define tailq_entry(type) TAILQ_ENTRY(type)
//
#define tailq_init(head) TAILQ_INIT(head)
//head：queue head, elem: ptr to elem, filed：hook of list
#define tailq_insert_tail(head,elem,field) TAILQ_INSERT_TAIL(head,elem,field)

#define tailq_remove(head,elem,field) TAILQ_REMOVE(head,elem,field)

#define tailq_first(head) TAILQ_FIRST(head)


struct zstore_extent_t {
    uint32_t lba_;
    uint32_t len_; 
};

union zstore_superblock_t {
    struct {
        uint32_t magic;
        uint32_t ssd_nr_pages;
        uint32_t pm_nr_pages;
        uint32_t pm_ulog_ofst; //4K~(4K*256)
        uint32_t pm_dy_bitmap_ofst;
        uint32_t pm_ssd_bitmap_ofst;
        uint32_t pm_otable_ofst;
        uint32_t pm_dy_space_ofst;
    };
    uint8_t align[PAGE_ALIGN];
};

struct stupid_bitmap_entry_t {
    uint64_t bits_[8];
};



union otable_entry_t {
    struct {
        uint64_t oid;
        uint64_t mtime;
        uint64_t lsize;
        uint64_t psize;
        uint32_t attr_idx_addr;
        uint32_t data_idx_addr;
    };
    uint8_t align[64];
};


struct zstore_data_bio {
    // struct spdk_bdev_io *bio_;
    struct iovec iov[4];
    tailq_entry(zstore_data_bio) bio_lhook_;
};

enum zstore_tx_state {
    DATA_IO = 1,
    PM_TX,
};

struct zstore_transacion_t {
    int state_;
    uint32_t bio_outstanding_;
    tailq_head(bio_list_t, zstore_data_bio) bio_list_;
    union pmem_transaction_t *pm_tx_;
    tailq_entry(zstore_transacion_t) zstore_tx_lhook_;
};





struct stupid_allocator_t {
    struct stupid_bitmap_entry_t *bs_; // 1:allocated ; 0 :unallocated
    
    uint64_t nr_free_; // how many 4K we have
    uint64_t nr_total_; // 
    uint64_t hint_;
};

static int stupid_allocator_constructor(struct stupid_allocator_t *allocator , uint64_t nr_total) {
    allocator->bs_ = malloc(sizeof(struct stupid_bitmap_entry_t) * nr_total >> 9);
    allocator->nr_total_ = nr_total;
    allocator->nr_free_ = nr_total;
    allocator->hint_ = 0;
}

static int stupid_allocator_set_bitmap_entry(struct stupid_allocator_t *allocator, size_t i, const void *src) {
    memcpy(&allocator->bs_[i], src, sizeof(allocator->bs_[i]));
}   

static int stupid_allocator_destructor(struct stupid_allocator_t *allocator) {
    free(allocator->bs_);
}

static int 
stupid_alloc_space
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

static int 
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

struct zstore_context_t {
    struct spdk_bdev *nvme_bdev_;
    struct spdk_bdev_desc *nvme_bdev_desc_;
    struct spdk_io_channel *nvme_io_channel_;

    struct pmem_t *pmem_;

    //Transaction 
    uint32_t tx_outstanding_;
    tailq_head(zstore_tx_list_t , zstore_transacion_t) tx_list_;
};

static __thread struct zstore_context_t *zstore; 

extern int zstore_mkfs(const char *dev_list[], int flags);

extern int zstore_mount(const char *dev_list[], /* size = 2*/  int flags /**/)
{

    // TAILQ_INIT

}

extern int zstore_unmount();

extern const int zstore_obj_async_op_context_size();

extern int zstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);





