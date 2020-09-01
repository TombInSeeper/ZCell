
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/sock.h"
#include "spdk/util.h"


#include "messager.h"
#include "fixed_cache.h"


#define NR_REACTOR_MAX 256

static const char *g_base_ip = "0.0.0.0";
static int g_base_port = 18000;
static const char *g_core_mask = "0x1";

typedef struct reactor_ctx_t {
    int reactor_id;
    const char *ip;
    int port;
    msgr_server_if_t mimpl;
    volatile bool running;
} reactor_ctx_t;


static  reactor_ctx_t g_reactor_ctxs[NR_REACTOR_MAX];

static inline reactor_ctx_t* reactor_ctx() {
    return &g_reactor_ctxs[spdk_env_get_current_core()];
}

static inline int reactor_reduce_state() {
    int i;
    int r = 0;
    SPDK_ENV_FOREACH_CORE(i) {
        r += g_reactor_ctxs[i].running;
    }
    return r;
}

static void *alloc_data_buffer( uint32_t sz) {
    // void *p = fcache_get(dma_pages);    
    // if(p) {
    //     return p;
    // }
    uint32_t align = (sz % 0x1000 == 0 )? 0x1000 : 0;
    return spdk_dma_malloc(sz, align, NULL);
}

static void free_data_buffer(void *p) {
    // fcache_put(dma_pages, p);
    spdk_dma_free(p);
}

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

static void _on_recv_message(message_t *m)
{
    // msgr_info("Recv a message done , m->meta=%u, m->data=%u\n" , m->header.meta_length ,m->header.data_length);
    msgr_info("Recv a message done , m->id=%lu, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    message_t _m ;
    message_move(&_m, m);
    message_state_reset(&_m);
    msgr_info("Echo \n");

    reactor_ctx()->mimpl.messager_sendmsg(&_m);
}

static void _on_send_message(message_t *m)
{
    msgr_info("Send a message done , m->id=%lu, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
}

void _per_reactor_stop(void * ctx , void *err) {
    (void)err;
    (void)ctx;
    reactor_ctx_t * rctx = reactor_ctx();
    // SPDK_NOTICELOG("Stopping server[%d],[%s:%d]....\n", rctx->reactor_id,rctx->ip,rctx->port);

    rctx->mimpl.messager_stop();
    rctx->mimpl.messager_fini();

    //...
    rctx->running = false;
    SPDK_NOTICELOG("Stopping server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
    return;
}
void _sys_fini()
{
    int i;
    SPDK_ENV_FOREACH_CORE(i) {
        if(i != spdk_env_get_first_core())  {
            struct spdk_event * e = spdk_event_allocate(i,_per_reactor_stop,NULL,NULL);
            spdk_event_call(e);
        }
    }

    if(spdk_env_get_current_core() == spdk_env_get_first_core()) {
        _per_reactor_stop( NULL, NULL);
        while (reactor_reduce_state() != 0)
            spdk_delay_us(1000);

        //IF master
        SPDK_NOTICELOG("Stoping app....\n");
        spdk_app_stop(0);
    }
}

void _per_reactor_boot(void * ctx , void *err) {
    (void)err;
    (void)ctx;
    reactor_ctx_t * rctx = reactor_ctx();
    // SPDK_NOTICELOG("Booting server[%d],[%s:%d]....\n", rctx->reactor_id,rctx->ip,rctx->port);
    msgr_server_if_init(&rctx->mimpl);
    msgr_server_if_t *pmif = &rctx->mimpl;
    messager_conf_t conf = {
        // .ip = rctx->ip,
        .port = rctx->port,
        .on_recv_message = _on_recv_message,
        .on_send_message = _on_send_message,
        .data_buffer_alloc = alloc_data_buffer,
        .data_buffer_free = free_data_buffer
    };
    strcpy(conf.ip , rctx->ip);
    int rc = pmif->messager_init(&conf);
    assert (rc == 0);
    rc = pmif->messager_start();
    assert (rc == 0);
    rctx->running = true;
    SPDK_NOTICELOG("Booting server[%d],[%s:%d]....done\n", rctx->reactor_id,rctx->ip,rctx->port);
}

void _sys_init(void *arg)
{
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

    mb();
    
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

int main( int argc , char **argv)
{
    parse_args(argc ,argv);
    return spdk_app_run();
}
