#ifndef FAKESTORE_H
#define FAKESTORE_H

#include "common.h"

extern int fakestore_info(char *out , uint32_t len);
extern int fakestore_mkfs_async(const char* dev_list[], int flags, cb_func_t , void*);
extern int fakestore_mount_async(const char* dev_list[], /* size = 3*/  int flags /**/, cb_func_t , void*);
extern int fakestore_unmount_async(cb_func_t , void*);

extern int fakestore_mkfs(const char* dev_list[], int flags);
extern int fakestore_mount(const char* dev_list[], /* size = 3*/  int flags /**/);
extern int fakestore_unmount();

extern int fakestore_create(uint32_t oid , cb_func_t , void*);
extern int fakestore_delete(uint32_t oid , cb_func_t , void*);
extern int fakestore_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, cb_func_t , void*);
extern int fakestore_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, cb_func_t , void*);

#endif