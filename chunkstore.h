#ifndef CHUNKSTORE_H
#define CHUNKSTORE_H

#include "common.h"

extern int chunkstore_stat(char *out , uint32_t len);
extern int chunkstore_mkfs(const char* dev_list[], int flags);
extern int chunklstore_mount(const char* dev_list[], /* size = 3*/  int flags /**/);
extern int chunkstore_unmount();

extern const int chunkstore_obj_async_op_context_size();
extern int chunkstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);

#endif