#include "string.h"
#include "msgr.h"

void _sys_fini()
{
    SPDK_NOTICELOG("Stoping app\n");
    spdk_app_stop(0);
}

void _on_recv_message(struct message *m)
{
    // SPDK_NOTICELOG("recv msg done\n");

    struct message *_m = malloc(sizeof(struct message));
    memcpy(_m, m , sizeof(*m));
    _m->rw_len = 0 ;
    _m->rwstate = 0 ;

    msgr_push_msg(_m); // Echo
}

void _on_send_message(struct message *m)
{
    // SPDK_NOTICELOG("send msg done\n");
    spdk_free(m->payload);
    free(m);
}


void _sys_init(void *arg)
{
    (void)arg;
    msgr_init_opt_t opts = {
        .ip = "127.0.0.1",
        .port = 18000,
        .type = 0,
        .on_recv_message = _on_recv_message,
        .on_send_message = _on_send_message
    };
    int rc = msgr_init(&opts);
    assert ( rc == 0 );
}


int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "test_msgr";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;
    SPDK_NOTICELOG("starting app\n");
    int rc = spdk_app_start(&opts , _sys_init , NULL);
    if(rc) {
        SPDK_ERRLOG("fuck\n");
        return -1;
    }
    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();

    return 0;
}