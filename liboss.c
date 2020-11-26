#include "liboss.h"

#include "operation.h"
#include "messager.h"

#include "util/fixed_cache.h"
#include "util/log.h"
#include "util/bitmap.h"

#define RING_MAX 1024
#define POLL_MAX 64

enum  {
    OP_NEW,
    OP_WAITING_SUBMIT,
    OP_IN_PROGRESS,
    OP_COMPLETED,
    OP_END
};

typedef struct op_ctx_t {
    message_t reqeust_and_response;
    uint8_t state;
    uint8_t rsv[3];
} op_ctx_t ;


typedef struct small_buffer_t {
    uint8_t raw[128];
} small_buffer_t;

struct io_channel {
    void *session_;
    
    uint32_t queue_depth_;
    uint32_t reap_depth_;
    
    uint64_t seq_; // >=0
    
    uint32_t  bitmap_hint_;
    bitmap_t *op_ctxs_bitmap_;    
    op_ctx_t *op_ctxs_;

    uint32_t  cpl_nr_;
    op_ctx_t** cpl_ops_;
};

struct io_channel* io_channel_new(uint32_t qd , uint32_t rd ) {
    struct io_channel *ch = calloc(1, sizeof(struct io_channel));
    assert(ch);
    
    ch->session_ = NULL;
    ch->queue_depth_ = (qd) ? qd : RING_MAX;
    ch->reap_depth_ = (rd) ? rd :RING_MAX;
    
    ch->op_ctxs_bitmap_ = bitmap_constructor(ch->queue_depth_, 1);
    assert(ch->op_ctxs_bitmap_);
    do {
        log_debug("Bitmap Allocator: %u bit_len, %u word_len \n" , ch->op_ctxs_bitmap_->bit_length,
            ch->op_ctxs_bitmap_->words_length);
    } while(0);

    ch->op_ctxs_ = calloc(ch->queue_depth_ , sizeof(op_ctx_t));
    assert(ch->op_ctxs_);

    ch->cpl_ops_ = calloc(ch->reap_depth_, sizeof(op_ctx_t*));
    assert(ch->cpl_ops_);

    return ch;
}

void io_channel_delete(struct io_channel *ch) {
    free(ch->cpl_ops_);
    free(ch->op_ctxs_);
    bitmap_destructor(ch->op_ctxs_bitmap_);
    free(ch);
}


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
    uint32_t id = message_get_rsv(_m, 0); //
    return &ch->op_ctxs_[id];
}

static void* msgr_meta_buffer_alloc(size_t sz) {
    return malloc(sz);
}
static void msgr_meta_buffer_free(void *ptr) {
    free(ptr);
}
static void* msgr_data_buffer_alloc(uint32_t sz) {
    return malloc(sz);
}
static void msgr_data_buffer_free(void *ptr) {
    free(ptr);
}


//接收 response
static void msgr_on_recv_msg(message_t *msg) {
    //Response meta_buffer must be NULL
    assert(msg->meta_buffer == NULL);

    liboss_ctx_t *lc = tls_liboss_ctx();
    void *sess = msg->priv_ctx;
    io_channel *ch = lc->msgr->messager_get_session_ctx(sess);

    op_ctx_t *op = _get_assi_op_from_msg(msg);
    assert(op->reqeust_and_response.header.seq == msg->header.seq);
    assert(op->reqeust_and_response.header.type == msg->header.type);

    uint16_t status = message_get_status(msg);
    log_debug("response of msg[%lu], status=%u\n", message_get_seq(msg), status);

    op->state = OP_COMPLETED;

    message_move(&op->reqeust_and_response , msg);
    // ch->nr_inflight_op_this_time_--;
    
    ch->cpl_ops_[ch->cpl_nr_] = op;
    ch->cpl_nr_++;

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
        .on_send_message = msgr_on_send_msg,
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

    return 0; 
}

extern int tls_io_ctx_fini() {
    liboss_ctx_t *lc = tls_liboss_ctx();

    lc->msgr->messager_fini();
    lc->msgr = NULL;

    return 0;
}

extern io_channel *get_io_channel_with(const char *ip, int port, int max_qd) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    io_channel *ch = io_channel_new(max_qd, max_qd);
    ch->session_ = lc->msgr->messager_connect(ip , port, ch);
    if (!ch->session_) {
        log_err("Socket Connect Failed with [%s:%d]\n", ip, port);
        io_channel_delete(ch);
        return NULL;
    }
    return ch;
}

extern void put_io_channel( io_channel *ioch) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    lc->msgr->messager_close(ioch->session_);
    io_channel_delete(ioch);
    return;
}


static op_ctx_t* _alloc_op(io_channel *ch , int *id_) {
    int id = bitmap_find_next_set_and_clr(ch->op_ctxs_bitmap_ , ch->bitmap_hint_);
    if(id < 0) {
        *id_ = -1;
        return NULL;
    }
    ch->bitmap_hint_ = id + 1;
    *id_ = id;
    log_debug("op_id = %d \n" , *id_);
    return &ch->op_ctxs_[id];
}

static void _free_op(io_channel *ch , op_ctx_t *op) {
    assert(op->state == OP_END);
    uint32_t id = message_get_rsv(&op->reqeust_and_response, 0);
    assert(bitmap_get_bit(ch->op_ctxs_bitmap_, id) == 0);
    bitmap_set_bit(ch->op_ctxs_bitmap_, id);
    ch->bitmap_hint_ = id < ch->bitmap_hint_ ? id : ch->bitmap_hint_;
    
    op->state = OP_NEW;
    
    return;
}

static int _io_prepare_op_common(io_channel *ch,  int16_t op_type ,uint32_t meta_size, void* meta_buffer, uint32_t data_length, void* data_buffer ) {
    int op_id ;
    op_ctx_t *op = _alloc_op(ch , &op_id);
    if(!op) {
        return -ENOMEM;
    }
    msg_hdr_t _hdr = {
        .seq = cpu_to_le64(ch->seq_++),
        .type = cpu_to_le16(op_type),
        .status = 0,
        .prio = cpu_to_le16(8),
        .meta_length = cpu_to_le16(meta_size),
        .data_length = cpu_to_le32(data_length),
        .crc_meta = 0,        
        
        /**
         * This is an ugly trick
         */
        .rsv[0] = cpu_to_le32(op_id),
        .rsv[1] = 0,

    };
    //header fill
    memcpy(&op->reqeust_and_response.header , &_hdr, sizeof(msg_hdr_t));
    
    //data and meta buffer set
    op->reqeust_and_response.data_buffer = data_buffer;
    op->reqeust_and_response.meta_buffer = meta_buffer;
    
    //session bind
    op->reqeust_and_response.priv_ctx = ch->session_;
    
    //send state reset
    message_state_reset(&op->reqeust_and_response);

    op->state = OP_WAITING_SUBMIT;

    return op_id;
}

extern int io_stat(io_channel *ch) {
    return _io_prepare_op_common(ch, msg_oss_op_stat, 0, NULL, 0, NULL);
}
extern int  io_create(io_channel *ch , uint64_t oid) {
    uint32_t meta_size = sizeof(op_create_t);
    void *meta_buffer = msgr_meta_buffer_alloc(sizeof(op_create_t));
    do {
        op_create_t *op_args = meta_buffer;
        op_args->oid = cpu_to_le64(oid);
    } while(0);
    return _io_prepare_op_common(ch , msg_oss_op_create, meta_size, meta_buffer , 0, NULL);
}
extern int  io_delete(io_channel *ch , uint64_t oid) {
    uint32_t meta_size = sizeof(op_delete_t);
    void *meta_buffer = msgr_meta_buffer_alloc(sizeof(op_delete_t));
    do {
        op_delete_t *op_args = meta_buffer;
        op_args->oid = cpu_to_le64(oid);
    } while(0);
    return _io_prepare_op_common(ch , msg_oss_op_delete, meta_size, meta_buffer , 0, NULL);
}

extern int  io_buffer_alloc(void** ptr, uint32_t size) {
    *ptr = msgr_data_buffer_alloc(size);
    return 0;
}
extern int  io_buffer_free (void* ptr) {
    msgr_data_buffer_free(ptr);
    return 0;
}

extern int  io_read(io_channel  *ch, uint64_t oid, uint64_t ofst, uint32_t len) {
    uint32_t meta_size = sizeof(op_read_t);
    void *meta_buffer = msgr_meta_buffer_alloc(sizeof(op_read_t));
    do {
        op_read_t *op_args = meta_buffer;
        op_args->oid = cpu_to_le64(oid);
        op_args->ofst = cpu_to_le64((uint32_t)ofst);
        op_args->len = cpu_to_le64(len);
        op_args->flags = cpu_to_le64(0);
    } while(0);
    return _io_prepare_op_common(ch , msg_oss_op_read, meta_size, meta_buffer , 0, NULL);
}

extern int  io_write(io_channel *ch, uint64_t oid, const void* buffer, uint64_t ofst, uint32_t len) {
    uint32_t meta_size = sizeof(op_write_t);
    void *meta_buffer = msgr_meta_buffer_alloc(sizeof(op_write_t));
    const void *data_buffer = buffer;
    do {
        op_write_t *op_args = meta_buffer;
        op_args->oid = cpu_to_le64(oid);
        op_args->ofst = cpu_to_le64((uint32_t)ofst);
        op_args->len = cpu_to_le64(len);
        op_args->flags = cpu_to_le64(0);
    } while(0);
    return _io_prepare_op_common(ch , msg_oss_op_write, meta_size, meta_buffer , len, (void*)data_buffer);
}

extern int  io_submit_to_channel(io_channel *ch , int *ops , int op_nr) {
    liboss_ctx_t *lc = tls_liboss_ctx();
    int i; 
    for (i = 0 ; i < op_nr ; ++i) {
        int opd = ops[i];
        log_debug("ops[%d]=%d\n", i, opd);
        op_ctx_t *op = &ch->op_ctxs_[opd];
        if( 0 <= opd && opd <= ch->queue_depth_ &&  op->state == OP_WAITING_SUBMIT) {
            log_debug("Prepare to submit opd(%d): type=%d\n", opd, message_get_op(&op->reqeust_and_response));
        } else {
            log_warn("Invalid opd = %d\n", opd);
            return -EINVAL;
        }
    }
    for (i = 0 ; i < op_nr ; ++i )  {
        op_ctx_t *op = &ch->op_ctxs_[ops[i]];
        // int rc = lc->msgr->messager_sendmsg(&op->reqeust_and_response);
        lc->msgr->messager_sendmsg(&op->reqeust_and_response);
        // assert (rc == 0);
    }

    //Busy Loop to send and flush
    int nr_m = 0; 
    do {
        log_debug("Preapre to flush message to peer\n");
        int rc = lc->msgr->messager_flush_msg_of(ch->session_);
        assert (rc >= 0);
        nr_m += rc;
        if(nr_m < op_nr) {
            usleep(5);
        }
    } while ( nr_m < op_nr);
    
    log_debug("Flush message to peer..done\n");
    for (i = 0 ; i < op_nr ; ++i )  {
        op_ctx_t *op = &ch->op_ctxs_[ops[i]];
        op->state = OP_IN_PROGRESS;
    }
    return 0;
}

extern int io_poll_channel(io_channel *ch, int *op_cpl, int min, int max) {
    int nr_reap = max < ch->reap_depth_ ? max : ch->reap_depth_;
    liboss_ctx_t *lc = tls_liboss_ctx();
    int cpls = 0;
    int retry_times = 10;
    if(min > max) {
        return -EINVAL;
    }
    
    if(!ch->cpl_nr_) {
        int rc = 0;
        int n = 0;
        while( ( min && n < min ) || (!rc && ( retry_times--)) ) {
            rc = lc->msgr->messager_wait_msg_of(ch->session_);
            assert(rc >= 0);
            if(rc == 0) {
                _mm_pause();
            }
            n += rc;
        }
        log_debug("msgr get %u response this time\n", ch->cpl_nr_);
    }
    if(ch->cpl_nr_) {
        int i;
        for(i = 0 ; i < ch->cpl_nr_ && nr_reap; ++i, --nr_reap, ++cpls) {
            op_cpl[i] =  message_get_rsv(&ch->cpl_ops_[i]->reqeust_and_response,0);
            log_debug ("op_id[%u], seq [%lu]\n", op_cpl[i], message_get_seq(&ch->cpl_ops_[i]->reqeust_and_response));
        }
        ch->cpl_nr_ -= cpls;
    }
    return cpls;
}

extern int  op_claim_result(io_channel *ch, int op_id, int *status, int* op_type, void** data_buffer, uint32_t *data_len) {
    if ( 0 <= op_id && op_id < ch->queue_depth_  && ch->op_ctxs_[op_id].state == OP_COMPLETED) {
        int status_, op_type_;
        void *data_buffer_;
        uint32_t data_len_;
        op_type_ = message_get_op(&ch->op_ctxs_[op_id].reqeust_and_response);
        status_ = message_get_status(&ch->op_ctxs_[op_id].reqeust_and_response);
        data_len_ = message_get_data_len(&ch->op_ctxs_[op_id].reqeust_and_response);
        data_buffer_ = ch->op_ctxs_[op_id].reqeust_and_response.data_buffer;
        log_debug("op_id:%d,op_type:%u,status:%d,data_buffer:%p,data_len:%u\n",
            op_id, op_type_, status_, data_buffer_, data_len_);

        
        ! op_type ? (void)0 : ({(*op_type) = op_type_;});
        ! status ? (void)0 : ({(*status) = status_;});
        ! data_buffer ? (void)0 : ({(*data_buffer) = data_buffer_;});
        ! data_len ? (void)0 : ({(*data_len)= data_len_;});

        ch->op_ctxs_[op_id].state = OP_END;
        return 0;
    }
    log_warn("Invlaid op_id:%d\n", op_id);
    return -EINVAL;
}

extern int  op_destory( io_channel *ch, int op_id) {
    if ( 0 <= op_id && op_id < ch->queue_depth_) {
        int st = ch->op_ctxs_[op_id].state;
        op_ctx_t *op = &ch->op_ctxs_[op_id];
        if (st == OP_END) {
            _free_op(ch , op);
            return 0;
        } else if (st == OP_IN_PROGRESS) {
            log_warn("op(%d) is in process\n", op_id);
            return -EINPROGRESS;
        } else if (st == OP_WAITING_SUBMIT) {
            log_warn("op(%d) is dropped before submitting , if data_buffer is not null, you should free it\n", op_id);
            if(op->reqeust_and_response.meta_buffer) {
                msgr_meta_buffer_free(op->reqeust_and_response.meta_buffer);
            }
            _free_op(ch, op);
            return 0;
        } else if ( st == OP_COMPLETED) {
            log_warn("op(%d) 'result isn't claimed \n", op_id);
            assert(op->reqeust_and_response.meta_buffer == NULL);
            if(op->reqeust_and_response.data_buffer) {
                msgr_data_buffer_free(op->reqeust_and_response.data_buffer);
            }
            _free_op(ch, op);
            return 0;            
        } else {
            log_err("Unexpected op state:%d\n", st);
            abort();  
        }
    }
    log_warn("Invalid op_id:%d\n",op_id);
    return -EINVAL;
}