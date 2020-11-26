#ifndef STORE_COMMON_H
#define STORE_COMMON_H
#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "message.h"
#include "operation.h"

#define ostore_rqst(req) ((void*)(req))

#define ostore_async_ctx(req) ((void*)( (char*)((req)) + sizeof(message_t)))

typedef int (*op_handle_func_ptr_t) (void* rqst_ctx , cb_func_t cb);

#endif