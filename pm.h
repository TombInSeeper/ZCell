#ifndef PM_H
#define PM_H

#include "util/common.h"

struct pmem_t {
    int fd;
    void *map_base;
};

struct pmem_update_entry_t {
    uint64_t offset_;       // (void*)dst-(void*)base
    uint32_t len_;          //Must be 64B aligned
    void     *old_value_;   
    void     *new_value_;
};

extern struct pmem_t *pmem_open(const char *path, uint64_t mem_size);
// void pmem_memcpy_64B_aligned(const void *src , void *dest , size_t len);
// void pmem_flush_64B_aligned(const void *src , void *dest , size_t len);
// //Use ntstore
// void pmem_nt_memcpy_64B_aligned(const void *src , void *dest , size_t len);
extern void pmem_read(struct pmem_t *pmem, void *dest, uint64_t offset , size_t length);

//Multithread Unsafe 
extern void pmem_atomic_multi_update(struct pmem_t *pmem, int cpu, size_t n, struct pmem_update_entry_t *upe);

extern void pmem_close(struct pmem_t *pmem);

#endif