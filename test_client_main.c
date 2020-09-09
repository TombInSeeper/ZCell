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
#include "operation.h"


#define NR_MAX_TASK 1024

static const char *g_base_ip = "127.0.0.1";
static int g_base_port = 18000;
static int g_n_tasks = 1;
static int g_n_servers = 1;
static int g_qd = 64;
static int g_data_sz = 0x1000;
static int g_rqsts = 10000;

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
}

void on_recv_message(message_t *msg) {
    // message_t m;
    // message_move(&m , msg); // prevent from free msg_buffer
    ++n_recv;
}


volatile unsigned int g_task_start = 0;

typedef struct client_task_data {
    const msgr_client_if_t * cif;
    int cpuid;
    int rqsts;
    int qd;
    const char *srv_ip;
    int srv_port;
    //........
    uint64_t start;
    uint64_t end;
    double qps; // K OPS
    double bd; // MB/s
} client_task_data;

void*  client_task(void* arg) {
    client_task_data *data = arg;
    printf("Task[%d] ,Starting...\n",data->cpuid);
    data->cif = msgr_get_client_impl();
    messager_conf_t conf = {
        .on_send_message= on_send_message,
        .on_recv_message = on_recv_message,
    };
    int rc = data->cif->messager_init(&conf);
    assert (rc== 0);

    void *session1 = data->cif->messager_connect(data->srv_ip,data->srv_port);
    if(!session1) {
        return NULL;
    }
    // printf("Task[%d] , Connect OK \n",data->cpuid);

    static op_read_t read_op_meta = {
        .oid = cpu_to_le32(0),
        .ofst= cpu_to_le32(0),
        .len = cpu_to_le32(0x1000),
        .flags = cpu_to_le32(0),
    };

    message_t msg = {
        .state = {
            .hdr_rem_len = sizeof(msg_hdr_t),
            .meta_rem_len = sizeof(op_read_t),
            .data_rem_len = 0
        },
        .header = {
            .seq = 0,
            .type = MSG_OSS_OP_READ,
            .meta_length = 0,
            .data_length = 0,
        },
        .meta_buffer = (char*)&read_op_meta,
        .data_buffer = NULL,
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
            data->cif->messager_sendmsg(&msg);
            msg.header.seq++;
        }
        int rc = 0;
        int n_send = 0 ;
 
        while(1) {
            rc = data->cif->messager_flush();
            if (rc > 0) {
                n_send += rc;
                if(n_send == qd) {
                    break;
                }
            } 
        }

        int n_wait = n_send;
        int recv = n_recv;
        do {
            rc = data->cif->messager_wait_msg();
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
    double bd = (data->rqsts * g_data_sz) / tt ;
    printf("Task[%d] done, rqsts = %d, time = %lf us, avg_lat=%lf ,qps= %lf K, bd = %lf MB/s \n",
        data->cpuid, data->rqsts, (data->end - data->start) / 1e3, avg_lat , qps , bd);
    data->qps =qps;
    data->bd = bd;
    data->cif->messager_close(session1);
    data->cif->messager_fini();
    return NULL;
}

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "i:p:n:s:b:h:q:r:")) != -1) {
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
        case 'r':
			g_rqsts = atoi(optarg);
			break; 
        case 'h':      
		default:
			fprintf(stderr, "Usage: %s [-i ip]\
             [-p port] [-n nr_client threads] [-n servers][-b block_size][-q qd ]\n", argv[0]);
			exit(1);
		}
	}
}


int main(int argc, char **argv) {
    
    parse_args(argc, argv);
    assert(_system_init() == 0);
    
    const int NR_cpu = 24;

    pthread_t tasks[NR_MAX_TASK];
    client_task_data data[NR_MAX_TASK];
    int i;
    for ( i = 0 ; i < g_n_tasks ; ++i) {
        client_task_data _tmp = {
            .srv_ip = g_base_ip,
            .srv_port = g_base_port + (i % g_n_servers),
            .cpuid = i % NR_cpu,
            .rqsts = g_rqsts
        };
        data[i] = _tmp;

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(data[i].cpuid,&cpuset);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr,sizeof(cpuset),&cpuset);
        pthread_attr_setstacksize(&attr , 16 << 20);
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
    printf("|| Sum:qps=%lf K , bandwidth=%lf MB/s ||\n" , qps, bd );
    printf("====================[Main]====================\n");

    _system_fini();
}