#ifndef FAKESTORE_H
#define FAKESTORE_H

#include "common.h"

extern int fakestore_stat(char *out , uint32_t len);
extern int fakestore_mkfs_async(const char* dev_list[], int flags, cb_func_t , void*);
extern int fakestore_mount_async(const char* dev_list[], /* size = 3*/  int flags /**/, cb_func_t , void*);
extern int fakestore_unmount_async(cb_func_t , void*);

extern int fakestore_mkfs(const char* dev_list[], int flags);
extern int fakestore_mount(const char* dev_list[], /* size = 3*/  int flags /**/);
extern int fakestore_unmount();

extern const int fakestore_obj_async_op_context_size();
extern int fakestore_obj_async_op_call(void *request_msg_with_op_context, cb_func_t _cb);

extern int fakestore_obj_create(uint32_t oid , cb_func_t , void*);
extern int fakestore_obj_delete(uint32_t oid , cb_func_t , void*);
extern int fakestore_obj_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, cb_func_t , void*);
extern int fakestore_obj_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, cb_func_t , void*);

#endif