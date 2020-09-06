#ifndef OPERATION_H
#define OPERATION_H

#include "common.h"





//Operation 从消息的 meta_buffer 中解码出来
typedef struct op_create_t {
    uint32_t oid;
} op_create_t;

typedef struct op_delete_t {
    uint32_t oid;
} op_delete_t;

typedef struct op_read_t {
    uint32_t oid;
    uint32_t ofst;
    uint32_t len;
    uint32_t flags;
} op_read_t;

typedef struct op_write_t {
    uint32_t oid;
    uint32_t ofst;
    uint32_t len;
    uint32_t flags;
} op_write_t;

#endif