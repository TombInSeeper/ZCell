#include "objectstore.h"
#include "objectstore_errcode.h"

#include "spdk/event.h"
#include "spdk/env.h"

objstore_interface_t *os;

char *wbuf;
char *rbuf;


void _sys_fini()
{
    SPDK_NOTICELOG("Stoping app\n");
    spdk_app_stop(0);
}

void _unmount_cb (void * arg , int st)
{
    assert (st == EXECUTE_OK);
    SPDK_NOTICELOG("unmount OK\n");

    obj_if_destruct(os);
    spdk_app_stop(0);
}
void _delete_cb(void * arg , int st)
{
    assert (st == EXECUTE_OK);
    SPDK_NOTICELOG("obj_delete OK\n");

    os->unmount(_unmount_cb , NULL);
}

void _read_cb(void * arg , int st)
{
    assert (st == EXECUTE_OK);
    //verify
    if(1) {
        char *p = rbuf;
        for ( ; ( p - (char*)rbuf) < 0x1000 ; ++p) {
            assert ( *p == 0x23);
        }
    }

    SPDK_NOTICELOG("obj_read OK\n");

    spdk_free(rbuf);
    os->obj_delete( 0 ,_delete_cb , NULL);
}

void _write_cb(void * arg , int st)
{
    assert (st == EXECUTE_OK);
    SPDK_NOTICELOG("obj_write OK\n");

    spdk_free(wbuf);
    rbuf = spdk_dma_malloc(0x1000,0x1000,NULL);
    os->obj_read(0, 0 , 0x1000, rbuf, _read_cb, NULL);
}


void _create_cb(void * arg , int st)
{
    assert (st == EXECUTE_OK);
    SPDK_NOTICELOG("obj_create OK\n");
    wbuf = spdk_dma_malloc(0x10000,0x1000,NULL);
    memset(wbuf,0x23,0x1000);
    os->obj_write(0, 0 , 0x10000, wbuf, _write_cb, NULL);
}

void _mount_cb(void * arg , int st)
{
    assert (st == EXECUTE_OK);
    SPDK_NOTICELOG("mount OK\n");

    char sinfo [1024];
    os->info(sinfo , 1024);
    SPDK_NOTICELOG("osinfo:%s\n" , sinfo);

    os->obj_create(0, _create_cb , NULL);
}

void _sys_init(void *arg)
{
    (void)arg;
    os = obj_if_construct(FAKESTORE);
    os->mount(NULL, FAKESTORE, _mount_cb , NULL);
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