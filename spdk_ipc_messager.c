

#include <spdk/env.h>
#include <spdk/event.h>

//------Extern API---------
#include "util/log.h"
#include "net.h"
#include "messager.h"

#include "spdk_ipc_config.h"


#define REQ_BATCH_SIZE 32



typedef message_t msg;

static void* default_alloc_meta_buffer(uint32_t sz){
    log_debug("Messager Internal alloc message meta buffer\n");
    return spdk_malloc(sz , 0 , NULL , SPDK_ENV_SOCKET_ID_ANY , SPDK_MALLOC_SHARE);
}
static void default_free_meta_buffer( void *mptr) {
    log_debug("Messager Internal free message meta buffer\n");
    spdk_free(mptr);
}
static void* default_alloc_data_buffer(uint32_t sz) {
    log_debug("Messager Internal alloc message data buffer\n");
    return spdk_malloc(sz , 0 , NULL , SPDK_ENV_SOCKET_ID_ANY , 
        SPDK_MALLOC_SHARE | SPDK_MALLOC_DMA);
}
static void default_free_data_buffer( void *dptr) {
    log_debug("Messager Internal free message data buffer \n");
    spdk_free(dptr);
}


static  void *(*alloc_meta_buffer)(uint32_t sz)  = default_alloc_meta_buffer;
static  void (*free_meta_buffer)(void *dptr)  = default_free_meta_buffer;
static  void *(*alloc_data_buffer)(uint32_t sz)  = default_alloc_data_buffer;
static  void (*free_data_buffer)(void *dptr)  = default_free_data_buffer;





static inline msg *msg_alloc() {
    msg* m = spdk_malloc(sizeof(msg) , 0 , NULL, SPDK_ENV_SOCKET_ID_ANY , 
        SPDK_MALLOC_SHARE);
    memset(m , 0 , sizeof(*m));
    return m;
}




static inline void msg_free(msg* m) {
    spdk_free(m);
}

typedef struct session_t {

    void *priv;
    //Peer
    uint32_t tgt_core;

    int nr_to_flush;
    struct spdk_ring *out_q; // Out

    struct spdk_ring *in_q; // In
    TAILQ_ENTRY(session_t) _session_list_hook;
} session_t;

typedef struct messager_t {
    messager_conf_t conf;
    bool is_running;

    uint32_t my_lcore;

    struct zcell_ipc_config_t *ipc_config;

    struct spdk_poller* ring_poller;

    TAILQ_HEAD(session_queue, session_t) session_q;

} messager_t;

static __thread messager_t g_msgr;
static inline messager_t* get_local_msgr() {
    return &g_msgr;
}

static int message_send(const message_t  *m)  
{
    messager_t *msgr = get_local_msgr();

    struct session_t *s = m->priv_ctx;
    
    msg *m_send = msg_alloc();
    memcpy(m_send , m , sizeof(msg));

    spdk_ring_enqueue(s->out_q , (void**)(&(m_send)) , 1 , NULL);

    msgr->conf.on_send_message(m);

    return 0;
}

static int message_recv_poll(void *arg) {
    messager_t *msgr = get_local_msgr();
    (void)arg;
    struct session_t *s;
    int count = 0;
    TAILQ_FOREACH(s , &msgr->session_q , _session_list_hook ) {
        struct spdk_ring *ring = s->in_q;
        if(spdk_ring_count(ring)) {
            void *msgs[REQ_BATCH_SIZE];
            size_t count = spdk_ring_dequeue(ring, msgs , 32);
            //for_each_msg
            //do handle
            size_t i;
            for ( i = 0 ; i < count ; ++i) {
                msg *m = (msg *)(msgs[i]);

                //Callback，调用 message_move 
                msgr->conf.on_recv_message(m);

                ++count;

                //释放空壳子
                msg_free(m);
            }
        }       
    }
    return count;
}

static int message_recv_poll_session(void *sess) {
    messager_t *msgr = get_local_msgr();
    struct session_t *s = sess;

    // struct session_t *s;
    int count = 0;
    // TAILQ_FOREACH(s , &msgr->session_q , _session_list_hook ) {
    struct spdk_ring *ring = s->out_q;
    if(spdk_ring_count(ring)) {
        void *msgs[REQ_BATCH_SIZE];
        size_t count = spdk_ring_dequeue(ring, msgs , 32);
        //for_each_msg
        //do handle
        size_t i;
        for ( i = 0 ; i < count ; ++i) {
            msg *m = (msg *)(msgs[i]);

            //Callback，调用 message_move 
            msgr->conf.on_recv_message(m);

            ++count;

            //释放空壳子
            msg_free(m);
        }
    }       

    return count;
}

static int _messager_constructor(messager_conf_t *conf , bool is_server) {
    messager_t *msgr = get_local_msgr();
    // (void)is_server;
    if(1){
        memcpy(&(msgr->conf) , conf , sizeof (*conf));
        msgr->my_lcore = spdk_env_get_current_core();
    } 
    if(1) {
        if(conf->meta_buffer_alloc) {
            alloc_meta_buffer = conf->meta_buffer_alloc;
        }
        if(conf->data_buffer_alloc) {
            alloc_data_buffer = conf->data_buffer_alloc;
        }
        if(conf->meta_buffer_free) {
            free_meta_buffer = conf->meta_buffer_free;
        }
        if(conf->data_buffer_free) {
            free_data_buffer = conf->data_buffer_free;
        }
    };

    if(is_server) {
        // log_info("IPC shm id=%u\n", conf->shm_id);
        msgr->ipc_config = spdk_memzone_lookup(ZCELL_IPC_CONFIG_NAME);
        if(!msgr->ipc_config) {
            log_err("%s memzone not found\n" , ZCELL_IPC_CONFIG_NAME);
            return -1;
        }

        //Add session
        uint32_t i , j;
        struct zcell_ipc_config_t *zic = msgr->ipc_config;
        for (i = 0 ; i < zic->tgt_nr ; ++i) {
            uint32_t tgt = zic->tgt_cores[i];
            struct session_t *s = calloc( 1 , sizeof(session_t));
            s->in_q = zic->rings[tgt][msgr->my_lcore];
            s->out_q = zic->rings[msgr->my_lcore][tgt];
            s->tgt_core = tgt;
            TAILQ_INSERT_TAIL(&msgr->session_q , s , _session_list_hook);
            log_info("Get session with [%u]\n", tgt );  
        }


        msgr->ring_poller = spdk_poller_register(message_recv_poll , NULL , 0);
        spdk_poller_pause(msgr->ring_poller);
    }


    msgr->is_running = 1;
    return 0;
}

static void _messager_destructor(bool is_server) {
    messager_t *msgr = get_local_msgr();
    if(!msgr->is_running) 
        return;
    spdk_poller_pause(msgr->ring_poller);
    spdk_poller_unregister(&msgr->ring_poller);

    while(!TAILQ_EMPTY(&msgr->session_q)) {
        struct session_t *s = TAILQ_FIRST(&msgr->session_q);
        TAILQ_REMOVE(&msgr->session_q , s , _session_list_hook);
        free(s);
    }

    msgr->is_running = 0;
}

static int _srv_messager_constructor(messager_conf_t *conf) {
    return _messager_constructor(conf , true);
}

static int _srv_messager_start() {
    messager_t *msgr = get_local_msgr();
    spdk_poller_resume(msgr->ring_poller);
    return 0;
}

static void _srv_messager_stop() {
    messager_t *msgr = get_local_msgr();
    spdk_poller_pause(msgr->ring_poller);
}

static void _srv_messager_destructor() {
    _messager_destructor(true);
}

static int _srv_messager_sendmsg(const message_t *_msg) {
    return message_send(_msg);
}

static int _cli_messager_constructor(messager_conf_t *conf) {
    return _messager_constructor(conf , false);
}

static void _cli_messager_destructor() {
    _messager_destructor(false);
}

static void* _cli_messager_connect(const char *ip , int port, void *sess_priv_ctx ) {
    log_err("Unsupported API\n");
    abort();
}

static void* _cli_messager_connect2(uint32_t lcore , void *sess_priv_ctx ) {
    struct messager_t *msgr = get_local_msgr();
    struct session_t *sess = calloc(1 , sizeof(struct session_t));
    struct zcell_ipc_config_t *zcfg = msgr->ipc_config;
    uint32_t my_lcore = msgr->my_lcore;
    sess->out_q = zcfg->rings[my_lcore][lcore];
    sess->in_q = zcfg->rings[lcore][my_lcore];
    assert(sess->out_q);
    assert(sess->in_q);
    sess->priv = sess_priv_ctx;
    sess->tgt_core = lcore;
    TAILQ_INSERT_TAIL(&msgr->session_q , sess , _session_list_hook);
    return (void*)sess;
}

static void _cli_messager_close (void * _s) {
    struct messager_t *msgr = get_local_msgr();
    struct session_t *sess = _s;
    TAILQ_REMOVE(&msgr->session_q , sess , _session_list_hook);
    if(spdk_ring_count(sess->out_q)) {
        log_warn("Fuck\n");
    }
    if(spdk_ring_count(sess->in_q)) {
        log_warn("Fuck\n");
    }
    free(sess);
}

static int _cli_messager_sendmsg(const message_t *_msg) {
    return message_send(_msg);
}

static int _cli_messager_flush() {
    //Do nothing
    return 0;
}

static int _cli_messager_wait_msg() {
    return message_recv_poll(NULL);
}

static int _cli_messager_wait_msg_of(void *session) {
    return message_recv_poll_session(session);
}

static int _cli_messager_flush_msg_of(void *session) {
    //Do nothing
    return 0;
}

static void* _cli_messager_get_session_ctx (void* session) {
    struct session_t *s = session;
    return s->priv;
}





static __thread msgr_server_if_t msgr_ipc_server_impl = {
    .messager_init = _srv_messager_constructor,
    .messager_start = _srv_messager_start,
    .messager_stop = _srv_messager_stop,
    .messager_fini = _srv_messager_destructor,
    .messager_sendmsg = _srv_messager_sendmsg,
    .messager_last_busy_ticks = NULL,
};

static __thread msgr_client_if_t msgr_ipc_client_impl = {
    .messager_init = _cli_messager_constructor,
    .messager_fini = _cli_messager_destructor,
    .messager_connect = _cli_messager_connect,
    .messager_connect2 = _cli_messager_connect2,
    .messager_close = _cli_messager_close,
    .messager_sendmsg = _cli_messager_sendmsg,
    .messager_flush= _cli_messager_flush,
    .messager_wait_msg = _cli_messager_wait_msg,
    .messager_wait_msg_of = _cli_messager_wait_msg_of,
    .messager_flush_msg_of = _cli_messager_flush_msg_of,
    .messager_get_session_ctx = _cli_messager_get_session_ctx,
};

extern const msgr_server_if_t *msgr_get_ipc_server_impl() {
    return &msgr_ipc_server_impl;
} 

extern const msgr_client_if_t *msgr_get_ipc_client_impl() {
    return &msgr_ipc_client_impl;
}



