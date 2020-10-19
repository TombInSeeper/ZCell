#ifndef  _OBJECTSTORE_H_
#define  _OBJECTSTORE_H_

#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "message.h"
#include "operation.h"

#define ostore_rqst(req) ((void*)(req))
#define ostore_async_ctx(req) ((void*)( (char*)((req)) + sizeof(message_t)))
typedef int (*op_handle_func_ptr_t) (void* rqst_ctx , cb_func_t cb);


enum OBJECTSTORE_TYPE{
 
    NULLSTORE,
    CHUNKSTORE,
    ZSTORE,
};

typedef struct objstore_interface_t {  
    // int (*stat)(char *out, uint32_t len);
    //通常不会使用异步的 mount 和 unmount , mkfs
    // int (*mkfs) (const char* dev_list[], int mkfs_flag, cb_func_t , void*);
    // int (*mount)(const char* dev_list[], /* size = 3*/  int mount_flag /**/, cb_func_t , void*);
    // int (*unmount)(cb_func_t , void*);

    int (*mkfs) (const char* dev_list[], int mkfs_flag);
    int (*mount)(const char* dev_list[], /* size = 3*/  int mount_flag /**/);
    int (*unmount)();
    const int (*obj_async_op_context_size)();
    //Enter of specific operation
    //see op_***_t in "operation.h"
    int (*obj_async_op_call)(void *request_msg_with_op_context, cb_func_t _cb);

    //Private API
} objstore_interface_t ;

typedef objstore_interface_t objstore_impl_t;       
      
extern const objstore_impl_t* ostore_get_impl( int store_type );

#endif