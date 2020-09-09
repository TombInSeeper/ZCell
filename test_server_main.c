
#include "spdk/env.h"
#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/util.h"


#include "messager.h"
#include "objectstore.h"
#include "operation.h"
#include "fixed_cache.h"


#define NR_REACTOR_MAX 256

static const char *g_base_ip = "0.0.0.0";
static int g_base_port = 18000;
static const char *g_core_mask = "0x1";
static const int g_store_type = NULLSTORE;

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "i:p:c:")) != -1) {
		switch (opt) {
		case 'i':
			g_base_ip = optarg;
			break;
		case 'p':
			g_base_port = atoi(optarg);
			break;
        case 'c':
			g_core_mask = (optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-i ip] [-p port] [-c core_mask]\n", argv[0]);
			exit(1);
		}
	}
}


typedef struct reactor_ctx_t {
    int reactor_id;
    const char *ip;
    int port;

    
    const msgr_server_if_t *msgr_impl;
    const objstore_impl_t  *os_impl;

    struct fcache_t *dma_pages;

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


static void *alloc_meta_buffer(uint32_t sz) {
    return malloc(sz);
}
static void free_meta_buffer(void *p) {
    free(p);
}

static void *alloc_data_buffer(uint32_t sz) {
    void *ptr;
    if(sz <= 0x1000)
        ptr =  fcache_get(reactor_ctx()->dma_pages); 
    else {    
        uint32_t align = (sz % 0x1000 == 0 )? 0x1000 : 0;
        ptr =  spdk_dma_malloc(sz, align, NULL);
    }
    return ptr;
}
static void free_data_buffer(void *p) {
    fcache_t *fc = reactor_ctx()->dma_pages;
    if(fcache_in(fc , p)) {
        fcache_put(fc, p);
    } else {
        spdk_dma_free(p);
    }
}


/**
 * 复用 request 结构生成 response
 * 这里我们对 request 的 meta_buffer 和 data_buffer 的处理方式是 lazy 的：
 * 只要将 header 的 meta_length 和 data_length 长度置 0， 
 * 那么 messager 发送时就不会发送 meta_buffer 和 data_buffer 的内容，：
 * request message 析构时，meta_buffer 和 data_buffer 如果是非空指针，会被自动被释放
 * 
 * meta_buffer : glibc free (后续可能也加上用户重载函数)
 * data_buffer : .. 用户重载的函数
 * 
 * 
 */
static inline void _response_with_reusing_request(message_t *request, uint16_t status_code) {
    request->header.status = cpu_to_le16(status_code);
    message_state_reset(request);
    msgr_debug("Perpare to send reponse :[status=%u]\n",request->header.status);
    reactor_ctx()->msgr_impl->messager_sendmsg(request);
}

static inline void _response_broken_op(message_t *request, uint16_t status_code) {
    request->header.data_length = 0;
    request->header.meta_length = 0;
    _response_with_reusing_request(request, status_code);
}

//Operation handle
static void _do_op_unknown(message_t *request) {
    _response_broken_op(request, UNKNOWN_OP);
}

static void oss_op_cb(void *ctx, int status_code) {
    message_t *request = ctx;
    if(status_code != SUCCESS || status_code != OSTORE_EXECUTE_OK) {
        //Broken operation
        _response_broken_op(request,status_code);
    }
    _response_with_reusing_request(request, status_code);
    free(request);
}


//Step1:Check
static bool oss_op_valid(message_t *request) {
    int op = le16_to_cpu(request->header.type);
    bool rc = false;
    switch (op) {
        case MSG_OSS_OP_STATE: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == 0) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer == NULL);       
        }
            break;
        case MSG_OSS_OP_CREATE: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == sizeof(op_create_t)) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer != NULL);       
        }
            break; 
        case MSG_OSS_OP_DELETE: {
            rc = (le32_to_cpu(request->header.data_length) == 0) && 
            (le16_to_cpu(request->header.meta_length) == sizeof(op_delete_t)) && 
            (request->data_buffer == NULL) && 
            (request->meta_buffer != NULL) ;       
        }
            break;
        case MSG_OSS_OP_WRITE:{
            op_write_t *op = (op_write_t *)request->meta_buffer;
            rc =  (request->data_buffer != NULL) && 
            (request->meta_buffer != NULL) && 
            (le32_to_cpu(request->header.data_length) == le32_to_cpu(op->len)) && 
            (le16_to_cpu(request->header.meta_length) > 0 );       
        }
            break;
        case MSG_OSS_OP_READ:{
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
        case MSG_OSS_OP_STATE : {
            request->header.meta_length = sizeof(op_stat_t);
            request->meta_buffer = alloc_meta_buffer(sizeof(op_stat_t));
        }
            break;
        case MSG_OSS_OP_CREATE: {
            request->header.meta_length = 0;      
        }
            break; 
        case MSG_OSS_OP_DELETE: {
            request->header.meta_length = 0;            
        }
            break;
        case MSG_OSS_OP_WRITE:{
            request->header.meta_length = 0;             
            request->header.data_length = 0;             
        }
            break;
        case MSG_OSS_OP_READ:{
            op_read_t *op = (op_read_t *)request->meta_buffer;
            request->header.meta_length = 0;             
            request->header.data_length = op->len; 
            request->data_buffer = alloc_data_buffer(le32_to_cpu(op->len));           
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

    msgr_debug("Prepare to execute op:[seq=%u,op_code=%u,meta_len=%u,data_len=%u]", request->header.seq,
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
    if(op_code == MSG_PING) {
        _do_op_ping(request);
    } else if (MSG_TYPE_OSS(op_code)) {
        _do_op_oss(request);
    } else {
        _do_op_unknown(request);
    }
}






static void _on_recv_message(message_t *m) {
    // msgr_info("Recv a message done , m->meta=%u, m->data=%u\n" , m->header.meta_length ,m->header.data_length);
    msgr_info("Recv a message done , m->id=%u, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    message_t _m ;
    /**
     * 承接 original message *m* 中的所有内容
     * 阻止 _on_** 调用后释放 m 内的 meta_buffer 和 data_buffer
     * 尽管这个操作看起来有些奇怪
     */
    message_move(&_m, m);

    op_execute(&_m);
}
static void _on_send_message(message_t *m) {
    msgr_info("Send a message done , m->id=%u, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
}

//Stop routine
int _ostore_stop(const objstore_impl_t *oimpl){
    int rc = oimpl->unmount();
    return rc;
}
int _msgr_stop(const msgr_server_if_t *smsgr_impl) {
    smsgr_impl->messager_stop();
    smsgr_impl->messager_fini();
    return 0;
}
void _per_reactor_stop(void * ctx , void *err) {
    (void)err;
    (void)ctx;
    reactor_ctx_t * rctx = reactor_ctx();
    SPDK_NOTICELOG("Stopping server[%d],[%s:%d]....\n", rctx->reactor_id,rctx->ip,rctx->port);
    // SPDK_NOTICELOG("Stopping server[%d],[%s:%d]....\n", rctx->reactor_id,rctx->ip,rctx->port);
    
    _msgr_stop(rctx->msgr_impl);
    // SPDK_NOTICELOG("Stopping server[%d],[%s:%d] msgr .... done \n", rctx->reactor_id,rctx->ip,rctx->port);

    _ostore_stop(rctx->os_impl);
    // SPDK_NOTICELOG("Stopping server[%d],[%s:%d] ostore .... done \n", rctx->reactor_id,rctx->ip,rctx->port);
    
    //...
    fcache_destructor(rctx->dma_pages);

    rctx->running = false;
    SPDK_NOTICELOG("Stopping server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
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
        SPDK_NOTICELOG("Stoping app....\n");
        spdk_app_stop(0);
    }
}

int _ostore_boot(const objstore_impl_t *oimpl , int new) {
    //TODO get ostore global config
    //....
    const char *dev_list[] = {NULL , NULL ,NULL};
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
        .data_buffer_free = free_data_buffer
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

    //Load dma page pool
    rctx->dma_pages = fcache_constructor(15000, 0x1000, SPDK_MALLOC);
    assert(rctx->dma_pages);

    //ObjectStore initialize
    rctx->os_impl = ostore_get_impl(g_store_type);
    _ostore_boot(rctx->os_impl,true);

    //Msgr initialize
    rctx->msgr_impl = msgr_get_server_impl();
    _msgr_boot(rctx->msgr_impl);

    rctx->running = true;
    // spdk_thread_get
    SPDK_NOTICELOG("Booting server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
}
void _sys_init(void *arg) {
    (void)arg;
    int i;
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

        SPDK_NOTICELOG("All reactors are running\n");
    }
}


int spdk_app_run() {
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.reactor_mask = g_core_mask;
    opts.shutdown_cb = _sys_fini;
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
