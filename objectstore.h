#ifndef  _OBJECTSTORE_H_
#define  _OBJECTSTORE_H_

#include  <stdint.h>

typedef void (*obj_op_cb_t) (void*);

struct objectstore_ctx_t;


extern int async_mkfs(const char* dev_list[], int mkfs_flag, obj_op_cb_t , void*);

extern int async_mount(const char* dev_list[], /* size = 3*/  int mount_flag /**/, obj_op_cb_t , void*);

extern int async_unmount(obj_op_cb_t , void*);

extern int async_obj_list(uint32_t** oids, uint32_t*size);

extern int async_obj_create(uint32_t oid , obj_op_cb_t , void*);

extern int async_obj_delete(uint32_t oid , obj_op_cb_t , void*);

extern int async_obj_read(uint32_t oid, uint64_t off, uint32_t len, void* rbuf, obj_op_cb_t , void*);

extern int async_obj_write(uint32_t oid, uint64_t off, uint32_t len, void* wbuf, obj_op_cb_t , void*);


#endif