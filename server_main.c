
#include "spdk/env.h"
#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/util.h"

#include "util/log.h"
#include "util/fixed_cache.h"
#include "util/errcode.h"


#include "spdk_ipc_config.h"
#include "messager.h"
#include "objectstore.h"


#define NR_REACTOR_MAX NR_CORE_MAX


static int g_ipc_msgr = 0;
static int g_net_msgr = 0;
static const char *g_base_ip = "0.0.0.0";
static int g_base_port = 18000;
// static const char *g_zcell_core_mask = "0x1";
// static const char *g_tgt_core_mask = "0x2";
static int g_store_type = NULLSTORE;
// static int g_idle = 0;
static int g_new = 0;
static const char *dev_list[] = {"Nvme0n1", "/run/pmem0" ,NULL};


static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "i:p:INc:C:s:n")) != -1) {
		switch (opt) {
		case 'i':
			g_base_ip = optarg;
			break;
        case 'I':
            g_ipc_msgr = 1;
            break;
        case 'N':
            g_net_msgr = 1;
            break;
		case 'p':
			g_base_port = atoi(optarg);
			break;
        // case 'c':
		// 	g_tgt_core_mask = (optarg);
		// 	break;
        // case 'C':
		// 	g_zcell_core_mask = (optarg);
		// 	break;
        case 's':
            if(!strcmp(optarg,"null")){
                log_info("Ostore type is nullstore \n");
                g_store_type = NULLSTORE;
            } else if (!strcmp(optarg,"chunk")) {
                log_info("Ostore type is chunkstore\n");
                g_store_type = CHUNKSTORE;
            } else if (!strcmp(optarg,"zeta")) {
                log_info("Ostore type is zetastore\n");
                g_store_type = ZSTORE;
            } else {
                log_err("Unknown storage backend:%s \n" , optarg);
                exit(1);
            }
            break;
        case 'n':
			g_new = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s [-i ip] [-p port]  [-s[null|chunk|zeta]] [-n new objstore] \n", argv[0]);
			exit(1);
		}
	}
}



typedef struct reactor_ctx_t {
    
    int reactor_id;
    
    struct {
        const char *ip;
        int port;
    };

    const msgr_server_if_t *msgr_impl;
    //
    const msgr_server_if_t *ipc_msgr_impl;
    //
    const objstore_impl_t  *os_impl;
    // bool idle_enable;
    // uint64_t idle_poll_start_us;
    // uint64_t idle_poll_exe_us;
    // uint64_t rx_last_window;
    // uint64_t tx_last_window;
    // uint64_t rx_io_last_window;
    // uint64_t tx_io_last_window;
    // struct spdk_poller *idle_poller;
    // int running_level;

    volatile bool running;
} reactor_ctx_t;

static reactor_ctx_t g_reactor_ctxs[NR_REACTOR_MAX];

static inline reactor_ctx_t* reactor_ctx() {
    return &g_reactor_ctxs[spdk_env_get_current_core()];
}
static int reactor_reduce_state() {
    int i;
    int r = 0;
    SPDK_ENV_FOREACH_CORE(i) {
        r += g_reactor_ctxs[i].running;
    }
    return r;
}


static void* alloc_data_buffer(uint32_t sz) {
    
    // static __thread tls_data_buf[4 << 20];
    
    void *ptr;
    if(sz <= 0x1000) {
        // log_debug("[fixed_cahce] \n");
        // ptr =  fcache_get(reactor_ctx()->dma_pages); 
        sz = 0x1000;
    }
    // uint32_t align = (sz % 0x1000 == 0 )? 0x1000 : 0;
    ptr =  spdk_malloc(sz, 0x1000, NULL , SPDK_ENV_SOCKET_ID_ANY , 
        SPDK_MALLOC_DMA | SPDK_MALLOC_SHARE);
    // log_debug("[spdk_dma_malloc] \n");
    log_debug("[spdk_malloc] 0x%p\n", ptr);
    
    return ptr;
}
 
static void free_data_buffer(void *p) {
    // fcache_t *fc = reactor_ctx()->dma_pages;
    // if(fcache_in(fc , p)) {
        // log_debug("[fixed_cahce] \n");
        // fcache_put(fc, p);
    // } else {
    log_debug("[spdk_free] 0x%p\n", p);
    spdk_free(p);
    // }
}

static void* alloc_meta_buffer(uint32_t sz) {
    void *ptr;
    ptr =  spdk_malloc(sz, 0, NULL , SPDK_ENV_SOCKET_ID_ANY , 
        SPDK_MALLOC_DMA);
    // log_debug("[spdk_dma_malloc] \n");
    log_debug("[spdk_malloc] 0x%p\n", ptr);
    return ptr;
}

static void free_meta_buffer(void *p) {
    log_debug("[spdk_free] 0x%p\n", p);
    spdk_free(p);
}

static inline void msg_free_resource(message_t *m) {
    if(m->header.meta_length == 0 && m->meta_buffer) {
        free_meta_buffer(m->meta_buffer);
        m->meta_buffer = NULL;
    }
    if(m->header.data_length == 0 && m->data_buffer) {
        free_data_buffer(m->data_buffer);
        m->data_buffer = NULL;
    }
}
/**
 * 复用 request 结构生成 response
 * 这里我们对 request 的 meta_buffer 和 data_buffer 的处理方式是 lazy 的：
 * 只要将 header 的 meta_length 和 data_length 长度置 0， 
 * 那么 messager 发送时就不会发送 meta_buffer 和 data_buffer 的内容，：
 * response message 的 meta_buffer 和 data_buffer 
 * 如果是非空指针，并且对应的头部长度是0，会被发送函数自动被释放
 */
static inline void _response_with_reusing_request(message_t *request, uint16_t status_code) {
    request->header.status = cpu_to_le16(status_code);
    message_state_reset(request);
    log_debug("Perpare to send response :[status=%u , meta_len=%u, data_len=%u]\n",
        request->header.status,
        request->header.meta_length,
        request->header.data_length);

    //释放原request的资源
    if(request->meta_buffer && request->header.meta_length == 0)
        free_meta_buffer(request->meta_buffer);
    if(request->data_buffer && request->header.data_length == 0)
        free_data_buffer(request->data_buffer);

    reactor_ctx()->msgr_impl->messager_sendmsg(request);
}

static inline void _response_broken_op(message_t *request, uint16_t status_code) {
    request->header.data_length = 0;
    request->header.meta_length = 0;
    if(request->meta_buffer)
        free_meta_buffer(request->meta_buffer);
    if(request->data_buffer)
        free_data_buffer(request->data_buffer);
    _response_with_reusing_request(request, status_code);
}

//Operation handle
static void _do_op_unknown(message_t *request) {
    _response_broken_op(request, UNKNOWN_OP);
}

static void oss_op_cb(void *ctx, int status_code) {
    message_t *request = ctx;

    if(status_code != OSTORE_EXECUTE_OK) {
        _response_broken_op(request,status_code);
        free(request);
        return;
    }
    _response_with_reusing_request(request, status_code);
    free(request);
}

//Step1:Check
static bool oss_op_valid(message_t *request) {
    int op = le16_to_cpu(request->header.type);
    bool rc = false;
    switch (op) {
        case msg_oss_op_stat: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == 0) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer == NULL);       
        }
            break;
        case msg_oss_op_create: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == sizeof(op_create_t)) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer != NULL);       
        }
            break; 
        case msg_oss_op_delete: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == sizeof(op_delete_t)) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer != NULL) ;       
        }
            break;
        case msg_oss_op_write:{
            op_write_t *op = (op_write_t *)request->meta_buffer;
            rc =  (request->data_buffer != NULL) && 
            (request->meta_buffer != NULL) && 
            (le32_to_cpu(request->header.data_length) == le32_to_cpu(op->len)) && 
            (le16_to_cpu(request->header.meta_length) > 0 );       
        }
            break;
        case msg_oss_op_read:{
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) > 0 ) && 
            (request->data_buffer == NULL) &&
            (request->meta_buffer != NULL);       
        }
            /* code */
            break;        
        default:
            break;
    }
    return rc;
}

//Step2: prepare response structure reusing request 
// 复用 request 结构填充成 response 结构
static int  oss_op_refill_request_with_reponse(message_t *request) {
    int op = le16_to_cpu(request->header.type);
    int rc = 0;
    switch (op) {
        case msg_oss_op_stat: {
            request->header.meta_length = 0;             
            request->header.data_length = sizeof(op_stat_result_t);
            request->data_buffer = alloc_data_buffer(sizeof(op_stat_result_t));
        }
            break; 
        case msg_oss_op_create: {
            request->header.meta_length = 0;      
        }
            break; 
        case msg_oss_op_delete: {
            request->header.meta_length = 0;            
        }
            break;
        case msg_oss_op_write:{
            request->header.meta_length = 0;             
            request->header.data_length = 0;             
        }
            break;
        case msg_oss_op_read:{
            op_read_t *op = (op_read_t *)request->meta_buffer;
            request->header.meta_length = 0;             
            request->header.data_length = op->len; 
            if(op->read_buffer_zero_copy_addr) {
                //IPC Call
                request->data_buffer = (void*)((uintptr_t)(op->read_buffer_zero_copy_addr));
            } else {
                //RPC Call
                request->data_buffer = alloc_data_buffer(le32_to_cpu(op->len));           
            }
        }
            /* code */
            break;        
        default:
            break;
    }
    return rc;
}

/** 
 * 由于是异步操作，所以需要复制 request 内容到全局
 * 需要 malloc 
 * 子例程的异步上下文指针是 request 本身加上一段 store_type 依赖的上下文
 */ 
static void _do_op_oss(message_t * _request) {

    const objstore_impl_t *os_impl = reactor_ctx()->os_impl;

    const int async_op_ctx_sz = os_impl->obj_async_op_context_size();
    void *ctx = malloc(sizeof(message_t) + async_op_ctx_sz);
    
    message_t *request = ctx;
    memcpy(request, _request, sizeof(message_t));

    log_debug("Prepare to execute op:[seq=%lu,op_code=%u,meta_len=%u,data_len=%u]\n", request->header.seq,
        request->header.type, request->header.meta_length, request->header.data_length);

    int rc = INVALID_OP;
    if(!oss_op_valid(request)) {
        goto label_broken_op;
    }
    oss_op_refill_request_with_reponse(request);

    rc = os_impl->obj_async_op_call(request,oss_op_cb);
    
    
    if(rc == OSTORE_SUBMIT_OK) {
        return;
    }

label_broken_op:
    _response_broken_op(request, rc );
    free(request);
    return;
}

static void _do_op_ping(message_t *request) {
    _response_with_reusing_request(request, SUCCESS);
}

static void op_execute(message_t *request) {
    int op_code = le16_to_cpu(request->header.type);
    if(op_code == msg_ping) {
        _do_op_ping(request);
    } else if (MSG_TYPE_OSS(op_code)) {
        _do_op_oss(request);
    } else {
        _do_op_unknown(request);
    }
}

static void _on_recv_message(message_t *m) {
    // log_info("Recv a message done , m->meta=%u, m->data=%u\n" , m->header.meta_length ,m->header.data_length);
    
    log_debug("Recv a message done , m->id=%lu, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    message_t _m ;
    
    
    /**
     * 承接源 message *m* 中的所有内容
     * 阻止 _on_** 调用后释放 m 内的 meta_buffer 和 data_buffer
     * 尽管这个操作看起来有些奇怪
     */
    message_move(&_m, m);
    
    // reactor_ctx()->rx_last_window += message_get_data_len(&_m) +
    //     message_get_meta_len(&_m) + sizeof(message_t);
    // reactor_ctx()->rx_io_last_window++;

    op_execute(&_m);
}


static void _on_send_message(message_t *m) {
    log_debug("Send a message done , m->id=%lu, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    // reactor_ctx()->tx_last_window += message_get_data_len(m) +
    //     message_get_meta_len(m) + sizeof(message_t);
    // reactor_ctx()->tx_io_last_window++;
}



// static void _idle_reset(void *rctx_) {
//     reactor_ctx_t *rctx = rctx_;
//     rctx->idle_poll_start_us = now();
//     rctx->rx_last_window = 0;
//     rctx->tx_last_window = 0;
//     rctx->rx_io_last_window = 0;
//     rctx->tx_io_last_window = 0;
// }

// static int _do_idle(void *rctx_) {
//     reactor_ctx_t *rctx = rctx_;

//     //1ms
//     static const uint64_t window_10Gbps = 
//         (1250 * 1000 * 1000ULL) / (1000); 
//     //1ms
//     static const uint64_t window_iops = 16; 
    
//     uint64_t dx = spdk_max(rctx->tx_last_window , rctx->rx_last_window);
//     uint64_t dx_iops = spdk_max(rctx->tx_io_last_window , rctx->rx_io_last_window);
    
//     log_debug("dx=%lu,dx_iops=%lu\n",dx , dx_iops);
    
//     if(dx >= window_10Gbps / 2  || dx_iops >= window_iops / 2) {
//         rctx->running_level = BUSY_MAX;
//         return 0;
//     } else if (dx >= window_10Gbps / 4 || dx_iops >= window_iops / 4 ) {
//         rctx->running_level = BUSY1;
//         int i = 10;
//         while(--i)
//             spdk_pause();
//         return 0;
//     } else if (dx >= window_10Gbps / 8 || dx_iops >= window_iops / 8) {
//         rctx->running_level = BUSY2;
//         int i = 10;
//         while(--i)
//             spdk_pause();       
//         return 0;
//     } else if ( dx > 0  || dx_iops > 0 ) {
//         sched_yield();     
//         return 0;
//     } else {
//         rctx->running_level++;
//         if(rctx->running_level == IDLE) {
//             rctx->running_level = 0;
//             usleep(1000);
//             return 0;
//         } else {
//             usleep(100);
//             return 0;
//         }
//     }
//     return 0;
// }

// static int idle_poll(void *rctx_) {
//     reactor_ctx_t *rctx = rctx_;
//     uint64_t now_ = now();
//     uint64_t dur = now_ - rctx->idle_poll_start_us;
//     if(dur >= rctx->idle_poll_exe_us) {
//         _do_idle(rctx_);
//         _idle_reset(rctx_);
//         return 1;
//     }
//     return 0;
// }

//Stop routine
int _ostore_stop(const objstore_impl_t *oimpl){
    int rc = oimpl->unmount();
    return rc;
}
int _msgr_stop(const msgr_server_if_t *smsgr_impl) {
    if(smsgr_impl) {
        smsgr_impl->messager_stop();
        smsgr_impl->messager_fini();
    }

    return 0;
}
void _per_reactor_stop(void * ctx , void *err) {
    (void)err;
    (void)ctx;
    reactor_ctx_t * rctx = reactor_ctx();
    log_info("Stopping server[%d],[%s:%d]....\n", rctx->reactor_id,rctx->ip,rctx->port);
    
    _msgr_stop(rctx->msgr_impl);
    _msgr_stop(rctx->ipc_msgr_impl);

    _ostore_stop(rctx->os_impl);
    
    //...
    // if(rctx->idle_enable)
    //     spdk_poller_unregister(&rctx->idle_poller);

    rctx->running = false;
    _mm_mfence();
    log_info("Stopping server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
    return;
}
void _sys_fini() {
    int i;
    SPDK_ENV_FOREACH_CORE(i) {
        if(i != spdk_env_get_first_core())  {
            struct spdk_event * e = spdk_event_allocate(i,_per_reactor_stop,NULL,NULL);
            spdk_event_call(e);
        }
    }

    if(spdk_env_get_current_core() == spdk_env_get_first_core()) {
        _per_reactor_stop( NULL, NULL);
        while (reactor_reduce_state() != 0) //Wait until 
            spdk_delay_us(1000);

        //IF master
        log_info("Stoping app....\n");
        spdk_app_stop(0);
    }
}

int _ostore_boot(const objstore_impl_t *oimpl , int new) {
    //TODO get ostore global config
    //....
    
    int flags = 0;
    int rc;
    if(new) {
        rc = oimpl->mkfs(dev_list,flags);
        assert (rc == OSTORE_EXECUTE_OK);
    }
    rc = oimpl->mount(dev_list,flags);
    return rc;
}

int _msgr_boot(const msgr_server_if_t *smsgr_impl) {

    //TODO get msgr global config
    //....
    reactor_ctx_t *rctx = reactor_ctx();
    messager_conf_t conf = {
        .sock_type = 0,
        .ip = rctx->ip,
        .port = rctx->port,
        .on_recv_message = _on_recv_message,
        .on_send_message = _on_send_message,
        .data_buffer_alloc = alloc_data_buffer,
        .data_buffer_free = free_data_buffer,
        .meta_buffer_alloc = alloc_meta_buffer,
        .meta_buffer_free = free_data_buffer
    };
    int rc = smsgr_impl->messager_init(&conf);
    if(rc) {
        return rc;
    }

    rc = smsgr_impl->messager_start();
    return rc;
}

void _per_reactor_boot(void * ctx , void *err) {
    (void)err;
    (void)ctx;
    reactor_ctx_t *rctx = reactor_ctx();

    // rctx->idle_enable = g_idle;
    // if(rctx->idle_enable) {
    //     rctx->idle_poller = spdk_poller_register(idle_poll,rctx, 1000);
    //     rctx->idle_poll_start_us = now();
    //     rctx->idle_poll_exe_us = 1000;
    // }

    //ObjectStore initialize
    rctx->os_impl = ostore_get_impl(g_store_type);
    _ostore_boot(rctx->os_impl,true);
    log_info("Booting object store, type =[%d]....done\n", g_store_type);

    if(!g_ipc_msgr && !g_net_msgr) {
        log_warn("No messager init\n");
    }
    //Msgr initialize
    if(g_ipc_msgr) {
        rctx->ipc_msgr_impl = msgr_get_ipc_server_impl();
    }
    if(g_net_msgr) {
        rctx->msgr_impl = msgr_get_server_impl();
    }
    
    if(g_net_msgr) {
        _msgr_boot(rctx->msgr_impl);
    }
    if(g_ipc_msgr) {
        _msgr_boot(rctx->ipc_msgr_impl);
    }

    log_info("Booting messager ....done\n");

    rctx->running = true;
    _mm_mfence();

    // spdk_thread_get
    log_info("Booting server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
}

static void _zcell_config_init() {

    void *cfg = spdk_memzone_reserve_aligned(ZCELL_IPC_CONFIG_NAME, sizeof(struct zcell_ipc_config_t) ,SPDK_ENV_SOCKET_ID_ANY , 0 , 0);
    assert(cfg);
    memset(cfg , 0 , sizeof(struct zcell_ipc_config_t));
    
    struct zcell_ipc_config_t *zic = cfg;


    zic->tgt_nr = 1;
    zic->tgt_cores[0] = 1;

    zic->zcell_nr = 1;
    zic->zcell_cores[0] = 0;

    //Hardcode
    uint32_t i , j;
    for (i = 0 ; i < zic->tgt_nr ; ++i) {
        for (j = 0 ; j < zic->zcell_nr ; ++j) {
            uint32_t from = zic->tgt_cores[i];
            uint32_t to = zic->zcell_cores[j];
            log_info("Create ring from %u to %u \n" , from , to);
            zic->rings[from][to] = spdk_ring_create(SPDK_RING_TYPE_SP_SC, 512 , SPDK_ENV_SOCKET_ID_ANY);
            log_info("Create ring from %u to %u \n" , to , from);
            zic->rings[to][from] = spdk_ring_create(SPDK_RING_TYPE_SP_SC, 512 , SPDK_ENV_SOCKET_ID_ANY);
            assert(zic->rings[from][to]);
            assert(zic->rings[to][from]);
        }
    }
    return;
}

void _sys_init(void *arg) {
    (void)arg;
    int i;
    if(g_ipc_msgr) {
        if(spdk_env_get_current_core() == spdk_env_get_first_core()) {
            _zcell_config_init();
            log_info("IPC rings created done\n");
        }
    }


    //prepare per reactor context
    SPDK_ENV_FOREACH_CORE(i) {
        reactor_ctx_t myctx = {
            .reactor_id = i,
            .ip = g_base_ip,
            .port = g_base_port + i,
        };
        memcpy(&g_reactor_ctxs[i], &myctx, sizeof(myctx));
    }


    SPDK_ENV_FOREACH_CORE(i) {
        if(i != spdk_env_get_first_core())  {
            struct spdk_event * e = spdk_event_allocate(i,_per_reactor_boot,NULL,NULL);
            spdk_event_call(e);
        }      
    }

    spdk_delay_us(1000);
    
    if(spdk_env_get_current_core() == spdk_env_get_first_core()) {
        _per_reactor_boot(NULL, NULL);       
        while (reactor_reduce_state() != spdk_env_get_core_count())
            spdk_delay_us(1000);

        log_info("All reactors are running\n");
    }
}


int spdk_app_run() {
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;
    opts.config_file = "spdk.conf";
    // opts.print_level = 1;
    int rc = spdk_app_start(&opts , _sys_init , NULL);
    if(rc) {
        return -1;
    }
    spdk_app_fini();
    return 0;
}

int main( int argc , char **argv) {
    parse_args(argc ,argv);
    return spdk_app_run();
}
