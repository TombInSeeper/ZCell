#ifndef NET_POSIX_H
#define NET_POSIX_H

#include "net.h"
extern int posix_getaddr( struct sock *sock, char *saddr, int slen, uint16_t *sport, char *caddr,
            int clen, uint16_t *cport);
extern struct sock *posix_connect(const char *ip, int port);
extern struct sock *posix_listen(const char *ip, int port);
extern struct sock *posix_accept(struct sock *sock);
extern int  posix_close(struct sock *sock);
extern int  posix_read(struct sock *sock, void *buf, uint32_t len);
extern int  posix_write(struct sock *sock, void *buf, uint32_t len);
extern int posix_set_recvbuf(struct sock *sock, int sz);
extern int posix_set_sendbuf(struct sock *sock, int sz);
extern ssize_t  posix_readv (struct sock *sock, struct iovec *iov, uint32_t iovcnt);
extern ssize_t  posix_writev (struct sock *sock, struct iovec *iov, uint32_t iovcnt);  

extern sock_group* posix_group_create (void);
extern int  posix_group_add_sock (struct sock_group *group, struct sock *sock);
extern int posix_group_remove_sock(struct sock_group *group, struct sock *sock);
extern int posix_group_poll(struct sock_group *group, int max_events,
                struct sock **socks);
extern int posix_group_close(struct sock_group *group);

#endif