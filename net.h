#ifndef NET_H
#define NET_H


#include "stdint.h"
#include "sys/uio.h"
#include "sys/queue.h"

enum {
    SOCK_TYPE_POSIX,
};

typedef struct sock {
    int type_info;
	void *ctx;
} sock;

typedef struct sock_group {
    int type_info;
	void *ctx;
} sock_group;

typedef struct net_impl {
    int type;
    const char *name;
	int priority;
	int (*getaddr)( struct sock *sock, char *saddr, int slen, uint16_t *sport, char *caddr,
		       int clen, uint16_t *cport);
	struct sock *(*connect)(const char *ip, int port);
	struct sock *(*listen)(const char *ip, int port);
	struct sock *(*accept)(struct sock *sock);
	int (*close)(struct sock *sock);
	// int (*read)(struct sock *sock, void *buf, uint32_t len);
	// int (*write)(struct sock *sock, void *buf, uint32_t len);
	int (*set_recvbuf)(struct sock *sock, int sz);
	int (*set_sendbuf)(struct sock *sock, int sz);
	
    ssize_t (*readv)(struct sock *sock, struct iovec *iov, uint32_t iovcnt);
	ssize_t (*writev)(struct sock *sock, struct iovec *iov, uint32_t iovcnt);   

	struct sock_group *(*group_create)();
	int (*group_add_sock)(struct sock_group *group, struct sock *sock);
	int (*group_remove_sock)(struct sock_group *group, struct sock *sock);
	int (*group_poll)(struct sock_group *group, int max_events,
			       struct sock **socks);
	int (*group_close)(struct sock_group *group);

	// STAILQ_ENTRY(spdk_net_impl) link;
} net_impl;
extern net_impl *net_impl_constructor(int type);
extern void net_impl_destructor(net_impl * impl);
	

// void (*writev_async)(struct sock *sock, struct sock_request *req);
// int (*flush)(struct sock *sock);

// int (*set_recvlowat)(struct sock *sock, int nbytes);
// int (*set_recvbuf)(struct sock *sock, int sz);
// int (*set_sendbuf)(struct sock *sock, int sz);
// int (*set_priority)(struct sock *sock, int priority);
// bool (*is_ipv6)(struct sock *sock);
// bool (*is_ipv4)(struct sock *sock);
// bool (*is_connected)(struct sock *sock);
// int (*get_placement_id)(struct sock *sock, int *placement_id);
#endif