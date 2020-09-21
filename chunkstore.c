#include "chunkstore.h"
#include "objectstore.h"
#include "util/log.h"

#include "spdk/bdev.h"

#define op_handler(name) static int _do_ ## name ( void* ctx, cb_func_t cb) 


/**
 * 
 * 
 * 
 * 
 * Chunk Store Staic Divide the block device into N 4MiB objects
 * 
 * [BLock Lba]
 * 
 * [---] [---] [---] [---] [---][---][---][---]
 *  obj0 obj1 obj2 obj3
 * 
 */ 
static void fake_async_cb_wrapper(void *cb  , void* cb_arg) {
    cb_func_t _cb =  cb;
    if(_cb) {
        _cb(cb_arg, OSTORE_EXECUTE_OK);
    }
}

static void fake_async_cb(cb_func_t  cb , void * cb_arg) {
    struct spdk_event *e = spdk_event_allocate( spdk_env_get_current_core(),
        fake_async_cb_wrapper , cb , cb_arg);
    spdk_event_call(e);
    return;
}

struct chunkstore_context_t {
    struct {
        struct spdk_bdev *bdev;
        struct spdk_bdev_desc *bdev_desc;
        struct spdk_io_channel *ioch;
    } device;

    struct {
        uint64_t data_gb;
        uint64_t max_oid;
        uint64_t max_obj_sz;
    }stat;
};

static __thread struct chunkstore_context_t chkstore;
static inline struct chunkstore_context_t *get_local_store_ptr() {
    return &chkstore;
} 

static void spdk_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *event_ctx) {
    return;
}

static void _bdev_open(const char *name) {
    struct chunkstore_context_t *cs = get_local_store_ptr();

    int rc =  spdk_bdev_open_ext(name,true, spdk_bdev_event_cb,NULL, &cs->device.bdev_desc);
    if(rc) {
      	log_err("Could not open a bdev %s,\n" , name);
		spdk_app_stop(-1);
		return;  
    }

    log_info("Open bdev %s OK\n" , name);

    cs->device.ioch = spdk_bdev_get_io_channel(cs->device.bdev_desc);
    if(!cs->device.ioch) {
       	log_err("Could not spdk_bdev_get_io_channel %s,\n" , name);
		spdk_app_stop(-1);
		return;         
    }

    log_info("Get io channel %s OK\n" , name);
}
static void _hardcode_stat() {
    struct chunkstore_context_t *cs = get_local_store_ptr();
    cs->device.bdev = spdk_bdev_desc_get_bdev(cs->device.bdev_desc);
    assert(spdk_bdev_get_block_size(cs->device.bdev) == 4096);

    uint64_t cb = spdk_bdev_get_num_blocks(cs->device.bdev) * 
        spdk_bdev_get_block_size(cs->device.bdev);
    cs->stat.max_obj_sz = 4 << 20;
    cs->stat.max_oid = cb / (cs->stat.max_obj_sz);
    cs->stat.data_gb =  cb / (1024 * 1024 * 1024);

}


static void _bdev_close() {
    struct chunkstore_context_t *cs = get_local_store_ptr();
    spdk_put_io_channel(cs->device.ioch);
    spdk_bdev_close(cs->device.bdev_desc);
}

extern int chunkstore_stat(char *out , uint32_t len) { 
    return OSTORE_EXECUTE_OK;
}
extern int chunkstore_mkfs(const char* dev_list[], int flags) {
    return OSTORE_EXECUTE_OK;
}
extern int chunkstore_mount(const char* dev_list[], /* size = 3*/  int flags /**/) {
    _bdev_open(dev_list[0]);
    _hardcode_stat();
    return OSTORE_EXECUTE_OK;
}
extern int chunkstore_unmount() {
    _bdev_close();
    return OSTORE_EXECUTE_OK;
}

typedef struct async_op_context_t {
    uint16_t state;
    uint16_t err;
    cb_func_t end_cb;
    uint32_t rsv[1];
}async_op_context_t;


op_handler(stat) {
    struct chunkstore_context_t* cs = get_local_store_ptr();
    message_t *m = ctx;
    // async_op_context_t *actx = ostore_async_ctx(m);
    op_stat_result_t *stat =(void*)m->data_buffer;
    stat->max_obj_size_kib = cpu_to_le32(0x1000);
    stat->capcity_gib = cpu_to_le32((uint32_t)(cs->stat.data_gb));
    stat->max_oid = cpu_to_le32(cs->stat.max_oid);
    stat->obj_blk_sz_kib = cpu_to_le32(0x1000);
    stat->type = cpu_to_le32(1);
    fake_async_cb(cb,ctx);
    return OSTORE_SUBMIT_OK;
}
op_handler(create) {
    fake_async_cb(cb,ctx);
    return OSTORE_SUBMIT_OK;
}
op_handler(delete) {
    fake_async_cb(cb,ctx);
    return OSTORE_SUBMIT_OK;
}

void rw_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg) {
    // message_t *m = cb_arg;
    spdk_bdev_free_io(bdev_io);
    async_op_context_t *actx = ostore_async_ctx(cb_arg);
    if(success) {
        actx->err = OSTORE_EXECUTE_OK;
    } else {
        actx->err = OSTORE_IO_ERROR;
    }
    actx->end_cb(cb_arg, actx->err);
}


static void _obj2blk(uint32_t oid , uint32_t o_ofst, uint32_t o_len, 
    uint64_t *b_ofst, uint64_t *b_len)  {
    *b_ofst = (oid * (1024)) + (o_ofst >> 12);
    *b_len = (o_len)   >> 12;
}

op_handler(read) {
    message_t *m = ctx;
    async_op_context_t *actx = ostore_async_ctx(ctx);
    actx->end_cb = cb;
    struct chunkstore_context_t* cs = get_local_store_ptr();
    op_read_t *op_args = (void*) m->meta_buffer;
    uint64_t bdev_ofst, bdev_len;
    _obj2blk(le32_to_cpu(op_args->oid), le32_to_cpu(op_args->ofst),le32_to_cpu(op_args->len),
        &bdev_ofst,&bdev_len);


    // log_info("oid=%u, ofst=%u KiB,len= %u KiB, bdev_block_ofst=%lu,bdev_block_num=%lu \n",
    //     op_args->oid, op_args->ofst, op_args->len, bdev_ofst,bdev_len);

    int rc = spdk_bdev_read_blocks(cs->device.bdev_desc,
        cs->device.ioch, m->data_buffer,bdev_ofst,bdev_len,
        rw_cb, ctx);

    if(rc) {
        return OSTORE_IO_ERROR;
    }
    return OSTORE_SUBMIT_OK;
}

op_handler(write) {
    message_t *m = ctx;
    async_op_context_t *actx = ostore_async_ctx(ctx);
    actx->end_cb = cb;
    struct chunkstore_context_t* cs = get_local_store_ptr();
    op_write_t *op_args =(void*) m->meta_buffer;

    uint64_t bdev_ofst, bdev_len;
    _obj2blk(le32_to_cpu(op_args->oid), le32_to_cpu(op_args->ofst),le32_to_cpu(op_args->len),
        &bdev_ofst,&bdev_len);

    int rc = spdk_bdev_write_blocks(cs->device.bdev_desc,
        cs->device.ioch, m->data_buffer,bdev_ofst,bdev_len,
        rw_cb, ctx);
    
    if(rc) {
        return OSTORE_IO_ERROR;
    }
    // log_info("seq=%u,oid=%u, ofst=%u KiB,len= %u KiB, bdev_block_ofst=%lu,bdev_block_num=%lu submit OK\n",
    //     m->header.seq,
    //     op_args->oid, op_args->ofst, op_args->len, bdev_ofst,bdev_len);
    return OSTORE_SUBMIT_OK;
}



extern const int chunkstore_obj_async_op_context_size() {
    return sizeof(async_op_context_t);
}
static const op_handle_func_ptr_t obj_op_table[] = {
    [msg_oss_op_stat] = _do_stat,
    [msg_oss_op_create] = _do_create,
    [msg_oss_op_delete] = _do_delete,
    [msg_oss_op_write] = _do_write,
    [msg_oss_op_read] = _do_read,
};

extern int chunkstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    uint16_t op = message_get_op(request_msg_with_op_context);
    return (obj_op_table[op])(request_msg_with_op_context, _cb);
}
