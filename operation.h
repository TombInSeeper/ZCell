#ifndef OPERATION_H
#define OPERATION_H

#include "common.h"

//Meta filed
typedef struct op_create_t {
    _le32 oid;
} op_create_t;

typedef struct op_delete_t {
    _le32 oid;
} op_delete_t;

typedef struct op_read_t {
    _le32 oid;
    _le32 ofst;
    _le32 len;
    _le32 flags;
} op_read_t;

typedef struct op_write_t {
    _le32 oid;
    _le32 ofst;
    _le32 len;
    _le32 flags;
} op_write_t;

#endif