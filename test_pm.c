#include "pm.h"
#include "util/log.h"
#include "util/chrono.h"
#include "util/assert.h"





// int main( int argc , char **argv) {
//     if(argc < 2) {
//         log_info("Usage ./test_pm [pm_path] \n");
//         return 1;
//     }
//     struct pmem_t * pm =  pmem_open(argv[1], 1 << 20);
//     // int nr_entrys = atoi(argv[2]);
//     char oldv[4096] __attribute__((aligned(64)));
//     char newv[4096] __attribute__((aligned(64)));

//     memset(oldv,0x00,sizeof(oldv));
//     memset(newv,0xff,sizeof(newv));

//     struct Value *pov = (void*)oldv;
//     struct Value *pnv = (void*)newv;
//     uint64_t st = now();
//     int loop = 10000;
//     while(loop--) {
//         union pmem_transaction_t  *tx = pmem_transaction_alloc(pm);
//         pmem_transaction_add(pm,tx, 1 << 16 , 64, &newv[1]);
//         pmem_transaction_add(pm,tx, 2 << 16 , 64, &newv[2]);
//         pmem_transaction_add(pm,tx, 3 << 16 , 64, &newv[3]);
//         pmem_transaction_add(pm,tx, 4 << 16 , 64, &newv[4]);
//         bool success = pmem_transaction_apply(pm,tx);
//         if(!success) {
//             log_err("Transaction execute error\n");
//             return 1;
//         }
//         pmem_transaction_free(pm,tx);
//     }
//     uint64_t end = now();
//     log_info("%lu us in 10000 times us\n" , end- st);

//     pmem_close(pm);
//     return 0;
// }

#include "zstore_allocator.h"

void alloc_result_str(struct zstore_extent_t *ze, uint64_t nr) {
    
    // char str[4096];
    printf("Allocate %lu extent:", nr);
    size_t i;
    for ( i = 0 ; i < nr ; ++i) {
        printf("{%lu,%lu}" , ze[i].lba_, ze[i].len_);
    }
    printf("\n");
}

int main(int argc , char **argv) {

    struct stupid_allocator_t *al = malloc(sizeof(struct stupid_allocator_t));
    stupid_allocator_constructor(al ,1024 * 1024);
    struct zstore_extent_t ze[64];
    uint64_t en;
    //ALloc
    stupid_alloc_space(al, 2 ,ze, &en);
    ASSERT(en ==1);
    alloc_result_str(ze,en);
    
    //Free
    stupid_free_space(al, ze, en);

    stupid_alloc_space(al, 128 ,ze, &en);
    ASSERT(en ==1);
    alloc_result_str(ze,en);


    stupid_allocator_destructor(al);
    
}