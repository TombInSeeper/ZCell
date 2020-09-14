#include "objectstore.h"
#include "errcode.h"


#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"

static int g_store = CHUNKSTORE;
static const char* g_nvme_dev[] = { "Nvme0n1" , NULL, NULL };
static int g_nr_ops = 100;
static int g_nr_cpl = 0;
static int g_dbg_level = 10;

// static uint64_t g_start_tsc;
// static uint64_t g_end_tsc;

const objstore_impl_t *os;
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


void* _alloc_write_op() {

    static int seq = 0;

    void *p = calloc(1, sizeof(message_t) + 16);
    memcpy(p, &fake_write_request_msg, sizeof(fake_write_request_msg));

    message_t *m = p;
    m->header.seq = seq++;
    m->meta_buffer = malloc(sizeof(op_write_t));
    op_write_t *_op_args = (void*)m->meta_buffer;
    _op_args->len = 0x1000;
    _op_args->ofst = 0x0;
    _op_args->oid = 0x0;
    _op_args->flags = 0x0;
    m->data_buffer = spdk_dma_zmalloc(0x1000,0x1000,NULL);
    return p;
}

void _free_write_op(void *p) {
    message_t *m = p;
    spdk_free(m->data_buffer);
    free(m->meta_buffer);
    free(p);
}

void _sys_fini()
{
    os->unmount();


    spdk_free(rbuf);
    spdk_free(wbuf);

    spdk_app_stop(0);
}


void _write_complete(void *ctx, int sts) {
    if(sts) {
        assert("status code error\n" == NULL);
    }
    _free_write_op(ctx);
    g_nr_cpl++;
    if(g_nr_cpl >= g_nr_ops) {
        _sys_fini();
    } else {
       void *op = _alloc_write_op();
       int rc = os->obj_async_op_call(op, _write_complete);
       if(rc) {
            assert("submit error\n" == NULL);
       }
    }
}

void _do_uint_test() {
    int dp = 1;
    while (--dp) {
        void *op = _alloc_write_op();
        if(os->obj_async_op_call(op, _write_complete)) {
            assert("submit error\n" == NULL);
        }
    }
}

void _sys_init(void *arg) {
    (void)arg;
    spdk_log_set_level(g_dbg_level);

    rbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);
    wbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);


    os = ostore_get_impl(g_store);
    int rc = os->mkfs(g_nvme_dev,0);
    assert(rc == SUCCESS);

    rc = os->mount(g_nvme_dev,0);
    assert(rc == SUCCESS);

    _do_uint_test();
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "d:")) != -1) {
		switch (opt) {
		case 'd':
			g_dbg_level = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-d dbg_level]\n", argv[0]);
			exit(1);
		}
	}
}

int main( int argc , char **argv)
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "server";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;

    parse_args(argc,argv);

    SPDK_NOTICELOG("starting app\n");
    int rc = spdk_app_start(&opts , _sys_init , NULL);
    if(rc) {
        return -1;
    }
    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();

    return 0;
}