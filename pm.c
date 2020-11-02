#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "pm.h"
#include "pmem.h"
#include "util/log.h"
#include "util/common.h"
#include "util/assert.h"

union pm_undolog_header_t {
    struct {
        uint64_t valid : 1;
        uint64_t rsv : 15;
        uint64_t nr_logs:16;
        uint64_t align_length:32;
    };
    uint8_t align[64];
};

struct pm_undolog_entry_t {
    uint64_t ofst;
    uint64_t length;
    uint64_t value[0];
};



extern struct pmem_t *pmem_open(const char *path, uint64_t pmem_size) {
    struct pmem_t *p = calloc(1, sizeof(struct pmem_t));
    if(!p) {        
        return NULL;
    }
    int fd = open(path, O_RDWR);
    void* dest = mmap(NULL, pmem_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, fd, 0);
    close(fd);
    p->map_base = dest;
    return p;
}

extern void pmem_read(struct pmem_t *pmem, void *dest, uint64_t offset , size_t length){
    void *src = (char*)(pmem->map_base) + offset;
    memcpy(dest, src, length);
}

static void pmem_write_async(struct pmem_t *pmem, const void* src, uint64_t offset, size_t length){
    void *dst = pmem->map_base + offset;
    memcpy(dst,src,length);
}


static void pmem_flush(struct pmem_t *pmem, int sync, uint64_t offset, size_t length){
    void *dst = pmem->map_base + offset;
    persist_data(sync,CLFLUSH_USED,dst,length);
}

static void pmem_write(struct pmem_t *pmem, int sync, const void* src, uint64_t offset, size_t length){
    void *dst = pmem->map_base + offset;
    // memcpy(dst,src,length);
    // persist_data(sync,CLFLUSH_USED,dst,length);
    nvmem_memcpy(sync,dst,src,length);
}



extern void pmem_rollback(struct pmem_t *pmem , int cpu) {
    uint64_t offset_ulog = 4096 + cpu * 4096;
    union pm_undolog_header_t uh;
    char  pm_undolog_pload [4096];
    uint64_t offset_ulog_pload = offset_ulog + sizeof(uh);
    pmem_read(pmem, &uh,offset_ulog,sizeof(uh));
    if(uh.valid) {
        log_critical("Pmem transaction roll back.\n");
        uint16_t n = uh.nr_logs;
        uint32_t len = uh.align_length;
        assert( len % 256 == 0);
        pmem_read(pmem,pm_undolog_pload,offset_ulog_pload,len);
        int i;
        char *cur_entry = pm_undolog_pload;
        for(i = 0; i < n ; ++i) {
            struct pm_undolog_entry_t* e = (void*)cur_entry;
            assert(e->length % 64 == 0);
            assert(e->ofst % 64 == 0);
            pmem_write(pmem, 1 , e->value, e->ofst, e->length);
            cur_entry += sizeof(*e) + e->length;
        }
        memset(&uh,0,sizeof(uh));
        pmem_write(pmem,1,&uh,offset_ulog,sizeof(uh));
        log_critical("Pmem transaction roll back done.\n");
    }
}

extern void pmem_atomic_multi_update(struct pmem_t *pmem, int cpu, size_t n, struct pmem_update_entry_t *upe) {
    size_t i ;    
    uint64_t offset_ulog = 4096 + cpu * 4096;
    uint64_t offset_ulog_pload = offset_ulog + sizeof(union pm_undolog_header_t);
    uint64_t alen = 0;
    
    char tmp [4096]__attribute__((aligned (64)));

    union pm_undolog_header_t *uh = (void*)tmp;
    uh->nr_logs = n;
    uh->valid = 1;
    struct pm_undolog_entry_t *ue = (void*)(char *)(uh + 1);
    
    char *ue_start = (void*)ue;

    for ( i = 0 ; i < n ; ++i) {
        alen += sizeof(struct pm_undolog_entry_t) + upe[i].len_;
        if(alen > 4096 - 64) {
            log_err("Transaction is too big:alen=%lu\n", alen);
            return;
        }
        ue->length = upe[i].len_;
        ue->ofst = upe[i].offset_;
        memcpy(ue->value, upe->old_value_, ue->length);
        
        ue = (void*)((char*)ue + 
            sizeof(struct pm_undolog_entry_t) + ue->length);    
    }
    alen = (alen + 256 - 1) & (~( 256 - 1));
    if (alen > 4096 - 64) {
        log_err("Transaction is too big:alen=%lu\n", alen);
        return;      
    }
    uh->align_length = alen;
    
    //Step1. write_flush ulog payload
    pmem_write(pmem,1,ue_start,offset_ulog_pload, alen);
    
    //Step2. write_flush ulog header 
    pmem_write(pmem,1,uh,offset_ulog,sizeof(*uh));
    
    //Step3. Apply all new data
    for( i = 0 ; i < n; ++i) {
        pmem_write(pmem, 0 , upe[i].new_value_,upe[i].offset_,upe[i].len_);
    }
    _mm_sfence();

    //Step4. 
    memset(uh,0,sizeof(*uh));
    pmem_write(pmem,1,uh,offset_ulog,sizeof(*uh));

    return;
}


extern void pmem_close(struct pmem_t *pmem) {
    free(pmem);
}