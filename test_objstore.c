#include "objectstore.h"
#include "util/errcode.h"
#include "util/chrono.h"
#include "util/log.h"

#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/util.h"

// static __thread int g_store = ZSTORE;
// static __thread const char* g_nvme_dev[] = { "Nvme0n1" , "/tmp/mempool", NULL };
// static __thread int g_nr_ops = 100 * 10000;
// static __thread int g_nr_submit = 0;
// static __thread int g_nr_cpl = 0;
// static __thread int g_qd = 32;
// static __thread int g_max_oid = 30 * 1024;

// static uint64_t g_start_us;
// static uint64_t g_end_us;

// const objstore_impl_t *os;
// void *session;

// char meta_buffer[0x1000];
// void *rbuf;
// void *wbuf;

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
    .meta_buffer = NULL,
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
    .meta_buffer = NULL,
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
    .meta_buffer = NULL,
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




// void _dump_perf() {
//     double time = g_end_us - g_start_us; 
//     double qps = (g_nr_ops / time) * 1e3;
//     SPDK_NOTICELOG("qps=%lf K/s \n" , qps);
//     SPDK_NOTICELOG("avg_lat= %lf us \n" , time / g_nr_ops);
// }

// void _write_complete(void *ctx, int sts) {
//     if(sts) {
//         assert("status code error\n" == NULL);
//     }
//     _free_write_op(ctx);
//     g_nr_cpl++;
//     if(g_nr_cpl == g_nr_submit) {
//         g_end_us = now();
//         _dump_perf();
//         _sys_fini();
//     } else if (g_nr_submit < g_nr_ops){
//        void *op = _alloc_write_op();
//        int rc = os->obj_async_op_call(op, _write_complete);
//        if(rc) {
//             assert("submit error\n" == NULL);
//        }
//        g_nr_submit++;
//     } else {

//     }
// }


// void _do_write_test( void* ctx , int st) {
//     (void)ctx;
//     (void)st;
//     int dp = g_qd;
//     g_start_us = now();
//     while (dp--) {
//         void *op = _alloc_write_op();
//         if(os->obj_async_op_call(op, _write_complete)) {
//             assert("submit error\n" == NULL);
//         }
//         g_nr_submit++;
//     }
// }


// void _stat_cb( void* rqst ,int st) {
//     message_t *m = rqst;
//     op_stat_result_t *rst = (void*)(m->meta_buffer);    
//     SPDK_NOTICELOG("ostore state:max_oid=%u ,cap_gb=%u G ,obj_sz=%u K ,obj_blksz=%u K\n",
//         rst->max_oid ,rst->capcity_gib, rst->max_obj_size_kib, rst->obj_blk_sz_kib);
//     g_max_oid = rst->max_oid;
//     free(rqst);
//     // _sys_fini();

//     _do_write_test(NULL,0);
// }
// void _delete_test_done(void * r , int status) {
//     assert(status == 0);
//     _free_op_common(r);

//     _sys_fini();    
// }

// void _do_delete_test() {
//     void *op = _alloc_op_common(msg_oss_op_delete, os->obj_async_op_context_size());    
//     op_delete_t *opc = ((message_t *)(op))->meta_buffer;
//     opc->oid = 0x1;
//     int rc = os->obj_async_op_call(op, _delete_test_done);
//     if(rc) {
//         _sys_fini();
//     }
// }


// void _read_test_done(void * r , int status) {
//     assert(status == 0);
//     _free_op_common(r);
    
//     assert(!strcmp(rbuf,"123456789"));

//     _do_delete_test();
// }

// void _do_read_test() {
//     void *op = _alloc_op_common(msg_oss_op_read, os->obj_async_op_context_size());    
//     op_read_t *opc = ((message_t *)(op))->meta_buffer;
//     opc->oid = 0x1;
//     opc->ofst = 0x0;
//     opc->flags = 0;
//     opc->len = 0x1000;
    
//     message_t *m = op;
//     m->data_buffer = rbuf;
//     int rc = os->obj_async_op_call(op, _read_test_done);
//     if(rc) {
//         _sys_fini();
//     }
// }

// void _write_test_done(void * r , int status) {
//     assert(status == 0);
//     _free_op_common(r);
    
//     _do_read_test();
// }
// void _do_write_test() {
//     void *op = _alloc_op_common(msg_oss_op_write, os->obj_async_op_context_size());    
//     op_write_t *opc = ((message_t *)(op))->meta_buffer;
//     opc->oid = 0x1;
//     opc->ofst = 0x0;
//     opc->flags = 0;
//     opc->len = 0x1000;
    
//     message_t *m = op;
//     m->data_buffer = wbuf;
//     strcpy(wbuf , "123456789");  
//     int rc = os->obj_async_op_call(op, _write_test_done);
//     if(rc) {
//         _sys_fini();
//     }
// }

// void _create_test_done(void * r , int status) {
//     assert(status == 0);
//     _free_op_common(r);
    
//     _do_write_test();
//     _sys_fini();
// }

// void _do_create_test() {
//     void *op = _alloc_op_common(msg_oss_op_create, os->obj_async_op_context_size());    
//     op_create_t *opc = ((message_t *)(op))->meta_buffer;
//     opc->oid = 0x1;
//     int rc = os->obj_async_op_call(op, _create_test_done);
//     if(rc) {
//         _sys_fini();
//     }
// }

struct perf_context_t {

    objstore_impl_t *os;
    
    void *dma_wbuf;
    void *dma_rbuf;

    const char *devs[3];




    struct {
        uint64_t start_tsc;
        uint64_t oid;
        uint64_t nr_obj;
        uint64_t nop_submit;
        uint64_t nop_cpl;
        uint64_t end_tsc;
    } prep_obj_ctx;


};

struct perf_context_t g_perf_ctx;


double _tsc2choron(uint64_t start , uint64_t end) {
    uint64_t hz = spdk_get_ticks_hz();
    return ((end-start)/(double)(hz)) * 1e6;
}

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

void _free_op_common(void *p) {
    free(p);
}


void _sys_fini() {
    g_perf_ctx.os->unmount();
    spdk_free(g_perf_ctx.dma_rbuf);
    spdk_free(g_perf_ctx.dma_wbuf);
    spdk_app_stop(0);
}

void _submit_op(void *op , cb_func_t cb) {
    int rc = g_perf_ctx.os->obj_async_op_call(op , cb);
    if(rc) {
        log_err("Submit error\n");
        _sys_fini();
    } 
}






void _lanuch_perf() {
    _sys_fini();
}

void _prepare_objects_free_op(void * op) {
    _free_op_common(op);
}
void* _prepare_objects_gen_op() {
    objstore_impl_t *os = g_perf_ctx.os;
    void *op = _alloc_op_common(msg_oss_op_create, os->obj_async_op_context_size());    
    op_create_t *opc = ((message_t *)(op))->meta_buffer;
    opc->oid = g_perf_ctx.prep_obj_ctx.oid;
    g_perf_ctx.prep_obj_ctx.oid++;
    return op;
}

void _prepare_objects_submit_op(void *op , cb_func_t cb)  {
    g_perf_ctx.prep_obj_ctx.nop_submit++;
    _submit_op(op , cb);
}
void _prepare_object_continue(void *r , int status) {
    if(status) {
        log_err("Status:%u , err:%s\n",status , errcode_str(status));
        _sys_fini();
    }
    g_perf_ctx.prep_obj_ctx.nop_cpl++; 
    if((g_perf_ctx.prep_obj_ctx.nop_cpl ==
        g_perf_ctx.prep_obj_ctx.nr_obj) ) 
    {
        g_perf_ctx.prep_obj_ctx.end_tsc = rdtsc();
        double t = g_perf_ctx.prep_obj_ctx.end_tsc - g_perf_ctx.prep_obj_ctx.start_tsc;
        log_info("Create %lu objs , time %lf us\n", g_perf_ctx.prep_obj_ctx.nr_obj ,
            _tsc2choron(g_perf_ctx.prep_obj_ctx.start_tsc, g_perf_ctx.prep_obj_ctx.end_tsc ));
        // rdtsc()
        _lanuch_perf();
    } else {
        if(g_perf_ctx.prep_obj_ctx.nop_submit == 
            g_perf_ctx.prep_obj_ctx.nr_obj) {
                return;
        } else {
            void *op = _prepare_objects_gen_op();
            _prepare_objects_submit_op( op ,_prepare_object_continue);
        }
    }
}

void _prepare_objects_start() {
    g_perf_ctx.prep_obj_ctx.nr_obj = 100 * 1024;
    g_perf_ctx.prep_obj_ctx.oid = 1;
    g_perf_ctx.prep_obj_ctx.nop_cpl = 0;
    g_perf_ctx.prep_obj_ctx.nop_submit = 0;
    g_perf_ctx.prep_obj_ctx.start_tsc = rdtsc();

    log_debug("Start...\n");
    uint64_t i;
    for ( i = 0 ; i < 8 ; ++i) {
        void *op = _prepare_objects_gen_op();
        _prepare_objects_submit_op(op , _prepare_object_continue);
    }
}
void _load_objstore() {
    
    g_perf_ctx.os = ostore_get_impl(ZSTORE);
    objstore_impl_t *os = g_perf_ctx.os;

    g_perf_ctx.devs[0] = "Nvme0n1";
    g_perf_ctx.devs[1] = "/tmp/mempool";

    uint64_t s , e ; 
    s = now();
    int rc = os->mkfs(g_perf_ctx.devs , 0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mkfs time %lu us \n" , (e - s));

    s = now();
    rc = os->mount(g_perf_ctx.devs,0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mount time %lu us \n" , (e - s));

    _prepare_objects_start();
}

void _sys_init(void *arg) {
    (void)arg;
    g_perf_ctx.dma_rbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);
    g_perf_ctx.dma_wbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);

    _load_objstore();
}

// static void parse_args(int argc , char **argv) {
//     int opt = -1;
// 	while ((opt = getopt(argc, argv, "q:c:o:")) != -1) {
// 		switch (opt) {
// 		case 'q':
// 			g_qd = atoi(optarg);
// 			break;
//         case 'c':
// 			g_nr_ops = atoi(optarg);
// 			break;
//         case 'o':
// 			g_max_oid = atoi(optarg);
// 			break;
// 		default:
// 			fprintf(stderr, "Usage: %s [-q qd] [-c nr_ops] \n", argv[0]);
// 			exit(1);
// 		}
// 	}
// }


int main( int argc , char **argv) {
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "test_objectstore";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.shutdown_cb = _sys_fini;

    // parse_args(argc,argv);

    int rc = spdk_app_start(&opts , _sys_init , NULL);
 
    spdk_app_fini();

    return 0;
}