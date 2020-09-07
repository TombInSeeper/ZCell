#include "objectstore.h"
#include "fakestore.h"
#include "fixed_cache.h"

#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/json.h"
//Fake Store Type

const static uint32_t block_size = 4096; 
const static uint32_t data_dev_size =  128 << 10;
const static uint32_t node_dev_size =  10 * 10000; 
const static uint32_t onode_max = 10 * 10000;

typedef struct onode_t {
    uint32_t _data[1024];
} onode_t;

enum FAKESTORE_STATE {
    booting,
    running,
    stopping,
    died,
};

typedef struct fakestore_t {

    int state;

    fcache_t *data_cache;
    fcache_t *node_cache;
    onode_t **onodes;
} fakestore_t;

static __thread fakestore_t fakestore;

static inline fakestore_t* fakestore_ptr() {
    return &fakestore;
}

static void fake_async_cb_wrapper(void *cb  , void* cb_arg)
{
    cb_func_t _cb =  cb;
    if(_cb) {
        _cb(cb_arg, OSTORE_EXECUTE_OK);
    }
}
static void fake_async_cb(cb_func_t  cb , void * cb_arg)
{
    struct spdk_event *e = spdk_event_allocate( spdk_env_get_current_core(),
        fake_async_cb_wrapper , cb , cb_arg);
    spdk_event_call(e);
    return;
}

extern int fakestore_stat(char *out , uint32_t len)
{
    fakestore_t *fs = fakestore_ptr();
    if ( fs->state == running ) {
        snprintf(out, len, "[maxoid=%u,max_obj_size=%s,free_nodes:%u, free_blocks:%u]",
            onode_max, "4MiB", node_dev_size , data_dev_size );
    }
    return OSTORE_EXECUTE_OK;
}
extern int fakestore_mkfs(const char* dev_list[], int flags) {
    return OSTORE_EXECUTE_OK;
}
extern int fakestore_mount(const char* dev_list[], /* size = 3*/  int flags /**/) {
    // Do in-memory object table create
    fakestore_t *fc = fakestore_ptr();
    fc->state = booting;
    fc->onodes = malloc(sizeof(onode_t*) * onode_max);
    assert(fc->onodes);
    fc->data_cache = fcache_constructor(data_dev_size, block_size, MALLOC);
    assert(fc->data_cache);
    
    fc->node_cache = fcache_constructor(node_dev_size, block_size, MALLOC);
    assert(fc->node_cache);
    
    fc->state = running;
    return OSTORE_EXECUTE_OK;
}

extern int fakestore_unmount() {
    fakestore_t *fc = fakestore_ptr();
    SPDK_NOTICELOG("start\n");

    fc->state= stopping;
    SPDK_NOTICELOG("node_cache\n");

    fcache_destructor(fc->node_cache);
    SPDK_NOTICELOG("data_cache\n");

    fcache_destructor(fc->data_cache);

    SPDK_NOTICELOG("onodes\n");
    free(fc->onodes);
    fc->state= died;
    SPDK_NOTICELOG("done\n");
    return OSTORE_EXECUTE_OK;
}

extern int fakestore_mkfs_async (const char* dev_list[], int mkfs_flag, cb_func_t  cb , void* cb_arg)
{
    // Do nothing
    fake_async_cb(cb , cb_arg);
    return OSTORE_SUBMIT_OK;
}

extern int fakestore_mount_async(const char* dev_list[], /* size = 3*/  int mount_flag /**/, cb_func_t cb , void* cb_arg)
{
    // Do in-memory object table create
    fakestore_mount(dev_list, mount_flag);
    fake_async_cb(cb,cb_arg);
    return OSTORE_SUBMIT_OK;
}

extern int fakestore_unmount_async(cb_func_t cb, void* cb_arg)
{
    fakestore_unmount();
    fake_async_cb(cb,cb_arg);
    return OSTORE_SUBMIT_OK;
}




extern int fakestore_obj_create(uint32_t oid , cb_func_t cb , void* cb_arg)
{
    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (o) {
        return OSTORE_OBJECT_EXIST;
    }
    onode_t *new_o = fcache_get(fs->node_cache);
    if(!new_o) {
        return OSTORE_NO_NODE;
    }

    memset(new_o, 0xff ,sizeof(onode_t));

    fs->onodes[oid] = new_o;

    fake_async_cb(cb, cb_arg);
    return OSTORE_SUBMIT_OK;
}

extern int fakestore_obj_delete(uint32_t oid , cb_func_t cb , void* cb_arg)
{
    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }

    //Give back data cache
    for ( int i = 0 ; i < 1024 ; ++i) {
        fcache_id_put(fs->data_cache, o->_data[i]);
    }

    //...
    // Onode put back
    fcache_put(fs->node_cache, o);

    fs->onodes[oid] = NULL;

    // memset(new_o, 0 ,sizeof(onode_t));

    fake_async_cb(cb, cb_arg);
    return OSTORE_SUBMIT_OK;
}

extern int fakestore_obj_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, cb_func_t cb , void* cb_arg)
{
    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }

    uint32_t soff = off / 0x1000;
    uint32_t slen = len / 0x1000;

    int i;
    for ( i = soff ; i < soff + slen ; ++i) {
        if( i >= 1024) {
            return OSTORE_READ_EOF;
        }
        if ( o->_data[i] == 0xffffffff) {
            memset((char*)rbuf + i * 0x1000, 0 , 0x1000);      
        } else {
            void *data_page = fcahe_id_elem(fs->data_cache , o->_data[i]);
            memcpy((char*)rbuf + i * 0x1000, data_page , 0x1000);
        }
    }
    fake_async_cb(cb , cb_arg);
    return OSTORE_SUBMIT_OK;
}

extern int fakestore_obj_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, cb_func_t cb, void* cb_arg)
{
    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }
    uint32_t soff = off / 0x1000;
    uint32_t slen = len / 0x1000;

    int i;
    for ( i = soff ; i < soff + slen ; ++i) {
        if( i >= 1024) {
            return OSTORE_WRITE_OUT_MAX_SIZE;
        }
        void *this_page;
        if (o->_data[i] == 0xffffffff) {
            void *data_page = fcache_get(fs->data_cache);
            if(!data_page) {
                return OSTORE_NO_SPACE;
            }
            this_page = data_page;
            o->_data[i] = fcache_elem_id(fs->data_cache,data_page);
        } else {
            this_page = fcahe_id_elem(fs->data_cache , o->_data[i]);
        }
        memcpy(this_page ,(char*)wbuf + i * 0x1000, 0x1000);
    }
    fake_async_cb(cb , cb_arg);
    return OSTORE_SUBMIT_OK;
}


typedef struct async_state_t {
    int st;
    int err;
    int rsv[2];
} async_state_t;

extern const int fakestore_obj_async_op_context_size() {
    return sizeof(async_state_t);
}

static int _do_write(void *request_msg_with_op_context, cb_func_t cb) {
    message_t *request = request_msg_with_op_context;
    async_state_t *st = (void*)(request + 1);    
    (void)st;
    op_write_t* op = (void*)request->meta_buffer;
    uint32_t oid = le32_to_cpu(op->oid);
    uint32_t off = le32_to_cpu(op->ofst);
    uint32_t len = le32_to_cpu(op->len);
    void *wbuf = request->data_buffer;

    void *cb_arg = request_msg_with_op_context;

    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }
    uint32_t soff = off / 0x1000;
    uint32_t slen = len / 0x1000;

    int i;
    for ( i = soff ; i < soff + slen ; ++i) {
        if( i >= 1024) {
            return OSTORE_WRITE_OUT_MAX_SIZE;
        }
        void *this_page;
        if (o->_data[i] == 0xffffffff) {
            void *data_page = fcache_get(fs->data_cache);
            if(!data_page) {
                return OSTORE_NO_SPACE;
            }
            this_page = data_page;
            o->_data[i] = fcache_elem_id(fs->data_cache,data_page);
        } else {
            this_page = fcahe_id_elem(fs->data_cache , o->_data[i]);
        }
        memcpy(this_page ,(char*)wbuf + i * 0x1000, 0x1000);
    }
    fake_async_cb(cb , cb_arg);
    return OSTORE_SUBMIT_OK; 
}
static int _do_read(void *request_msg_with_op_context, cb_func_t cb) {
    message_t *request = request_msg_with_op_context;
    async_state_t *st = (void*)(request + 1);    
    (void)st;
    
    op_read_t* op = (void*)request->meta_buffer;
    uint32_t oid = le32_to_cpu(op->oid);
    uint32_t off = le32_to_cpu(op->ofst);
    uint32_t len = le32_to_cpu(op->len);
    void *rbuf = request->data_buffer;

    void *cb_arg = request_msg_with_op_context;

    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }

    uint32_t soff = off / 0x1000;
    uint32_t slen = len / 0x1000;

    int i;
    for ( i = soff ; i < soff + slen ; ++i) {
        if( i >= 1024) {
            return OSTORE_READ_EOF;
        }
        if ( o->_data[i] == 0xffffffff) {
            memset((char*)rbuf + i * 0x1000, 0 , 0x1000);      
        } else {
            void *data_page = fcahe_id_elem(fs->data_cache , o->_data[i]);
            memcpy((char*)rbuf + i * 0x1000, data_page , 0x1000);
        }
    }
    fake_async_cb(cb , cb_arg);
    return OSTORE_SUBMIT_OK;
}
static int _do_create(void *request_msg_with_op_context, cb_func_t cb) {
    message_t *request = request_msg_with_op_context;
    async_state_t *st = (void*)(request + 1); 
    (void)st;
    op_create_t *op = (void*)request->meta_buffer;
    uint32_t oid = le32_to_cpu(op->oid);
    
    void *cb_arg = request_msg_with_op_context;

    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (o) {
        return OSTORE_OBJECT_EXIST;
    }
    onode_t *new_o = fcache_get(fs->node_cache);
    if(!new_o) {
        return OSTORE_NO_NODE;
    }

    memset(new_o, 0xff ,sizeof(onode_t));

    fs->onodes[oid] = new_o;

    fake_async_cb(cb, cb_arg);
    return OSTORE_SUBMIT_OK; 
}
static int _do_delete(void *request_msg_with_op_context, cb_func_t cb) {
    message_t *request = request_msg_with_op_context;
    async_state_t *st = (void*)(request + 1); 
    (void)st;
    op_create_t *op = (void*)request->meta_buffer;
    uint32_t oid = le32_to_cpu(op->oid);
    
    void *cb_arg = request_msg_with_op_context;
    fakestore_t *fs = fakestore_ptr();
    onode_t *o = fs->onodes[oid];
    if (!o) {
        return OSTORE_OBJECT_NOT_EXIST;
    }

    //Give back data cache
    for ( int i = 0 ; i < 1024 ; ++i) {
        fcache_id_put(fs->data_cache, o->_data[i]);
    }

    //...
    // Onode put back
    fcache_put(fs->node_cache, o);

    fs->onodes[oid] = NULL;

    // memset(new_o, 0 ,sizeof(onode_t));

    fake_async_cb(cb, cb_arg);
    return OSTORE_SUBMIT_OK;
}

typedef int (*os_op_func_ptr_t)(void*, cb_func_t);
const os_op_func_ptr_t obj_op_table[] = {
    [MSG_OSS_OP_CREATE] = _do_create,
    [MSG_OSS_OP_DELETE] = _do_delete,
    [MSG_OSS_OP_WRITE] = _do_write,
    [MSG_OSS_OP_READ] = _do_read,
};

extern int fakestore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    message_t *request = request_msg_with_op_context;
    int op = le16_to_cpu(request->header.type);
    os_op_func_ptr_t op_func = obj_op_table[op];
    return op_func(request_msg_with_op_context , _cb);
}


