#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/bdev.h>
#include <spdk/rpc.h>
#include <spdk/nvme.h>
#include "util/log.h"

static const char *devname = "ZDisk1";

// SPDK_RPC_REGISTER("construct_zcell_bdev", spdk_rpc_construct_zcell_bdev, SPDK_RPC_STARTUP | SPDK_RPC_RUNTIME);

// static void rpc_reg ()


static void *wbuf;
static void *rbuf;
static struct spdk_bdev_desc *bd;
static struct spdk_io_channel *ch;

void spdk_app_done(void *arg) {

    spdk_free(rbuf);
    spdk_free(wbuf);
    if(ch) {
        spdk_put_io_channel(ch);
    }
    if(bd) {
        spdk_bdev_close(bd);
    }

    spdk_app_stop(0);
}

void read_cb (struct spdk_bdev_io *bdev_io,
		bool success,
		void *cb_arg)
{
    log_info("Read done\n");
    
    spdk_app_done(NULL);
}

void write_cb (struct spdk_bdev_io *bdev_io,
		bool success,
		void *cb_arg)
{
    assert(success);
    spdk_bdev_free_io(bdev_io);
    log_info("Write done\n");
    int rc = spdk_bdev_read_blocks(bd , ch , rbuf , 0 , 1 , read_cb , NULL);
    assert(rc == 0);
}


void spdk_app_run_tgt(void *arg) {


};

void spdk_app_run(void *arg) {


    struct spdk_bdev *d = spdk_bdev_get_by_name(devname);
    if(!d) {
        log_err("Fuck\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good!\n");
    }
    spdk_bdev_open( d , 1 , NULL , NULL, &bd);
    
    if(!bd) {
        log_err("Fuck2\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good2!\n");
    }
    ch = spdk_bdev_get_io_channel(bd);
    if(!ch) {
        log_err("Fuck3\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good3!\n");
    }
    wbuf = spdk_dma_malloc(0x1000, 0x1000, NULL);
    rbuf = spdk_dma_malloc(0x1000, 0x1000, NULL);
    int rc = spdk_bdev_write_blocks(bd , ch , wbuf , 0 , 1 , write_cb , NULL);
    assert(rc == 0);
}




static const char *null_bdev[] = {
    "Null0",
    "Null1",
    "Null2",
    "Null3",
};

struct qos_context {
    struct spdk_bdev_desc *bdev_desc[16];
    struct spdk_io_channel *bdev_ioch[16];
};

int qos_poller ( void *arg ) 
{

}



void spdk_app_run_qos_test(void *arg) {
    

    struct spdk_bdev *d = spdk_bdev_get_by_name(devname);
    if(!d) {
        log_err("Fuck\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good!\n");
    }
    spdk_bdev_open( d , 1 , NULL , NULL, &bd);
    
    if(!bd) {
        log_err("Fuck2\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good2!\n");
    }
    ch = spdk_bdev_get_io_channel(bd);
    if(!ch) {
        log_err("Fuck3\n");
        spdk_app_done(NULL);
    } else {
        log_info("Good3!\n");
    }
    wbuf = spdk_dma_malloc(0x1000, 0x1000, NULL);
    rbuf = spdk_dma_malloc(0x1000, 0x1000, NULL);
    int rc = spdk_bdev_write_blocks(bd , ch , wbuf , 0 , 1 , write_cb , NULL);
    assert(rc == 0);
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

    if(argc > 1) {
        spdk_app_start(&opt, spdk_app_run , NULL);
    } else {
        spdk_app_start(&opt, spdk_app_run_tgt , NULL);
    }

    spdk_app_fini();
}