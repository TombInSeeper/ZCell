
#include "spdk/queue.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/env.h"
#include "net.h"

#include "messager.h"


#define READ_EVENT_MAX 64

static inline  void* alloc_meta_buffer(size_t sz){
    msgr_debug("Messager Internal alloc message meta buffer\n");
    return malloc(sz);
}
static inline void free_meta_buffer( void *mptr) {
    msgr_debug("Messager Internal free message meta buffer\n");
    free(mptr);
}

static void* default_alloc_data_buffer(uint32_t sz) {
    msgr_debug("Messager Internal alloc message data buffer\n");
    return malloc(sz);
}
static void default_free_data_buffer( void *dptr) {
    msgr_debug("Messager Internal alloc message data buffer\n");
    free(dptr);
}

static void *(*alloc_data_buffer)(uint32_t sz)  = default_alloc_data_buffer;
static void (*free_data_buffer)(void *dptr)  = default_free_data_buffer;



typedef struct msg {
    message_t message;
    TAILQ_ENTRY(msg) _msg_list_hook;
}msg;

static inline msg *msg_construct( void * session ) {
    msg* m = calloc(1, sizeof(msg));
    m->message.priv_ctx = session;
    m->message.state.hdr_rem_len = sizeof(m->message.header);
    return m;
}


static inline void msg_destruct(msg* m) {
    if (m->message.state.hdr_rem_len || 
        m->message.state.data_rem_len || 
        m->message.state.meta_rem_len) {
        if(m->message.state.hdr_rem_len == sizeof(m->message.header))
            ;
        else 
            msgr_debug("Warning: destructing uncompleted message\n");
    }
    if(m->message.meta_buffer) {
        free_meta_buffer(m->message.meta_buffer);
    }
    if(m->message.data_buffer) {
        free_data_buffer(m->message.data_buffer);
    }
    free(m);
}
static inline bool msg_new(const msg *m) {
    return m->message.state.hdr_rem_len == 
        sizeof(m->message.header);
}
static inline uint32_t msg_rem_tlen(const msg *m) {
    return m->message.state.hdr_rem_len +
    m->message.state.meta_rem_len +
    m->message.state.data_rem_len;
}
static inline bool msg_rw_complete(const msg *m) {
    return msg_rem_tlen(m) == 0;
}

static inline void set_sock_ctx(struct sock *_sock , void *_ctx) {
    _sock->ctx = _ctx;
}

static inline void* get_sock_ctx(struct sock *_sock) {
    return _sock->ctx;
}


typedef struct session_t {
    sock *_sock;
    struct {
        //peer information
        char ip[46];
        int port;
    };
    void *msgr;

    unsigned int recv_reap;

    TAILQ_HEAD(recv_queue, msg) recv_q;
    TAILQ_HEAD(send_queue, msg) send_q;
    TAILQ_ENTRY(session_t) _session_list_hook;
} session_t;

typedef struct messager_t {
    messager_conf_t conf;
    bool is_running;
    net_impl *_net_impl;
    TAILQ_HEAD(session_queue, session_t) session_q;
    struct {
        union {
            sock_group *_sock_group;
        };
        //for server
        struct {
            struct sock *listen_sock;
            struct spdk_poller *accept_poller;
            struct spdk_poller *recver_poller;
            struct spdk_poller *sender_poller;
        };
    }; 
} messager_t;

static __thread messager_t g_msgr;
static inline messager_t* get_local_msgr() {
    return &g_msgr;
}

static inline session_t * session_construct(const char *ip , int port,  
    struct sock * _sock, void *msgr)  {
    session_t * s = malloc(sizeof(session_t));
    strcpy(s->ip , ip);
    s->port = port;
    s->msgr = msgr;
    s->_sock = _sock;
    s->recv_reap = 0;

    set_sock_ctx(s->_sock , s);

    messager_t * _msgr = msgr;
    TAILQ_INIT(&(s->recv_q));
    TAILQ_INIT(&(s->send_q));
    
    if ( _msgr->_net_impl->group_add_sock(_msgr->_sock_group , s->_sock) ) {
        msgr_err("Error: add sock to group\n");
        _msgr->_net_impl->close(s->_sock);
        free(s);
        return NULL;
    }
    return s;
}

static inline void session_destruct(session_t *ss) {

    messager_t* msgr = ss->msgr;
    msgr_debug("starting..\n");
    if(!TAILQ_EMPTY(&ss->recv_q)) {
        msg *h = TAILQ_FIRST(&ss->recv_q);
        if(!msg_new(h)) 
            msgr_debug("session_with:[%s:%d] has uncompletely recieved/processed messages\n" , ss->ip, ss->port);
        while(!TAILQ_EMPTY(&ss->recv_q)) {
            msg *h = TAILQ_FIRST(&ss->recv_q);
            TAILQ_REMOVE(&ss->recv_q, h , _msg_list_hook);
            msg_destruct(h);
        }
    }
    if(!TAILQ_EMPTY(&ss->send_q)) {
        msgr_debug("session_with:[%s:%d] has uncompletely sended/processed messages\n" , ss->ip, ss->port);
        while(!TAILQ_EMPTY(&ss->send_q)) {
            msg *h = TAILQ_FIRST(&ss->send_q);
            TAILQ_REMOVE(&ss->send_q, h , _msg_list_hook);
            msg_destruct(h);
        }
    }

    msgr_debug("group_remove_sock..\n");
    if ( msgr->_net_impl->group_remove_sock( msgr->_sock_group , ss->_sock) ) {
        msgr_debug("Failed to remove [%s:%d] sock from gruop \n" , ss->ip , ss->port);
    }

    msgr_debug("close_sock..\n");
    if (msgr->_net_impl->close(ss->_sock)) {
        msgr_debug("Failed to closed [%s:%d] sock\n" , ss->ip , ss->port);
    }


    free(ss);
}

static int _sock_rw_rc_handle( int rc , uint32_t *ptr) 
{
    if ( rc <= 0 ) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            //Send Buffer is full
            errno = 0;
            return SOCK_EAGAIN;
        } else {
            //Socket Error
            return SOCK_NEED_CLOSE;
        }
    }  else  {                
        *ptr -= rc;
        if ( *ptr) {
            //Uncompleted
            return SOCK_EAGAIN;
        } else {
            return SOCK_RWOK;
        }
    }
}

/**
 * @brief 
 *  rc = 0 : Success (Send Done);
 *  rc = -1 : Uncompleted;
 *  rc = -2 : Socket Error;
 */
static int _do_send_message(msg * m) {
    messager_t *msgr = get_local_msgr();
    message_t *ms = &m->message;
    session_t *sess = ms->priv_ctx;

    int err;
    int sock_rc;

    uint32_t o_hdr_len = sizeof(ms->header);
    uint32_t o_meta_len = ms->header.meta_length;
    uint32_t o_data_len = ms->header.data_length;

    do {
        if(ms->state.hdr_rem_len) {
            char *hdrstart = (char*)(&(ms->header)) + ( o_hdr_len - ms->state.hdr_rem_len) ;
            struct iovec hdr_iov = {
                .iov_base = hdrstart,
                .iov_len = ms->state.hdr_rem_len
            };
            sock_rc = msgr->_net_impl->writev(sess->_sock, &hdr_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.hdr_rem_len)) ) != SOCK_RWOK ) {
                return err;
            }           
        }
        if(ms->state.meta_rem_len) {
            char *metastart = ms->meta_buffer + ( o_meta_len - ms->state.meta_rem_len);
            struct iovec meta_iov = {
                .iov_base = metastart,
                .iov_len = ms->state.meta_rem_len
            };
            sock_rc = msgr->_net_impl->writev(sess->_sock, &meta_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.meta_rem_len)) ) != SOCK_RWOK ) {
                return err;
            } 
        }
        if(ms->state.data_rem_len) {
            char *datastart = ms->data_buffer + ( o_data_len -ms->state.data_rem_len);
            struct iovec data_iov = {
                .iov_base = datastart,
                .iov_len = ms->state.data_rem_len
            };
            sock_rc = msgr->_net_impl->writev(sess->_sock, &data_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.data_rem_len)) ) != SOCK_RWOK ) {
                return err;
            } 
        }
    } while(0);
    return SOCK_RWOK;
}
static int _do_recv_message(msg * m) {
    messager_t *msgr = get_local_msgr();
    message_t *ms = &m->message;
    session_t *sess = ms->priv_ctx;
    int err;
    int sock_rc;

    uint32_t o_hdr_len = sizeof(ms->header);
    uint32_t o_meta_len = ms->header.meta_length;
    uint32_t o_data_len = ms->header.data_length;

    do {
        if(ms->state.hdr_rem_len) {
            char *hdrstart = (char*)(&(ms->header)) +  ( o_hdr_len - ms->state.hdr_rem_len) ;
            struct iovec hdr_iov = {
                .iov_base = hdrstart,
                .iov_len = ms->state.hdr_rem_len
            };
            sock_rc = msgr->_net_impl->readv(sess->_sock, &hdr_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.hdr_rem_len)) ) != SOCK_RWOK ) {
                return err;
            } else {
                assert(ms->state.hdr_rem_len == 0);
                msgr_debug("Messager msg_header recv done\n");    
                msgr_debug("meta_len=%u,data_len=%u\n",ms->header.meta_length,ms->header.data_length);              
                if ((ms->state.meta_rem_len = ms->header.meta_length) > 0 ) {
                    o_meta_len = ms->header.meta_length;
                    ms->meta_buffer = alloc_meta_buffer(ms->header.meta_length);
                }
                if ((ms->state.data_rem_len = ms->header.data_length) > 0 ) {
                    o_data_len = ms->header.data_length;
                    ms->data_buffer = alloc_data_buffer(ms->header.data_length);
                }
            }           
        }
        if(ms->state.meta_rem_len) {
            char *metastart = ms->meta_buffer +  ( o_meta_len - ms->state.meta_rem_len) ;
            struct iovec meta_iov = {
                .iov_base = metastart,
                .iov_len = ms->state.meta_rem_len
            };
            sock_rc = msgr->_net_impl->readv(sess->_sock, &meta_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.meta_rem_len)) ) != SOCK_RWOK ) {
                return err;
            } 
            msgr_debug("Messager msg_meta recv done\n");    
        }
        if(ms->state.data_rem_len) {
            char *datastart = ms->data_buffer +( o_data_len - ms->state.data_rem_len) ;
            struct iovec data_iov = {
                .iov_base = datastart,
                .iov_len = ms->state.data_rem_len
            };
            sock_rc = msgr->_net_impl->readv(sess->_sock, &data_iov , 1);
            if ( ( err =  _sock_rw_rc_handle(sock_rc , &(ms->state.data_rem_len)) ) != SOCK_RWOK ) {
                return err;
            } 
            msgr_debug("Messager msg_data recv done\n");    
        }
    } while(0);
    return SOCK_RWOK;
}

// static inline bool _recv_list_empty(session_t *sess)
// {
//     session_t *ss = sess;
//     return TAILQ_EMPTY(&(ss->recv_q));
// }

// static inline msg* _last_recv_msg_entry(session_t *sess)
// {
//     session_t *ss = sess;
//     return TAILQ_LAST(&(ss->recv_q), recv_queue);
// }

// static inline void _insert_tail

static inline msg* _tail_recv_msg(session_t *ss) {
    msg *m;
    if(!(TAILQ_EMPTY(&(ss->recv_q)))) {
        m = TAILQ_LAST(&(ss->recv_q), recv_queue);
        if(msg_rw_complete(m)) {
            m = msg_construct(ss);
            TAILQ_INSERT_TAIL(&(ss->recv_q) , m , _msg_list_hook);
        }
    } else {
        m = msg_construct(ss);
        TAILQ_INSERT_TAIL(&(ss->recv_q) , m , _msg_list_hook);
    }
    return m;     
}

static void  _read_event_callback(void * sess , struct sock_group *_group, struct sock *_sock) {
    session_t *ss = sess;
    messager_t *msgr = ss->msgr;
    int err;
    (void)(_group);
    (void)(_sock);
    msg *m;
    //Recieve all
    for(;;) {
        m = _tail_recv_msg(ss);
        err = _do_recv_message(m);
        if( err == SOCK_RWOK) {
            assert(msg_rw_complete(m));
        } else if ( err == SOCK_EAGAIN) {
            break;
        } else if ( err == SOCK_NEED_CLOSE) {
            TAILQ_REMOVE(&(msgr->session_q), ss , _session_list_hook);
            session_destruct(ss);
            break;
        }
    }

    //Pop all
    if(!TAILQ_EMPTY(&ss->recv_q)) {
        msg *miter;
        for(;;) {
            miter = TAILQ_FIRST(&ss->recv_q);
            if ( miter == NULL || ! msg_rw_complete(miter)) {
                break;
            } else {
                msgr->conf.on_recv_message(&miter->message);
                // ss->recv_reap++;
                TAILQ_REMOVE(&ss->recv_q, miter, _msg_list_hook);
                msg_destruct(miter);
            }
        }
    }

    // if(ss->recv_reap) {
    //     msgr_debug("Reap %u messages\n", ss->recv_reap);
    //     ss->recv_reap = 0;
    // }
}

static inline int _push_msg(const message_t *_msg_rvalue_ref) {
    session_t *s = _msg_rvalue_ref->priv_ctx;
    msg* m = msg_construct(s); 
    memcpy(&m->message, _msg_rvalue_ref , sizeof(message_t));

    msgr_debug("_push_msg m->meta=%u, m->data=%u\n" , m->message.header.meta_length ,m->message.header.data_length);
    TAILQ_INSERT_TAIL(&s->send_q, m , _msg_list_hook);
    return 0;
}

static int  _flush_all(messager_t *msgr) {

    static session_t *died_ss[NR_SESSION_MAX];  
    session_t *s;
    int cnt = 0;
    int died_ss_n = 0;
    TAILQ_FOREACH(s , &msgr->session_q , _session_list_hook) {
        msg *miter = TAILQ_FIRST(&s->send_q);
        int  err;
        while (miter) {
            err = _do_send_message(miter);
            if(err == SOCK_EAGAIN) {
                break;
            } else if (err == SOCK_RWOK) {
                TAILQ_REMOVE(&s->send_q,miter,_msg_list_hook);
                msgr->conf.on_send_message(&miter->message);
                msg_destruct(miter);
                miter = TAILQ_FIRST(&s->send_q);
                ++cnt;
            } else {
                died_ss[died_ss_n++] = s;
                break;
            }
        }
    }

    int i;
    //destroy all died session
    for ( i = 0 ; i < died_ss_n ; ++i) {
        session_t *s = died_ss[i];
        TAILQ_REMOVE(&msgr->session_q, s, _session_list_hook);
        session_destruct(s);
    }

    return cnt;
}

static int _poll_read_events() {
    struct messager_t *msgr = get_local_msgr();
	struct sock * _results[READ_EVENT_MAX];
    int rc;
	// rc = spdk_sock_group_poll(msgr->sock_group);
    rc = msgr->_net_impl->group_poll(msgr->_sock_group, 32 , _results);
	if (rc < 0) {
		msgr_err("Failed to poll sock_group=%p\n", msgr->_sock_group);
        return rc;
    }
    int i;
    for (i = 0 ; i < rc ; ++i ) {
        _read_event_callback( _results[i]->ctx , msgr->_sock_group,  _results[i]);
    }
	return rc;
}

static int sock_accept_poll(void * messager)
{
	messager_t *msgr = messager;
	struct sock *sock;
	int rc;
	int count = 0;
	char saddr[46], caddr[46];
	uint16_t cport, sport;
	while (1) {
		sock = msgr->_net_impl->accept(msgr->listen_sock);
		if (sock != NULL) {
			rc = msgr->_net_impl->getaddr(sock, saddr, sizeof(saddr), &sport, caddr, sizeof(caddr), &cport);
			if (rc < 0) {
				msgr_warn("Cannot get connection addresses\n");
				msgr->_net_impl->close(sock);
				return -1;
			}
			msgr_debug("Accepting a new connection from (%s, %hu) to (%s, %hu)\n",
				       caddr, cport, saddr, sport);
            // struct client_t* c = _get_client_ctx(ctx,sock);
            session_t *new_sess = session_construct(caddr,cport,sock,msgr);
            if(new_sess) {
                msgr_debug("New session to (%s, %hu) established OK\n " , caddr ,cport);
                TAILQ_INSERT_TAIL(&msgr->session_q, new_sess , _session_list_hook);
			    count++;
            } else {
                msgr_debug("New session to (%s, %hu) established Error!! \n " , caddr ,cport);
            }
		} else {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				msgr_err("accept error(%d): %s\n", errno, strerror(errno));
			}
			break;
		}
	}

	return count;
}

static int sock_group_poll(void *arg)
{
	(void)arg;
    return _poll_read_events();
}

static int sock_reply_poll(void *arg) {
    messager_t *msgr = arg;
    return _flush_all(msgr);
}

static int _messager_constructor(messager_conf_t *conf , bool is_server) {
    messager_t *msgr = get_local_msgr();
    if(msgr->is_running) {
        return 0;
    }
    msgr->_net_impl = net_impl_constructor(SOCK_TYPE_POSIX);
    if(!msgr->_net_impl) {
        return -1;
    }

    if(1){
        memcpy(&(msgr->conf) , conf , sizeof (*conf));
        if(msgr->conf.data_buffer_alloc && msgr->conf.data_buffer_free) {
            msgr_debug("Override defalut data_buffer allocator\n");
            alloc_data_buffer = msgr->conf.data_buffer_alloc;
            free_data_buffer = msgr->conf.data_buffer_free;
        } else {

        }
    } 
    if(is_server){
        const char *ip = msgr->conf.ip;
        int port = msgr->conf.port;
        msgr_debug("Starting listening connection on %s:%d\n", ip , port);
        msgr->listen_sock = msgr->_net_impl->listen(ip , port);
        if (msgr->listen_sock  == NULL) {
            msgr_err("Cannot create server socket\n");
            return -1;
        }
    }

    if(1) {    
        /*
        * Create sock group for server socket
        */
        msgr->_sock_group = msgr->_net_impl->group_create();
        assert(msgr->_sock_group);
    } 

    

    if(is_server) {
        /*
        * Start acceptor and group poller
        */
        msgr->accept_poller = spdk_poller_register(sock_accept_poll, msgr, 10000);

        msgr->recver_poller = spdk_poller_register(sock_group_poll, msgr, 0);
        msgr->sender_poller = spdk_poller_register(sock_reply_poll, msgr,0);

        //Suspend 
        spdk_poller_pause(msgr->sender_poller);
        spdk_poller_pause(msgr->accept_poller);
        spdk_poller_pause(msgr->recver_poller);
    }


    if(1) {
        TAILQ_INIT(&(msgr->session_q));
    }

    msgr->is_running = 1;

    return 0;
}

static void _messager_destructor( bool is_server) {
    messager_t *msgr = get_local_msgr();
    if(!msgr->is_running) 
        return;

    msgr->is_running = 0;

    if(1) {  
        session_t *sp = TAILQ_FIRST(&msgr->session_q);
        while(sp) {
            msgr_warn("Living sessions!!\n");
            TAILQ_REMOVE(&msgr->session_q, sp , _session_list_hook);
            session_destruct(sp);
            sp = TAILQ_FIRST(&msgr->session_q);
        }
    }
    if(is_server) {
        spdk_poller_unregister(&msgr->sender_poller);
        spdk_poller_unregister(&msgr->recver_poller);
        spdk_poller_unregister(&msgr->accept_poller);
        msgr->_net_impl->close(msgr->listen_sock);  
        msgr->_net_impl->group_remove_sock(msgr->_sock_group, msgr->listen_sock);
    }      
    msgr->_net_impl->group_close(msgr->_sock_group);
    net_impl_destructor(msgr->_net_impl);
}

static int _srv_messager_constructor(messager_conf_t *conf) {
    return _messager_constructor(conf , true);
}

static int _srv_messager_start() {
    messager_t *msgr = get_local_msgr();

    spdk_poller_resume(msgr->accept_poller);
    spdk_poller_resume(msgr->recver_poller);
    spdk_poller_resume(msgr->sender_poller);

    return 0;
}

static void _srv_messager_stop() {
    messager_t *msgr = get_local_msgr();
    spdk_poller_pause(msgr->accept_poller);
    spdk_poller_pause(msgr->recver_poller);
    spdk_poller_pause(msgr->sender_poller);
}

static void _srv_messager_destructor() {
    _messager_destructor(true);
}

static int _srv_messager_sendmsg(const message_t *_msg) {
    return _push_msg(_msg);
}

static int _cli_messager_constructor(messager_conf_t *conf) {
    return _messager_constructor(conf , false);
}

static void _cli_messager_destructor() {
    _messager_destructor(false);
}

static void*_cli_messager_connect (const char *ip , int port) {
    messager_t *msgr = get_local_msgr();
    struct sock *_sock =  msgr->_net_impl->connect(ip, port);
    session_t *s = NULL;
    if(_sock) {
        s = session_construct(ip,port, _sock, msgr);
        TAILQ_INSERT_TAIL(&msgr->session_q, s , _session_list_hook);
    }
    return s;
}
static void _cli_messager_close (void * _s) {
    messager_t *msgr = get_local_msgr();
    session_t *s = _s;
    if(s) {
        session_destruct(s);
        TAILQ_REMOVE(&msgr->session_q, s , _session_list_hook);
    }
}

static int _cli_messager_sendmsg(const message_t *_msg) {
    return _push_msg(_msg);
}

static int _cli_messager_flush( ) {
    messager_t *msgr = get_local_msgr();
    return _flush_all(msgr);
}

static int _cli_messager_wait_msg() {
    return _poll_read_events();
}
    // int  (*messager_wait_msg)();


extern int msgr_server_if_init(msgr_server_if_t * sif)  {
    sif->messager_init = _srv_messager_constructor;
    sif->messager_start = _srv_messager_start;
    sif->messager_stop = _srv_messager_stop;
    sif->messager_fini = _srv_messager_destructor;
    sif->messager_sendmsg = _srv_messager_sendmsg;
    return 0;
}

extern int msgr_client_if_init(msgr_client_if_t * cif) {
    msgr_client_if_t _c = {
        .messager_init = _cli_messager_constructor,
        .messager_fini = _cli_messager_destructor,
        .messager_connect = _cli_messager_connect,
        .messager_close = _cli_messager_close,
        .messager_sendmsg = _cli_messager_sendmsg,
        .messager_flush= _cli_messager_flush,
        .messager_wait_msg = _cli_messager_wait_msg,
    };
    memcpy(cif, &_c , sizeof(msgr_client_if_t));
    
    return 0;
}
