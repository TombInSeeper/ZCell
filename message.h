#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

#define MSGR_DEBUG 
#define MSGR_DEBUG_LEVEL 1
#define NR_SESSION_MAX (10 * 1000)



enum MessageType {
    MSG_HDR = 0,
    MSG_PING = 1, 
    MSG_OSS_OP_MIN = 10,
    MSG_OSS_OP_STATE = 11,
    MSG_OSS_OP_CREATE = 12,
    MSG_OSS_OP_READ = 13,
    MSG_OSS_OP_WRITE = 14,
    MSG_OSS_OP_DELETE = 15,
    MGS_OSS_OP_MAX = 74,

};

#define MSG_TYPE_OSS(_op) ({typeof(_op) op = (_op); MSG_OSS_OP_MIN < op && op < MGS_OSS_OP_MAX; })


/**

 * 

*/
typedef struct msg_hdr_t {
    _le32 seq;  //sequntial number in onesession
    union {
        _le16 type; // Operation type , for "request"
        _le16 status; //Status Code, for "response"
    };
    _le16 meta_length;  //MAX 64K
    _le32 data_length;
} msg_hdr_t;

typedef struct message_state_object_t {
    uint16_t hdr_rem_len;
    uint16_t meta_rem_len;
    uint32_t data_rem_len;
}message_state_object_t;

typedef struct message_t {
    message_state_object_t state;
    struct {
        msg_hdr_t header;
        char *meta_buffer;
        char *data_buffer;
        void *priv_ctx;  //指针：属于哪个 session
    };   
} message_t;

static void inline message_move(message_t *dst ,  message_t *src) {
    memcpy(dst,src,sizeof(*src));
    memset(src,0,sizeof(*src));
}

static uint32_t inline message_len(const message_t *m) {
    uint32_t len = sizeof(m->header) + 
        le16_to_cpu(m->header.meta_length) +
        le32_to_cpu(m->header.data_length);
    return len;   
}

static void inline message_state_reset(message_t *m) {
    m->state.hdr_rem_len = sizeof(m->header);
    m->state.meta_rem_len = le16_to_cpu(m->header.meta_length);
    m->state.data_rem_len = le32_to_cpu(m->header.data_length);
}

#endif