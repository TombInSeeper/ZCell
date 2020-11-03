
#include "util/common.h"

//For meta data storage
//
#include "pm.h"

#include "zstore.h"

#define PAGE_ALIGN 4096
#define PAGE_ALIGN_SHIFT 12

struct zstore_extent_t {
    uint32_t lba_;
    uint32_t len_; 
};

struct zstore_ssd_space_allocator_t {
    int (*alloc)(uint64_t sz , struct extent_t *et);
    int (*dealloc)(uint64_t sz , struct extent_t *et);
};

struct zstore_pm_space_allocator_t {
    uint64_t pm_dynamic_base_addr_;
    int (*alloc)(uint64_t sz , struct extent_t *et);
    int (*dealloc)(uint64_t sz , struct extent_t *et);
};

union zstore_superblock_t {
    struct {
        uint32_t magic;
        uint32_t ssd_nr_pages;
        uint32_t pm_nr_pages;
        uint32_t pm_ulog_ofst; //4K~(4K*256)
        uint32_t pm_dy_bitmap_ofst;
        uint32_t pm_ssd_bitmap_ofst;
        uint32_t pm_otable_ofst;
        uint32_t pm_dy_space_ofst;
    };
    uint8_t align[PAGE_ALIGN];
};

struct zstore_bitmap_entry_t {
    uint64_t bits[8];
};

union otable_entry_t {
    struct {
        uint64_t oid;
        uint64_t mtime;
        uint64_t lsize;
        uint64_t psize;
        uint32_t attr_idx_addr;
        uint32_t data_idx_addr;
    };
    uint8_t align[64];
};





