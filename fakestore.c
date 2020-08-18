#include "objectstore.h"

#include "spdk/event.h"
#include "spdk/env.h"


struct objectstore_ctx_t;

//Fake Store Type

const static uint32_t block_size = 4096; 

typedef struct onode_t {




} onode_t;



typedef struct ramdisk_t {

    void *base_addr;
    // 1GiB + 3 GiB

}radmdisk_t;



static void fake_async_cb_wrapper(void *cb  , void* cb_arg)
{
    obj_op_cb_t _cb =  cb;
    if(_cb) {
        _cb(cb_arg);
    }
}
static void fake_async_cb(obj_op_cb_t  cb , void * cb_arg)
{
    struct spdk_event *e = spdk_event_allocate( spdk_env_get_current_core(),
        fake_async_cb_wrapper , cb , cb_arg);
    spdk_event_call(e);
    return;
}

extern int async_mkfs(const char* dev_list[], int mkfs_flag, obj_op_cb_t  cb , void* cb_arg)
{
    // Do nothing

    fake_async_cb(cb , cb_arg);
    return 0;
}

extern int async_mount(const char* dev_list[], /* size = 3*/  int mount_flag /**/, obj_op_cb_t cb , void* cb_arg)
{
    // Do in-memory object table create



}

extern int async_unmount(obj_op_cb_t , void*);

extern int async_obj_list(uint32_t** oids, uint32_t*size);

extern int async_obj_create(uint32_t oid , obj_op_cb_t , void*);

extern int async_obj_delete(uint32_t oid , obj_op_cb_t , void*);

extern int async_obj_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, obj_op_cb_t , void*);

extern int async_obj_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, obj_op_cb_t , void*);


