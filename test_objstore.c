#include "objectstore.h"
#include "util/errcode.h"
#include "util/chrono.h"
#include "util/log.h"

#include "message.h"
#include "operation.h"

#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/util.h"


#define ERROR_ON(status)\
if(status) { \
   log_err("Status:%u , err:%s\n",status , errcode_str(status)); \
   _sys_fini();\
}

#define ASYNC_TASK_CTX_OP(op) message_get_ctx(op)
#define ASYNC_TASK_DECLARE(task_name) \
    void  task_name ## _Then(void *ctx);\
    bool  task_name ## _Terminate(void *ctx);\
    bool  task_name ## _StopSubmit(void *ctx);\
    void  task_name ## _OpComplete(void *op);\
    int   task_name ## _SubmitOp(void *op , cb_func_t cb);\
    void* task_name ## _OpGenerate(void *ctx);\
    void  task_name ## _Continue(void *op , int s) {\
        ERROR_ON(s);\
        void *ctx = ASYNC_TASK_CTX_OP(op);\
        task_name ## _OpComplete(op);\
        if(task_name ## _Terminate(ctx)) {\
            task_name ## _Then(ctx);\
        } else if (task_name ## _StopSubmit (ctx)) {\
        \
        } else {\
            void *op = task_name ## _OpGenerate(ctx);\
            task_name ## _SubmitOp(op , task_name ## _Continue);\
        }\
    }\
    void  task_name ## _Start(void *ctx , int dp) { \
        int i ;\
        for ( i = 0 ; i < dp ; ++i ) { \
            void *op = task_name ## _OpGenerate(ctx);\
            task_name ## _SubmitOp(op , task_name ## _Continue);\
        }\
    }\
    struct task_name ## _context_t

#define ASYNC_TASK_FUNC_IMPL(task)

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




double _tsc2choron(uint64_t start , uint64_t end) {
    uint64_t hz = spdk_get_ticks_hz();
    return ((end-start)/(double)(hz)) * 1e6;
}


struct global_context_t {
    const objstore_impl_t *os;
    const char *devs[3];
    char *dma_wbuf;
    char *dma_rbuf;
    
    uint32_t obj_sz;
    int obj_nr;
    int obj_create_dp;
    int obj_fill_dp;
    int obj_perf_dp;
};


//全局上下文
struct global_context_t g_global_ctx;

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
    g_global_ctx.os->unmount();
    spdk_free(g_global_ctx.dma_rbuf);
    spdk_free(g_global_ctx.dma_wbuf);
    spdk_app_stop(0);
}


void _submit_op(void *op , cb_func_t cb) {
    int rc = g_global_ctx.os->obj_async_op_call(op , cb);
    ERROR_ON(rc);
}




//第三阶段：执行Perf测试
//TimeBased
//1. 纯顺序写(128K)
//2. 纯随机写(4K)
//3. 纯顺序读(128K)
//4. 纯随机读(4K)
//5. 读写混合(4K)
ASYNC_TASK_DECLARE(perf) {

    uint64_t tsc_hz;
    uint64_t time_sec;
    uint64_t start_tsc;
    uint64_t total_tsc;
    double   read_radio;
    uint64_t qd;
    uint64_t io_size;
    uint64_t max_offset;

    uint64_t rw_rio_prep;
    uint64_t rw_rio_submit;
    uint64_t rw_rio_cpl;

    uint64_t rw_wio_prep;
    uint64_t rw_wio_submit;
    uint64_t rw_wio_cpl;

}g_perf_ctx;


void  perf_Then(void *ctx_) {
    struct perf_context_t *ctx = ctx_;
    double t = _tsc2choron(ctx->start_tsc , rdtsc());
    double bd = ( ((ctx->rw_wio_cpl * ctx->io_size) >> 20) * 1e6 ) / t ;
    log_info("Use time :%lf s, Bandwidth= %lf MiB/s \n", t / 1e6 ,  bd);
    _sys_fini();
}
bool  perf_Terminate(void *ctx_) {
    struct perf_context_t *ctx = ctx_;
    return (rdtsc() >= (ctx->start_tsc + ctx->total_tsc)) 
        && ctx->rw_wio_submit == ctx->rw_wio_cpl;
}
bool  perf_StopSubmit(void *ctx_) {
    struct perf_context_t *ctx = ctx_;
    return rdtsc() >= (ctx->start_tsc + ctx->total_tsc);
}
void  perf_OpComplete(void *op) {
    struct perf_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    ctx->rw_wio_cpl++;
    _free_op_common(op);
}
int   perf_SubmitOp(void *op , cb_func_t cb) {
    struct perf_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    if(message_get_op(op) == msg_oss_op_read) {
        ctx->rw_rio_submit++;
    } else {
        ctx->rw_wio_submit++;
    }
    _submit_op(op  , cb);
    return 0;
}
void* perf_OpGenerate(void *ctx_) {
    struct perf_context_t *ctx = ctx_;
    const objstore_impl_t *os = g_global_ctx.os;
    void *op;
    bool is_read = false;
    
    if(is_read) {
        ERROR_ON(1);
    } else {
        op = _alloc_op_common(msg_oss_op_write, os->obj_async_op_context_size());    
    }
    
    message_t *r = op;
    r->priv_ctx = ctx_;
    r->data_buffer = g_global_ctx.dma_wbuf;
    op_write_t *opc = message_get_meta_buffer(op);

    uint64_t prep_offset;
    prep_offset = (ctx->rw_wio_prep * ctx->io_size) % ctx->max_offset;
    opc->oid = (prep_offset >> 22);
    opc->ofst = (prep_offset & ((4ul << 20) -1));
    opc->len = ctx->io_size;
    opc->flags = 0;
    ctx->rw_wio_prep++;
    return op;
}
//void ObjectFill_Continue(void *op , int s);
//void ObjectFill_Start(void *ctx , int dp);




//第二阶段：填充所有Object
ASYNC_TASK_DECLARE(ObjectFill) {
    uint64_t start_tsc;
    uint64_t prep_offset;
    uint64_t submit_offset;
    uint64_t cpl_offset;
    uint64_t total_len;
    uint64_t end_tsc;
}g_objfill_ctx;
void  ObjectFill_Then(void *ctx_) {
    struct ObjectFill_context_t *ctx = ctx_;
    double t = _tsc2choron(ctx->start_tsc , rdtsc());
    double bd = ( (ctx->total_len >> 20) * 1e6 ) / t ;
    log_info("Use time :%lf s, Bandwidth= %lf MiB/s \n", t / 1e6 ,  bd);
    

    log_info("Start perf...\n");

    memset(&g_perf_ctx , 0 , sizeof(g_perf_ctx));

    g_perf_ctx.tsc_hz = spdk_get_ticks_hz();
    g_perf_ctx.time_sec = 15;
    g_perf_ctx.total_tsc = 15 * g_perf_ctx.tsc_hz;
    g_perf_ctx.start_tsc = rdtsc();
    g_perf_ctx.read_radio = 0;
    g_perf_ctx.io_size = (64 << 10);
    g_perf_ctx.qd = 128;
    g_perf_ctx.max_offset = g_global_ctx.obj_sz * g_global_ctx.obj_nr;

    perf_Start(&g_perf_ctx , g_perf_ctx.qd);

}
bool  ObjectFill_Terminate(void *ctx_) {
    struct ObjectFill_context_t *ctx = ctx_;
    return ctx->total_len == ctx->cpl_offset;
}
bool  ObjectFill_StopSubmit(void *ctx_) {
    struct ObjectFill_context_t *ctx = ctx_;
    return ctx->total_len == ctx->submit_offset;
}
void  ObjectFill_OpComplete(void *op) {
    struct ObjectFill_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    ctx->cpl_offset += (128 * 1024);
    _free_op_common(op);
}
int   ObjectFill_SubmitOp(void *op , cb_func_t cb) {
    struct ObjectFill_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    _submit_op(op  , cb);
    ctx->submit_offset += (128 * 1024);
    return 0;
}
void* ObjectFill_OpGenerate(void *ctx_) {
    struct ObjectFill_context_t *ctx = ctx_;
    const objstore_impl_t *os = g_global_ctx.os;
    void *op = _alloc_op_common(msg_oss_op_write, os->obj_async_op_context_size());    
    message_t *r = op;
    r->priv_ctx = ctx_;
    r->data_buffer = g_global_ctx.dma_wbuf;
    op_write_t *opc = message_get_meta_buffer(op);
    opc->oid = (ctx->prep_offset >> 22);
    opc->ofst = (ctx->prep_offset & ((4ul << 20)-1));
    opc->len = 128 << 10;
    opc->flags = 0;
    ctx->prep_offset += (128 << 10);
    return op;
}
//void ObjectFill_Continue(void *op , int s);
//void ObjectFill_Start(void *ctx , int dp);


//第一阶段：创建指定个数的Object
ASYNC_TASK_DECLARE(ObjectPrep) {
    uint64_t start_tsc;
    uint64_t total_obj;
    uint64_t nr_prep_oid;
    uint64_t nr_submit;
    uint64_t nr_cpl;
    uint64_t end_tsc;
} g_objprep_ctx;
void ObjectPrep_Then(void *ctx_) {
    g_objprep_ctx.end_tsc = rdtsc();
    log_info("Prepare %lu objects done, use time %lf us \n" ,
        g_objprep_ctx.total_obj , _tsc2choron(g_objprep_ctx.start_tsc , g_objprep_ctx.end_tsc));



    memset(&g_objfill_ctx , 0 , sizeof(g_objfill_ctx));
    g_objfill_ctx.start_tsc = rdtsc();
    g_objfill_ctx.total_len = g_objprep_ctx.total_obj * g_global_ctx.obj_sz;
    log_info("Start fill %lu objects, total size=%lu kiB\n" ,g_objprep_ctx.total_obj, g_objfill_ctx.total_len >> 10 );
    ObjectFill_Start(&g_objfill_ctx , g_global_ctx.obj_fill_dp);
}
bool ObjectPrep_Terminate(void *ctx_) {
    struct ObjectPrep_context_t *ctx = ctx_;
    return ctx->nr_cpl == ctx->total_obj;
}
bool ObjectPrep_StopSubmit(void *ctx_) {
    struct ObjectPrep_context_t *ctx = ctx_;
    return ctx->nr_submit == ctx->total_obj;
}
void ObjectPrep_OpComplete(void *op) {
    struct ObjectPrep_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    ctx->nr_cpl++;
    _free_op_common(op);
}
int  ObjectPrep_SubmitOp(void *op , cb_func_t cb) {
    struct ObjectPrep_context_t *ctx = ASYNC_TASK_CTX_OP(op);
    _submit_op(op  , cb);
    ctx->nr_submit++;
    return 0;
}
void* ObjectPrep_OpGenerate(void *ctx_) {
    struct ObjectPrep_context_t *ctx = ctx_;
    const objstore_impl_t *os = g_global_ctx.os;
    void *op = _alloc_op_common(msg_oss_op_create, os->obj_async_op_context_size());  
    message_t *r = op;
    r->priv_ctx = ctx;  
    op_create_t *opc = message_get_meta_buffer(op);
    opc->oid = ctx->nr_prep_oid++;
    // g_perf_ctx.prep_obj_ctx.oid++;
    return op;
}
//void ObjectPrep_Continue(void *op , int s);
//void ObjectPrep_Start(void *ctx , int dp);



void _load_objstore() {
    
    g_global_ctx.os = ostore_get_impl(ZSTORE);
    const objstore_impl_t *os = g_global_ctx.os;

    g_global_ctx.devs[0] = "Nvme0n1";
    g_global_ctx.devs[1] = "/tmp/mempool";

    uint64_t s , e ; 
    s = now();
    int rc = os->mkfs(g_global_ctx.devs , 0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mkfs time %lu us \n" , (e - s));

    s = now();
    rc = os->mount(g_global_ctx.devs,0);
    assert(rc == SUCCESS);
    e = now();
    log_info("mount time %lu us \n" , (e - s));


    memset(&g_objprep_ctx , 0 , sizeof(g_objprep_ctx));
    g_objprep_ctx.start_tsc = rdtsc();
    g_objprep_ctx.total_obj = g_global_ctx.obj_nr;
    ObjectPrep_Start(&g_objprep_ctx , 1);
}

void _sys_init(void *arg) {
    (void)arg;
    g_global_ctx.dma_rbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);
    g_global_ctx.dma_wbuf = spdk_dma_zmalloc(0x1000 * 1024, 0x1000, NULL);
    g_global_ctx.obj_sz = 4 << 20;
    g_global_ctx.obj_create_dp = 1;
    g_global_ctx.obj_fill_dp = 16;
    g_global_ctx.obj_perf_dp = 0;
    _load_objstore();
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			g_global_ctx.obj_nr = atoi(optarg);
			break;
		default:
			log_info("Usage: %s [-n nr_objects] \n", argv[0]);
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

    spdk_app_start(&opts , _sys_init , NULL);
 
    spdk_app_fini();

    return 0;
}