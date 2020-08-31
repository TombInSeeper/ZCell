#include"messager.h"

#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/sock.h"
#include "spdk/util.h"

static msgr_server_if_t sif;
// static msgr_client_if_t cif;

static void *alloc_data_buffer( uint32_t sz) {
    uint32_t align = (sz % 0x1000 == 0 )? 0x1000 : 0;
    return spdk_dma_malloc(sz, align, NULL);
}

static void free_data_buffer(void *p) {
    spdk_dma_free(p);
}

void _sys_fini()
{
    SPDK_NOTICELOG("Stoping messager\n");
    sif.messager_stop();
    sif.messager_fini();

    SPDK_NOTICELOG("Stoping app\n");
    spdk_app_stop(0);
}

void _on_recv_message(message_t *m)
{
    // msgr_info("Recv a message done , m->meta=%u, m->data=%u\n" , m->header.meta_length ,m->header.data_length);
    msgr_info("Recv a message done , m->id=%u, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    message_t _m ;
    message_move(&_m, m);
    message_state_reset(&_m);
    msgr_info("Echo \n");
    sif.messager_sendmsg(&_m);
}

void _on_send_message(message_t *m)
{
    msgr_info("Send a message done , m->id=%u, m->meta=%u, m->data=%u\n" , m->header.seq,
     m->header.meta_length ,m->header.data_length);
    // msgr_info("Send a message done\n");
}


void _sys_init(void *arg)
{
    (void)arg;
    msgr_server_if_init(&sif);

    messager_conf_t conf = {
        .ip = "127.0.0.1",
        .port = 18000,
        .on_recv_message = _on_recv_message,
        .on_send_message = _on_send_message,
        // .data_buffer_alloc = alloc_data_buffer,
        // .data_buffer_free = free_data_buffer
    };
    int rc = sif.messager_init(&conf);
    assert (rc == 0);

    rc = sif.messager_start();
    assert (rc == 0);
}


int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "test_messager";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;
    SPDK_NOTICELOG("test_messager echo server bootstarp\n");
    int rc = spdk_app_start(&opts , _sys_init , NULL);
    if(rc) {
        SPDK_ERRLOG("fuck\n");
        return -1;
    }
    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();

    return 0;
}
