#include "objectstore.h"
#include "fakestore.h"

#include "string.h"

static __thread int _store_type;
// static __thread const char* store_type_str[] = {
//     "",
//     "FAKE_STORE",
//     "ZSTORE",
// };

static __thread objstore_interface_t oif;

objstore_interface_t* obj_if_construct(int store_type)
{
    if(store_type == FAKESTORE) {
        objstore_interface_t _oif = {
            .info = fakestore_info,
            .mkfs = fakestore_mkfs,
            .mount = fakestore_mount,
            .unmount = fakestore_unmount,
            .obj_create = fakestore_create,
            .obj_delete = fakestore_delete,
            .obj_write = fakestore_write,
            .obj_read = fakestore_read
        };
        memcpy(&oif,&_oif,sizeof(_oif));
        _store_type = store_type;
        return &oif;
    } else  {
        return NULL;
    }
}

void obj_if_destruct(objstore_interface_t * _oif)
{
    return;
}

