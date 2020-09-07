#include "objectstore.h"
#include "fakestore.h"
#include "string.h"


static __thread objstore_impl_t fakestore = {
    .stat = fakestore_stat,
    .mkfs = fakestore_mkfs,
    .mount = fakestore_mount,
    .unmount = fakestore_unmount,    
    // .mkfs_async = fakestore_mkfs_async,
    // .mount_async = fakestore_mount_async,
    // .unmount_async = fakestore_unmount_async,
    // .obj_create = fakestore_obj_create,
    // .obj_delete = fakestore_obj_delete,
    // .obj_write = fakestore_obj_write,
    // .obj_read = fakestore_obj_read,
    .obj_async_op_context_size = fakestore_obj_async_op_context_size,
    .obj_async_op_call = fakestore_obj_async_op_call
};

const objstore_impl_t* get_ostore_impl(int store_type) {
    if(store_type == FAKESTORE) {
        return &fakestore;
    } else  {
        return NULL;
    }
}
