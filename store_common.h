#ifndef STORE_COMMON_H
#define STORE_COMMON_H

#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/env.h"


#include "message.h"
#include "operation.h"
#include "fixed_cache.h"

#define ostore_async_ctx(req) ((void*)( (char*)((req)) + sizeof(message_t)))


#endif