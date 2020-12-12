#include <spdk/env.h>
#include <spdk/event.h>

#include <spdk/bdev.h>

#include "util/log.h"

static const char *g_coremask = "0x1";


void _sys_fini() {
    spdk_app_stop(0);
}


void _sys_init(void *arg) {
    (void)arg;
    log_info("Number of cores:%u\n" , spdk_env_get_core_count());
    log_info("Master cores:%u\n" , spdk_env_get_current_core());
    const char  *name = "share";
    struct spdk_mempool *m = spdk_mempool_lookup(name);
    if(!m) {
        log_info("Create mempool:%s\n", name);
        m = spdk_mempool_create(name, 1024 , sizeof(int) , SPDK_MEMPOOL_DEFAULT_CACHE_SIZE , SPDK_ENV_SOCKET_ID_ANY);
        if(!m) {
            log_err("Fuck !\n");
            _sys_fini();
        }
    } else {
        log_info("Found mempool:%s\n", name);
        _sys_fini();
    }
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
        case 'c':
            g_coremask = optarg;
            break;
        default:
            log_info("Usage: ./test_ipc -c cpu_mask\n");
            exit(1);
            break;
        }       
	}
}



int main( int argc , char **argv) {

    parse_args(argc,argv);

    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.shm_id = 1;
    opts.name = "test_ipc";
    opts.reactor_mask = g_coremask;
    opts.shutdown_cb = _sys_fini;
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      



    spdk_app_start(&opts , _sys_init , NULL);
 
    spdk_app_fini();

    return 0;
}