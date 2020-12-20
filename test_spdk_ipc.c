#include <spdk/env.h>
#include <spdk/event.h>

#include <spdk/bdev.h>

#include "util/log.h"

static const char *g_tgt_process_coremask = "[0,1]";
static const char *g_zcell_process_coremask = "[2,3]";
static int g_is_primary;

void _sys_fini() {
    spdk_app_stop(0);
}



// struct zcell_cfg_t {
//     union {
//         struct {
//             void *myself;
//             uint32_t id;
//             uint32_t cpu;
//             struct spdk_ring *sq;
//             struct spdk_ring *cq;
//             struct spdk_mempool *msg_pool;
//         };
//         uint8_t _pad[128];
//     };
// };

// void _primary_process_reactor_init()
// {

//     char _reactor_config_space_name[128];
//     snprintf(_reactor_config_space_name , 128 , "zcell%u");

//     void *cfg = spdk_memzone_reserve_aligned(_reactor_config_space_name , sizeof(struct zcell_cfg_t), 
//     SPDK_ENV_SOCKET_ID_ANY , 0 , 0);
//     if(!cfg) {
//         log_err("memzone allocated failed\n");
//         return;
//     }

//     // struct zcell_cfg_t *zcfg = cfg;
//     // zcfg->myself = zcfg;
//     // zcfg->cpu = spdk_env_get_current_core();
//     // zcfg->cq = spdk_ring_create(RI)

// }

void _tgt_init()
{
    log_info("******Target******\n");
    log_info("Number of cores:%u\n" , spdk_env_get_core_count());
    log_info("Master core:%u\n" , spdk_env_get_current_core());
    log_info("Thread count=%u\n" , spdk_thread_get_count());
}






void _zcell_init()
{
    log_info("******ZCell******\n");
    log_info("Number of cores:%u\n" , spdk_env_get_core_count());
    log_info("Master core:%u\n" , spdk_env_get_current_core());
    log_info("Thread count=%u\n" , spdk_thread_get_count());
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
        opts.name = "zcell";
    } else {
        opts.reactor_mask = g_tgt_process_coremask;
        opts.name = "spdk-tgt";
    }
    opts.shutdown_cb = _sys_fini;
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
    spdk_app_start(&opts , _sys_init , NULL);
 
    spdk_app_fini();

    return 0;
}