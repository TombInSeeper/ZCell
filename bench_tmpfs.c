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


#include "spdk/env.h"



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


const size_t fN = 1024;
char fnames[1024][80];
int fds[1024];
void prep_fname()
{
    int i ;
    for (i = 0 ; i < fN ; ++i) {
        snprintf(fnames[i],80,"/dev/shm/t%04d",i);
    }
}

void test_create( void* arg)
{ 
    (void)arg;
    int i ;
    for (i = 0 ; i < fN ; ++i) {
        int fd = open(fnames[i], O_CREAT | O_RDWR );
        if(!fd) {
            perror("fuck,create error!\n");
            return;
        }
        fds[i] = fd;
    }
}

void test_write(void* arg)
{ 
    (void)arg;

    int i ;
    int c = 30;
    while ( c-- ) {
        char buf[4096];
        for (i = 0 ; i < fN ; ++i) {
            int n = write(fds[i],buf,4096);
            if( n != 4096 ) {
                printf("fd=%d\n",fds[i]);
                perror("");
                exit(-1);
            }
        }
    }
}

void test_read(void* arg)
{ 
    (void)arg;
    int i ;
    int c = 30;
    while ( c-- ) {
        char buf[4096];
        for (i = 0 ; i < fN ; ++i) {
            int n = read(fds[i],buf,4096);
            if( n != 4096 && n != 0) {
                printf("fd=%d\n",fds[i]);
                perror("");
                exit(-1);
            }
        }
    }
}


void test_delete( void* arg)
{ 
    (void)arg;
    int i ;
    for (i = 0 ; i < fN ; ++i) {
        unlink(fnames[i]);
    }
}

enum {
    CREATE,
    WRITE,
    READ,
    DELETE
};

typedef void (*test_fn_t)(void*);
struct test_t {
    const char* name;
    test_fn_t fn;
    struct {
        uint64_t total;
    };
};

struct test_t ts[] = {
    {.name = "CREATE", .fn= test_create, .total = 1024},
    {.name = "WRITE", .fn= test_write, .total = 30 * 1024},
    {.name = "READ", .fn= test_read, .total = 30 * 1024},
    {.name = "DELETE", .fn= test_delete, .total = 1024},
};


void test_time(int which)
{
    // timespec start = {0},end = {0};
    uint64_t start_tsc,end_tsc;
    start_tsc = spdk_get_ticks();
    // clock_gettime(CLOCK_MONOTONIC,&start);
    
    (ts[which]).fn(NULL);

    end_tsc = spdk_get_ticks();	
    double  tus = ((double)(end_tsc -start_tsc) / (double)(spdk_get_ticks_hz()) * 1e6);
    double  qps = (((double)(ts[which].total) * 1e6) / (double)tus);
    printf("%lu us\n" , (uint64_t)tus );
    printf("%s: %lu K qps\n" ,ts[which].name, (uint64_t)(qps / 1000.0) );
}

int main()
{

    struct spdk_env_opts opt;
    spdk_env_opts_init(&opt);
    spdk_env_init(&opt);
    dma_cache_init();
    prep_fname();

    test_time(CREATE);
    test_time(WRITE);
    test_time(READ);
    test_time(DELETE);

    dma_cache_fini();
    spdk_env_fini();
    return 0;
}




