#ifndef OPERATION_H
#define OPERATION_H

#include "common.h"



// static inline const char* opcode_str(int op_code) {
//     static const char* opcode_strs[] = {
//         [MSG_OSS_OP_CREATE] = "MSG_OSS_OP_CREATE",
//         [MSG_OSS_OP_DELETE] = "MSG_OSS_OP_DELETE",
//         [MSG_OSS_OP_WRITE] = "MSG_OSS_OP_WRITE",
//         [MSG_OSS_OP_READ] = "MSG_OSS_OP_READ",
//         [MSG_OSS_OP_CREATE] = "MSG_OSS_OP_CREATE",
//     }
// }


//Operation 从消息的 meta_buffer 中解码出来
typedef struct op_create_t {
    _le32 oid;
} _packed op_create_t;

typedef struct op_delete_t {
    _le32 oid;
} _packed op_delete_t;

typedef struct op_read_t {
    _le32 oid;
    _le32 ofst;
    _le32 len;
    _le32 flags;
} _packed op_read_t;

typedef struct op_write_t {
    _le32 oid;
    _le32 ofst;
    _le32 len;
    _le32 flags;
} _packed op_write_t;

//Result
typedef struct op_stat_t {
    _le32 type;
    _le32 capcity_mib;
    _le32 obj_blk_sz_kib;
    _le32 max_oid;
    _le32 max_obj_size_kib;
} _packed op_stat_t;

#endif