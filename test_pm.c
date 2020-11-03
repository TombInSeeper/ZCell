#include "pm.h"
#include "util/log.h"
#include "util/chrono.h"




int main( int argc , char **argv) {
    if(argc < 2) {
        log_info("Usage ./test_pm [pm_path] [nr_log_entry(1~60)]\n");
        return 1;
    }
    struct pmem_t * pm =  pmem_open(argv[1], 1 << 20);
    // int nr_entrys = atoi(argv[2]);
    char oldv[4096] __attribute__((aligned(64)));
    char newv[4096] __attribute__((aligned(64)));

    memset(oldv,0x00,sizeof(oldv));
    memset(newv,0xff,sizeof(newv));

    struct Value *pov = (void*)oldv;
    struct Value *pnv = (void*)newv;
    uint64_t st = now();
    int loop = 10000;
    union pmem_transaction_t  *tx = pmem_transaction_alloc(pm);
    while(loop--) {
        pmem_transaction_add(pm,tx, 1 << 10 , 64, newv[1]);
        pmem_transaction_add(pm,tx, 2 << 10 , 64, newv[2]);
        pmem_transaction_add(pm,tx, 3 << 10 , 64, newv[3]);
        pmem_transaction_add(pm,tx, 4 << 10 , 64, newv[4]);
        bool success = pmem_transaction_apply(pm,tx);
        if(!success) {
            log_err("Transaction execute error\n");
            return 1;
        }
    }
    uint64_t end = now();
    pmem_transaction_free(pm,tx);

    log_info("%lu us in 10000 times us\n" , end- st);

    pmem_close(pm);
    return 0;
}