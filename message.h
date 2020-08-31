#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

// #define MSGR_DEBUG 
#define MSGR_DEBUG_LEVEL 10
#define NR_SESSION_MAX (10 * 1000)

enum SOCK_STATUS {
    SOCK_RWOK,
    SOCK_ENOMSG,
    SOCK_EAGAIN,    
    SOCK_NEED_CLOSE,
};


enum MessageType {
    MSG_HDR = 0,
    MSG_PING = 1, 
    MSG_OSS_OP_NULL = 10,
    MSG_OSS_OP_CREATE,
    MSG_OSS_OP_READ,
    MSG_OSS_OP_WRITE,
    MSG_OSS_OP_DELETE,
};

#define _msg_filed_get_set(msg_type, filed , filed_type) \
    static inline filed_type msg_type ## _get_ ## filed ( msg_type * m) { \
        return (m->filed); \
    }; \
    static inline void msg_type ## _set_ ## filed ( msg_type * m , filed_type var) { \
        (m->filed) = (var); \
    }

/**

 * 

*/
typedef struct msg_hdr_t {
    _le64 seq;  //unique seq in a session
    _le16 type; // message type
    _le16 prio; //
    _le32 meta_length;  //
    _le32 data_length;
} msg_hdr_t;

// static inline uint64_t msg_hdr_get_seq(msg_hdr_t *hdr) {
//     return le64_to_cpu(hdr->seq);
// }
// _msg_filed_get_set(msg_hdr_t, seq , _le64);
// _msg_filed_get_set(msg_hdr_t, type , _le16);
// _msg_filed_get_set(msg_hdr_t, prio , _le16);
// _msg_filed_get_set(msg_hdr_t, meta_length , _le32);
// _msg_filed_get_set(msg_hdr_t, data_length , _le32);


typedef struct message_state_object_t {
    uint32_t hdr_rem_len;
    uint32_t meta_rem_len;
    uint32_t data_rem_len;
}message_state_object_t;

typedef struct message_t {
    message_state_object_t state;
    struct {
        msg_hdr_t header;
        char *meta_buffer;
        char *data_buffer;
        ///transparent ptr to privous context
        ///e.g. session
        ///Requst and  Response must be the same one 
        void *priv_ctx;
    };
    
} message_t;

static void inline message_move(message_t *dst , message_t *src) {
    memcpy(dst,src,sizeof(*src));
    memset(src,0,sizeof(*src));
}

static void inline message_state_reset(message_t *m) {
    m->state.hdr_rem_len = sizeof(m->header);
    m->state.meta_rem_len = m->header.meta_length;
    m->state.data_rem_len = m->header.data_length;
}

#endif