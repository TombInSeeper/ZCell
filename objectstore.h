#ifndef  _OBJECTSTORE_H_
#define  _OBJECTSTORE_H_

#include  "common.h"


enum OBJECTSTORE_TYPE{
    FAKESTORE = 1,
    ZSTORE = 2
};

typedef struct objstore_interface_t {
    int (*info)(char *out, uint32_t len);
    int (*mkfs) (const char* dev_list[], int store_type, cb_func_t , void*);
    int (*mount)(const char* dev_list[], /* size = 3*/  int store_type /**/, cb_func_t , void*);
    int (*unmount)(cb_func_t , void*);
    int (*obj_create)(uint32_t oid , cb_func_t , void*);
    int (*obj_delete)(uint32_t oid , cb_func_t , void*);
    int (*obj_read)(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, cb_func_t , void*);
    int (*obj_write)(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, cb_func_t , void*);
} objstore_interface_t ;


extern objstore_interface_t *obj_if_construct( int store_type );
extern void obj_if_destruct( objstore_interface_t *);

#endif