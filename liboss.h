#ifndef LIBOSS_H
#define LIBOSS_H

#include "operation.h"
#include "message.h"

struct io_channel;
typedef struct io_channel io_channel;


extern int tls_io_ctx_init(int flags);
extern int tls_io_ctx_fini();

extern io_channel *get_io_channel_with(const char *ip, int port);
extern void put_io_channel( io_channel *ioch);

extern int  io_stat(io_channel *ch , uint32_t oid);
extern int  io_create(io_channel *ch , uint32_t oid);
extern int  io_delete(io_channel *ch , uint32_t oid);

extern int  io_read(io_channel  *ch, uint32_t oid, uint64_t ofst, uint32_t len);
extern int  io_write(io_channel *ch, uint32_t oid, const void* buffer, uint64_t ofst, uint32_t len);

extern int  io_buffer_alloc(void** ptr, size_t size);
extern int  io_buffer_free (void* ptr);

extern int  op_claim_result(int op_id, int *status, int* op_type, void** data_buffer, uint64_t data_len);
extern int  op_destory( int *ops, uint32_t op_nr);

extern int  io_submit_to_channel(io_channel *ch , int *ops , uint32_t op_nr);
extern int  io_poll_channel(io_channel *ch, int *op_cpl, int max);

#endif