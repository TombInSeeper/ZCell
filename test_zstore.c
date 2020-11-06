#include <spdk/env.h>
#include <spdk/event.h>


#include "zstore.h"
#include "util/log.h"

void _sys_fini() {
    log_info("Shutdown...\n");
    zstore_unmount();
    spdk_app_stop(0);
}

void _sys_init(void *arg) {
    const char *devs[] = {
        "Nvme0n1",
        "/tmp/mempool"
    };
    int rc = zstore_mkfs(devs,0);
    assert (rc == 0);

    rc = zstore_mount(devs,0);
    assert (rc == 0);

}


int main( int argc , char **argv) {
    

    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = argv[0];
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;
    


    spdk_app_start(&opts, _sys_init , NULL);
    
    
    spdk_app_fini();
    return 0;
}