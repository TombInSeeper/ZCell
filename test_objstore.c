#include "objectstore.h"
#include "errcode.h"


#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"

static int g_store = CHUNKSTORE;
static const char* g_nvme_dev[] = { "Nvme0n1" , NULL, NULL };

objstore_impl_t *os;
void *session;

char meta_buffer[0x1000];
void *rbuf;
void *wbuf;

const message_t fake_stat_request_msg = {
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
const message_t fake_create_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_CREATE,
        .meta_length = sizeof(op_create_t),
        .data_length = 0,
    },
    .meta_buffer = meta_buffer,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};
const message_t fake_delete_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_DELETE,
        .meta_length = sizeof(op_stat_t),
        .data_length = 0,
    },
    .meta_buffer = meta_buffer,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};
const message_t fake_write_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_WRITE,
        .meta_length = sizeof(op_write_t),
        .data_length = 0,
    },
    .meta_buffer = NULL,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};
const message_t fake_read_request_msg = {
    .state = {
        .hdr_rem_len = 0,
        .meta_rem_len = 0,
        .data_rem_len = 0
    },
    .header = {
        .seq = 0,
        .type = MSG_OSS_OP_READ,
        .meta_length = sizeof(op_read_t),
        .data_length = 0,
    },
    .meta_buffer = NULL,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};



void _sys_fini()
{
    spdk_app_stop(0);
}


void _do_uint_test()
{
    os->unmount();
}

void _sys_init(void *arg)
{
    (void)arg;
    os = ostore_get_impl(g_store);
    int rc = os->mkfs(g_nvme_dev,0);
    assert(rc == SUCCESS);

    rc = os->mount(g_nvme_dev,0);
    assert(rc == SUCCESS);

    _do_uint_test();
}


int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "server";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;
    SPDK_NOTICELOG("starting app\n");
    int rc = spdk_app_start(&opts , _sys_init , NULL);
    if(rc) {
        return -1;
    }
    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();

    return 0;
}