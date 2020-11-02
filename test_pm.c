#include "pm.h"
#include "util/log.h"
#include "util/chrono.h"

struct Value {
    uint64_t v_[8];
};

int main( int argc , char **argv) {
    if(argc < 3) {
        log_info("Usage ./test_pm [pm_path] [nr_log_entry(1~60)]\n");
        return 1;
    }
    struct pmem_t * pm =  pmem_open(argv[1], 1 << 20);
    int nr_entrys = atoi(argv[2]);
    char oldv[4096] __attribute__((aligned(64)));
    char newv[4096] __attribute__((aligned(64)));

    memset(oldv,0x00,sizeof(oldv));
    memset(newv,0xff,sizeof(newv));

    struct Value *pov = (void*)oldv;
    struct Value *pnv = (void*)newv;

    struct pmem_update_entry_t pes[64]; 
    int i;
    for ( i = 0 ; i < 64 ; ++i) {
        pes[i].len_ = 64;
        pes[i].offset_ = 4096 * i;
        pes[i].old_value_ = &pov[i];
        pes[i].new_value_ = &pnv[i];
    }

    uint64_t st = now();
    int loop = 1000;
    while(loop--) {
        pmem_atomic_multi_update(pm,0, nr_entrys ,pes);
    }
    uint64_t end = now();
    log_info("%lu us in 1000 times us\n" , end- st);

    pmem_close(pm);
    return 0;
}