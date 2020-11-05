#ifndef ZSTORE_H
#define ZSTORE_H

#include "util/common.h"

extern int zstore_mkfs(const char *dev_list[], int flags);

extern int zstore_mount(const char *dev_list[], /* size = 3*/  int flags /**/);

extern int zstore_unmount();

extern const int zstore_obj_async_op_context_size();

extern int zstore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);


#endif