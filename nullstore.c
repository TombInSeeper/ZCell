#include "nullstore.h"
#include "store_common.h"


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


extern int nullstore_stat(char *out , uint32_t len) {
    return OSTORE_EXECUTE_OK;
}

// extern int nullstore_mkfs(const char* dev_list[], int mkfs_flag, cb_func_t cb, void* cb_arg) {
//     fake_async_cb(cb,cb_arg);
//     return OSTORE_SUBMIT_OK;
// }
// extern int nullstore_mount(const char* dev_list[], /* size = 3*/  int mount_flag /**/, cb_func_t cb , void* cb_arg) {
//     fake_async_cb(cb,cb_arg);
//     return OSTORE_SUBMIT_OK;
// }
// extern int nullstore_unmount(cb_func_t cb , void* cb_arg) {
//     fake_async_cb(cb,cb_arg);
//     return OSTORE_SUBMIT_OK;
// }

extern int nullstore_mkfs(const char* dev_list[], int mkfs_flag) {
    return OSTORE_EXECUTE_OK;
}
extern int nullstore_mount(const char* dev_list[], /* size = 3*/  int mount_flag /**/) {
    return OSTORE_EXECUTE_OK;
}
extern int nullstore_unmount() {
    return OSTORE_EXECUTE_OK;
}


extern const int nullstore_obj_async_op_context_size() {
    return 16;
}

typedef struct async_ctx_t {
    uint8_t dummpy[16];
}async_ctx_t;


static int _do_create(void *req_ctx, cb_func_t cb) {
    (void)req_ctx;
    fake_async_cb(cb , req_ctx);
    return OSTORE_SUBMIT_OK;
}
static int _do_delete(void *req_ctx, cb_func_t cb) {
    (void)req_ctx;
    fake_async_cb(cb , req_ctx);
    return OSTORE_SUBMIT_OK;
}
static int _do_write(void *req_ctx, cb_func_t cb) {
    (void)req_ctx;
    fake_async_cb(cb , req_ctx);
    return OSTORE_SUBMIT_OK; 
}
static int _do_read(void *req_ctx, cb_func_t cb) {
    (void)req_ctx;
    fake_async_cb(cb , req_ctx);
    return OSTORE_SUBMIT_OK;
}
typedef int (*os_op_func_ptr_t)(void*, cb_func_t);
static const os_op_func_ptr_t obj_op_table[] = {
    [MSG_OSS_OP_CREATE] = _do_create,
    [MSG_OSS_OP_DELETE] = _do_delete,
    [MSG_OSS_OP_WRITE] = _do_write,
    [MSG_OSS_OP_READ] = _do_read,
};

extern int nullstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    message_t *request = request_msg_with_op_context;
    int op = le16_to_cpu(request->header.type);
    os_op_func_ptr_t op_func = obj_op_table[op];
    return op_func(request_msg_with_op_context , _cb);
}
