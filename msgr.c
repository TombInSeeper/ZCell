#include "msgr.h"

#define ADDR_STR_LEN INET6_ADDRSTRLEN

#define NR_MAX_CLIENTS (1 * 8)
#define NR_MSG_PENDING (1 * 128)

// static char tmpbuf[1024 * 1024];


enum  {
    MSG_NEW = 0,
    MSG_HEADER = 1,
    MSG_PAYLOAD = 2,
    MSG_COMPLETED = 3,
};

enum {
    MSG_READ = 1,
    MSG_WRITE = 2,
};

struct network_context_t;
struct client_t {

    struct network_context_t* nctx;
    struct spdk_sock* sock;

    char host[32];
    int  port;

    bool living;

    // uint16_t qsend_tail;
    // struct message send_pending[NR_MSG_PENDING];

    uint16_t qrecv_tail;
    struct  message recv_pending[NR_MSG_PENDING];

};

struct network_context_t {

    //local address info
    char host[32];
    int  port;


    struct spdk_sock* sock;

    //client_sock group
    struct spdk_sock_group* group;

    struct spdk_poller* accept_poller;
    struct spdk_poller* group_poller; //Poll for sock read event (recv_msg)
    struct spdk_poller* reply_poller;

    struct client_t clients[NR_MAX_CLIENTS];

    struct spdk_ring* sendq;

};

struct client_t* _get_client_ctx(struct network_context_t* ctx, struct spdk_sock* sock)
{
    char saddr[ADDR_STR_LEN], caddr[ADDR_STR_LEN];
	uint16_t cport, sport;

	spdk_sock_getaddr(sock, saddr, sizeof(saddr), &sport, caddr, sizeof(caddr), &cport);

    return &(ctx->clients[cport % NR_MAX_CLIENTS]);

}

static int  _msg_iov(struct message* m , struct iovec* iov) {

    if(m->rwstate == MSG_COMPLETED)
        return -1;

    if(m->rwstate == MSG_HEADER) {
        iov->iov_base =  (char*)(&(m->hdr)) + m->rw_len;
        iov->iov_len = sizeof(struct request_hdr_t) - m->rw_len;
    } else {
        iov->iov_base =  (char*)(m->payload) + ( m->rw_len -sizeof(struct request_hdr_t) );
        iov->iov_len = m->hdr.oph.payload_length - (m->rw_len - sizeof(struct request_hdr_t));
    }
    return 0;
}

static int _do_send_msg(struct message* m)
{
	int n;
    int cnt = 0;
    struct client_t *c = m->cli_priv;
    struct spdk_sock* sock = c->sock;
    do  {
        struct iovec iov;
        switch (m->rwstate) {
            case(MSG_NEW):
                m->rw_len = 0;
                m->rwstate = MSG_HEADER;
                //fallthrough;
            case(MSG_HEADER): {
                _msg_iov(m,&iov);
                n = spdk_sock_writev(sock,&iov,1);
                if( n > 0 ) {
                    m->rw_len += n;
                    //Header complete
                    if(m->rw_len == sizeof(struct request_hdr_t)) {
                        if(m->hdr.oph.op_flag & OPFLAG_HAS_PAYLOAD ) {  
                            m->rwstate = MSG_PAYLOAD;               
                        }
                        else 
                            m->rwstate = MSG_COMPLETED;
                    }            
                }
                break;
            }
            case(MSG_PAYLOAD): {
                _msg_iov(m,&iov);
                n = spdk_sock_writev(sock,&iov,1);
                if(n > 0) {
                    m->rw_len += n;
                    if(m->rw_len == sizeof(struct request_hdr_t) + m->hdr.oph.payload_length ) {
                        m->rwstate = MSG_COMPLETED;
                    }
                } 
                break;
            }
            case(MSG_COMPLETED): {
                cnt++;
                break;
            }
            default:
                break;
        }
    } while ( n > 0  && cnt == 0);

    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return cnt;
        } else {
            SPDK_ERRLOG("spdk_sock_recv() failed, errno %d: %s\n",
                    errno, strerror(errno));
            return -1;
        }  
    }
    return cnt;
}

static int _do_recv_msgs(struct client_t* c)
{
	int n = 0;
    int cnt = 0;
    struct spdk_sock *sock = c->sock;

    do {
        //The last uncompleted msg
        struct message* m = c->recv_pending + c->qrecv_tail;
        struct iovec iov;
        switch (m->rwstate) {
            case(MSG_NEW):
                m->rw_len = 0;
                m->rwstate = MSG_HEADER;
                //fallthrough;
            case(MSG_HEADER): {
                _msg_iov(m,&iov);
                n = spdk_sock_readv(sock,&iov,1);
                if( n > 0 ) {
                    m->rw_len += n;
                    //Header complete
                    if(m->rw_len == sizeof(struct request_hdr_t)) {
                        if(m->hdr.oph.op_flag & OPFLAG_HAS_PAYLOAD ) {  
                            uint64_t payload_len = m->hdr.oph.payload_length;
                            // SPDK_NOTICELOG("Payload Read , payload len = %lu \n" , payload_len);

                            m->rwstate = MSG_PAYLOAD;
                            //DMA memory alloc
                            m->payload = spdk_malloc(payload_len,0,NULL,
                                SPDK_ENV_SOCKET_ID_ANY , SPDK_MALLOC_DMA);
                            // m->payload = tmpbuf;
                            if(!m->payload) {
                                SPDK_ERRLOG("spdk_malloc %lu KiBytes failed\n", (uint64_t)(payload_len / 1024.0) );
                                return -1;
                            }                 
                        }
                        else 
                            m->rwstate = MSG_COMPLETED;
                    }            
                }
                break;
            }
            case(MSG_PAYLOAD): {
                _msg_iov(m,&iov);
                n = spdk_sock_readv(sock,&iov,1);
                if(n > 0) {
                    m->rw_len += n;
                    if(m->rw_len == sizeof(struct request_hdr_t) + m->hdr.oph.payload_length ) {
                        m->rwstate = MSG_COMPLETED;
                    }
                } 
                break;
            }
            case(MSG_COMPLETED): {
                m->cli_priv = c;
                cnt++;
                c->qrecv_tail++;
                break;
            }
            default:
                break;
        }

    }  while (n > 0 && c->qrecv_tail < NR_MSG_PENDING);
    
    //The last read return value
    if ( n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
        errno = 0;
        return cnt;
    }       
    else {
        // Free all buffer, if allocated
        int i;
        for( i = 0 ; i < NR_MSG_PENDING ; ++i) {
            spdk_free(c->recv_pending[i].payload);
        }
        return -1;
    }
}



static void _async_send_msg_free(void* arg , int err)
{
    struct spdk_sock_request* rsp = arg;
    struct iovec* iov =(struct iovec*) (rsp + 1);
    free(iov->iov_base);
    free(rsp);
}

static void _recv_msgs(void *arg, struct spdk_sock_group *group, struct spdk_sock *sock)
{
	int n;
    struct client_t* c = arg;
    n = _do_recv_msgs(c);
    if(n > 0) {
        // SPDK_NOTICELOG("Reply to clients\n");
        // Reap and fill reply queue
        for (int i = 0 ; i < n ; ++i) {

            struct message* rqst = &(c->recv_pending[i]);
            spdk_free(rqst->payload);




            struct message* m = calloc(1 , sizeof(struct message));
            // Construct message
            m->cli_priv = c;
            m->hdr.oph.op_flag = 100;
            m->hdr.oph.op_seq = c->recv_pending[i].hdr.oph.op_seq;
            
            void* objs[] = {m};         
            // int rc = _do_send_msg(m);
            spdk_ring_enqueue(c->nctx->sendq , objs , 1 , NULL);
        }

        //Reset recv_pending
        if ( n == c->qrecv_tail) {
            struct message last_uncompleted = c->recv_pending[c->qrecv_tail];
            memset(c->recv_pending,0,sizeof(struct message) *(n+1) );
            c->qrecv_tail = 0 ;
            c->recv_pending[0] = last_uncompleted;
        } else {
            SPDK_ERRLOG("Invalid qrecv_tail:%u,n:%d\n",c->qrecv_tail,n);
        }
        return;
    } else if ( n == 0 ) {
        SPDK_NOTICELOG("uncompleted message\n");
        return;
    }  else {
		goto sock_closed;
	}

sock_closed:
	SPDK_NOTICELOG("Connection closed\n");
    /* Client ctx destory*/
    c->living = false;
    
	/* Connection closed */
	spdk_sock_group_remove_sock(group, sock);
	spdk_sock_close(&sock);
}

static int sock_accept_poll(void *arg)
{
	struct network_context_t *ctx = arg;
	struct spdk_sock *sock;
	int rc;
	int count = 0;
	char saddr[ADDR_STR_LEN], caddr[ADDR_STR_LEN];
	uint16_t cport, sport;

	while (1) {
		sock = spdk_sock_accept(ctx->sock);
		if (sock != NULL) {
			rc = spdk_sock_getaddr(sock, saddr, sizeof(saddr), &sport, caddr, sizeof(caddr), &cport);
			if (rc < 0) {
				SPDK_ERRLOG("Cannot get connection addresses\n");
				spdk_sock_close(&ctx->sock);
				return -1;
			}

			SPDK_NOTICELOG("Accepting a new connection from (%s, %hu) to (%s, %hu)\n",
				       caddr, cport, saddr, sport);

            struct client_t* c = _get_client_ctx(ctx,sock);
            if(c->living) {
                spdk_sock_close(&sock);
				SPDK_ERRLOG(" port number is replicated \n");
				break;
            } else {
                c->sock = sock;
                c->living = true;         
                c->port = cport;
                c->nctx = ctx;
                strcpy(c->host,caddr);
            }

            rc = spdk_sock_group_add_sock(ctx->group, sock, _recv_msgs, c);

			if (rc < 0) {
				spdk_sock_close(&sock);
				SPDK_ERRLOG("failed\n");
				break;
			}

			count++;
		} else {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				SPDK_ERRLOG("accept error(%d): %s\n", errno, strerror(errno));
			}
			break;
		}
	}

	return count;
}

static int sock_group_poll(void *arg)
{
	struct network_context_t *ctx = arg;
	int rc;

	rc = spdk_sock_group_poll(ctx->group);
	if (rc < 0) {
		SPDK_ERRLOG("Failed to poll sock_group=%p\n", ctx->group);
	}

	return -1;
}

static int sock_reply_poll(void *arg)
{
    struct network_context_t *ctx = arg;
    struct spdk_ring* sq = ctx->sendq;
    int cnt = spdk_ring_count(sq);
    if( cnt == 0) {
        return 0;
    }

    // SPDK_NOTICELOG("send message count:%d\n", cnt);

    int ret = 0;
    // struct message msg;
    void* msgs[] = {NULL};
    while(cnt--) {
        spdk_ring_dequeue(sq,msgs,1);
        struct message* msg = msgs[0];

        struct client_t *c= msg->cli_priv;
        if(!c->living) {
            SPDK_WARNLOG("Client[%s:%u] has been died, drop msg\n",c->host,c->port);
            continue;
        }
        // spdk_sock_flush()
        int success = _do_send_msg(msgs[0]);
        if(success == 0) {
            spdk_ring_enqueue(sq,msgs,1,NULL);
            continue;
        } else if (success > 0 ) {
            // SPDK_NOTICELOG("Send Successfully\n");
            spdk_free(msg->payload);
            free(msg);
            ret++;
        } else {
            SPDK_WARNLOG("Client Connection Closed\n");
	        spdk_sock_group_remove_sock(ctx->group, c->sock);
            spdk_sock_close(&(c->sock));
            memset(c,0,sizeof(*c));
        }
    }

    return ret;
}

static int _do_server_init(struct network_context_t** nctx, char*ip ,int port )
{

    SPDK_NOTICELOG("Starting init msgr\n");

    *nctx = calloc(1,sizeof(struct network_context_t));
    struct network_context_t* ctx = *nctx ;
    if(!ctx) {
        SPDK_ERRLOG("fuck\n");
        return -1;
    }

    strcpy(ctx->host,ip);
    ctx->port = port;
    if(1) {
        SPDK_NOTICELOG("Starting listening connection on %s:%d\n", ctx->host, ctx->port);
        ctx->sock = spdk_sock_listen(ctx->host, ctx->port, NULL);
        if (ctx->sock == NULL) {
            SPDK_ERRLOG("Cannot create server socket\n");
            return -1;
        }
    }
	/*
        Create send queue
    */
    ctx->sendq = spdk_ring_create(SPDK_RING_TYPE_SP_SC, 64 * 1024 ,SPDK_ENV_SOCKET_ID_ANY);

	/*
	 * Create sock group for server socket
	 */
	ctx->group = spdk_sock_group_create(NULL);


    
	/*
	 * Start acceptor and group poller
	 */
    ctx->reply_poller = spdk_poller_register(sock_reply_poll,ctx,0);
	ctx->accept_poller = spdk_poller_register(sock_accept_poll, ctx, 1000);
	ctx->group_poller = spdk_poller_register(sock_group_poll, ctx, 0);

    return 0;
}

static __thread struct network_context_t* nctx;

int msgr_init(char*ip , int port)
{
    return _do_server_init(&nctx, ip , port);
}

int msgr_fini()
{   
    struct network_context_t* ctx = nctx;
    spdk_poller_unregister(&(ctx->reply_poller));
    spdk_poller_unregister(&(ctx->accept_poller));
    spdk_poller_unregister(&(ctx->group_poller));
    spdk_sock_group_close(&(ctx->group));

    int n = spdk_ring_count(ctx->sendq);
    if(n > 0) {
        SPDK_WARNLOG("%d msgs are inflight\n" , n );
        //TODO 
    }

    spdk_ring_free(ctx->sendq);
    spdk_sock_close(&(ctx->sock));
    free(ctx);
    return 0;
}