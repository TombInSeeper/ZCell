#ifndef LIBOSS_H
#define LIBOSS_H

#include <stdint.h>

struct io_channel;
typedef struct io_channel io_channel;



struct oss_stat_t {
    uint32_t object_max_num;
    uint32_t object_max_size;
    uint32_t oid_block_size;
    uint64_t total_space;
    uint64_t free_space;
};

struct local_peer_t {
    uint32_t lcore;
};

extern int tls_io_ctx_init(int flags);

extern int tls_io_ctx_fini();

extern io_channel *get_io_channel_with(const char *ip, int port ,int max_qd);

extern io_channel *get_io_channel_with_local(uint32_t core ,int max_qd);

extern void put_io_channel(io_channel *ioch);
extern int  io_stat(io_channel *ch);
extern int  io_create(io_channel *ch , uint64_t oid);
extern int  io_delete(io_channel *ch , uint64_t oid);
extern int  io_read(io_channel  *ch, uint64_t oid, uint64_t ofst, uint32_t len);
extern int  io_read2(io_channel  *ch, void *rbuf, uint64_t oid, uint64_t ofst, uint32_t len );

extern int  io_write(io_channel *ch, uint64_t oid, const void* buffer, uint64_t ofst, uint32_t len);
extern int  io_buffer_alloc(io_channel *ch, void** ptr, uint32_t size);
extern int  io_buffer_free (io_channel *ch, void* ptr);

extern int  op_claim_result(io_channel *ch, int op_id, int *status, int* op_type, void** data_buffer, uint32_t *data_len);
extern int  op_destory(io_channel *ch, int op_id);

extern int  stat_result_parse_to(void *data_buffer, struct oss_stat_t *stat_, char *str);

extern int  io_submit_to_channel(io_channel *ch , int *ops , int nr);
extern int  io_poll_channel(io_channel *ch, int *op_cpl, int min,  int max);

#endif