#ifndef _MSG_H_
#define _MSG_H_


#define OP_TEST 0x0





#define OPFLAG_HAS_PAYLOAD 0x1

#include <stdint.h>

struct request_hdr_t {
    uint32_t op_seq;   
    uint16_t op_id;
    uint16_t op_flag;
    uint32_t coll_id;
    uint64_t obj_id;
    uint64_t obj_offset;
    uint32_t payload_length;
    uint64_t rsv[3];
    // char pad[4096 - 64];
};

// struct response_hdr_t {
//     uint32_t  op_seq;
//     uint16_t  op_status;
//     uint32_t  op_payload_length;
// };

struct message {
    void*       cli_priv;
    
    uint8_t     rwstate;  
    uint32_t    rw_len;
    union {
        struct request_hdr_t oph;
        char bhdr[sizeof(struct request_hdr_t)];
    } hdr;
    //
    void*  payload;
};

#endif