#include "net_posix.h"
#include "malloc.h"
#include "string.h"

net_impl *net_impl_constructor(int type) {
    if(type == SOCK_TYPE_POSIX) {
        net_impl *_impl = malloc(sizeof(net_impl));
        net_impl impl = {
            .type = SOCK_TYPE_POSIX,
            .name = "posix",
            .priority = 0,
            .listen = posix_listen,
            .accept = posix_accept,
            .connect = posix_connect,
            .getaddr = posix_getaddr,
            .readv = posix_readv,
            .writev = posix_writev,
            .set_recvbuf = posix_set_recvbuf,
            .set_sendbuf = posix_set_sendbuf,
            .close = posix_close,
            .group_create = posix_group_create,
            .group_add_sock = posix_group_add_sock,
            .group_remove_sock = posix_group_remove_sock,
            .group_poll = posix_group_poll,
            .group_close = posix_group_close
        };
        memcpy(_impl , &impl , sizeof(*_impl));
        return _impl;
    } else {
        return NULL;
    }

}
void net_impl_destructor(net_impl * impl) {
    free(impl);
}