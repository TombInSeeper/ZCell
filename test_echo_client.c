#include "spdk/stdinc.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/sock.h"
#include "spdk/util.h"

#include "msg.h"

static uint64_t g_seq = 0;

void make_message(struct request_hdr_t *m)
{
    memset(m, 0, sizeof(struct request_hdr_t));
    m->op_seq = g_seq++;
    m->op_id = OP_TEST;
    m->coll_id = 2333;
    m->obj_id = 23333;
    m->obj_offset = 0;
    m->payload_length = 0x1000;
    m->op_flag |= OPFLAG_HAS_PAYLOAD;
}






int main( int argc , char **argv)
{

    int dp = 1;

    if(argc == 2) {
        dp = atoi(argv[1]);
    } else {
        printf("Usage: %s [request_depth]", argv[0]);
        exit(0);
    }

    struct spdk_env_opts opts;
    spdk_env_opts_init(&opts);
    opts.core_mask = "[1]";
    spdk_env_init(&opts);
    size_t len = sizeof(struct request_hdr_t) + 0x1000;
    char *buf = malloc(len);

    make_message((struct request_hdr_t *)buf);

    char *response = malloc(sizeof(struct request_hdr_t));
    struct spdk_sock *sock = spdk_sock_connect("127.0.0.1", 18000, "posix");
    if (!sock) {
        SPDK_ERRLOG("sock create error , fuck\n");
        goto end;
    }




    // const int dp = 512;
    const int total = 1 * 1000 ;
    // int n = total;
    int n = 0;
    uint64_t start_tsc, end_tsc;

    start_tsc = spdk_get_ticks();

    while ( n ++ < total) {
        int i;
        struct iovec iov;
        for ( i = 0 ; i < dp ; ++i) {
            iov.iov_base = buf;
            iov.iov_len =  len;      
            while (iov.iov_len)
            {
                int r = spdk_sock_writev(sock, &iov, 1);
                if (r < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    SPDK_ERRLOG("fuck,write error\n");
                    goto end;
                }
                int ad = (r > 0) ? r : 0;
                iov.iov_base = (char *)(iov.iov_base) + ad;
                iov.iov_len -= ad;
            }
            // SPDK_NOTICELOG("send done\n");
        }
        // SPDK_NOTICELOG("Write successfully\n");
        for (i = 0 ; i < dp ; ++i) {
            iov.iov_base = buf;
            iov.iov_len =  len;  
            while (iov.iov_len)
            {
                int r = spdk_sock_readv(sock, &iov, 1);
                if (r < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    SPDK_ERRLOG("fuck,read error\n");
                    goto end;
                }
                int ad = (r > 0) ? r : 0;
                iov.iov_base = (char *)(iov.iov_base) + ad;
                iov.iov_len -= ad;
            }
            // SPDK_NOTICELOG("recv done\n");

        }
    }

    // sleep(1);
    // SPDK_NOTICELOG("Read successfully\n");
    end_tsc = spdk_get_ticks();

    double tus = ((double)(end_tsc - start_tsc) / (double)(spdk_get_ticks_hz()) * 1e6);
    double iops = (((double)total * dp * 1e6) / (double)tus);
    printf("%lu us\n", (uint64_t)tus);
    printf("%lf K iops\n", iops / 1000.0);

end:
    free(buf);
    free(response);
    spdk_sock_close(&sock);
    spdk_env_fini();
}