#include "objectstore.h"
#include "nullstore.h"
#include "chunkstore.h"
#include "string.h"


const static __thread objstore_impl_t chunkstore = {
    .stat = chunkstore_stat,
    .mkfs = chunkstore_mkfs,
    .mount = chunkstore_mount,
    .unmount = chunkstore_unmount,    
    // .mkfs_async = fakestore_mkfs_async,
    // .mount_async = fakestore_mount_async,
    // .unmount_async = fakestore_unmount_async,
    // .obj_create = fakestore_obj_create,
    // .obj_delete = fakestore_obj_delete,
    // .obj_write = fakestore_obj_write,
    // .obj_read = fakestore_obj_read,
    .obj_async_op_context_size = chunkstore_obj_async_op_context_size,
    .obj_async_op_call = chunkstore_obj_async_op_call
};
const static __thread objstore_impl_t nullstore = {
    .stat = nullstore_stat,
    .mkfs = nullstore_mkfs,
    .mount = nullstore_mount,
    .unmount = nullstore_unmount,    
    // .mkfs_async = fakestore_mkfs_async,
    // .mount_async = fakestore_mount_async,
    // .unmount_async = fakestore_unmount_async,
    // .obj_create = fakestore_obj_create,
    // .obj_delete = fakestore_obj_delete,
    // .obj_write = fakestore_obj_write,
    // .obj_read = fakestore_obj_read,
    .obj_async_op_context_size = nullstore_obj_async_op_context_size,
    .obj_async_op_call = nullstore_obj_async_op_call
};

extern const objstore_impl_t* ostore_get_impl(int store_type) {
    if(store_type == NULLSTORE) {
        return &nullstore;
    }
    return NULL;
}
