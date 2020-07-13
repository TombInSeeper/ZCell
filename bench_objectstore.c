#include "objectstore.h"
#include "spdk/event.h"
#include "spdk/env.h"

const char* dev_list[] = {
    "Malloc0",
    "Malloc1",
    "Malloc2"
};

enum {
    S_MOUNT,
    S_CREATE,
    S_READ,
    S_WRITE,
    S_DEL,
    S_UNMOUNT
};

typedef void (*cb_func_t) (void*);
struct ctx_t {
    int count;
    int state;
    cb_func_t run;

    uint64_t tsc_st;
    uint64_t tsc_ed;

    uint32_t qd;

    void*  fake_buf;

} main_ctx;

void result_out(uint64_t start_tsc, uint64_t end_tsc, int nr_rq)
{
    double  tus = ((double)(end_tsc -start_tsc) / (double)(spdk_get_ticks_hz()) * 1e6);
    double  iops = (((double)nr_rq * 1e6) / (double)tus);
    printf("%lu us, " , (uint64_t)tus );
    printf("%lf K iops\n" ,iops / 1000.0 );
}

void unmount_cb(void *unmount_cb_arg)
{
    (void)(unmount_cb_arg);
    SPDK_NOTICELOG("obj-fs unmount done\n");
    spdk_app_stop(0);
}

void write_obj_cb(void* _ctx)
{
    struct ctx_t *ctx = _ctx;
    ctx->count++;
    if(ctx->count >= 1024){
        ctx->state = S_UNMOUNT;
        ctx->count = 0;
        ctx->tsc_ed = spdk_get_ticks();
        SPDK_NOTICELOG("write_obj done\n");
        result_out(ctx->tsc_st,ctx->tsc_ed,1024);
    }
    //Continue
    ctx->run(ctx);
}


void create_obj_cb(void* _ctx)
{
    struct ctx_t *ctx = _ctx;
    ctx->count++;
    if(ctx->count >= 1024){
        ctx->state = S_WRITE;
        ctx->count = 0;
        ctx->tsc_ed = spdk_get_ticks();
        SPDK_NOTICELOG("create_obj done\n");
        result_out(ctx->tsc_st,ctx->tsc_ed,1024);
    }
    //Continue
    ctx->run(ctx);
}

void _run_state_machine(void *arg1)
{
    struct ctx_t *ctx = arg1;
    switch (ctx->state) {
        case S_MOUNT:
            async_mount(dev_list,0 , NULL , NULL); // sync
            ctx->state = S_CREATE;
            SPDK_NOTICELOG("obj-fs mount done\n");
            //fall-thru    
        case S_CREATE:
            if(ctx->count == 0) 
                ctx->tsc_st = spdk_get_ticks(); // for create stage
            async_obj_create(ctx->count,create_obj_cb, ctx );
            break;
        case S_WRITE:
            if(ctx->count == 0) 
                ctx->tsc_st = spdk_get_ticks(); // for create stage
            async_obj_write(ctx->count,0, 4096 ,ctx->fake_buf,write_obj_cb,ctx);
            break;
        case S_UNMOUNT:
            async_unmount(NULL , NULL); // sync
            SPDK_NOTICELOG("obj-fs unmount done\n");
            spdk_app_stop(0);
            break;
    }
}


void start_fn(void*arg1)
{
    main_ctx.state = S_MOUNT;
    main_ctx.count = 0;
    main_ctx.run = _run_state_machine;
    main_ctx.fake_buf = spdk_dma_malloc(4096 * 1024 ,4096,NULL);
    main_ctx.qd = 8;
    _run_state_machine(&main_ctx);
}

int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "demo";
    opts.max_delay_us = 1000 * 1000;
    opts.config_file = "spdk.conf";
    opts.shm_id = 1;


    int rc = spdk_app_start(&opts,start_fn, &main_ctx);
	if (rc) {
		SPDK_ERRLOG("ERROR starting application\n");
	}

end:
    spdk_free(main_ctx.fake_buf);
    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();
}
