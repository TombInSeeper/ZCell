#include "objectstore.h"
#include "util/errcode.h"
#include "util/chrono.h"
#include "util/log.h"

#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/util.h"


static __thread int g_store = ZSTORE;
static __thread const char* g_nvme_dev[] = { "Nvme0n1" , "/tmp/mempool", NULL };
static __thread int g_nr_ops = 100 * 10000;
static __thread int g_nr_submit = 0;
static __thread int g_nr_cpl = 0;
static __thread int g_qd = 32;
static __thread int g_max_oid = 10000;

static uint64_t g_start_us;
static uint64_t g_end_us;

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
        .type = msg_oss_op_stat,
        .meta_length = sizeof(op_stat_result_t),
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
        .type = msg_oss_op_create,
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
        .type = msg_oss_op_delete,
        .meta_length = sizeof(op_stat_result_t),
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
        .type = msg_oss_op_write,
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
        .type = msg_oss_op_read,
        .meta_length = sizeof(op_read_t),
        .data_length = 0,
    },
    .meta_buffer = NULL,
    .data_buffer = NULL,
    .priv_ctx = NULL 
};


void *_alloc_op_common(uint16_t op_type, uint64_t actx_sz) {
    // static int seq = 0;
    void *p = calloc(1, sizeof(message_t) + actx_sz + 128);
    switch (op_type) {
    case msg_oss_op_create:
        memcpy(p, &fake_create_request_msg, sizeof(fake_create_request_msg));
        break;
    case msg_oss_op_read:
        memcpy(p, &fake_read_request_msg, sizeof(fake_read_request_msg));
        break;
    case msg_oss_op_write:
        memcpy(p, &fake_write_request_msg, sizeof(fake_write_request_msg));
        break;
    case msg_oss_op_delete:
        memcpy(p, &fake_delete_request_msg, sizeof(fake_delete_request_msg));
        break;   
    default:
        break;
    }
    message_t *m = p;
    m->meta_buffer = (char*)(p + sizeof(message_t) + actx_sz);
    return p;

}

void _free_op_common( void *p) {
    free(p);
}

void* _alloc_write_op() {

    static int seq = 0;

    void *p = calloc(1, sizeof(message_t) + 16);
    memcpy(p, &fake_write_request_msg, sizeof(fake_write_request_msg));

    message_t *m = p;
    m->header.seq = seq++;
    // m->meta_buffer = malloc(sizeof(op_write_t));

    m->meta_buffer = meta_buffer;

    op_write_t *_op_args = (void*)m->meta_buffer;
    _op_args->len = 0x1000;
    _op_args->ofst = 0x0;
    _op_args->oid = (m->header.seq) % g_max_oid;
    _op_args->flags = 0x0;
    // m->data_buffer = spdk_dma_zmalloc(0x1000,0x1000,NULL);

    m->data_buffer = wbuf;

    return p;
}

void _free_write_op(void *p) {
    // message_t *m = p;
    // // spdk_free(m->data_buffer);
    // // free(m->meta_buffer);
    free(p);
}

void _sys_fini() {
    os->unmount();
    spdk_free(rbuf);
    spdk_free(wbuf);
    spdk_app_stop(0);
}


void _dump_perf() {
    double time = g_end_us - g_start_us; 
    double qps = (g_nr_ops / time) * 1e3;
    SPDK_NOTICELOG("qps=%lf K/s \n" , qps);
    SPDK_NOTICELOG("avg_lat= %lf us \n" , time / g_nr_ops);
}

void _write_complete(void *ctx, int sts) {
    if(sts) {
        assert("status code error\n" == NULL);
    }
    _free_write_op(ctx);
    g_nr_cpl++;
    if(g_nr_cpl == g_nr_submit) {
        g_end_us = now();
        _dump_perf();
        _sys_fini();
    } else if (g_nr_submit < g_nr_ops){
       void *op = _alloc_write_op();
       int rc = os->obj_async_op_call(op, _write_complete);
       if(rc) {
            assert("submit error\n" == NULL);
       }
       g_nr_submit++;
    } else {

    }
}


void _do_write_test( void* ctx , int st) {
    (void)ctx;
    (void)st;
    int dp = g_qd;
    g_start_us = now();
    while (dp--) {
        void *op = _alloc_write_op();
        if(os->obj_async_op_call(op, _write_complete)) {
            assert("submit error\n" == NULL);
        }
        g_nr_submit++;
    }
}


void _stat_cb( void* rqst ,int st) {
    message_t *m = rqst;
    op_stat_result_t *rst = (void*)(m->meta_buffer);    
    SPDK_NOTICELOG("ostore state:max_oid=%u ,cap_gb=%u G ,obj_sz=%u K ,obj_blksz=%u K\n",
        rst->max_oid ,rst->capcity_gib, rst->max_obj_size_kib, rst->obj_blk_sz_kib);
    g_max_oid = rst->max_oid;
    free(rqst);
    // _sys_fini();

    _do_write_test(NULL,0);
}


void _unit_test_done(void * r , int status) {
    assert(status == 0);
    _free_op_common(r);
    
    _sys_fini();
}

void _do_uint_test() {
    void *op = _alloc_op_common(msg_oss_op_create, os->obj_async_op_context_size());    
    op_create_t *opc = ((message_t *)(op))->meta_buffer;
    opc->oid = 0x1;
    os->obj_async_op_call(op, _unit_test_done);
}

void _sys_init(void *arg) {
    (void)arg;

    rbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);
    wbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);


    os = ostore_get_impl(g_store);

    uint64_t s , e ; 
    s = now();
    int rc = os->mkfs(g_nvme_dev,0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mkfs time %lu us \n" , (e - s));

    s = now();
    rc = os->mount(g_nvme_dev,0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mount time %lu us \n" , (e - s));
    

    _do_uint_test();

}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "q:c:o:")) != -1) {
		switch (opt) {
		case 'q':
			g_qd = atoi(optarg);
			break;
        case 'c':
			g_nr_ops = atoi(optarg);
			break;
        case 'o':
			g_max_oid = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-q qd] [-c nr_ops] \n", argv[0]);
			exit(1);
		}
	}
}

int main( int argc , char **argv) {
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "test_objectstore";
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