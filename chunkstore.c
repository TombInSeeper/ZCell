#include "chunkstore.h"
#include "spdk/bdev.h"
#include "spdk/log.h"
#include "store_common.h"

#define op_handler(name) static int _do_ ## name ( void* ctx, cb_func_t cb) 

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
        uint32_t data_gb;
        uint32_t max_oid;
        uint32_t max_obj_sz;
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
      	SPDK_ERRLOG("Could not open a bdev %s,\n" , name);
		spdk_app_stop(-1);
		return;  
    }

    SPDK_NOTICELOG("Open bdev %s OK\n" , name);

    cs->device.ioch = spdk_bdev_get_io_channel(cs->device.bdev_desc);
    if(!cs->device.ioch) {
       	SPDK_ERRLOG("Could not spdk_bdev_get_io_channel %s,\n" , name);
		spdk_app_stop(-1);
		return;         
    }

    SPDK_NOTICELOG("Get io channel %s OK\n" , name);
}
static void _hardcode_stat() {
    struct chunkstore_context_t *cs = get_local_store_ptr();
    cs->stat.data_gb = 100;
    cs->stat.max_obj_sz = 4 << 20;
    cs->stat.max_oid = 25 << 10;
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


op_handler(state) {
    message_t *m = ctx;
    // async_op_context_t *actx = ostore_async_ctx(m);
    op_stat_t *stat =(void*)m->meta_buffer;
    stat->max_obj_size_kib = cpu_to_le32(0x1000);
    stat->capcity_mib = cpu_to_le32(100 * 1024);
    stat->max_oid = cpu_to_le32(100000);
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
    async_op_context_t *actx = ostore_async_ctx(cb_arg);
    if(success) {
        actx->err = OSTORE_EXECUTE_OK;
    } else {
        actx->err = OSTORE_IO_ERROR;
    }
    actx->end_cb(cb_arg, actx->err);
    spdk_bdev_free_io(bdev_io);
}

op_handler(read) {
    message_t *m = ctx;
    async_op_context_t *actx = ostore_async_ctx(ctx);
    actx->end_cb = cb;
    struct chunkstore_context_t* cs = get_local_store_ptr();
    op_read_t *op_args = (void*) m->meta_buffer;
    uint64_t bdev_ofst = (op_args->oid * (1024)) + (op_args->ofst >> 12);
    uint64_t bdev_len = op_args->len >> 12;

    SPDK_NOTICELOG("oid=%u, ofst=%u KiB,len= %u KiB, bdev_block_ofst=%lu,bdev_block_num=%lu \n",
        op_args->oid, op_args->ofst, op_args->len, bdev_ofst,bdev_len);

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

    uint64_t bdev_ofst = (op_args->oid * (1024)) + (op_args->ofst >> 12);
    uint64_t bdev_len = op_args->len >> 12;

    SPDK_NOTICELOG("oid=%u, ofst=%u KiB,len= %u KiB, bdev_block_ofst=%lu,bdev_block_num=%lu \n",
        op_args->oid, op_args->ofst, op_args->len, bdev_ofst,bdev_len);

    int rc = spdk_bdev_write_blocks(cs->device.bdev_desc,
        cs->device.ioch, m->data_buffer,bdev_ofst,bdev_len,
        rw_cb, ctx);
    
    if(rc) {
        return OSTORE_IO_ERROR;
    }
    return OSTORE_SUBMIT_OK;
}

typedef int (*os_op_func_ptr_t)(void*, cb_func_t);
static const os_op_func_ptr_t obj_op_table[] = {
    [MSG_OSS_OP_STATE] = _do_state,
    [MSG_OSS_OP_CREATE] = _do_create,
    [MSG_OSS_OP_DELETE] = _do_delete,
    [MSG_OSS_OP_WRITE] = _do_write,
    [MSG_OSS_OP_READ] = _do_read,
};

extern const int chunkstore_obj_async_op_context_size() {
    return sizeof(async_op_context_t);
}

extern int chunkstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    uint16_t op = message_get_op(request_msg_with_op_context);
    return (obj_op_table[op])(request_msg_with_op_context, _cb);
}
