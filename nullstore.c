#include "nullstore.h"
#include "store_common.h"
#include "util/errcode.h"

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




typedef struct async_ctx_t {
    uint8_t dummpy[16];
}async_ctx_t;



static int _do_stat(void *req_ctx, cb_func_t cb) {
    (void)req_ctx;
    fake_async_cb(cb , req_ctx);
    return OSTORE_SUBMIT_OK;
}

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

static const op_handle_func_ptr_t obj_op_table[] = {
    [msg_oss_op_stat] = _do_stat,
    [msg_oss_op_create] = _do_create,
    [msg_oss_op_delete] = _do_delete,
    [msg_oss_op_write] = _do_write,
    [msg_oss_op_read] = _do_read,
};

extern const int nullstore_obj_async_op_context_size() {
    return sizeof(async_ctx_t);
}

extern int nullstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb) {
    uint16_t op = message_get_op(request_msg_with_op_context);
    return (obj_op_table[op])(request_msg_with_op_context, _cb);
}
