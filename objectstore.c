#include "objectstore.h"
#include "fakestore.h"
#include "string.h"

static __thread objstore_impl_t fakestore = {
    .info = fakestore_info,
    .mkfs = fakestore_mkfs,
    .mount = fakestore_mount,
    .unmount = fakestore_unmount,    
    .mkfs_async = fakestore_mkfs_async,
    .mount_async = fakestore_mount_async,
    .unmount_async = fakestore_unmount_async,
    .obj_create = fakestore_create,
    .obj_delete = fakestore_delete,
    .obj_write = fakestore_write,
    .obj_read = fakestore_read
};

const objstore_impl_t* get_ostore_impl(int store_type) {
    if(store_type == FAKESTORE) {
        return &fakestore;
    } else  {
        return NULL;
    }
}
