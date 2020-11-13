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

#include "store_common.h"
#define PAGE_ALIGN 4096
#define PAGE_ALIGN_SHIFT 12

#define ZSTORE_MAGIC 0x1997070519980218

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
        uint64_t valid :1;
        uint64_t rsv : 15;
        uint64_t oid : 48;
        uint64_t mtime;
        uint64_t lsize;
        uint64_t psize;
        uint32_t rsv;
        uint32_t data_idx_id;
    };
    uint8_t align[64];
};


enum IO_TYPE {
    IO_READ = 1,
    IO_WRITE = 2,
};

// struct zstore_data_bio {
//     void *ztore_tx;
//     int io_type;
//     struct iovec iov;
//     tailq_entry(zstore_data_bio) bio_lhook_;
// };
enum zstore_tx_state {
    PREPARE ,
    DATA_IO ,
    PM_TX,
};
enum zstore_tx_type {
    TX_RDONLY = 1,
    TX_WRITE = 2
};




struct zstore_transacion_t {
    void *zstore_;
    int tx_type_;
    uint16_t state_;
    uint16_t err_;

    cb_func_t user_cb_;
    void *user_cb_arg_;

    uint32_t bio_outstanding_;
    struct {
        uint32_t blk_ofst;
        uint32_t blk_len;
    } *bios_;
    // tailq_head(bio_list_t, zstore_data_bio) bio_list_;
    
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
    union zstore_superblock_t *zsb_;

    //Allocator
    struct stupid_allocator_t *ssd_allocator_;
    struct stupid_allocator_t *pm_allocator_;

    //onode table
    union otable_entry_t *otable_;

    //Transaction 
    uint32_t tx_outstanding_;
    tailq_head(zstore_tx_list_t , zstore_transacion_t) tx_list_;

    uint32_t tx_rdonly_outstanding_;
    tailq_head(zstore_tx_rdonly_list_t , zstore_transacion_t) tx_rdonly_list_;

    struct spdk_poller *tx_poller;
};

static __thread struct zstore_context_t *zstore; //TLS 


static int zstore_tx_poll(void *zs_);


static int 
zstore_ctx_init(struct zstore_context_t **zs) 
{
    *zs = calloc(1, sizeof(**zs));
    if(!(*zs)) {
        return -1;
    }
    struct zstore_context_t *zs_ = *zs;
    tailq_init(&zs_->tx_list_);
    zs_->zsb_ = calloc( 1, sizeof(union zstore_superblock_t));
    
    zs_->ssd_allocator_ = calloc(1, sizeof(*zs_->ssd_allocator_));
    zs_->pm_allocator_ = calloc(1, sizeof(*zs_->pm_allocator_));

    zs_->otable_ = calloc( 1UL << 20U, sizeof(union otable_entry_t));

    //Others
    return 0;
}

static void 
zstore_ctx_fini(struct zstore_context_t *zs) 
{
    free(zs->otable_);
    free(zs->pm_allocator_);
    free(zs->ssd_allocator_);
    free(zs->zsb_);
    free(zs);
}


static int 
zstore_bdev_open(struct zstore_context_t *zs, const char *dev) 
{
    zs->nvme_bdev_ = spdk_bdev_get_by_name(dev);
    // zs->nvme_bdev_ = spdk_bdev_desc_get_bdev(zs->nvme_bdev_desc_);
    if(!zs->nvme_bdev_) {
        log_err("bdev %s get bdev failed\n", dev);
        return -1;
    }
    
    int rc = spdk_bdev_open(zs->nvme_bdev_, 1 , NULL, NULL, &zs->nvme_bdev_desc_);
    if(rc) {
        log_err("bdev %s open failed\n", dev);
        return rc;
    }

    zs->nvme_io_channel_ = spdk_bdev_get_io_channel(zs->nvme_bdev_desc_);
    if(!zs->nvme_io_channel_) {
        log_err("bdev %s get io-channel failed\n", dev);
        return -1;
    }
    return 0;
}

static void 
zstore_bdev_close(struct zstore_context_t *zs) {
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

extern int 
zstore_mkfs(const char *dev_list[], int flags) {

    int rc = zstore_ctx_init(&zstore);
    assert(rc == 0);

    union zstore_superblock_t zsb_;
    union zstore_superblock_t *zsb = &zsb_;
    zsb->magic = ZSTORE_MAGIC;
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
    // uint64_t nblks_ = FLOOR_ALIGN(nblks, 4096 * 8);
    // log_info("SSD Block number = %lu , floor_align 32768 to %lu\n", nblks , nblks_);
    // assert (nblks == nblks_);
    zsb->ssd_nr_pages = nblks;

    uint64_t npm_blks_; 
    rc = zstore_pm_file_open(zstore, dev_list[1], &npm_blks_);
    assert(rc == 0);

    npm_blks_ = npm_blks_ >> 12;
    zsb->pm_nr_pages = npm_blks_;

    // log_info("PM Block number = %lu , floor_align 32768 to %lu\n", npm_blks_ , FLOOR_ALIGN(npm_blks_, 32768) );

    // npm_blks_ = FLOOR_ALIGN(npm_blks_ , 32768);

    uint64_t onode_rsv = 1ULL << 20;
    zsb->onodes_rsv = onode_rsv;
    uint64_t ssd_bitmap_sz = 32UL << 20;  
    uint64_t pm_bitmap_sz = 1UL << 20;

    zsb->pm_ssd_bitmap_ofst = 1ULL << 20;
    zsb->pm_dy_bitmap_ofst = (1ULL << 20) + ssd_bitmap_sz;    
    zsb->pm_otable_ofst = (1ULL << 20) + ssd_bitmap_sz + pm_bitmap_sz ;    
    zsb->pm_dy_space_ofst = zsb->pm_otable_ofst + (sizeof(union otable_entry_t)) * onode_rsv;
    
    assert(zsb->pm_ssd_bitmap_ofst % 4096 == 0);
    assert(zsb->pm_dy_bitmap_ofst % 4096 == 0);
    assert(zsb->pm_otable_ofst % 4096 == 0);
    assert(zsb->pm_dy_space_ofst % 4096 == 0);


    // log_info("ZStore:\n");
    log_info("ZStore size in 4KB : superblock_sz : %lu, log_region_sz :%lu, ssd_bitmap_sz: %lu, dy_bitmap_sz: %lu, otable_sz : %lu\n" ,
        1UL , 255UL , ssd_bitmap_sz >> 12 , pm_bitmap_sz >> 12, (64*onode_rsv)>>12 );
        //128M * 8 * 
    log_info("ZStore manage ssd GB max :%lu GB, real: %lu GB\n" , (ssd_bitmap_sz << 3 << 12 >> 30) , nblks << 12 >> 30 );
    log_info("ZStore manage pm GB max :%lu GB ,  real: %lu GB\n" , (pm_bitmap_sz << 3 << 12 >> 30) , npm_blks_ << 12 >> 30 );
    log_info("dynamic space sz:%lu \n" , (npm_blks_-(zsb->pm_dy_space_ofst>>12)));

    char zeros[4096] = {0};
    pmem_write(zstore->pmem_, 1, zsb ,0 , 4096);    
    log_info ("Superblock write done\n");
    int i;
    for (i = 0 ; i < 255 ; ++i) {
        pmem_write(zstore->pmem_, 1, zeros , (i+ 1)*4096 , 4096);    
    }
    log_info ("Log region clean done\n");

    for ( i = (zsb->pm_ssd_bitmap_ofst >> 12 ) ; i < (zsb->pm_dy_space_ofst >> 12) ; ++i) {
        pmem_write(zstore->pmem_, 1, zeros , i << 12 , 4096);    
    }
    log_info ("Bitmap region and onode table region clean done\n");

    zstore_bdev_close(zstore);
    zstore_pm_file_close(zstore);
    zstore_ctx_fini(zstore);

    return 0;
}

extern int 
zstore_mount(const char *dev_list[], /* size = 2*/  int flags /**/) {
    int rc = zstore_ctx_init(&zstore);
    if(rc) {
        log_err("zstore_ctx_init failed\n");
        return rc;
    }
    uint64_t pm_size = 0;
    zstore_pm_file_open(zstore, dev_list[1] , &pm_size);

    pmem_read(zstore->pmem_ , zstore->zsb_ , 0 , 4096);
    assert(zstore->zsb_->magic == ZSTORE_MAGIC);

    rc = zstore_bdev_open(zstore, dev_list[0]);
    assert(rc == 0);

    //If necessary
    pmem_recovery(zstore->pmem_);
    
    stupid_allocator_constructor(zstore->ssd_allocator_, zstore->zsb_->ssd_nr_pages );
    log_info("SSD allocator bitmap entry number:%lu\n",zstore->ssd_allocator_->nr_entrys_);

    //Read from ssd bitmap region
    struct stupid_bitmap_entry_t se;
    size_t i;
    for ( i = 0 ; i < zstore->ssd_allocator_->nr_entrys_ ; ++i) {
        uint64_t ofst = zstore->zsb_->pm_ssd_bitmap_ofst + (sizeof(se) * i);
        pmem_read(zstore->pmem_, &se, ofst, sizeof(se));
        stupid_allocator_init_bitmap_entry(zstore->ssd_allocator_, i ,&se);
    }

    log_info("SSD allocator detect %lu blocks , %lu free blocks\n" 
        ,zstore->ssd_allocator_->nr_total_
        ,zstore->ssd_allocator_->nr_free_);

    stupid_allocator_constructor(zstore->pm_allocator_, zstore->zsb_->pm_nr_pages );
    log_info("PM allocator bitmap entry number:%lu\n",zstore->pm_allocator_->nr_entrys_);
    for ( i = 0 ; i < zstore->pm_allocator_->nr_entrys_ ; ++i) {
        uint64_t ofst = zstore->zsb_->pm_dy_bitmap_ofst + (sizeof(se) * i);
        pmem_read(zstore->pmem_, &se, ofst, sizeof(se));
        stupid_allocator_init_bitmap_entry(zstore->pm_allocator_, i ,&se);
    }

    log_info("PM allocator detect %lu blocks , %lu free blocks\n" 
        ,zstore->pm_allocator_->nr_total_
        ,zstore->pm_allocator_->nr_free_);


    log_info("Onode table recovery..\n");
    uint64_t objs = 0;
    for ( i = 0 ; i < zstore->zsb_->onodes_rsv ; ++i) {
        uint64_t ofst = zstore->zsb_->pm_otable_ofst + i * sizeof(union otable_entry_t);
        pmem_read(zstore->pmem_ , &zstore->otable_[i], ofst , sizeof(union otable_entry_t));
        if(zstore->otable_[i].valid) {
            objs++;
        }
    }
    log_info("Onode table object_id max:%lu, objs find:%lu \n",zstore->zsb_->onodes_rsv, objs);


    // log_info("Transaction poller boot\n");
    // spdk_poller_register()

    return 0;
}

extern int 
zstore_unmount() {
    stupid_allocator_destructor(zstore->pm_allocator_);
    stupid_allocator_destructor(zstore->ssd_allocator_);
    zstore_bdev_close(zstore);
    zstore_pm_file_close(zstore);
    zstore_ctx_fini(zstore);
    return 0;
}



static int 
zstore_tx_metadata(struct zstore_transacion_t *tx) 
{
    struct zstore_context_t *zs = zstore;
    assert(tx->bio_outstanding_ == 0);
    pmem_transaction_apply(zs->pmem_ , tx->pm_tx_);
    pmem_transaction_free(zs->pmem_, tx->pm_tx_);
    return 0;
}

static int 
zstore_tx_end(struct zstore_transacion_t *tx) 
{
    struct zstore_context_t *zs = zstore;
    tx->user_cb_(tx->user_cb_arg_ , OSTORE_EXECUTE_OK);
    if(tx->tx_type_ == TX_RDONLY) {
        zs->tx_rdonly_outstanding_--;
        tailq_remove(&zs->tx_rdonly_list_ , tx , zstore_tx_lhook_);    
    } else {
        zs->tx_outstanding_--;
        tailq_remove(&zs->tx_list_, tx , zstore_tx_lhook_); 
    }
    return 0;
}


void zstore_bio_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg) 
{
    // message_t *m = cb_arg;
    assert (success);
    
    spdk_bdev_free_io(bdev_io);
    
    // struct zstore_data_bio *zdb = cb_arg;
    struct zstore_transacion_t *tx_ctx = cb_arg;
    struct zstore_context_t *zs = tx_ctx->zstore_;
    // tailq_remove(&tx_ctx->bio_list_ , zdb , bio_lhook_);
    tx_ctx->bio_outstanding_--;
    
    if(tx_ctx->bio_outstanding_ == 0) {
        if(tx_ctx->tx_type_ == TX_RDONLY) {
            zstore_tx_end(tx_ctx);
            log_debug("Current tx_rdonly =%u\n",zs->tx_rdonly_outstanding_);
        } else if (tx_ctx->tx_type_ == TX_WRITE) {
            tx_ctx ->state_ = PM_TX;
            if(tailq_first(&zs->tx_rdonly_list_) == tx_ctx) {
                do {
                    struct zstore_transacion_t *tx = tailq_first(&zs->tx_list_);
                    if ( tx == NULL || tx->state_ != PM_TX) {
                        break;
                    }
                    if (tx->state_ == PM_TX) {
                        zstore_tx_metadata(tx);
                        tx->err_ = OSTORE_EXECUTE_OK;
                        zstore_tx_end(tx);
                        log_debug("Current tx=%u\n",zs->tx_outstanding_);
                    }
                } while (1);
            }
        };
    }
}

static union otable_entry_t *onode_entry(struct zstore_context_t *zs, uint64_t oid) {
    if(oid < zs->zsb_->onodes_rsv)
        return &zs->otable_[oid];
    else
        return NULL;
}

/**
 * 
 * 获取 op_ofst 到 op_len 之间的物理块地址
 * onode data index block 中的 0xff 代表无效地址  
 * 调用者需要检查 ext_nr
 */
static void object_lba_range_get(struct zstore_context_t *zs, 
    union otable_entry_t *oe , 
    uint32_t *ext_nr, struct zstore_extent_t *exts ,  
    uint64_t op_ofst , uint64_t op_len)
{
    uint64_t bofst = op_ofst >> PAGE_ALIGN_SHIFT;
    uint64_t blen = op_len >> PAGE_ALIGN_SHIFT;

    uint64_t dib_addr = zs->zsb_->pm_dy_space_ofst + 
        (((uint64_t)oe->data_idx_id) << PAGE_ALIGN_SHIFT);
    
    uint32_t dib_[1024];
    pmem_read(zs->pmem_, &dib_[bofst] , dib_addr + bofst << 2 , blen << 2);

    uint32_t i , j;
    struct zstore_extent_t *p = exts;
    p->lba_ = -1;
    p->len_ = 0;

}


int _do_rw(void *r)  {
    uint64_t oid;
    uint64_t ofst;
    uint64_t len;
    uint64_t flag;
    struct zstore_context_t *zs = zstore;
    message_t *opr = ostore_rqst(r);
    struct zstore_transacion_t *tx = ostore_async_ctx(r);


    bool is_write = false;

    if(message_get_op(r) == msg_oss_op_read) {
        tx->tx_type_ = TX_RDONLY;
        struct op_read_t *op = (void*)opr->meta_buffer;
        oid = op->oid << 16 >> 16;
        ofst = op->ofst;
        len = op->len;
        flag = op->flags;
    } else {
        tx->tx_type_ = TX_WRITE;
        struct op_write_t *op = (void*)opr->meta_buffer;
        oid = op->oid << 16 >> 16;
        ofst = op->ofst;
        len = op->len;
        flag = op->flags;

        is_write = true;       
    }

    union otable_entry_t *ote = onode_entry(zs, oid); 
    if(!ote->valid) {
        return OSTORE_OBJECT_NOT_EXIST;
    }

    //PM 事务要求 64 字节对齐的写
    //data index page 用 4 字节 index 指示一个 4K 对齐的 SSD LBA
    
    uint64_t orig_start = ofst >> 12;
    uint64_t orig_len = len >> 12;
    //向下对齐
    uint64_t d_start = (ofst >> 12) & 15;  
    //向上对齐
    uint64_t d_end = FLOOR_ALIGN((ofst >> 12) + (len >> 12), 16);
    uint64_t d_len = d_end - d_start;
    assert( d_len % 16 == 0 );

    uint64_t obj_data_index_page_addr = ((uint64_t)ote->data_idx_id)<<12;
    uint64_t r_start = obj_data_index_page_addr + d_start << 2;
    uint64_t r_len  = d_len << 2;


    //代表了object-oid 从 d_ofst 处偏移 d_len 的每 4K 对应的 LBA  
    uint32_t data_index_range[d_len];
    pmem_read(zs->pmem_,data_index_range, r_start , r_len);



    if(!is_write) {
        uint64_t i ;
        uint32_t cofst = 0 , clen = 0;
        for (i = orig_start ; i < orig_len ; ++i) {
            if(cofst + clen == i) {
                //是连续的
                clen++;
            } else {

            }
            
            if(!cofst) {
                cofst = i;
                clen = 1;
            } 
            
            tx->bio_outstanding_++;

        };
    } else {
        tx->pm_tx_ = pmem_transaction_alloc(zs->pmem_);
        //Read lba range     
        //Prepare BIO
        assert(len & 4095 == 0);
        assert(ofst & 4095 == 0);
        struct zstore_extent_t ze[64];
        uint64_t ze_nr;
        stupid_alloc_space(zs->ssd_allocator_, len >> 12 , ze , &ze_nr);

        uint64_t i;

    }




    return 0;
} 

int _do_read(void *r , cb_func_t cb_) {
    message_t *opr = ostore_rqst(r);
    struct op_read_t* op = (void*)opr->meta_buffer;
    (void)op;
    cb_( r , 0 );
    return OSTORE_SUBMIT_OK;
}
int _do_write(void *r , cb_func_t cb_) {
    message_t *opr = ostore_rqst(r);
    struct op_write_t* op = (void*)opr->meta_buffer;
    (void)op;
    // uint64_t oid = op->oid;
    cb_( r , 0 );
    return OSTORE_SUBMIT_OK;
}

static int _do_create_delete_common(void *r) 
{
    struct zstore_context_t *zs = zstore;
    message_t *opr = ostore_rqst(r);
    struct op_create_t *opcr; 
    struct op_delete_t *opde; 
    uint64_t oid;
    bool is_create = false;
    if(message_get_op(opr) == msg_oss_op_create) {
        opcr = (void*)opr->meta_buffer;
        oid = (opcr->oid << 16) >> 16;
        is_create = true;
    } else {
        opde = (void*)opr->meta_buffer;
        oid = (opde->oid << 16) >> 16;
    }
    
    //Lookup
    union otable_entry_t ote = {0};
    struct zstore_extent_t ze[1];
    uint64_t ze_nr = 0;
    int rc;
    if( 0 <= oid && oid < zs->zsb_->onodes_rsv) {
        if(!zs->otable_[oid].valid && is_create) {
            rc =  stupid_alloc_space(zs->pm_allocator_, 1 , ze, &ze_nr);
            if(rc){
                return OSTORE_NO_NODE;
            }
            uint64_t align_len_ = ze->len_;
            char zero_tmp[4096];
            memset(zero_tmp , 0xff , 4096);
            //Initialize
            uint64_t did_pm_ofst = (ze->lba_ << 12) + zs->zsb_->pm_dy_space_ofst;
            pmem_write(zs->pmem_, 0 , zero_tmp , did_pm_ofst ,4096);

            ote.data_idx_id = ze->lba_;
            ote.oid = oid;
            ote.valid = 1;
            ote.rsv = 0;
        } else if (zs->otable_[oid].valid == 1 && !is_create) {

            union otable_entry_t *oe = onode_entry(zs , oid);
            ze[0].lba_ = oe->data_idx_id;
            ze[0].len_ = 1;

            //free data_index block
            stupid_free_space(zs->pm_allocator_, ze , 1);
        
        } else {
            return OSTORE_NO_NODE;
        }
    } else {
        return OSTORE_NO_NODE;
    }

    struct zstore_transacion_t *tx = ostore_async_ctx(r);   
    tx->state_ = PM_TX;
    tx->pm_tx_ = pmem_transaction_alloc(zstore->pmem_);
    //1. onode entry
    pmem_transaction_add(zs->pmem_ ,tx->pm_tx_, 
        zs->zsb_->pm_otable_ofst + sizeof(union otable_entry_t)*oid , 
        &zs->otable_[oid],
        64 , &ote);   
    //2. bitmap
    int i ;
    for ( i = 0 ; i < ze_nr ; ++i) {
        pmem_transaction_add(zs->pmem_ , tx->pm_tx_ ,
            zs->zsb_->pm_dy_bitmap_ofst + sizeof(struct stupid_bitmap_entry_t)* (ze[i].lba_ >> 9) , 64 ,
            NULL,
            &zs->pm_allocator_->bs_[(ze[i].lba_ >> 9)]);    
    }
    return 0;
}

static void zstore_tx_prepare(void *request , cb_func_t user_cb,
    struct zstore_transacion_t *tx) 
{

    assert (tx == ostore_async_ctx(request));

    uint16_t op = message_get_op(request);
    //填充通用字段
    tx->user_cb_ = user_cb;
    tx->user_cb_arg_ = request;
    tx->zstore_ = zstore;
    tx->err_ = 0;
    tx->state_ = PREPARE;
    tx->bios_ = NULL;
    tx->bio_outstanding_ = 0;
    tx->pm_tx_ = NULL;
    tx->tx_type_ = 0;

    switch (op) {
    case msg_oss_op_create:
        _do_create_delete_common(request);
        break;
    case msg_oss_op_delete:
        _do_create_delete_common(request);
        break;
    case msg_oss_op_read:
        break;
    case msg_oss_op_write:
        break;
    default:
        break;
    }


    return;
}


static void zstore_tx_enqueue(struct zstore_transacion_t *tx) {
    if(tx->tx_type_ == TX_RDONLY) {
        tailq_insert_tail(&zstore->tx_rdonly_list_ , tx , zstore_tx_lhook_);
        zstore->tx_rdonly_outstanding_++;
    } else {
        tailq_insert_tail(&zstore->tx_list_ , tx , zstore_tx_lhook_);
        zstore->tx_outstanding_++;
    }
}


extern const int zstore_obj_async_op_context_size() {
    return sizeof(struct zstore_transacion_t);
}


extern int zstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    // uint16_t op = message_get_op(request_msg_with_op_context); 
    void *r = request_msg_with_op_context;
    struct zstore_transacion_t *tx = ostore_async_ctx(r);
    zstore_tx_prepare(r, _cb, tx);
    zstore_tx_enqueue(tx);
    return 0;
}




