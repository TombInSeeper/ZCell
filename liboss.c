#include "liboss.h"
#include "messager.h"
#include "util/fixed_cache.h"
#include "util/log.h"

#define RING_MAX 1024
#define POLL_MAX 64

typedef struct op_ctx_t {
    message_t reqeust_and_response;
    int16_t  is_cpl;
    int16_t  id;
} op_ctx_t ;


typedef struct small_buffer_t {
    uint8_t raw[128];
} small_buffer_t;

struct io_channel {
    void *session_;
    uint64_t seq_; // >=0
    
    op_ctx_t op_buffers[RING_MAX];

    uint32_t inflight_op_nr_; // >=0
    op_ctx_t inflight_ops_[RING_MAX];

    uint32_t  cpl_nr_;
    op_ctx_t* cpl_ops_[POLL_MAX];
};

typedef struct percore_liboss_ctx_t {
    const msgr_client_if_t *msgr; //本线程的 msgr 实例
    fcache_t *small_meta_buffers;
} percore_liboss_ctx_t;

typedef percore_liboss_ctx_t liboss_ctx_t;
static __thread liboss_ctx_t liboss_ctx;
static inline liboss_ctx_t* tls_liboss_ctx() {
    return &liboss_ctx;
}

static inline op_ctx_t *_get_assi_op_from_msg(message_t *_m) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    void *sess = _m->priv_ctx;
    io_channel *ch = lc->msgr->messager_get_session_ctx(sess);
    //找到是哪个 op
    uint32_t id = message_get_seq(_m) % (RING_MAX);
    return &(ch->inflight_ops_[id]);
}

static void* msgr_meta_buffer_alloc(size_t sz) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    return fcache_get(lc->small_meta_buffers);
}
static void msgr_meta_buffer_free(void *ptr) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    fcache_put(lc->small_meta_buffers, ptr);
}

static void* msgr_data_buffer_alloc(size_t sz) {
    return malloc(sz);
}
static void* msgr_data_buffer_free(void *ptr) {
    free(ptr);
}


//接收 response
static void msgr_on_recv_msg(message_t *msg) {
    //Response meta_buffer must be NULL
    assert(msg->meta_buffer == NULL);

    liboss_ctx_t *lc = tls_liboss_ctx();
    void *sess = msg->priv_ctx;
    io_channel *ch = lc->msgr->messager_get_session_ctx(sess);
    uint32_t id = message_get_seq(msg) % (RING_MAX);

    op_ctx_t *op = &(ch->inflight_ops_[id]);
    assert(op->id == id);
    assert(op->reqeust_and_response.header.seq == msg->header.seq);
    assert(op->reqeust_and_response.header.type == msg->header.type);

    uint16_t status = message_get_status(msg);
    op->is_cpl = true;
    if(status != SUCCESS) {
        return -1;
    }
    message_move(&op->reqeust_and_response , msg);
    // ch->nr_inflight_op_this_time_--;
    
    ch->cpl_ops_[ch->cpl_nr_] = op;
    ch->cpl_nr_++;

    return 0;
}

static void msgr_on_send_msg(message_t *msg) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    (void)lc;    
    //Hold data_buffer and free meta_buffer
    msg->data_buffer = NULL;


    msgr_meta_buffer_free(msg->meta_buffer);
    // free(msg->meta_buffer);
    msg->meta_buffer = NULL;

    return;
}


static int _do_msgr_init() {
    liboss_ctx_t *lc = tls_liboss_ctx();
    lc->msgr = msgr_get_client_impl();
    messager_conf_t msgr_conf = {
        .on_recv_message = msgr_on_recv_msg,
        .on_recv_message = msgr_on_send_msg,
        .data_buffer_alloc = msgr_data_buffer_alloc,
        .data_buffer_free = msgr_data_buffer_free,
    };
    int rc = lc->msgr->messager_init(&msgr_conf);
    assert(rc == 0);
    return rc;
}


extern int tls_io_ctx_init(int flags) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    (void)flags;
    lc->small_meta_buffers = fcache_constructor(RING_MAX,sizeof(small_buffer_t),MALLOC);
    _do_msgr_init(); 
}

extern int tls_io_ctx_fini() {
    liboss_ctx_t *lc = tls_liboss_ctx();
    lc->msgr->messager_fini();
    lc->msgr = NULL;
}

extern io_channel *get_io_channel_with(const char *ip, int port) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    io_channel *ch = calloc(1, sizeof(io_channel));
    ch->session_ = lc->msgr->messager_connect(ip , port, ch);
    if(!ch->session_) {
        log_err("Socket Connect Failed with [%s:%d]\n", ip, port);
        free(ch);
        return NULL;
    }
    return ch;
}

extern void put_io_channel( io_channel *ioch) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    lc->msgr->messager_close(ioch->session_);
    free(ioch);
    return;
}


static op_ctx_t* _alloc_op(io_channel *ch) {
    if(ch->inflight_op_nr_ < RING_MAX) {
        uint64_t seq = ch->seq_;
        uint64_t id = seq % RING_MAX;
        op_ctx_t *op = &ch->inflight_ops_[id];
        op->id = ch->inflight_op_nr_;
        op->is_cpl = 0;
        ch->inflight_op_nr_++;
        return op;
    }
    return NULL;
}

extern int io_stat(io_channel *ch , uint32_t oid) {
    op_ctx_t *op = _alloc_op(ch);
    if(!op) {
        return -ENOMEM;
    }
    msg_hdr_t stat_hdr = {
        .seq = ch->seq_++,
        .type = msg_oss_op_stat,
        .status = 0,
        .prio = 0,
        .meta_length = 0,
        .data_length = 0
    };
    //header fill
    memcpy(&op->reqeust_and_response.header , &stat_hdr, sizeof(msg_hdr_t));
    //data and meta buffer prepare
    op->reqeust_and_response.data_buffer = NULL;
    op->reqeust_and_response.meta_buffer = NULL;
    //session bind
    op->reqeust_and_response.priv_ctx = ch->session_;
    //state prepare
    message_state_reset(&op->reqeust_and_response);
    return op->id;
}
extern int  io_create(io_channel *ch , uint32_t oid) {


}
extern int  io_delete(io_channel *ch , uint32_t oid);
extern int  io_read(io_channel  *ch, uint32_t oid, uint64_t ofst, uint32_t len);
extern int  io_write(io_channel *ch, uint32_t oid, const void* buffer, uint64_t ofst, uint32_t len);

extern int  io_submit_to_channel(io_channel *ch , int *ops , uint32_t op_nr);

extern int  io_poll_channel(io_channel *ch , int *op_cpl , int max);

