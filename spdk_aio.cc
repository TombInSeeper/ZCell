extern "C" {
    #include "spdk/stdinc.h"
    #include "spdk/env.h"
    #include "spdk/event.h"
    #include "spdk/log.h"
    #include "spdk/bdev.h"
    #include "spdk/net.h"
    #include "spdk/sock.h"
    #include "spdk/util.h"
}


#define Total (10 * 10000UL)

static spdk_bdev_desc* null_des;
static spdk_io_channel* io_ch;
static spdk_poller* issue_io_poller;
static int n = 0;
const  int total = 10 * 10000;

static uint64_t start_tsc;
static uint64_t end_tsc;


#define DMA_CACHE_SIZE 1024

__thread void* dma_cache[DMA_CACHE_SIZE];
__thread int tail = 0;
void* get_page()
{
    if(tail >= DMA_CACHE_SIZE)
        return NULL;
    void* p = dma_cache[tail];
    dma_cache[tail] = NULL;
    ++tail;
    return p;
}

void  put_page( void* p) 
{
    if(tail == 0)
        return;
    dma_cache[--tail] = p;
}



struct process_context_t {
    spdk_mempool* op_pool;
    LIST_HEAD(op_list_head,op_t) op_list;

};


void die()
{

    int i;
    for( i = 0 ; i < DMA_CACHE_SIZE ; ++i) {
        spdk_dma_free(dma_cache[i]);
    }

    spdk_poller_unregister(&issue_io_poller);
    spdk_put_io_channel(io_ch);
    spdk_bdev_close(null_des);
    spdk_app_stop(0);
}


int inflight = 0;


void complete_io(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
    put_page(cb_arg);
    spdk_bdev_free_io(bdev_io);
    if(success){
        --inflight;
        ++n;
    }
    else
    {
        SPDK_ERRLOG("IO error\n");
    }
    
    if(n == total) {
        end_tsc = spdk_get_ticks();
        die();
    }
}


int issue_io(void* arg)
{
    if(n == total) {
        spdk_poller_pause(issue_io_poller);
        return 0;
    }
    if(n == 0) {
        start_tsc = spdk_get_ticks();
    }

    // if(inflight)
    //     return 1;
    while(inflight < 1) {
        void* buf = get_page();
        if(!buf) {
            // SPDK_ERRLOG("spdk_dma_zmalloc isn't avaliable\n");
            // die();
            return 1;
        }
        int rc = spdk_bdev_write(null_des,io_ch,buf,0,0x1000,complete_io, buf);
        if( rc == -ENOMEM ) {
            SPDK_WARNLOG("bdev io entry isn't avaliable\n");
            return -1;
        }
        inflight++;
    }
    return 0;
}

void sock_moudle_init()
{
   
}



void start_fn(void*arg1)
{

    spdk_bdev* b = spdk_bdev_get_by_name("Null0");
    if(!b) {
        SPDK_ERRLOG("fuck\n");
        return;
    }

    SPDK_NOTICELOG("spdk_bdev_get_by_name ok\n");
   
    if(spdk_bdev_open(b, true, NULL, NULL,&null_des)) {
        SPDK_ERRLOG("spdk_bdev_open fuck\n");
        return;
    }

    spdk_bdev_desc* d = null_des;
    io_ch = spdk_bdev_get_io_channel(d);
    if(!io_ch) {
        SPDK_ERRLOG("spdk_io_channel fuck, pls kill me\n");
        return;
    }
    SPDK_NOTICELOG("spdk_io_channel ok\n");

    issue_io_poller = spdk_poller_register(issue_io,NULL,0);
    if(!issue_io_poller) {
        SPDK_ERRLOG("issue_io_poller fuck , pls kill me \n");
        return;
    }
    SPDK_NOTICELOG("issue_io_poller ok\n");


    {
        int i;
        for( i = 0 ; i < DMA_CACHE_SIZE ; ++i) {
            dma_cache[i] = spdk_dma_malloc(0x1000,0x1000,NULL);
        }
    }

    return;
}


int main()
{
    spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "demo";
    opts.max_delay_us = 1000 * 1000;
    opts.config_file = "spdk.conf";
    opts.shm_id = 1;
    int rc = spdk_app_start(&opts,start_fn,NULL);
	if (rc) {
		SPDK_ERRLOG("ERROR starting application\n");
	}
    double  tus = ((double)(end_tsc -start_tsc) / (double)(spdk_get_ticks_hz()) * 1e6);
    double  iops = (((double)total * 1e6) / (double)tus);
    printf("%lu us\n" , (uint64_t)tus );
    printf("%lf K iops\n" ,iops / 1000.0 );


    // spdk_poller_register ;


    SPDK_NOTICELOG("The end\n");
    spdk_app_fini();
}