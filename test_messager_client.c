#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "stdlib.h"
#include "unistd.h"
#include "stdio.h"
#include "assert.h"
#include "messager.h"
#include "sched.h"
#include "pthread.h"

int _system_init() {
    // struct spdk_env_opts opts;
    // spdk_env_opts_init(&opts);
    // // opts.master_core = 4;
    // int rc = spdk_env_init(&opts);
    // if(rc) {
    //     printf("SPDK env init failed\n");
    //     return -1;
    // }
    return 0;
}

void _system_fini() {
    // spdk_env_fini();
}

static __thread int  n_recv = 0;

void on_send_message(message_t *msg) {
    message_t m;
    message_move(&m , msg); // prevent from free msg_buffer
    // msgr_info("send one message\n");
}

void on_recv_message(message_t *msg) {
    message_t m;
    message_move(&m , msg); // prevent from free msg_buffer
    ++n_recv;
    // msgr_info("recive one message\n");
}


unsigned int g_task_start;
typedef struct client_task_data {
    int cpuid;
    int rqsts;
    uint64_t start;
    uint64_t end;
} client_task_data;

void*  client_task(void* arg)
{
    client_task_data *data = arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(data->cpuid,&cpuset);

    if ( sched_setaffinity(getpid(),sizeof(cpuset),&cpuset) )  {
        printf("sched_setaffinity failed\n");
        return NULL;
    }

    static msgr_client_if_t cif;
    msgr_client_if_init(&cif);
    messager_conf_t conf = {
        .on_send_message= on_send_message,
        .on_recv_message = on_recv_message
    };
    int rc = cif.messager_init(&conf);
    assert (rc== 0);
    char meta_buffer[128];
    char data_buffer[4096];
    void *session1 = cif.messager_connect("127.0.0.1",18000);
    if(!session1) {
        return NULL;
    }
    message_t msg = {
        .state = {
            .hdr_rem_len = sizeof(msg_hdr_t),
            .meta_rem_len = 128,
            .data_rem_len = 0x1000 
        },
        .header = {
            .seq = 0,
            .type = MSG_PING,
            .prio = 0,
            .meta_length = 128,
            .data_length = 0x1000,
        },
        .meta_buffer = meta_buffer,
        .data_buffer = data_buffer,
        .priv_ctx = session1 
    };
    while (g_task_start == 0)
        ;
    printf("Task[%d] Starting.., rqsts = %d\n",data->cpuid, data->rqsts);
    struct timespec ts, te;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    data->start = ts.tv_nsec  + ts.tv_sec * 1000000000ULL;
    int i;
    for ( i = 0 ; i < data->rqsts ; ++i) {
        cif.messager_sendmsg(&msg);
        int rc = 0;
        do {
            rc = cif.messager_flush();
        } while (!rc);

        int recv = n_recv;
        do {
            rc = cif.messager_wait_msg(0);
        } while (n_recv - recv < 1);
        assert (n_recv - recv == 1);
        msg.header.seq++;
    }
    clock_gettime(CLOCK_MONOTONIC, &te);
    data->end = te.tv_nsec  + te.tv_sec * 1000000000ULL;
    double tt =  (data->end - data->start) / 1e3;
    double avg_lat = tt / data->rqsts;
    double qps = data->rqsts * 1000 / tt ;
    printf("Task[%d] done, rqsts = %d, time = %lf us, avg_lat=%lf \n",
        data->cpuid, data->rqsts, (data->end - data->start) / 1e3, avg_lat );
    cif.messager_close(session1);
    cif.messager_fini();
    return NULL;
}


int main(int argc, char **argv) {
    assert(_system_init() == 0);
    int n_task = 1;
    if(argc > 1) {
        n_task = atoi(argv[1]);
    }

    pthread_t tasks[128];
    client_task_data data[128];
    int i;
    for ( i = 0 ; i < n_task ; ++i) {
        client_task_data _tmp = {
            .cpuid = i + 3,
            .rqsts = 10000
        };
        data[i] = _tmp;
        pthread_create(&tasks[i],NULL,client_task,&data[i]);
    }

    g_task_start = 1;

    for ( i = 0 ; i < n_task ; ++i) {
        pthread_join(tasks[i], NULL);
    }


    _system_fini();
}