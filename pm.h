#ifndef PM_H
#define PM_H

#include "util/common.h"

#define FLOOR_ALIGN(v , align) (((v) + (align) - 1) & (~((align)-1)) ) 

union  pmem_transaction_t;
struct pmem_t;


extern struct pmem_t *pmem_open(const char *path, uint64_t cpu, uint64_t *mem_size);
extern void pmem_read(struct pmem_t *pmem, void *dest, uint64_t offset , size_t length);

extern void pmem_write(struct pmem_t *pmem, int sync, const void* src, uint64_t offset, size_t length);

extern union pmem_transaction_t* pmem_transaction_alloc(struct pmem_t *pmem);
//pmem_addr % 64 == 0
//len % 64 == 0
extern bool pmem_transaction_add(struct pmem_t *pmem, union pmem_transaction_t *tx,
    const uint64_t pm_ofst, size_t len, void *new_value);
extern bool pmem_transaction_apply(struct pmem_t *pmem, union pmem_transaction_t *tx);
extern void pmem_transaction_free(struct pmem_t *pmem, union pmem_transaction_t *tx);

extern void pmem_close(struct pmem_t *pmem);

#endif