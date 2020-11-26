#ifndef OPERATION_H
#define OPERATION_H

#include "util/common.h"



//Operation 从消息的 meta_buffer 中解码出来
typedef struct op_create_t {
    _le64 oid;
} _packed op_create_t;

typedef struct op_delete_t {
    _le64 oid;
} _packed op_delete_t;

typedef struct op_read_t {
    _le64 oid;
    _le64 ofst;
    _le64 len;
    _le64 flags;
} _packed op_read_t;

typedef struct op_write_t {
    _le64 oid;
    _le64 ofst;
    _le64 len;
    _le64 flags;
} _packed  op_write_t;

//Result
typedef struct op_stat_result_t {
    _le32 type;
    _le32 capcity_gib;
    _le32 obj_blk_sz_kib;
    _le32 max_oid;
    _le32 max_obj_size_kib;
} _packed op_stat_result_t;

#endif