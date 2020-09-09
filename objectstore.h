#ifndef  _OBJECTSTORE_H_
#define  _OBJECTSTORE_H_

#include  "common.h"





enum OBJECTSTORE_TYPE{
    FAKESTORE = 1,
    NULLSTORE,
    ZSTORE
};

typedef struct objstore_interface_t {  
    //通常不会使用异步的 mount 和 unmount , mkfs
    // int (*mkfs_async) (const char* dev_list[], int mkfs_flag, cb_func_t , void*);
    
    // //通常不会使用异步的 mount 和 unmount , mkfs
    // int (*mount_async)(const char* dev_list[], /* size = 3*/  int mount_flag /**/, cb_func_t , void*);
    
    // //Generally we don't use asynchronous mount, unmount, mkfs
    // int (*unmount_async)(cb_func_t , void*);
    int (*stat)(char *out, uint32_t len);
    int (*mkfs) (const char* dev_list[], int mkfs_flag);
    int (*mount)(const char* dev_list[], /* size = 3*/  int mount_flag /**/);
    int (*unmount)();

    const int (*obj_async_op_context_size)();


    //Enter of specific operation
    //see op_***_t in "operation.h"
    int (*obj_async_op_call)(void *request_msg_with_op_context, cb_func_t _cb);

    // int (*obj_create)(uint32_t oid , cb_func_t , void*);
    // int (*obj_delete)(uint32_t oid , cb_func_t , void*);
    // int (*obj_read)(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, cb_func_t , void*);
    // int (*obj_write)(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, cb_func_t , void*);
} objstore_interface_t ;

typedef objstore_interface_t objstore_impl_t;


extern const objstore_impl_t* ostore_get_impl( int store_type );

#endif