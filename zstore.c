//For data storage
#include <spdk/bdev.h>
#include <spdk/util.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/thread.h>


#include "zstore_allocator.h"
#include "zstore.h"
#include "pm.h"
#include "store_common.h"

#include "util/log.h"
#include "util/errcode.h"


#define ZSTORE_PAGE_SHIFT 12
#define ZSTORE_PAGE_SIZE  (1UL << ZSTORE_PAGE_SHIFT)
#define ZSTORE_PAGE_MASK (~(ZSTORE_PAGE_SIZE -1))
#define ZSTORE_TX_IOV_MAX 32
#define ZSTORE_IO_UNIT_MAX (128UL * 1024)
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
    uint8_t align[ZSTORE_PAGE_SIZE];
};

union otable_entry_t {
    struct {
        uint64_t valid :1;
        uint64_t rsv : 15;
        uint64_t oid : 48;
        uint64_t mtime;
        uint64_t lsize;
        uint64_t psize;
        uint32_t rsv2;
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
    END,
};
enum zstore_tx_type {
    TX_RDONLY = 1,
    TX_WRITE = 2
};

struct zstore_transacion_t {
    void *zstore_;
    uint64_t tid_;
    int tx_type_;
    uint16_t state_;
    uint16_t err_;

    cb_func_t user_cb_;
    void *user_cb_arg_;

    uint32_t bio_outstanding_;
    char *data_buffer;
    struct {
        uint8_t  io_type;
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

    uint64_t tid_max_;

    //Transaction 
    uint32_t tx_outstanding_;
    tailq_head(zstore_tx_list_t , zstore_transacion_t) tx_list_;

    uint32_t tx_rdonly_outstanding_;
    tailq_head(zstore_tx_rdonly_list_t , zstore_transacion_t) tx_rdonly_list_;
};

static __thread struct zstore_context_t *zstore; //TLS 

// static int zstore_tx_poll(void *zs_);
static int 
zstore_ctx_init(struct zstore_context_t **zs) 
{
    *zs = calloc(1, sizeof(**zs));
    if(!(*zs)) {
        return -1;
    }
    struct zstore_context_t *zs_ = *zs;
    tailq_init(&zs_->tx_list_);
    tailq_init(&zs_->tx_rdonly_list_);
    zs_->zsb_ = calloc( 1, sizeof(union zstore_superblock_t));
    
    zs_->ssd_allocator_ = calloc(1, sizeof(*zs_->ssd_allocator_));
    zs_->pm_allocator_ = calloc(1, sizeof(*zs_->pm_allocator_));

    zs_->otable_ = calloc( 1UL << 20U, sizeof(union otable_entry_t));

    zs_->tid_max_ = 0;

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

static void 
zstore_bio_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);

static int
zstore_tx_data_bio(struct zstore_transacion_t *tx) 
{
    struct zstore_context_t *zs = tx->zstore_;
    uint32_t i;
    uint32_t n = tx->bio_outstanding_;
    char *buf = tx->data_buffer;
    for ( i = 0 ; i < n ; ++i) {
        int rc;
        log_debug("Tx=%lu, bio[%u]={%u,%u}\n", tx->tid_, i,
            tx->bios_[i].blk_ofst , tx->bios_[i].blk_len);
        if(tx->bios_[i].io_type == IO_READ) {
            rc = spdk_bdev_read_blocks(zs->nvme_bdev_desc_, zs->nvme_io_channel_ ,
            buf , tx->bios_[i].blk_ofst , tx->bios_[i].blk_len, zstore_bio_cb , tx);       
        }
        else {
            rc = spdk_bdev_write_blocks(zs->nvme_bdev_desc_, zs->nvme_io_channel_ ,
                buf , tx->bios_[i].blk_ofst , tx->bios_[i].blk_len, zstore_bio_cb , tx);
        }
        if(rc) {
            log_err("Submit error\n");
            return OSTORE_IO_ERROR;
        }
        buf += ((uint64_t)(tx->bios_[i].blk_len) << ZSTORE_PAGE_SHIFT);
    }
    return 0;
}

static int 
zstore_tx_end(struct zstore_transacion_t *tx) 
{
    struct zstore_context_t *zs = zstore;
    if(tx->tx_type_ == TX_RDONLY) {
        zs->tx_rdonly_outstanding_--;
        tailq_remove(&zs->tx_rdonly_list_ , tx , zstore_tx_lhook_);    
    } else {
        zs->tx_outstanding_--;
        tailq_remove(&zs->tx_list_, tx , zstore_tx_lhook_); 
    }
    log_debug("Tx=%lu End\n",tx->tid_);
    tx->user_cb_(tx->user_cb_arg_ , tx->err_);
    return 0;
}

static int 
zstore_tx_metadata(struct zstore_transacion_t *tx) 
{   
    struct zstore_context_t *zs = tx->zstore_;
    struct zstore_transacion_t *tx_ctx = tx;
    assert(tx->bio_outstanding_ == 0);
    if(tx_ctx->tx_type_ == TX_RDONLY) {
        zstore_tx_end(tx_ctx);
    } else if (tx_ctx->tx_type_ == TX_WRITE) {
        tx_ctx ->state_ = PM_TX;
        if(tailq_first(&zs->tx_list_) == tx_ctx) {
            do {
                struct zstore_transacion_t *tx = tailq_first(&zs->tx_list_);
                if ( tx == NULL || tx->state_ != PM_TX) {
                    break;
                }
                if (tx->state_ == PM_TX) {
                    bool s =  pmem_transaction_apply(zs->pmem_ , tx->pm_tx_);
                    tx->err_ = !s ? OSTORE_IO_ERROR : OSTORE_EXECUTE_OK;
                    pmem_transaction_free(zs->pmem_, tx->pm_tx_);
                    zstore_tx_end(tx);
                }
            } while (1);
        }
    };
    return 0;
}

static void 
zstore_tx_execute(struct zstore_transacion_t *tx) {
    switch (tx->state_) {
        case DATA_IO:
            log_debug("TX=%lu execute data IO\n" , tx->tid_);
            zstore_tx_data_bio(tx);
            break;
        case PM_TX:
            log_debug("TX=%lu execute meta tx and end life\n" , tx->tid_);
            zstore_tx_metadata(tx);
            break;
        default:
            break;
    }
}

static void 
zstore_bio_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg) 
{
    // message_t *m = cb_arg;
    assert (success);
    spdk_bdev_free_io(bdev_io);
    
    // struct zstore_data_bio *zdb = cb_arg;
    struct zstore_transacion_t *tx_ctx = cb_arg;
    // struct zstore_context_t *zs = tx_ctx->zstore_;
    // tailq_remove(&tx_ctx->bio_list_ , zdb , bio_lhook_);
    tx_ctx->bio_outstanding_--;
    log_debug("Tx=%lu , rest bios=%u\n" ,tx_ctx->tid_, tx_ctx->bio_outstanding_);
    if(tx_ctx->bio_outstanding_ == 0) {
        tx_ctx->state_ = PM_TX;
        if(tx_ctx->bios_) {
            free(tx_ctx->bios_);
            tx_ctx->bios_ = NULL;
        }
        zstore_tx_execute(tx_ctx);
    }
}

static inline union otable_entry_t *
onode_entry(struct zstore_context_t *zs, uint64_t oid) {
    if(oid < zs->zsb_->onodes_rsv)
        return &zs->otable_[oid];
    else
        return NULL;
}

static inline uint64_t 
onode_pm_ofst( struct zstore_context_t *zs , uint64_t oid ) {
    return zstore->zsb_->pm_otable_ofst + oid * sizeof(union otable_entry_t);
} 

static inline uint64_t
object_data_index_block( struct zstore_context_t *zs , union otable_entry_t *oe )
{
    uint64_t dib_addr = zs->zsb_->pm_dy_space_ofst + 
        (((uint64_t)oe->data_idx_id) << ZSTORE_PAGE_SHIFT);
    log_debug("Object %lu data index block addr = 0x%lx\n" , oe->oid, dib_addr);
    return dib_addr;
}

static void 
object_lba_merge_to(uint32_t *bi , uint32_t nbi, 
    uint32_t *n, struct zstore_extent_t *e ,
    uint32_t *mb_len )
{
    int i , j ;
    struct zstore_extent_t *p = e - 1;
    int in_found_ctx = 0;
    *mb_len = 0;
    for (i = 0 , j = 0; i < nbi; ++i) {
        uint32_t ba = bi[i];
        if(ba == -1) {
            in_found_ctx = 0;
            continue;
        } else if (ba != -1 && in_found_ctx) {
            *mb_len = *mb_len + 1;
            if (p->len_ + p->lba_ == ba
                && (p->lba_ >> 9 == ba >> 9) //不允许跨 bitmap_entry
                && (p->len_ < ZSTORE_IO_UNIT_MAX)) //不允许大于最大BIO粒度
            { 
                p->len_++;
            } else {
                ++p;
                ++j;
                p->lba_ = ba;
                p->len_ = 1;
            }  
        } else if (ba != -1 && !in_found_ctx) {
            *mb_len = *mb_len + 1;
            ++p;
            ++j;
            p->lba_ = ba;
            p->len_ = 1;
            in_found_ctx = 1;
        }
    }
    *n = j;
}

static void 
object_lba_range_get(struct zstore_context_t *zs, 
    union otable_entry_t *oe , 
    uint32_t *ext_nr, struct zstore_extent_t *exts ,  
    uint32_t *mapped_blen,
    uint32_t *dib,
    uint64_t op_ofst , uint64_t op_len)
{

    uint64_t bofst = op_ofst >> ZSTORE_PAGE_SHIFT;
    uint64_t blen = op_len >> ZSTORE_PAGE_SHIFT;
    uint64_t dib_addr = object_data_index_block(zs , oe);
    
    //64B alined read/write
    //
    //Data Index Block
    //bofst = 3 , blen = 14
    //istart = 0 , iend = 32
    //dib[0][1][2][3][4][5]..[15] | [16][17]..[31]
    //             3-----------------16
    uint64_t istart = FLOOR_ALIGN(bofst , 16);
    uint64_t iend = CEIL_ALIGN((bofst + blen ) , 16);
    uint64_t ilen = (iend - istart);
    
    log_debug("Read data index block:(bofst=%lu,blen=%lu,istart=%lu,ilen=%lu)\n", 
        bofst , blen , istart , ilen) ;

    uint32_t *dib_ = dib;
    uint32_t *dib_ofst_ = dib_ + (bofst - istart);

    //Read
    pmem_read(zs->pmem_, dib_ , dib_addr + (istart << 2) , ilen << 2);  
    
    do {
        uint64_t i ;
        log_debug("Data Index Read:");
        for ( i = 0  ; i < ilen ; ++i) {
            printf("0x%x," , dib_[i]);
        }
        printf("\n");
    } while (0);
    object_lba_merge_to(dib_ofst_ , blen, ext_nr , exts , mapped_blen);
}


static void 
lba_to_bitmap_id(const uint32_t ne , struct zstore_extent_t *e ,  int *bid , int *n )
{
    int i;
    *n = 0;
    for (i = 0 ; i < ne ; ++i) {
        int j;
        int in = 0;
        for ( j = 0 ; j < *n ; ++j) {
            if( (e[i].lba_ >> 9) == bid[j]) {
                in = 1;
                break;
            }
        }
        if(!in) {
            bid[*n] = (e[i].lba_>>9);
            *n = *n + 1;
        }
    }

}

static void 
dump_extent(struct zstore_extent_t *e , uint32_t ne) {
    uint32_t i;
    for (i = 0 ; i < ne ; ++i ) {
        log_debug("[0x%lu~0x%lu]\n", e[i].lba_ , e[i].len_ );        
    }
}

static int
_tx_prep_rw_common(void *r)  {
    uint16_t op = message_get_op(r);
    message_t *m = (message_t*)r;
    struct zstore_transacion_t *tx = ostore_async_ctx(r);   
    uint64_t oid, op_ofst, op_len , op_flags;
    void *data_buf = m->data_buffer;
    if(op  == msg_oss_op_read) {
        op_read_t * op = (void*)(m->meta_buffer);
        oid = op->oid;
        op_ofst = op->ofst;
        op_len = op->len;
        op_flags = op->flags;
    } else if ( op == msg_oss_op_write ){
        op_write_t * op = (void*)(m->meta_buffer);
        oid = op->oid;
        op_ofst = op->ofst;
        op_len = op->len;
        op_flags = op->flags;    
    } else {
        log_err("Op error\n");
        return UNKNOWN_OP;
    }

    if(!op_len || op_len % ZSTORE_PAGE_SIZE != 0) {
        return INVALID_OP;
    }
    if(op_ofst % ZSTORE_PAGE_SIZE != 0) {
        return INVALID_OP;
    }

    union otable_entry_t *oe = onode_entry(zstore, oid);
    
    
    if(!oe) {
        return OSTORE_NO_NODE;
    }
    // if(op == msg_oss_op_read) 
    uint32_t  mapped_blen = 0;

    uint32_t  ne;
    struct zstore_extent_t e[ZSTORE_TX_IOV_MAX];

    uint32_t blen = op_len >> ZSTORE_PAGE_SHIFT;
    uint32_t bofst = op_ofst >> ZSTORE_PAGE_SHIFT;
    
    uint32_t dib[ZSTORE_TX_IOV_MAX];


    //获取合并后的extent
    object_lba_range_get(zstore, oe , &ne , 
        e , &mapped_blen , dib ,op_ofst , op_len);
    
    if(1) {
        log_debug("TID=%lu,object_id:%lu,op_ofst:0x%lx,op_len:0x%lx,mapped len=0x%x\n" ,
            tx->tid_ , oid , op_ofst , op_len , mapped_blen);
        dump_extent(e,ne); 
    }


    tx->data_buffer = data_buf;
    
    if(op == msg_oss_op_read) {
        uint32_t i;
        tx->tx_type_ = TX_RDONLY;
        tx->state_ = DATA_IO;
        if(ne == 0 || blen != mapped_blen) {
            return OSTORE_READ_HOLE; 
        }
        tx->bios_ = malloc(sizeof(*tx->bios_) * ne);
        for (i = 0 ; i < ne ; ++i) {
            tx->bios_[i].io_type = IO_READ;
            tx->bios_[i].blk_len = e[i].len_;
            tx->bios_[i].blk_ofst = e[i].lba_;
        }
        tx->bio_outstanding_ = ne;
    
    } else {
        log_debug("Prepare write context\n");
        if (op_ofst + op_len > (4UL << 20)) {
            return OSTORE_WRITE_OUT_MAX_SIZE;
        }
        tx->tx_type_ = TX_WRITE;
        tx->state_ = DATA_IO;
        
        int bid[ZSTORE_TX_IOV_MAX] , bid2[ZSTORE_TX_IOV_MAX];
        int nbid = 0 , nbid2 = 0;
        
        if(ne != 0) {
            log_debug("overwrite\n");
            stupid_free_space(zstore->ssd_allocator_ , e , ne);
            lba_to_bitmap_id(ne ,e , bid , &nbid);
        }
        
        struct zstore_extent_t enew[ZSTORE_TX_IOV_MAX];
        uint64_t enew_nr;
        int rc = stupid_alloc_space(zstore->ssd_allocator_ , blen , enew , &enew_nr );
        dump_extent(enew,enew_nr); 
        
        if(rc) {
            log_err("Allocate new space failed\n");
            return rc;
        }
        lba_to_bitmap_id(enew_nr, enew, bid2, &nbid2);

        tx->bios_ = malloc(sizeof(*tx->bios_) * enew_nr);
        int i;
        for (i = 0 ; i < enew_nr ; ++i) {
            tx->bios_[i].io_type = IO_WRITE;
            tx->bios_[i].blk_len = enew[i].len_;
            tx->bios_[i].blk_ofst = enew[i].lba_;
        }
        tx->bio_outstanding_ = enew_nr;

        tx->pm_tx_ = pmem_transaction_alloc(zstore->pmem_);
        bool s;
        //保存SSD Bitmap 的新值
        for ( i = 0 ; i < nbid ; ++i) {
            uint64_t pm_ofst = zstore->zsb_->pm_ssd_bitmap_ofst +
                bid[i] * sizeof(struct stupid_bitmap_entry_t);
            // log_debug("Add tx log entry: base_ofst:%lu, ")
            s = pmem_transaction_add(zstore->pmem_,tx->pm_tx_, pm_ofst , NULL, 64 ,
                &zstore->ssd_allocator_->bs_[bid[i]]);
            if (!s) {
                log_err("Too big transaction\n");
                // pmem_transaction_free(tx->pm_tx_);
                pmem_transaction_free(zstore->pmem_, tx->pm_tx_);
                return INVALID_OP;
            }
        }
        for ( i = 0 ; i < nbid2 ; ++i) {
            uint64_t pm_ofst = zstore->zsb_->pm_ssd_bitmap_ofst +
                bid2[i] * sizeof(struct stupid_bitmap_entry_t);
            s = pmem_transaction_add(zstore->pmem_,tx->pm_tx_, pm_ofst , NULL, 64 ,
                &zstore->ssd_allocator_->bs_[bid2[i]]);
            if (!s) {
                log_err("Too big transaction\n");
                pmem_transaction_free(zstore->pmem_, tx->pm_tx_);
                return INVALID_OP;
            }
        }

        //DataIndex Update
        do {
            uint64_t data_index_shift = 2;
            uint64_t dib_addr = object_data_index_block(zstore, oe);
            uint64_t istart = FLOOR_ALIGN(bofst , 16);
            uint64_t iend = CEIL_ALIGN(bofst + blen , 16);
            uint64_t imlen = (iend - istart);
            uint64_t iofst = bofst - istart;
            uint64_t itail = (bofst + blen);
            uint64_t j;
            uint64_t i = iofst;
            for ( j = 0 ; j < enew_nr ; ++j) {
                uint64_t k;
                for ( k = 0 ; k < enew[j].len_ ; ++k) {
                    dib[i++] = enew[j].lba_ + k;
                }
            }
            assert(i == itail);

            do {
                uint64_t i ;
                log_debug("Data Index Update to:");
                for ( i = 0  ; i < imlen ; ++i) {
                    printf("0x%x," , dib[i]);
                }
                printf("\n");
            } while (0);

            pmem_transaction_add(zstore->pmem_, tx->pm_tx_ , 
                dib_addr + (istart << data_index_shift) ,
                NULL, imlen << data_index_shift, dib);

        } while(0);

        
        //oentry 的新值
        uint64_t oe_ofst = zstore->zsb_->pm_otable_ofst + 
            sizeof(*oe) * oid;

        pmem_transaction_add(zstore->pmem_, tx->pm_tx_, oe_ofst ,
            oe , sizeof(*oe) , oe );
    }
    return 0;
}

static int 
_tx_prep_cre_del_common(void *r) 
{
    struct zstore_context_t *zs = zstore;
    message_t *opr = ostore_rqst(r);
    struct op_create_t *opcr; 
    struct op_delete_t *opde; 
    uint64_t oid;
    bool is_create = false;
    if(message_get_op(opr) == msg_oss_op_create) {
        opcr = (void*)opr->meta_buffer;
        oid =  ((opcr->oid) << 16) >> 16;
        is_create = true;
    } else {
        opde = (void*)opr->meta_buffer;
        oid =  ((opde->oid) << 16) >> 16;
    }

    //Lookup
    union otable_entry_t ote;
    memset(&ote, 0 , sizeof(union otable_entry_t));
    struct zstore_extent_t ze[1];
    uint64_t ze_nr = 0;
    int rc;
    union otable_entry_t *old_oe = onode_entry(zs , oid);
    if(!is_create)
        log_debug("Delete, OID=%lu , oentry = { valid=%d , dib=%x }\n",oid , 
            old_oe->valid,
            old_oe->data_idx_id);

    if( 0 <= oid && oid < zs->zsb_->onodes_rsv) {
        if(!old_oe->valid && is_create) {
            log_debug("Create, OID=%lu\n",oid);
            
            rc =  stupid_alloc_space(zs->pm_allocator_, 1 , ze, &ze_nr);
            if(rc){
                return OSTORE_NO_NODE;
            }
            uint64_t align_len_ = ze->len_;
            char zero_tmp[4096];
            memset(zero_tmp , 0xff , 4096);
            //Initialize
            uint64_t did_pm_ofst = (ze->lba_ << ZSTORE_PAGE_SHIFT) + zs->zsb_->pm_dy_space_ofst;
            pmem_write(zs->pmem_, 0 , zero_tmp , did_pm_ofst , ZSTORE_PAGE_SIZE);

            ote.data_idx_id = ze->lba_;
            ote.oid = oid;
            ote.valid = 1;
            ote.rsv = 0;
        
        } else if (old_oe->valid == 1 && !is_create) {
            union otable_entry_t *oe = onode_entry(zs , oid);

            ze[0].lba_ = oe->data_idx_id;
            ze[0].len_ = 1;

            //free data_index block
            stupid_free_space(zs->pm_allocator_, ze , 1);

        } else {
            log_debug("Failed, OID=%lu\n",oid);
            return OSTORE_NO_NODE;
        }
    } else {
        return OSTORE_NO_NODE;
    }

    struct zstore_transacion_t *tx = ostore_async_ctx(r);   
    tx->tx_type_ = TX_WRITE;
    tx->state_ = PM_TX;
    tx->pm_tx_ = pmem_transaction_alloc(zstore->pmem_);
    
    //1. onode entry
    pmem_transaction_add(zs->pmem_ ,tx->pm_tx_, 
        zs->zsb_->pm_otable_ofst + sizeof(union otable_entry_t) * oid , 
        old_oe,
        sizeof(ote) , &ote);   

    //2. bitmap
    int i ;
    for ( i = 0 ; i < ze_nr ; ++i) {
        pmem_transaction_add(zs->pmem_ , tx->pm_tx_ ,          
            zs->zsb_->pm_dy_bitmap_ofst + sizeof(struct stupid_bitmap_entry_t)* (ze[i].lba_ >> 9) , 
            NULL,
            sizeof(struct stupid_bitmap_entry_t),
            &(zs->pm_allocator_->bs_[(ze[i].lba_ >> 9)]));    
    }
    return 0;
}

static int 
zstore_tx_prepare(void *request , cb_func_t user_cb,
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
    tx->tid_ = zstore->tid_max_++;
    int rc = 0;
    switch (op) {
    case msg_oss_op_create:
        rc = _tx_prep_cre_del_common(request);
        assert(tx->state_ == PM_TX);
        break;
    case msg_oss_op_delete:
        rc =_tx_prep_cre_del_common(request);
        assert(tx->state_ == PM_TX);
        break;
    case msg_oss_op_read:
        rc = _tx_prep_rw_common(request);
        assert(tx->state_ == DATA_IO);
        break;
    case msg_oss_op_write:
        rc =_tx_prep_rw_common(request);
        assert(tx->state_ == DATA_IO);
        break;
    default:
        rc = UNKNOWN_OP;
        break;
    }
    return rc;
}

static void 
zstore_tx_enqueue(struct zstore_transacion_t *tx) {
    
    log_debug("TX=%lu enqueue\n" , tx->tid_);
    if(tx->tx_type_ == TX_RDONLY) {
        tailq_insert_tail(&zstore->tx_rdonly_list_ , tx , zstore_tx_lhook_);
        zstore->tx_rdonly_outstanding_++;
    } else {
        tailq_insert_tail(&zstore->tx_list_ , tx , zstore_tx_lhook_);
        zstore->tx_outstanding_++;
    }
}

extern const int 
zstore_obj_async_op_context_size() {
    return sizeof(struct zstore_transacion_t);
}

extern int 
zstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    // uint16_t op = message_get_op(request_msg_with_op_context); 
    void *r = request_msg_with_op_context;
    struct zstore_transacion_t *tx = ostore_async_ctx(r);
    int rc = zstore_tx_prepare(r, _cb, tx);
    if(rc) {
        return INVALID_OP;
    }
    zstore_tx_enqueue(tx);
    zstore_tx_execute(tx);
    return 0;
}




