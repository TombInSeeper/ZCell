#ifndef MESSAGE_H
#define MESSAGE_H

#include "util/common.h"


#define NR_SESSION_MAX (10 * 1000)


enum message_type {
    msg_hdr = 0,
    msg_ping = 1, 
    msg_handshake = 2,
    msg_oss_op_min = 10,
    msg_oss_op_stat = 11,
    msg_oss_op_create = 12,
    msg_oss_op_read = 13,
    msg_oss_op_write = 14,
    msg_oss_op_delete = 15,
    msg_oss_op_max = 74,
};

#define MSG_TYPE_OSS(_op) ({typeof(_op) op = (_op); msg_oss_op_min < op && op < msg_oss_op_max; })


/**
 *  
 * 
 * request 格式：
 * hdr + meta_buffer (meta_buffer 存放具体的 rpc 参数) + data_buffer (如果有数据要传输，比如写操作)
 * 
 * response 格式：
 * hdr ( rpc id + 
 * 操作码 + 状态码) + data_buffer (保存 rpc 执行结果)
 * 
*/
typedef struct msg_hdr_t {
    _le64 seq;          //Seq number in one session
    _le16 type;         //Operation Type of this message
    _le16 status;       //Response Status Code, for "response"
    _le16 prio;         //For request
    _le16 meta_length;  //MAX 64K
    _le32 data_length;  //Max 4GB
    _le32 crc_meta; 
    union {
        struct {
            _u8 from; 
            _u8 to;
            _le16 pad;
        }_ipc_rsv;
    }; 
    _le32 rsv[1]; // reserve for some special using 
} _packed msg_hdr_t;


typedef struct message_state_object_t {
    uint16_t hdr_rem_len;
    uint16_t meta_rem_len;
    uint32_t data_rem_len;
}message_state_object_t;

typedef struct message_t {
    message_state_object_t state; 

    msg_hdr_t header; //被传输的内容
    char *meta_buffer; //被传输的内容
    char *data_buffer; //被传输的内容

    union {
        void *priv_ctx;  //被 network_server 使用，记录属于哪个 socket session
        // 被 ipc_client 使用，记录前一级别的上下文
    };
} message_t;

#define message_get_ctx(m) ((((message_t*)(m))->priv_ctx))
#define message_get_user_data(m) ((((message_t*)(m))->user_data_))
#define message_get_seq(m) (le64_to_cpu((((message_t*)(m))->header.seq)))
#define message_get_op(m) (le16_to_cpu((((message_t*)(m))->header.type)))
#define message_get_status(m) (le16_to_cpu((((message_t*)(m))->header.status)))
#define message_get_prio(m) (le16_to_cpu((((message_t*)(m))->header.prio)))
#define message_get_meta_crc(m) (le32_to_cpu((((message_t*)(m))->header.crc_meta)))
#define message_get_meta_len(m) (le16_to_cpu((((message_t*)(m))->header.meta_length)))
#define message_get_data_len(m) (le32_to_cpu((((message_t*)(m))->header.data_length)))
#define message_get_rsv(m,i) (le32_to_cpu((((message_t*)(m))->header.rsv[(i)])))
#define message_get_meta_buffer(m) ((void*)((((message_t*)(m))->meta_buffer)))
#define message_get_data_buffer(m) ((void*)((((message_t*)(m))->data_buffer)))




static void inline message_move(message_t *dst ,  message_t *src) {
    memcpy(dst,src,sizeof(*src));
    memset(src,0,sizeof(*src));
}

static inline void* message_claim_meta(message_t *src) {
    void *meta = src->meta_buffer;
    src->meta_buffer = NULL;
    return meta;
} 

static inline void* message_claim_data(message_t *src) {
    void *data = src->data_buffer;
    src->data_buffer = NULL;
    return data;
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