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


static const char *g_base_ip = "127.0.0.1";
static int g_base_port = 18000;
static int g_n_tasks = 1;
static int g_n_servers = 1;
static int g_qd = 64;
static int g_data_sz = 0x1000;

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


volatile unsigned int g_task_start = 0;

typedef struct client_task_data {
    int cpuid;
    int rqsts;
    int qd;
    const char *srv_ip;
    int srv_port;
    //........
    uint64_t start;
    uint64_t end;
    double qps; // K OPS
    double bd; // MiB/s
} client_task_data;

void*  client_task(void* arg)
{
    client_task_data *data = arg;

    printf("Task[%d] ,Staring...\n",data->cpuid);

    // if ( sched_setaffinity(getpid(),sizeof(cpuset),&cpuset) )  {
    //     printf("sched_setaffinity failed\n");
    //     return NULL;
    // }

    static msgr_client_if_t cif;
    msgr_client_if_init(&cif);
    messager_conf_t conf = {
        .on_send_message= on_send_message,
        .on_recv_message = on_recv_message
    };
    int rc = cif.messager_init(&conf);
    // printf("Task[%d] , Connect OK \n",data->cpuid);
    
    assert (rc== 0);
    static char meta_buffer[128];
    static char data_buffer[4096 * 1024];
    void *session1 = cif.messager_connect(data->srv_ip,data->srv_port);
    if(!session1) {
        return NULL;
    }
    // printf("Task[%d] , Connect OK \n",data->cpuid);

    message_t msg = {
        .state = {
            .hdr_rem_len = sizeof(msg_hdr_t),
            .meta_rem_len = 0,
            .data_rem_len = g_data_sz
        },
        .header = {
            .seq = 0,
            .type = MSG_PING,
            .prio = 0,
            .meta_length = 0,
            .data_length = g_data_sz,
        },
        .meta_buffer = meta_buffer,
        .data_buffer = data_buffer,
        .priv_ctx = session1 
    };
    printf("Task[%d] Wait for starting.., rqsts = %d, connect with[%s:%d]\n",data->cpuid, data->rqsts,
        data->srv_ip , data->srv_port);

    while (g_task_start == 0)
        ;

    struct timespec ts, te;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    data->start = ts.tv_nsec  + ts.tv_sec * 1000000000ULL;
    int i;
    for ( i = 0 ; i < data->rqsts ; ) {
        int qd = g_qd;
        int j ;
        for ( j = 0 ; j < qd ; ++j) {
            cif.messager_sendmsg(&msg);
            msg.header.seq++;
        }
        int rc = 0;
        int n_send = 0 ;

        while(1) {
            rc = cif.messager_flush();
            if (rc > 0) {
                n_send += rc;
                if(n_send == qd) {
                    break;
                }
            } 
            // else if (rc == 0) {
            //     if(n_send > 0) {
            //         break;
            //     }
            // }
        }

        if(n_send < qd) {
            printf ("Qd is too big and reassign to %d\n", n_send);
            qd = n_send;
        }

        int n_wait = n_send;
        int recv = n_recv;
        do {
            rc = cif.messager_wait_msg(0);
        } while (n_recv - recv != n_wait);
        assert (n_recv - recv == n_wait);

        i+= n_wait;
    }

    data->rqsts = i;

    clock_gettime(CLOCK_MONOTONIC, &te);
    data->end = te.tv_nsec  + te.tv_sec * 1000000000ULL;
    double tt =  (data->end - data->start) / 1e3;
    double avg_lat = tt / data->rqsts;
    double qps = data->rqsts * 1000 / tt ;
    double bd = (qps * g_data_sz) / 1000;
    printf("Task[%d] done, rqsts = %d, time = %lf us, avg_lat=%lf ,qps= %lf K, bd = %lf MB/s \n",
        data->cpuid, data->rqsts, (data->end - data->start) / 1e3, avg_lat , qps , bd);
    data->qps =qps;
    data->bd = bd;
    cif.messager_close(session1);
    cif.messager_fini();
    return NULL;
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "i:p:n:s:b:h")) != -1) {
		switch (opt) {
		case 'i':
			g_base_ip = optarg;
			break;
		case 'p':
			g_base_port = atoi(optarg);
			break;
        case 'n':
			g_n_tasks = atoi(optarg);
			break;
        case 's':
			g_n_servers = atoi(optarg);   
			break;
        case 'b':
			g_data_sz = atoi(optarg);
			break;  
        case 'q':
			g_qd = atoi(optarg);
			break; 
        case 'h':      
		default:
			fprintf(stderr, "Usage: %s [-i ip] [-p port] [-n nr_client threads] [-n servers][-b block_size][-p ]\n", argv[0]);
			exit(1);
		}
	}
}

int main(int argc, char **argv) {
    
    parse_args(argc, argv);
    assert(_system_init() == 0);

    pthread_t tasks[128];
    client_task_data data[128];
    int i;
    for ( i = 0 ; i < g_n_tasks ; ++i) {
        client_task_data _tmp = {
            .srv_ip = g_base_ip,
            .srv_port = g_base_port + (i % g_n_servers),
            .cpuid = i,
            .rqsts = 10000
        };
        data[i] = _tmp;


        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(data[i].cpuid,&cpuset);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr,sizeof(cpuset),&cpuset);
        pthread_attr_setstacksize(&attr , 16 << 20);
        pthread_attr_setschedpolicy(&attr,SCHED_RR);
        int t = pthread_create(&tasks[i],&attr,client_task,&data[i]);
        if(t) {
            printf("Thread create failed\n");
            exit(1);
        }
    }

    g_task_start = 1;

    double qps = 0;
    double bd  = 0;
    for ( i = 0 ; i < g_n_tasks ; ++i) {
        pthread_join(tasks[i], NULL);
        qps += data[i].qps;
        bd += data[i].bd;
    }
    printf("====================[Main]====================\n");
    printf("|| Sum:qps=%lf K , bandwidth=%lf MiB/s ||\n" , qps, bd );
    printf("====================[Main]====================\n");

    _system_fini();
}