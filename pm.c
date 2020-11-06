#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "pm.h"
#include "pm_impl.h"
#include "util/log.h"
#include "util/common.h"
#include "util/assert.h"


#define PM_LOG_REGION_SIZE 4096

struct pmem_t {    
    void *map_base;
    uint64_t log_region_ofst;
};

union pm_log_header_t {
    struct {
        uint64_t valid : 1;
        uint64_t rsv : 15;
        uint64_t nr_logs:16;
        uint64_t align_length:32;
    };
    uint8_t align[64];
};

struct pm_log_entry_t {
    uint64_t ofst;
    uint64_t length;
    uint64_t value[0];
};


extern struct pmem_t *pmem_open(const char *path, uint64_t cpu,  uint64_t *pmem_size) {
    struct pmem_t *p = calloc(1, sizeof(struct pmem_t));
    if(!p) {        
        return NULL;
    }

    struct stat st_;
    int rc = stat(path,&st_);
    if(rc) {
        free(p);
        log_err("Cannot get stat of %s, errs: %s" , path , strerror(errno));
        return NULL;
    }
    uint64_t fsize = st_.st_size;
    
    if(fsize & ((1 << 20) - 1 )) {
        log_err("Pmem file must be aligned to 2MiB\n");
        free(p);
        return NULL;
    }

    if(pmem_size) {
        *pmem_size = fsize;
    }


    int fd = open(path, O_RDWR);
    void* dest = mmap(NULL, fsize, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, fd, 0);
    close(fd);
    p->map_base = dest;
    
    //Per cpu
    p->log_region_ofst = 4096 + cpu * PM_LOG_REGION_SIZE;
    
    return p;
}

extern void pmem_read(struct pmem_t *pmem, void *dest, uint64_t offset , size_t length){
    void *src = (char*)(pmem->map_base) + offset;
    memcpy(dest, src, length);
}

extern void pmem_write(struct pmem_t *pmem, int sync, const void* src, uint64_t offset, size_t length){
    void *dst = pmem->map_base + offset;
    nvmem_memcpy(sync,dst,src,length);
}

extern void pmem_recovery(struct pmem_t *pmem) {
    uint64_t offset_log_reg = pmem->log_region_ofst;
    union pm_log_header_t lh;
    char  pm_log_pload [4096];
    uint64_t offset_ulog_pload = offset_log_reg + sizeof(lh);
    pmem_read(pmem, &lh,offset_log_reg,sizeof(lh));
    
    uint64_t cpu = (offset_log_reg >> 12) - 1 ;
    if(lh.valid) {
        log_critical("Pmem transaction in cpu[%lu] need to replay.\n" , cpu);
        uint16_t n = lh.nr_logs;
        uint32_t len = lh.align_length;
        assert( len % 256 == 0);
        pmem_read(pmem,pm_log_pload,offset_ulog_pload,len);
        int i;
        char *cur_entry = pm_log_pload;
        for(i = 0; i < n ; ++i) {
            struct pm_log_entry_t* e = (void*)cur_entry;
            assert(e->length % 64 == 0);
            assert(e->ofst % 64 == 0);
            pmem_write(pmem, 1 , e->value, e->ofst, e->length);
            cur_entry += sizeof(*e) + e->length;
        }
        memset(&lh,0,sizeof(lh));
        pmem_write(pmem,1,&lh,offset_log_reg,sizeof(lh));
        // log_critical("Pmem transaction roll forward done.\n");
        log_critical("Pmem transaction  in cpu[%lu] replay done.\n" , cpu);
    }
}



union pmem_transaction_t {
    struct {
        union  pm_log_header_t lh;
        struct pm_log_entry_t  le[0];
    };
    uint8_t align[PM_LOG_REGION_SIZE];
};

extern union pmem_transaction_t* pmem_transaction_alloc(struct pmem_t *pmem) {
    return malloc(sizeof(union pmem_transaction_t));
}

//pmem_addr % 64 == 0
//len % 64 == 0
extern bool pmem_transaction_add(struct pmem_t *pmem, union pmem_transaction_t *tx,
    const uint64_t pmem_ofst, size_t len, void *new_value)  
{
    // const void *pmem_addr = (char*)pmem->map_base + pmem_ofst;
    uint32_t alen = tx->lh.align_length;
    uint32_t tlen = (alen + sizeof(struct pm_log_entry_t) + len) + sizeof(union pm_log_header_t);
    tlen = FLOOR_ALIGN(tlen , 256);
    
    if(tlen > PM_LOG_REGION_SIZE) {
        log_err("Cannot add more range in this Transaction\n");
        return false;
    }

    uint32_t i = tx->lh.nr_logs++;    
    tx->le[i].length = len;
    tx->le[i].ofst   = pmem_ofst; 
    memcpy(tx->le[i].value , new_value, len);
    
    return true;
}

extern bool pmem_transaction_apply(struct pmem_t *pmem, union pmem_transaction_t *tx) {

    // const uint64_t zero_64B[8] __attribute__((aligned(64))) = { 0 };

    //Step1. 
    //.....
    pmem_write(pmem,1, tx->le, 
        pmem->log_region_ofst + sizeof(union pm_log_header_t),
        tx->lh.align_length);
    
    //Step2.
    //....
    pmem_write(pmem,1,&tx->lh,pmem->log_region_ofst, sizeof(tx->lh));

    //Step3.
    size_t i;
    for(i = 0 ; i < tx->lh.nr_logs; ++i) {
        pmem_write(pmem,0, tx->le[i].value, tx->le[i].ofst, tx->le[i].length);
    }
    _mm_sfence();

    memset(&tx->lh , 0 , sizeof(tx->lh));
    pmem_write(pmem,1, &tx->lh, pmem->log_region_ofst, sizeof(tx->lh));

    return true;
}

extern void pmem_transaction_free(struct pmem_t *pmem, union pmem_transaction_t *tx) {
    free(tx);
}






// extern void pmem_atomic_multi_update(struct pmem_t *pmem, int cpu, size_t n, struct pmem_update_entry_t *upe) {
//     size_t i ;    
//     uint64_t offset_ulog = 4096 + cpu * 4096;
//     uint64_t offset_ulog_pload = offset_ulog + sizeof(union pm_log_header_t);
//     uint64_t alen = 0;
//     union pm_log_header_t *uh = (void*)(pmem->log_prep_region);
//     struct pm_log_entry_t *ue = (void*)(char *)(uh + 1);
    
//     char *ue_start = (void*)ue;
//     for ( i = 0 ; i < n ; ++i) {
//         alen += sizeof(struct pm_log_entry_t) + upe[i].len_;
//         if(alen > 4096 - 64) {
//             log_err("Transaction is too big:alen=%lu\n", alen);
//             return;
//         }
//         ue->length = upe[i].len_;
//         ue->ofst = upe[i].offset_;
//         memcpy(ue->value, upe[i].old_value_, upe[i].len_);       
//         ue = (void*)((char*)ue + 
//             sizeof(struct pm_log_entry_t) + ue->length);    
//     }
//     alen = (alen + 256 - 1) & (~( 256 - 1));
//     if (alen > 4096 - 64) {
//         log_err("Transaction is too big:alen=%lu\n", alen);
//         return;      
//     }

//     uh->align_length = alen;
//     uh->nr_logs = n;
//     uh->valid = 1;
    
//     //Step1. write_flush ulog payload
//     pmem_write(pmem,1,ue_start,offset_ulog_pload, alen);
    
//     //Step2. write_flush ulog header 
//     pmem_write(pmem,1,uh,offset_ulog,sizeof(*uh));
    
//     //Step3. Apply all new update
//     for( i = 0 ; i < n; ++i) {
//         pmem_write(pmem, 0 , upe[i].new_value_,upe[i].offset_,upe[i].len_);
//     }
//     _mm_sfence();

//     //Step4. 

//     memset(uh,0,sizeof(*uh));
//     pmem_write(pmem,1,uh,offset_ulog,sizeof(*uh));

//     return;
// }


extern void pmem_close(struct pmem_t *pmem) {
    free(pmem);
}