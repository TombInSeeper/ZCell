#include "objectstore.h"

#include "spdk/log.h"
#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/assert.h"
#include "spdk/thread.h"

#define DEV_NMAX         3
#define DEV_META_JOURNAL 0
#define DEV_META         1
#define DEV_DATA         2

#define DEV_META_JOURNAL_BLK_SZ 4096
#define DEV_META_BLK_SZ         4096
#define DEV_DATA_BLK_SZ         4096

#define SEG_META_JOURANL_ALI_SZ (1024UL)  //4MiB
#define SEG_DATA_ALI_SZ         (32 * 1024UL) //128MiB


#define CACHE_MJ_DMA             8

#define OS_ERR_LEN               128

static __thread const char* dev_name[] = {
    "DEV_META_JOURNAL",
    "DEV_META",
    "DEV_DATA"
};

//Okay... X86 only, little endian
typedef uint8_t  _u8;
typedef uint16_t _le16;
typedef uint32_t _le32;
typedef uint64_t _le64;


#define le16_to_cpu(u) (u)
#define cpu_to_le16(u) (u)
#define le32_to_cpu(u) (u)
#define cpu_to_le32(u) (u)
#define le64_to_cpu(u) (u)
#define cpu_to_le64(u) (u)


//Cancel struct aligned
#define _packed __attribute__((packed))

typedef void (*cb_t)(void*);

typedef cb_t machine_run_t;

/**
 * 
 * Disk Layout 
 * 
 * meta_log
 * [record-block][record-block]
 * 
 * meta
 * [sb][...][node-bit-map][oid-table]  [seg-summary]   [ss-map]  [nodes]
 *  1MiB      1MiB          8MiB          1MiB          32MiB        
 * 
 * data
 * [zone0][zone1][zone2][...][zoneN-1]
 **/
#define D_SB_SZ DEV_META_BLK_SZ
#define D_SB_MAGIC 0x1997070500000000
struct d_superblock_t {
    _le64   _magic;
    _le32   _node_bitmap_alig_ofst;
    _le32   _onode_table_alig_ofst;
    _le32   _ssa_alig_ofst;
    _le32   _na_alig_ofst;
    //
    _u8     rsv[D_SB_SZ - 24];
} _packed;

struct bdev_attr_t {
    uint32_t blk_align;
    uint64_t blk_sz;
    uint64_t blk_num;
};




//---Onode-----
struct onode_t {
    union {
        struct {
            _le32 oid;
            _le64 mtime;
            _le32 d_self_nid;
            _le32 d_parent_nid;
            _le32 blkmap[800];
        };
        _u8 align[4096];
    };
};

typedef struct onode_t (onode_table_t)[1024] ;
static inline struct onode_t *get_onode_ref(onode_table_t tb, uint32_t oid) 
{
    return tb + oid;
}
//--dma_page_t 
struct dma_page_t {
    union {
        void* ptr;
        uintptr_t uptr;
    };
};

//----
struct objectstore_ctx_t {
    struct spdk_bdev        *bdev[DEV_NMAX];
    struct spdk_bdev_desc   *bdev_des[DEV_NMAX];
    struct spdk_io_channel  *bdev_ioch[DEV_NMAX];
    struct bdev_attr_t      bdev_attr[DEV_NMAX];

    onode_table_t           onode_table;
    
    //Data Zone
    uint64_t    _data_tail; //for submit

    //Meta Journal
    uint64_t    _mj_tail; 


    //dma cache of meta journal zone
    // struct dma_page_t  * _mj_pages;
    struct spdk_mempool* _mj_pool;

    void* fake_buf;

};

static __thread struct objectstore_ctx_t *g_objstore;

static void 
bdev_get_attr(struct spdk_bdev *bdev , struct bdev_attr_t *battr) 
{
    battr->blk_sz = spdk_bdev_get_block_size(bdev);
    battr->blk_num = spdk_bdev_get_num_blocks(bdev);
    battr->blk_align = spdk_bdev_get_buf_align(bdev);
}

static int 
bdev_channel_open(const char *name, struct spdk_bdev **bdev, struct spdk_bdev_desc **des, struct spdk_io_channel **ch)
{

    struct spdk_bdev* pbdev = spdk_bdev_get_by_name(name);
    if(!pbdev) {
        SPDK_ERRLOG("[spdk_bdev_get_by_name]:%s\n",name);
        return -1;
    } else {
       
    }
    struct spdk_bdev_desc* pdes;
    if ( spdk_bdev_open(pbdev,true,NULL,NULL,&pdes) ) {
        SPDK_ERRLOG("[spdk_bdev_open]:%s\n",name);
        return -1;
    }
    struct spdk_io_channel *pch = NULL;
    if((pch = spdk_bdev_get_io_channel(pdes)) == NULL){
        SPDK_ERRLOG("[spdk_bdev_get_io_channel]:%s\n",name);   
        return -1;
    }
    *bdev = pbdev;
    *des = pdes;
    *ch = pch;

    SPDK_NOTICELOG("[%s] bdev open OK! \n",name);

    return 0;
}

static int 
bdev_open_all(const char* bdev_name[]) 
{
    int r = 0;
    r += bdev_channel_open(bdev_name[DEV_META_JOURNAL],&(g_objstore->bdev[DEV_META_JOURNAL]),&(g_objstore->bdev_des[DEV_META_JOURNAL]),&(g_objstore->bdev_ioch[DEV_META_JOURNAL]));
    r += bdev_channel_open(bdev_name[DEV_META],&(g_objstore->bdev[DEV_META]),&(g_objstore->bdev_des[DEV_META]),&(g_objstore->bdev_ioch[DEV_META]));
    r += bdev_channel_open(bdev_name[DEV_DATA],&(g_objstore->bdev[DEV_DATA]),&(g_objstore->bdev_des[DEV_DATA]),&(g_objstore->bdev_ioch[DEV_DATA]));
    if(r) {
        return -1;
    }
    int i;
    for (i = 0 ; i < DEV_NMAX ; ++i) {
        struct spdk_bdev *pbdev = g_objstore->bdev[i];
        struct bdev_attr_t *pbattr = &(g_objstore->bdev_attr[i]);
        bdev_get_attr(pbdev , pbattr);
        SPDK_NOTICELOG("bdev:[%s:%s],blk_sz:%lu KiB ,blk_nr:%lu\n",
            dev_name[i],bdev_name[i] ,pbattr->blk_sz / (1<<10) , pbattr->blk_num);
    }
    return 0;
}

static void bdev_close_all()
{

    int i;
    for (i = 0 ; i < DEV_NMAX ; ++i) {
        struct spdk_io_channel *pioch = g_objstore->bdev_ioch[i];
        struct spdk_bdev_desc *pdes = g_objstore->bdev_des[i];
        spdk_put_io_channel(pioch);
        spdk_bdev_close(pdes);
    }
}

static inline void  bdev_io_common_free(struct spdk_bdev_io* bio)
{
    struct iovec *iov;
    int iovcnt;
    spdk_bdev_io_get_iovec(bio,&iov,&iovcnt);
    int i;
    for (i = 0 ; i < iovcnt ; ++ i) {
        spdk_dma_free(iov->iov_base);
        free(iov);
    }
    spdk_bdev_free_io(bio);
}

typedef struct iovec mj_page_t ;

// enum write_state {
//     OBJ_WRITE_START,
//     OBJ_WRITE_GET_ONODE,  //Onode need to be read from disk first
//     OBJ_WRITE_DATA_APPEND,
//     OBJ_WRITE_META_IN-MEM_UPDATE,
//     OBJ_WRITE_META_JOURNAL_APPEND,
//     OBJ_WRITE_END
// };
struct obj_write_ctx_t {
    //Onode ref
    struct onode_t *_o;
    
    obj_op_cb_t _cb;
    void* _cb_arg;
    uint32_t _oid;
    uint64_t _off;
    uint64_t _len;
    //Arg

    //JOURNAL-IO
    struct iovec _mj_io;

    //DATA-IO
    uint32_t _iocnt;
    struct iovec _iov[1];
};


static void _fake_prep_write_journal(struct obj_write_ctx_t* wctx )
{
    //Fake

    // struct dma_page_t *dp = spdk_mempool_get(g_objstore->_mj_pool);
    wctx->_mj_io.iov_base = g_objstore->fake_buf;
    wctx->_mj_io.iov_len = 4096;
    memset(wctx->_mj_io.iov_base,0,256);
}

static void obj_write_mj_complete(struct spdk_bdev_io *bio, bool success, void* cb_arg)
{
    spdk_bdev_free_io(bio);
    assert(success);

    if(success) {
        struct obj_write_ctx_t* wctx = cb_arg;
        wctx->_cb(wctx->_cb_arg);
        //free
        // spdk_mempool_put(g_objstore->_mj_pool, wctx->_mj_io.iov_base);
        //
        free(wctx);
    }
}
static void obj_write_data_complete(struct spdk_bdev_io *bio, bool success, void* cb_arg)
{
    spdk_bdev_free_io(bio);
    assert(success);

    if(success) {
        struct obj_write_ctx_t* wctx = cb_arg;
        struct onode_t *o = wctx->_o;
        uint32_t alig_off = wctx->_off >> 12;
        uint32_t alig_len = wctx->_len >> 12;
        uint32_t data_alig_off = wctx->_iov[0].iov_len >> 12;
        uint32_t i;
        for(i = alig_off ; i < alig_off + alig_len ; ++i) {
            o->blkmap[i] = data_alig_off + i;
        }
        // sizeof(struct onode_t);
        // Fake journal
        _fake_prep_write_journal(wctx);

        int rc = spdk_bdev_writev(g_objstore->bdev_des[DEV_META_JOURNAL],g_objstore->bdev_ioch[DEV_META_JOURNAL],
        &(wctx->_mj_io), 1 , g_objstore->_mj_tail, 4096 , obj_write_mj_complete, wctx);
        assert(rc == 0);
        g_objstore->_mj_tail += 4096;
    } else {
        SPDK_ERRLOG("bio error\n");
    }
}


int async_obj_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, obj_op_cb_t cb , void* cb_arg)
{
    struct onode_t *o = get_onode_ref(g_objstore->onode_table,oid);
    if(!o) {
        SPDK_ERRLOG("onode get error\n");
        return -1;
    }
    uint32_t iocnt = 1;
    struct obj_write_ctx_t* wctx = calloc(1,sizeof(struct obj_write_ctx_t));
    if(!wctx) {
        SPDK_ERRLOG("obj_write_ctx_t malloc error\n");
        return -1;
    }
    //prepare wctx
    wctx->_o = o;
    wctx->_cb = cb;
    wctx->_cb_arg = cb_arg;
    wctx->_off = off;
    wctx->_oid = oid;
    wctx->_len = len;
    wctx->_iocnt = iocnt;
    wctx->_iov[0].iov_base = wbuf;
    wctx->_iov[0].iov_len = len;

    // SPDK_NOTICELOG("async_obj_write wctx done\n");

    //Data append io submit
    int rc = spdk_bdev_writev(g_objstore->bdev_des[DEV_DATA],g_objstore->bdev_ioch[DEV_DATA],
        wctx->_iov, wctx->_iocnt ,g_objstore->_data_tail, wctx->_len, obj_write_data_complete, wctx);
    g_objstore->_data_tail +=  wctx->_len;

    // SPDK_NOTICELOG("async_obj_write data append\n");
    return rc;
}



struct obj_read_ctx_t {
    obj_op_cb_t cb;
    void* cb_arg;
    int inflight_rd;
};


static void obj_read_data_complete(struct spdk_bdev_io *bio, bool success, void* cb_arg)
{
    spdk_bdev_free_io(bio);
    struct obj_read_ctx_t* rctx = cb_arg;
    assert(success);
    if(success) {
        if(--rctx->inflight_rd == 0) {
            rctx->cb(rctx->cb_arg);
        }
        free(rctx);
    }
}

int async_obj_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, obj_op_cb_t cb , void* cb_arg)
{
    struct onode_t *o = get_onode_ref(g_objstore->onode_table,oid);
    if(!o) {
        return -1;
    }
    // uint64_t blk_sz = 4096;
    uint32_t alig_off = off >> 12;
    uint32_t alig_len = len >> 12;
    int i ;

    struct obj_read_ctx_t* rctx = malloc(sizeof(struct obj_read_ctx_t));
    rctx->inflight_rd = alig_len;
    rctx->cb = cb;
    rctx->cb_arg = cb_arg;
    // struct iovec *iov = malloc (sizeof(struct iovec) * alig_len);
    for ( i = alig_off ; i < alig_off + alig_len ; ++i) {
        uint64_t bdev_off = (uint64_t)(o->blkmap[i]) << 12;
        uint64_t bdev_len = 4096;
        void     *buf = (char*) rbuf + (bdev_len) * i;
        spdk_bdev_read(g_objstore->bdev_des[DEV_DATA] , 
            g_objstore->bdev_ioch[DEV_DATA],
            buf, bdev_off, bdev_len, obj_read_data_complete , rctx);
    }
    return 0;
}



//Fake
int async_obj_create(uint32_t oid , obj_op_cb_t cb , void* cb_arg)
{
    g_objstore->onode_table[oid].oid = oid;

    //Fake journal
    struct obj_write_ctx_t *wctx = malloc(sizeof(struct obj_write_ctx_t));
    wctx->_cb = cb;
    wctx->_cb_arg = cb_arg;
    _fake_prep_write_journal(wctx);
    int rc = spdk_bdev_writev(g_objstore->bdev_des[DEV_META_JOURNAL],g_objstore->bdev_ioch[DEV_META_JOURNAL],
    &(wctx->_mj_io), 1 , g_objstore->_mj_tail, 4096 , obj_write_mj_complete, wctx);
    assert(rc == 0);
    g_objstore->_mj_tail += 4096;

    return 0;
}

//Fake
int async_obj_delete(uint32_t oid , obj_op_cb_t cb , void* cb_arg)
{
    g_objstore->onode_table[oid].oid = -1;

    
    //Fake journal
    struct obj_write_ctx_t *wctx = malloc(sizeof(struct obj_write_ctx_t));
    wctx->_cb = cb;
    wctx->_cb_arg = cb_arg;
    _fake_prep_write_journal(wctx);
    int rc = spdk_bdev_writev(g_objstore->bdev_des[DEV_META_JOURNAL],g_objstore->bdev_ioch[DEV_META_JOURNAL],
    &(wctx->_mj_io), 1 , g_objstore->_mj_tail, 4096 , obj_write_mj_complete, wctx);
    assert(rc == 0);
    g_objstore->_mj_tail += 4096;

    return 0;
}


int async_mount(const char* dev_list[], /* size = 3*/  int mount_flag /**/, obj_op_cb_t cb , void* cb_arg)
{ 
    //allocate
    g_objstore =(struct objectstore_ctx_t *)calloc(1, sizeof(struct objectstore_ctx_t));
    if(!g_objstore) {
        SPDK_ERRLOG("g_objstore malloc\n");
        return -1;
    }
    
    g_objstore->_mj_pool = spdk_mempool_create("mj-dma-cache",
            CACHE_MJ_DMA,sizeof(struct dma_page_t),CACHE_MJ_DMA, 
            SPDK_ENV_SOCKET_ID_ANY);
    
    if(!g_objstore->_mj_pool) {
        SPDK_ERRLOG("g_objstore _mj_pool spdk_mempool_create\n");
        return -1;
    };

    if ( bdev_open_all(dev_list) ) {
        return -1;
    }

    // struct dma_page_t *dp = malloc(sizeof(struct dma_page_t) * CACHE_MJ_DMA);
    int i;
    uint64_t blk_sz = g_objstore->bdev_attr[DEV_META_JOURNAL].blk_sz;
    uint64_t blk_alig = g_objstore->bdev_attr[DEV_META_JOURNAL].blk_align;
    uint32_t mcnt = spdk_mempool_count(g_objstore->_mj_pool);
    SPDK_NOTICELOG("mempool count:%u\n", mcnt);

    void* dp[128];
    spdk_mempool_get_bulk(g_objstore->_mj_pool,dp,mcnt);
    for( i = 0 ; i < CACHE_MJ_DMA ; ++i ) {
        struct dma_page_t *p = dp[i];
        p->ptr =spdk_dma_malloc(blk_sz , blk_alig , NULL);
        assert(p->ptr);
    }
    spdk_mempool_put_bulk(g_objstore->_mj_pool,dp,mcnt);

    g_objstore->fake_buf = spdk_dma_malloc(blk_sz , blk_alig , NULL);
    // g_objstore->_mj_pages = dp;
    //Load-meta-data and recovery
    //TODO

    if(cb) 
        cb(cb_arg);

    return 0;
}


int async_unmount(obj_op_cb_t cb , void* cb_arg)
{
    //write back dirty data

    //close bdev and io channel
    bdev_close_all();
    SPDK_NOTICELOG("bdev_close_all done\n");
    int i;
    //Free obj-fs context
  
    for( i = 0 ; i < CACHE_MJ_DMA ; ++i ) { 
        struct dma_page_t *dp = spdk_mempool_get(g_objstore->_mj_pool);
        spdk_free(dp->ptr);
    }
    
    spdk_mempool_free(g_objstore->_mj_pool);

    spdk_free(g_objstore->fake_buf);

    // free(g_objstore->_mj_pages);
    free(g_objstore);

    if(cb)
        cb(cb_arg);
    
    return 0;
}



//-----------------------------------
/**  
 *  
 * State-machine-drived asyncrous process
 * This is a coding example
 **/

/*First list all states*/
enum mkfs_state {
    MKFS_START,
    MKFS_WRITE_SB,
    MKFS_END,
    MKFS_ERR = 0xf0,
};
/*Define context struct*/
struct mkfs_state_machine {
    struct {
        const char** _dev_list;
        int         _mkfs_flag;
        obj_op_cb_t _cb;
        void*       _cb_arg;
    } _arg;

    int  _state;
    int  _errno;
    machine_run_t _run;
};


void _mkfs_write_sb_complete(struct spdk_bdev_io *bio, bool success, void* cb_arg)
{
    struct mkfs_state_machine *mctx = cb_arg;
    // struct objectstore_ctx_t* ofs = g_objstore;

    bdev_io_common_free(bio);
    
    mctx->_state = success ? MKFS_END : MKFS_ERR;
    mctx->_run(mctx);
}

void _mkfs_write_sb(void* _mctx)
{
    struct mkfs_state_machine *mctx = _mctx;
    struct objectstore_ctx_t* ofs = g_objstore;
    uint64_t blk_align = ofs->bdev_attr[DEV_META].blk_align;
    const size_t d_sb_sz = sizeof(struct d_superblock_t);
    struct d_superblock_t *d_sb = spdk_dma_zmalloc(sizeof(struct d_superblock_t),blk_align,NULL);
    if(!d_sb) {
        SPDK_ERRLOG("spdk_dma_zmalloc falied\n");
        mctx->_errno = -1;
        mctx->_state = MKFS_ERR;
        return;
    }

    struct iovec *iov = malloc(sizeof(struct iovec) * 1);
    iov->iov_base = d_sb;
    iov->iov_len = d_sb_sz;
    int rc;
    rc = spdk_bdev_writev(ofs->bdev_des[DEV_META], ofs->bdev_ioch[DEV_META], iov, 1,
        0, d_sb_sz, _mkfs_write_sb_complete, _mctx);

    if (rc) {
        SPDK_ERRLOG("bdev-io submit error\n");
        spdk_dma_free(iov->iov_base);
        free(iov);
        mctx->_errno = -1;
        mctx->_state = MKFS_ERR;
    }

}

void _run_mkfs_state_machine(void* _mctx)
{
    struct mkfs_state_machine *mctx = _mctx;
    int state;
    do {
        state = mctx->_state;
        switch (state)  {
        case (MKFS_START):
            mctx->_errno = bdev_open_all(mctx->_arg._dev_list);
            mctx->_state = mctx->_errno ? MKFS_ERR : MKFS_WRITE_SB;
            break;
        case (MKFS_WRITE_SB):
            _mkfs_write_sb(mctx);
            break;
        case (MKFS_END):
            SPDK_NOTICELOG("mkfs done\n");
            //do callback
            if(mctx->_arg._cb)
                mctx->_arg._cb(mctx->_arg._cb_arg);
            break;
        case (MKFS_ERR):
        default:
            SPDK_ERRLOG("mkfs error\n");
            break;
        }
    } while (state != mctx->_state);

}
int async_mkfs(const char* dev_list[], /* size = 3*/  int mkfs_flag /**/, obj_op_cb_t cb , void* cb_arg)
{
    struct mkfs_state_machine *mctx = malloc(sizeof(struct mkfs_state_machine));
    if(!mctx) {
        return -1;
    }

    mctx->_arg._dev_list = dev_list;
    mctx->_arg._mkfs_flag = mkfs_flag;
    mctx->_arg._cb = cb;
    mctx->_arg._cb_arg = cb_arg;
    
    mctx->_state = MKFS_START;
    mctx->_errno = 0;

    mctx->_run = _run_mkfs_state_machine;
    //Run 
    mctx->_run(mctx);

    return 0;
}

