#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/bdev.h>
#include <spdk/rpc.h>
#include "util/log.h"

static const char *devname = "ZDisk1";

// SPDK_RPC_REGISTER("construct_zcell_bdev", spdk_rpc_construct_zcell_bdev, SPDK_RPC_STARTUP | SPDK_RPC_RUNTIME);

// static void rpc_reg ()


void spdk_app_done(void *arg) {
    spdk_app_stop(0);
}

void spdk_app_run(void *arg) {


    struct spdk_bdev *d = spdk_bdev_get_by_name(devname);
    if(!d) {
        log_err("Fuck\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good!\n");
        spdk_app_done(NULL);
    }
}

int main ( int argc , char **argv)
{
    struct spdk_app_opts opt;
    spdk_app_opts_init(&opt);
    opt.json_config_file = "bdev_demo.json";
    opt.shm_id = 1;
    opt.reactor_mask = "0x2";
    opt.rpc_addr = "/var/tmp/bdev_demo.sock";
    opt.enable_coredump = 1;

    int rc = spdk_app_start(&opt, spdk_app_run , NULL);
    if(rc) {


    }
    spdk_app_fini();
}