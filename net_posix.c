/* Standard C */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* POSIX */
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <regex.h>

/* GNU extension */
#include <getopt.h>


#include "net_posix.h"
#if defined(__linux__)
#include <sys/epoll.h>
#include <linux/errqueue.h>
#elif defined(__FreeBSD__)
#include <sys/event.h>
#endif

#define MAX_EVENTS_PER_POLL 64
#define MAX_TMPBUF 1024
#define PORTNUMLEN 32
#define SO_RCVBUF_SIZE (1 * 1024 * 1024)
#define SO_SNDBUF_SIZE (1 * 1024 * 1024)
#define IOV_BATCH_SIZE 64

#if defined(SO_ZEROCOPY) && defined(MSG_ZEROCOPY)
#define _ZEROCOPY
#endif

#define __posix_sock(sock) (struct posix_sock *)sock
#define __posix_group_impl(group) (struct posix_sock_group *)group


struct posix_sock {
	struct sock	base;
	int			fd;
};

struct posix_sock_group {
	struct sock_group	base;
	int				fd;
};

static int get_addr_str(struct sockaddr *sa, char *host, size_t hlen)
{
	const char *result = NULL;

	if (sa == NULL || host == NULL) {
		return -1;
	}

	switch (sa->sa_family) {
	case AF_INET:
		result = inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
				   host, hlen);
		break;
	case AF_INET6:
		result = inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
				   host, hlen);
		break;
	default:
		break;
	}

	if (result != NULL) {
		return 0;
	} else {
		return -1;
	}
}


extern int posix_getaddr( struct sock * _sock, char *saddr, int slen, uint16_t *sport, char *caddr,
            int clen, uint16_t *cport)
{
    struct posix_sock *sock = __posix_sock(_sock);
	struct sockaddr_storage sa;
	socklen_t salen;
	int rc;

	assert(sock != NULL);

	memset(&sa, 0, sizeof sa);
	salen = sizeof sa;
	rc = getsockname(sock->fd, (struct sockaddr *) &sa, &salen);
	if (rc != 0) {
		return -1;
	}

	switch (sa.ss_family) {
	case AF_UNIX:
		/* Acceptable connection types that don't have IPs */
		return 0;
	case AF_INET:
	case AF_INET6:
		/* Code below will get IP addresses */
		break;
	default:
		/* Unsupported socket family */
		return -1;
	}

	rc = get_addr_str((struct sockaddr *)&sa, saddr, slen);
	if (rc != 0) {
		return -1;
	}

	if (sport) {
		if (sa.ss_family == AF_INET) {
			*sport = ntohs(((struct sockaddr_in *) &sa)->sin_port);
		} else if (sa.ss_family == AF_INET6) {
			*sport = ntohs(((struct sockaddr_in6 *) &sa)->sin6_port);
		}
	}

	memset(&sa, 0, sizeof sa);
	salen = sizeof sa;
	rc = getpeername(sock->fd, (struct sockaddr *) &sa, &salen);
	if (rc != 0) {
		return -1;
	}

	rc = get_addr_str((struct sockaddr *)&sa, caddr, clen);
	if (rc != 0) {
		return -1;
	}

	if (cport) {
		if (sa.ss_family == AF_INET) {
			*cport = ntohs(((struct sockaddr_in *) &sa)->sin_port);
		} else if (sa.ss_family == AF_INET6) {
			*cport = ntohs(((struct sockaddr_in6 *) &sa)->sin6_port);
		}
	}

	return 0;
}

enum posix_sock_create_type {
	SOCK_CREATE_LISTEN,
	SOCK_CREATE_CONNECT,
};

static int
_posix_sock_set_recvbuf(struct sock *_sock, int sz)
{
	struct posix_sock *sock = __posix_sock(_sock);
	int rc;

	assert(sock != NULL);

	if (sz < SO_RCVBUF_SIZE) {
		sz = SO_RCVBUF_SIZE;
	}

	rc = setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int
_posix_sock_set_sendbuf(struct sock *_sock, int sz)
{
	struct posix_sock *sock = __posix_sock(_sock);
	int rc;

	assert(sock != NULL);

	if (sz < SO_SNDBUF_SIZE) {
		sz = SO_SNDBUF_SIZE;
	}

	rc = setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static struct posix_sock *
_posix_sock_alloc(int fd)
{
	struct posix_sock *sock;
	int rc;
#ifdef SPDK_ZEROCOPY
	int flag;
#endif

	sock = calloc(1, sizeof(*sock));
	if (sock == NULL) {
		// SPDK_ERRLOG("sock allocation failed\n");
		return NULL;
	}

	sock->fd = fd;

	rc = _posix_sock_set_recvbuf(&sock->base, SO_RCVBUF_SIZE);
	if (rc) {
		/* Not fatal */
	}

	rc = _posix_sock_set_sendbuf(&sock->base, SO_SNDBUF_SIZE);
	if (rc) {
		/* Not fatal */
	}

#ifdef SPDK_ZEROCOPY
	/* Try to turn on zero copy sends */
	flag = 1;
	rc = setsockopt(sock->fd, SOL_SOCKET, SO_ZEROCOPY, &flag, sizeof(flag));
	if (rc == 0) {
		sock->zcopy = true;
	}
#endif

	return sock;
}

static struct sock *
_posix_sock_create(const char *ip, int port, enum posix_sock_create_type type)
{
	struct posix_sock *sock;
	char buf[MAX_TMPBUF];
	char portnum[PORTNUMLEN];
	char *p;
	struct addrinfo hints, *res, *res0;
	int fd, flag;
	int val = 1;
	int rc;

	if (ip == NULL) {
		return NULL;
	}
	if (ip[0] == '[') {
		snprintf(buf, sizeof(buf), "%s", ip + 1);
		p = strchr(buf, ']');
		if (p != NULL) {
			*p = '\0';
		}
		ip = (const char *) &buf[0];
	}

	snprintf(portnum, sizeof portnum, "%d", port);
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_flags |= AI_PASSIVE;
	hints.ai_flags |= AI_NUMERICHOST;
	rc = getaddrinfo(ip, portnum, &hints, &res0);
	if (rc != 0) {
		return NULL;
	}

	/* try listen */
	fd = -1;
	for (res = res0; res != NULL; res = res->ai_next) {
retry:
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) {
			/* error */
			continue;
		}
		rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val);
		if (rc != 0) {
			close(fd);
			/* error */
			continue;
		}
		rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val);
		if (rc != 0) {
			close(fd);
			/* error */
			continue;
		}

		if (res->ai_family == AF_INET6) {
			rc = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val);
			if (rc != 0) {
				close(fd);
				/* error */
				continue;
			}
		}

		if (type == SOCK_CREATE_LISTEN) {
			rc = bind(fd, res->ai_addr, res->ai_addrlen);
			if (rc != 0) {
				// SPDK_ERRLOG("bind() failed at port %d, errno = %d\n", port, errno);
				switch (errno) {
				case EINTR:
					/* interrupted? */
					close(fd);
					goto retry;
				case EADDRNOTAVAIL:
						    // "Verify IP address in config file "
						    // "and make sure setup script is "
						    // "run before starting spdk app.\n", ip);
				/* FALLTHROUGH */
				default:
					/* try next family */
					close(fd);
					fd = -1;
					continue;
				}
			}
			/* bind OK */
			rc = listen(fd, 512);
			if (rc != 0) {
				close(fd);
				fd = -1;
				break;
			}
		} else if (type == SOCK_CREATE_CONNECT) {
			rc = connect(fd, res->ai_addr, res->ai_addrlen);
			if (rc != 0) {
				/* try next family */
				close(fd);
				fd = -1;
				continue;
			}
		}

		flag = fcntl(fd, F_GETFL);
		if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0) {
			// SPDK_ERRLOG("fcntl can't set nonblocking mode for socket, fd: %d (%d)\n", fd, errno);
			close(fd);
			fd = -1;
			break;
		}
		break;
	}
	freeaddrinfo(res0);

	if (fd < 0) {
		return NULL;
	}

	sock = _posix_sock_alloc(fd);
	if (sock == NULL) {
		close(fd);
		return NULL;
	}

	/* Disable zero copy for client sockets until support is added */
	if (type == SOCK_CREATE_CONNECT) {
		// sock->zcopy = false;
	}

	return &sock->base;
}

extern int posix_set_recvbuf(struct sock *sock, int sz) {
	return _posix_sock_set_recvbuf(sock,sz);
}
extern int posix_set_sendbuf(struct sock *sock, int sz){
	return _posix_sock_set_sendbuf(sock,sz);
}


extern struct sock *posix_connect(const char *ip, int port)
{
	return _posix_sock_create(ip, port, SOCK_CREATE_CONNECT);

}

extern struct sock *posix_listen(const char *ip, int port)
{
	return _posix_sock_create(ip, port, SOCK_CREATE_LISTEN);

}

extern struct sock *posix_accept(struct sock *_sock)
{
   	struct posix_sock		*sock = __posix_sock(_sock);
	struct sockaddr_storage		sa;
	socklen_t			salen;
	int				rc, fd;
	struct posix_sock		*new_sock;
	int				flag;

	memset(&sa, 0, sizeof(sa));
	salen = sizeof(sa);

	assert(sock != NULL);

	rc = accept(sock->fd, (struct sockaddr *)&sa, &salen);

	if (rc == -1) {
		return NULL;
	}

	fd = rc;

	flag = fcntl(fd, F_GETFL);
	if ((!(flag & O_NONBLOCK)) && (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0)) {
		// SPDK_ERR
		// ("fcntl can't set nonblocking mode for socket, fd: %d (%d)\n", fd, errno);
		close(fd);
		return NULL;
	}

	new_sock = _posix_sock_alloc(fd);
	if (new_sock == NULL) {
		close(fd);
		return NULL;
	}

	return &new_sock->base; 
}

extern int  posix_close(struct sock *_sock)
{
	struct posix_sock *sock = __posix_sock(_sock);
	/* If the socket fails to close, the best choice is to
	 * leak the fd but continue to free the rest of the sock
	 * memory. */
	close(sock->fd);
	free(sock);
	return 0;
}

// extern int  posix_read(struct sock *sock, void *buf, uint32_t len);
// extern int  posix_write(struct sock *sock, void *buf, uint32_t len);

extern ssize_t  posix_readv (struct sock *_sock, struct iovec *iov, uint32_t iovcnt)
{
	struct posix_sock *sock = __posix_sock(_sock);
	return readv(sock->fd, iov, iovcnt);

}

extern ssize_t  posix_writev (struct sock *_sock, struct iovec *iov, uint32_t iovcnt)
{
	struct posix_sock *sock = __posix_sock(_sock);
	return writev(sock->fd, iov, iovcnt);
}

extern sock_group* posix_group_create (void)
{
    struct posix_sock_group *group_impl;
	int fd;

#if defined(__linux__)
	fd = epoll_create1(0);
#elif defined(__FreeBSD__)
	fd = kqueue();
#endif
	if (fd == -1) {
		return NULL;
	}
	group_impl = calloc(1, sizeof(*group_impl));
	if (group_impl == NULL) {
		// SPDK_ERRLOG("group_impl allocation failed\n");
		close(fd);
		return NULL;
	}

	group_impl->fd = fd;
	return &group_impl->base;
}

extern int  posix_group_add_sock (struct sock_group *_group, struct sock *_sock)
{
	struct posix_sock_group *group = __posix_group_impl(_group);
	struct posix_sock *sock = __posix_sock(_sock);
	int rc;

#if defined(__linux__)
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	/* EPOLLERR is always on even if we don't set it, but be explicit for clarity */
	event.events = EPOLLIN | EPOLLERR;
	event.data.ptr = sock;

	rc = epoll_ctl(group->fd, EPOLL_CTL_ADD, sock->fd, &event);
#elif defined(__FreeBSD__)
	struct kevent event;
	struct timespec ts = {0};

	EV_SET(&event, sock->fd, EVFILT_READ, EV_ADD, 0, 0, sock);

	rc = kevent(group->fd, &event, 1, NULL, 0, &ts);
#endif
	return rc;
}

extern int posix_group_remove_sock(struct sock_group *_group, struct sock *_sock)
{
	struct posix_sock_group *group = __posix_group_impl(_group);
	struct posix_sock *sock = __posix_sock(_sock);
	int rc;

#if defined(__linux__)
	struct epoll_event event;

	/* Event parameter is ignored but some old kernel version still require it. */
	rc = epoll_ctl(group->fd, EPOLL_CTL_DEL, sock->fd, &event);
#elif defined(__FreeBSD__)
	struct kevent event;
	struct timespec ts = {0};

	EV_SET(&event, sock->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

	rc = kevent(group->fd, &event, 1, NULL, 0, &ts);
	if (rc == 0 && event.flags & EV_ERROR) {
		rc = -1;
		errno = event.data;
	}
#endif

	// spdk_sock_abort_requests(_sock);

	return rc;
}

extern int posix_group_poll(struct sock_group *_group, int max_events,
                struct sock **socks)
{
    struct posix_sock_group *group = __posix_group_impl(_group);
	struct sock *sock;
	int num_events, i, j;
#if defined(__linux__)
	struct epoll_event events[MAX_EVENTS_PER_POLL];
#elif defined(__FreeBSD__)
	struct kevent events[MAX_EVENTS_PER_POLL];
	struct timespec ts = {0};
#endif

	// /* This must be a TAILQ_FOREACH_SAFE because while flushing,
	//  * a completion callback could remove the sock from the
	//  * group. */
	// TAILQ_FOREACH_SAFE(sock, &_group->socks, link, tmp) {
	// 	rc = _sock_flush(sock);
	// 	if (rc) {
	// 		spdk_sock_abort_requests(sock);
	// 	}
	// }

#if defined(__linux__)
	num_events = epoll_wait(group->fd, events, max_events, 0);
#elif defined(__FreeBSD__)
	num_events = kevent(group->fd, NULL, 0, events, max_events, &ts);
#endif

	if (num_events == -1) {
		return -1;
	}

	for (i = 0, j = 0; i < num_events; i++) {
#if defined(__linux__)
		sock = events[i].data.ptr;

#ifdef SPDK_ZEROCOPY
		if (events[i].events & EPOLLERR) {
			rc = _sock_check_zcopy(sock);
			/* If the socket was closed or removed from
			 * the group in response to a send ack, don't
			 * add it to the array here. */
			if (rc || sock->cb_fn == NULL) {
				continue;
			}
		}
#endif
		if (events[i].events & EPOLLIN) {
			socks[j++] = sock;
		}

#elif defined(__FreeBSD__)
		socks[j++] = events[i].udata;
#endif
	}
	return j;
}

extern int posix_group_close(struct sock_group *_group)
{
	struct posix_sock_group *group = __posix_group_impl(_group);
	int rc;
	rc = close(group->fd);
	free(group);
	return rc;    
}
