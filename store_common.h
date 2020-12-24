#ifndef STORE_COMMON_H
#define STORE_COMMON_H
#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "message.h"
#include "operation.h"

#define ZSTORE_OVERWRITE  (1)
#define ZSTORE_ENBALE_TRIM (1 << 1)
#define ZSTORE_MKFS_RESERVE_OBJ_NR (512*512)
/**
 * 预留(直接创建) N 个 Object ID  
 */
#define ZSTORE_MKFS_RESERVE_OBJID (1 << 2)


#define ostore_rqst(req) ((void*)(req))

#define ostore_async_ctx(req) ((void*)( (char*)((req)) + sizeof(message_t)))

typedef int (*op_handle_func_ptr_t) (void* rqst_ctx , cb_func_t cb);

#endif