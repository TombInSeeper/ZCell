#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "unistd.h"
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern "C" {
    #include "spdk/env.h"
}


#define DMA_CACHE_SIZE 128
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


void dma_cache_init()
{
    int i;
    for( i = 0 ; i < DMA_CACHE_SIZE ; ++i) {
        posix_memalign(&dma_cache[i], 0x1000 ,0x1000);
    }
}

void dma_cache_fini()
{
    int i;
    for( i = 0 ; i < DMA_CACHE_SIZE ; ++i) {
        free(dma_cache[i]);
    }

}


int fd;

void open_null_dev()
{
    fd = open("/dev/null" ,  O_RDWR);
    if(fd == -1) {
        perror("fuck open");
        exit(-1);
    }
}

int main()
{


    spdk_env_opts opt;
    spdk_env_opts_init(&opt);

    spdk_env_init(&opt);

    dma_cache_init();
    open_null_dev();

    const size_t total = 10 * 10000UL;
    int i = 0;


    // timespec start = {0},end = {0};
    uint64_t start_tsc,end_tsc;
    start_tsc = spdk_get_ticks();
    // clock_gettime(CLOCK_MONOTONIC,&start);
    for(i = 0 ; i < total ; ++i) {
        void* buf = get_page();
        size_t n = write(fd,buf,0x1000);
        if(n != 0x1000) {
            perror("fuck write");
            exit(-1);
        }
        put_page(buf);
    }
    end_tsc = spdk_get_ticks();


	
    double  tus = ((double)(end_tsc -start_tsc) / (double)(spdk_get_ticks_hz()) * 1e6);
    double  iops = (((double)total * 1e6) / (double)tus);
    printf("%lu us\n" , (uint64_t)tus );
    printf("%lf K iops\n" ,iops / 1000.0 );

    dma_cache_fini();
    close(fd);


    spdk_env_fini();
    return 0;
}
