#include "objectstore.h"
#include "nullstore.h"
#include "chunkstore.h"
#include "string.h"



#define REGISTER_OSTORE(idx, name) \ 
    [idx] = {\
    .stat = name ## _stat,\
    .mkfs = name ## _mkfs,\
    .mount = name ## _mount,\
    .unmount = name ## _unmount,\    
    .obj_async_op_context_size = name ## _obj_async_op_context_size,\
    .obj_async_op_call =  name ## _obj_async_op_call\
}

const static objstore_impl_t impls[] = {
    REGISTER_OSTORE(NULLSTORE,nullstore),
    REGISTER_OSTORE(CHUNKSTORE,chunkstore),
};

static inline bool support_ostore(int store_type) {
    return store_type == NULLSTORE ||
        store_type == CHUNKSTORE ;
}

extern const objstore_impl_t* ostore_get_impl(int store_type) {
    if(support_ostore(store_type))
        return &impls[store_type];
    return NULL;
}
