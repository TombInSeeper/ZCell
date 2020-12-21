#ifndef IPC_SHM_H
#define IPC_SHM_H

#include <stdint.h>

#define NR_CORE_MAX (128)

struct zcell_ipc_config_t {
    union {
        struct {
            int32_t  zcell_running;
            uint32_t zcell_reactor_num;
            int32_t  tgt_running;
            uint32_t tgt_reactor_num;
            int32_t  zcell_cpu_cores[NR_CORE_MAX];
            struct spdk_ring *zcell_rings[NR_CORE_MAX];
            // struct spdk_mempool *zcell_msg_pool[NR_CORE_MAX];
            
            int32_t  tgt_cpu_cores[NR_CORE_MAX];
            struct spdk_ring *tgt_rings[NR_CORE_MAX];
            // struct spdk_ring *tgt_msg_pool[NR_CORE_MAX];
        };
        uint8_t _pad[4 * 1024];
    };
};




#endif