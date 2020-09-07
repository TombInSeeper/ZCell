#include "objectstore.h"
#include "errcode.h"
#include "spdk/event.h"
#include "spdk/env.h"

#include "message.h"
#include "operation.h"

objstore_impl_t *os;
void *session;

char meta_buffer[0x1000];
char rbuf[0x1000];
char wbuf[0x1000];

message_t fake_stat_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_STATE,
        .meta_length = sizeof(op_stat_t),
        .data_length = 0,
    },
    .meta_buffer = meta_buffer,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};
message_t fake_create_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_STATE,
        .meta_length = sizeof(op_stat_t),
        .data_length = 0,
    },
    .meta_buffer = meta_buffer,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};



void _sys_init(void *arg)
{
    (void)arg;
    os = ostore_get_impl(FAKESTORE);
    os->mkfs(NULL,FAKESTORE);
    os->mount(NULL, FAKESTORE);
}


int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "server";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    // opts.shutdown_cb = _sys_fini;
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