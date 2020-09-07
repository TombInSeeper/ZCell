#ifndef FAKESTORE_H
#define FAKESTORE_H

#include "common.h"

extern int fakestore_stat(char *out , uint32_t len);

extern int fakestore_mkfs(const char* dev_list[], int flags);
extern int fakestore_mount(const char* dev_list[], /* size = 3*/  int flags /**/);
extern int fakestore_unmount();

extern const int fakestore_obj_async_op_context_size();
extern int fakestore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);


#endif