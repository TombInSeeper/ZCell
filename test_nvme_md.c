#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/bdev.h"


typedef struct hello_ctx_t {
    void *dbuf;
    void *mbuf;

    void *rdbuf;
    void *rmbuf;

    struct spdk_bdev *bdev;
    struct spdk_bdev_desc *desc;
    struct spdk_io_channel *ioch;
} hello_ctx_t;

void spdk_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
				     void *event_ctx)
{
    return;
}

void _sys_stop ( ) {
    spdk_app_stop(0);
}
void _read_cb(struct spdk_bdev_io *bio, bool success, void *cb_arg ) {
    spdk_bdev_free_io(bio);
    if(!success) {
        printf("Write IO error\n");
        return;
    }
    hello_ctx_t *h = cb_arg;
    printf("Write:%s,Read:%s\n",(char*)h->dbuf,(char*)h->rdbuf);
    printf("Write(md):%s,Read(md):%s\n",(char*)h->mbuf,(char*)h->rmbuf);
    if(strncmp(h->dbuf,h->rdbuf,32)) {
        printf("Data inconsitent\n");
    }
    if(strncmp(h->mbuf,h->rmbuf,32)) {
        printf("MetaData inconsitent\n");
    }
    _sys_stop();
}
void _write_cb(struct spdk_bdev_io *bio, bool success, void *cb_arg ) {
    spdk_bdev_free_io(bio);
    if(!success) {
        printf("Write IO error\n");
        _sys_stop();
        return;
    }
    hello_ctx_t *h = cb_arg;
    spdk_bdev_read_blocks_with_md(h->desc,h->ioch,h->dbuf,h->mbuf,0,1,_read_cb, h);
}

void _sys_start(void * arg) {
    hello_ctx_t * h = arg;
    h->dbuf = spdk_dma_zmalloc(0x1000,0,NULL);
    h->mbuf = spdk_dma_zmalloc(0x1000,0,NULL);
    h->rdbuf = spdk_dma_zmalloc(0x1000,0,NULL);
    h->rmbuf = spdk_dma_zmalloc(0x1000,0,NULL);
    strcpy(h->dbuf, "data\n");
    strcpy(h->mbuf,"metadata\n");
    spdk_bdev_open_ext("Nvme0n1", true, spdk_bdev_event_cb , NULL, &h->desc);
    assert(h->desc);
    h->ioch = spdk_bdev_get_io_channel(h->desc);
    assert (h->ioch);
    spdk_bdev_write_blocks_with_md(h->desc,h->ioch,h->dbuf,h->mbuf,0,1,_write_cb, h);
}


int main() {

    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.shutdown_cb = _sys_stop;
    opts.config_file = "spdk.conf";
    hello_ctx_t *h = calloc(1, sizeof(hello_ctx_t));
    int rc = spdk_app_start(&opts , _sys_start , h);
    if(rc) {
        return -1;
    }
    free(h);
    spdk_app_fini();
    return 0;



    return 0;
}