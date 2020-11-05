//For meta data storage
//

//For data storage
#include <spdk/bdev.h>
#include <spdk/util.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/thread.h>


#include "zstore_allocator.h"
#include "zstore.h"
#include "pm.h"

#include "util/log.h"

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



union zstore_superblock_t {
    struct {
        uint64_t magic;
        
        uint32_t ssd_nr_pages;
        uint32_t pm_nr_pages;
        //64B aligned paddr ofst
        uint64_t pm_ulog_ofst; //4K~(4K*256)
        uint64_t pm_ssd_bitmap_ofst; // 1 << 20
        //Size: 512_aligned(pm_nr_pages) >> 3
        // 
        uint64_t pm_dy_bitmap_ofst; // 1 << 20 
        //Size: 512_aligned(pm_nr_pages) >> 3

        uint64_t onodes_rsv;        // 1M
        uint64_t pm_otable_ofst;    //
        //Size: 64B * 1024 * 1024
        
        uint64_t pm_dy_space_ofst;
    };
    uint8_t align[PAGE_ALIGN];
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






struct zstore_context_t {
    
    //Data Space
    struct spdk_bdev *nvme_bdev_;
    struct spdk_bdev_desc *nvme_bdev_desc_;
    struct spdk_io_channel *nvme_io_channel_;

    //MetaData Space
    struct pmem_t *pmem_;

    //Transaction 
    uint32_t tx_outstanding_;
    tailq_head(zstore_tx_list_t , zstore_transacion_t) tx_list_;

};

static __thread struct zstore_context_t *zstore; //TLS 


static void spdk_bdev_event_cb_common(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
				     void *event_ctx) 
{
    log_warn("Bdev event\n");
    return;
}

static int zstore_ctx_init(struct zstore_context_t **zs) {
    *zs = calloc(1, sizeof(**zs));
    if(!(*zs)) {
        return -1;
    }
    struct zstore_context_t *zs_ = *zs;
    tailq_init(&zs_->tx_list_);
    
    //Others
    return 0;
}

static void zstore_ctx_fini(struct zstore_context_t *zs) {
    free(zs);
}


static int zstore_bdev_open(struct zstore_context_t *zs, const char *dev) {
    int rc = spdk_bdev_open_ext(dev, 1, spdk_bdev_event_cb_common ,NULL, &zstore->nvme_bdev_desc_);
    if(rc) {
        log_err("bdev %s open failed\n", dev);
        return rc;
    }
    zs->nvme_io_channel_ = spdk_bdev_get_io_channel(zs->nvme_bdev_desc_);
    if(!zs->nvme_io_channel_) {
        log_err("bdev %s get io-channel failed\n", dev);
        return rc;
    }
    zs->nvme_bdev_ = spdk_bdev_desc_get_bdev(zs->nvme_bdev_desc_);
    if(!zs->nvme_bdev_) {
        log_err("bdev %s get bdev failed\n", dev);
        return rc;
    }
    return 0;
}

static void zstore_bdev_close(struct zstore_context_t *zs) {
    spdk_put_io_channel(zs->nvme_io_channel_);
    spdk_bdev_close(zs->nvme_bdev_desc_);
}

static int 
zstore_pm_file_open(struct zstore_context_t *zs, const char *path, uint64_t *pm_size) {
    zs->pmem_ = pmem_open(path, spdk_env_get_current_core(), pm_size);
    if(!zs->pmem_) {
        return -1;
    }
    return 0;
}

static void
zstore_pm_file_close(struct zstore_context_t *zs) {
    pmem_close(zs->pmem_);
}


extern int zstore_mkfs(const char *dev_list[], int flags) {

    int rc = zstore_ctx_init(&zstore);
    assert(rc == 0);

    union zstore_superblock_t zsb_;
    union zstore_superblock_t *zsb = &zsb_;
    zsb->magic = 0x1997070519980218;
    zsb->ssd_nr_pages = 0;
    zsb->pm_nr_pages = 0;
    zsb->pm_ulog_ofst = 4096;
    //ulog_region for 255 * 4K
    //Load block device 
    rc = zstore_bdev_open(zstore, dev_list[0]);
    assert(rc == 0);

    uint32_t blk_sz = spdk_bdev_get_block_size(zstore->nvme_bdev_);
    assert(blk_sz == 4096);

    uint64_t nblks = spdk_bdev_get_num_blocks(zstore->nvme_bdev_);
    uint64_t nblks_ = FLOOR_ALIGN(nblks, 4096 * 8);
    log_info("SSD Block number = %lu , floor_align 32768 to %lu\n", nblks , nblks_);
    // assert (nblks == nblks_);
    zsb->ssd_nr_pages = nblks;

    uint64_t npm_blks_; 
    rc = zstore_pm_file_open(zstore, dev_list[1], &npm_blks_);
    assert(rc == 0);

    npm_blks_ = npm_blks_ >> 12;
    log_info("PM Block number = %lu , floor_align 32768 to %lu\n", npm_blks_ , FLOOR_ALIGN(npm_blks_, 32768) );
    zsb->pm_nr_pages = npm_blks_;

    npm_blks_ = FLOOR_ALIGN(npm_blks_ , 32768);

    uint64_t onode_rsv = 1ULL << 20;
    zsb->pm_ssd_bitmap_ofst = 1ULL << 20;
    zsb->pm_dy_bitmap_ofst = (1ULL << 20) + (nblks_ >> 3);    
    zsb->pm_otable_ofst = (1ULL << 20) + (nblks_>>3) + (npm_blks_ >> 3) ;    
    zsb->pm_dy_space_ofst = zsb->pm_otable_ofst + (sizeof(union otable_entry_t)) * onode_rsv;
    
    assert(zsb->pm_ssd_bitmap_ofst % 4096 == 0);
    assert(zsb->pm_dy_bitmap_ofst % 4096 == 0);
    assert(zsb->pm_otable_ofst % 4096 == 0);
    assert(zsb->pm_dy_space_ofst % 4096 == 0);

    log_info("superblock_sz : %lu, log_region_sz :%lu,\ 
        ssd_bitmap_sz: %lu, dy_bitmap_sz: %lu, otable_sz : %lu\n" ,
        1UL , 255UL , (nblks_>>3)>>12 , (npm_blks_>>3)>>12, (64*onode_rsv)>>12 );
    
    if(zsb->pm_dy_space_ofst >= (zsb->pm_nr_pages << 12) ) {
        log_err("PM space is not enough, \ 
        pm_dy_sofst = %lu , pm_sz = %lu \n", 
        zsb->pm_dy_space_ofst,
        (uint64_t)zsb->pm_nr_pages << 12);
        return -1;
    }
    
    log_info("dynamic space sz:%lu \n" , (npm_blks_-zsb->pm_dy_space_ofst) >> 12);


    pmem_write(zstore->pmem_, 1, zsb ,0 , 4096);    

    zstore_bdev_close(zstore);
    zstore_pm_file_close(zstore);
    zstore_ctx_fini(zstore);
    return 0;
}


extern int zstore_mount(const char *dev_list[], /* size = 2*/  int flags /**/)
{

    // TAILQ_INIT

}

extern int zstore_unmount();

extern const int zstore_obj_async_op_context_size();

extern int zstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);





