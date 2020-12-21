#include <spdk/env.h>
#include <spdk/event.h>

#include <spdk/bdev.h>

#include "util/log.h"
#include "message.h"
#include "spdk_ipc_config.h"

static const char *g_tgt_process_coremask = "[0,1]";
static const char *g_zcell_process_coremask = "[2,3]";
static int g_is_primary;
static int g_zcell_master_init_done;
static const char *g_zcell_cfg_name = "zcell_ipc_config";

static struct zcell_ipc_config_t *g_zcfg;


struct reactor_ctx_t {
    struct spdk_poller *recv_poller;
};

static __thread struct reactor_ctx_t rctx;

void _sys_fini() {
    spdk_app_stop(0);
}


void _tgt_init()
{
    log_info("******Target******\n");
    log_info("Number of cores:%u\n" , spdk_env_get_core_count());
    log_info("Master core:%u\n" , spdk_env_get_current_core());
    log_info("Thread count=%u\n" , spdk_thread_get_count());
    
}



void _zcell_channel_setup()
{


}

void _zcell_configure_init()
{


}


void _zcell_master_init() {

    void *cfg = spdk_memzone_reserve_aligned(g_zcell_cfg_name, sizeof(struct zcell_ipc_config_t) ,SPDK_ENV_SOCKET_ID_ANY , 0 , 0);
    assert(cfg);

    struct zcell_ipc_config_t *zic = cfg;
    zic->zcell_reactor_num = spdk_env_get_core_count();
    for (uint32_t i = 0 , c = spdk_env_get_first_core(); i < spdk_env_get_core_count() ; 
        ++i , c = spdk_env_get_next_core(c)) {
        zic->zcell_cpu_cores[i] = c;
        // char name[256];
        // sprintf(name, "zcell_msg_pool%u" , c);
        // zic->zcell_msg_pool[c] = spdk_mempool_create( name , 1024 , sizeof(message_t) , 0 )
        zic->zcell_rings[c] = spdk_ring_create(SPDK_RING_TYPE_MP_SC , 512 , SPDK_ENV_SOCKET_ID_ANY);
        assert(zic->zcell_rings[c]);
    }
    return;
}


static inline struct spdk_ring* _zcell_myring()
{
    uint32_t lcore = spdk_env_get_current_core();
    return g_zcfg->zcell_rings[lcore];
}

int _zcell_recv_poll(void *foo)
{
    struct spdk_ring *ring = _zcell_myring();
    if(spdk_ring_count(ring)) {
        void *msg[32];
        size_t count = spdk_ring_dequeue(ring, msg , 32);
        //for_each_msg
        //do handle
        return count;
    }
    return 0;
}


void _zcell_init_continue(void *arg1 , void *arg2);

void _zcell_reactor_common_init()

{
    // while (*(volatile int *)&g_zcell_master_init_done == 0)
    //         ;
    rctx.recv_poller = spdk_poller_register(_zcell_recv_poll , NULL , 0 ) ;
    log_info("Zcell[%u] init done\n" , spdk_env_get_current_core());
    struct spdk_event *e = spdk_event_allocate(spdk_env_get_first_core(),_zcell_init_continue , NULL ,NULL);
    spdk_event_call(e);
    return;
}

void _zcell_reactor_master_init()
{
    if(spdk_env_get_current_core() == spdk_env_get_first_core()) {
        //master
        _zcell_master_init();
        g_zcfg = spdk_memzone_lookup(g_zcell_cfg_name);
        log_info("Zcell configure init done\n");
        // *(volatile int *)&g_zcell_master_init_done = 1;
        // _mm_mfence();
    } 
}
void _zcell_init_continue(void *arg1 , void *arg2)
{
    static int n = 0;
    ++n;
    if( n == spdk_env_get_core_count()) {
        log_info("Init done\n");
    }
}

void _zcell_init()
{
    log_info("******ZCell******\n");
    log_info("Number of cores:%u\n" , spdk_env_get_core_count());
    log_info("Master core:%u\n" , spdk_env_get_current_core());
    log_info("Reactor count=%u\n" , spdk_thread_get_count());


    _zcell_reactor_master_init();

    uint32_t i;
    SPDK_ENV_FOREACH_CORE(i) {
        _zcell_reactor_common_init();
    }


}


void _sys_init(void *arg) {
    (void)arg;
    if(g_is_primary) {
        _zcell_init();
    } else {
        _tgt_init();
    }
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "I")) != -1) {
        switch (opt) {
        case 'I':
            g_is_primary = 1;
            break;
        default:
            log_info("Usage: %s \n" , argv[0]);
            exit(1);
            break;
        }       
	}
}



int main(int argc , char **argv) {
    
    
    parse_args(argc,argv);

    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.shm_id = 1;

    if(g_is_primary) {
        opts.reactor_mask = g_zcell_process_coremask;
        opts.rpc_addr = "/var/tmp/spdk_zcell.sock";
        opts.name = "zcell";
    } else {
        opts.reactor_mask = g_tgt_process_coremask;
        opts.rpc_addr = "/var/tmp/spdk_tgt.sock";
        opts.name = "spdk-tgt";
    }
    opts.shutdown_cb = _sys_fini;
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
    spdk_app_start(&opts , _sys_init , NULL);
 
    spdk_app_fini();

    return 0;
}