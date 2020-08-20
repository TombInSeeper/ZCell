#include "msgr.h"
#include "objectstore.h"



#define MGSR_KEY 1
#define OBJ_KEY  1 



void msgr_start( void* arg , void* arg2)
{
    uint16_t port = spdk_env_get_current_core() + 45678;
    msgr_init("127.0.0.1", port);
    return;
}

void msgr_stop(void* arg , void* arg2)
{
    msgr_fini();
}


void _sys_fini()
{
    int i;
    SPDK_ENV_FOREACH_CORE(i) {
        if(MGSR_KEY) {
            struct spdk_event* se = spdk_event_allocate(i,msgr_stop,NULL,NULL);
            spdk_event_call(se);
        }
    }
    SPDK_NOTICELOG("Stoping app\n");
    spdk_app_stop(0);
}

void _sys_init( void* arg)
{
    SPDK_NOTICELOG("starting msgr\n");

    int i;
    SPDK_ENV_FOREACH_CORE(i) {
        if(MGSR_KEY) {
            struct spdk_event* se = spdk_event_allocate(i,msgr_start,NULL,NULL);
            spdk_event_call(se);
        }
    }

    return;
}


int main()
{
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "server";
    opts.config_file = "spdk.conf";
    opts.reactor_mask = "0x1";
    opts.max_delay_us = 1000*1000; 
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
