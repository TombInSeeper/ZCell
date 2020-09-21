#include "liboss.h"

#include "util/log.h"
#include "util/chrono.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct admin_context_t {
    const char *ip;
    int port;
    int errcode;
    
    //Liboss
    io_channel *ioch;
} admin_context_t;

typedef int (*handler_func) (admin_context_t *actx);

typedef struct command_handler_t {
    char name_[128];
    handler_func handler_;
} command_handler_t;






void print_usage_and_exit() {
    const char *_usage = "client_admin [ip] [port] [sub_command(stat/put/get/delete/perf)] [sub_args] \
    [NOTICE]:put 传入的字符串长度不能长于 4095 \n\
    [NOTICE]:perf 创建 10,000 个 4M 对象，生成 100,000 次 4K rand w/r 操作, 输出 IOPS 和 lat \n\
    put [object_id] [string]\n\
    get [object_id] \n\
    delete [object_id] \n\
    perf [seq/rand] [qd] \n\
    e.x.(put)  ./client_admin 10.0.12.10 6677 put 0 \"XYZABCDE\" \n\
    e.x.(get)  ./client_admin 10.0.12.10 6677 get 0 \n\
    e.x.(delete)  ./client_admin 10.0.12.10 6677 delete 0 \n\
    e.x.(perf)  ./client_admin 10.0.12.10 6677 perf  \n\
    \
    ";
    printf(_usage);
    exit(1);
} 

typedef struct _perf_io_t {
    uint32_t oid;
    uint32_t ofst;
    uint32_t len;
} _perf_io_t;


static int _do_create_or_delete_test_objects(admin_context_t *ac , int create) {
    int i;
    int n_objs = 10000;
    int *opds = calloc (256, sizeof(int));
    int *opds_cpl = calloc(256,sizeof(int));
    uint64_t cre_st = now();
    log_info("Creating %d objects..\n" , n_objs);
    for (i = 0 ; i < n_objs / (200) ; ++i) {
        int j;
        for (j = 0 ; j < 200; ++j) {
            if(create)
                opds[j] = io_create(ac->ioch,i);
            else 
                opds[j] = io_delete(ac->ioch,i);
            // log_debug("opd=%d\n",opds[j]);
            if(opds[j] < 0) {
                log_err("Unexpected opd:%d\n",opds[j]);    
                exit(1);
            }
        }

        // exit(1);

        int rc = io_submit_to_channel(ac->ioch,opds, 200);
        if(rc) {
            log_err("Submit error\n");
            exit(1);
        }        
        
        io_poll_channel(ac->ioch,opds_cpl,200,200);
        for ( j = 0; j < 200 ; ++j) {
            int status;
            op_claim_result(ac->ioch, opds_cpl[j], &status, NULL, NULL, NULL);
            if(status) {
                log_err("Unexpected status:%d, op_id=%d\n",status,opds_cpl[j]);    
                exit(1);
            }
        }
    }    
    uint64_t cre_ed = now();
    double cre_dur = cre_ed - cre_st;
    double cre_lat = n_objs / cre_dur;
    
    const char *pr = "";
    if(create)
        pr = "Create";
    else
        pr = "Delete";
    
    log_info("%s %d objects time:%lf us , avg_lat=%lf us \n", pr, n_objs, cre_dur, cre_lat);

    free(opds);
    free(opds_cpl);
    return 0;
}

int _do_perf(admin_context_t *ac, int qd) {
    log_info("Perf start...\n");
    _do_create_or_delete_test_objects(ac , 1);
    _do_create_or_delete_test_objects(ac , 0);
    log_info("Perf done...\n");
    return 0;
}


int _sync_with_op(admin_context_t *ac , int opd) {
    int rc = io_submit_to_channel(ac->ioch, &opd, 1);
    if(rc < 0) {
        log_err("opd=%d, submit falied, error= %s \n", opd , strerror(rc) );
        return -1;
    }
    log_debug("opd=%d, submit OK\n", opd);
    int cpl;
    rc = io_poll_channel(ac->ioch, &cpl, 1, 1);
    if(rc < 0) {
        log_err("opd=%d, poll falied , error = %s \n", opd , strerror(rc));
        return -1;
    }
    log_debug("opd=%d, complete OK\n", cpl);
    return 0;
}

int _do_create(admin_context_t *ac , uint32_t oid) {
    int opd = io_create(ac->ioch , oid);
    log_debug("opd=%d, prepare OK\n", opd);
    _sync_with_op(ac, opd);
    int cpl = opd;
    int op_type;
    void *data_buffer;
    uint32_t data_len;
    op_claim_result(ac->ioch, cpl, &ac->errcode, &op_type, &data_buffer, &data_len);
    log_debug("Execute result of op(%d), status_code=(%d)\n", cpl, ac->errcode);    
    op_destory(ac->ioch, cpl);
    return ac->errcode;
}

int _do_delete(admin_context_t *ac , uint32_t oid) {
    int opd = io_delete(ac->ioch , oid);
    log_debug("opd=%d, prepare OK\n", opd);
    _sync_with_op(ac, opd);
    int cpl = opd;
    int op_type;
    void *data_buffer;
    uint32_t data_len;
    op_claim_result(ac->ioch, cpl, &ac->errcode, &op_type, &data_buffer, &data_len);
    log_debug("Execute result of op(%d), status_code=(%d)\n", cpl, ac->errcode);    
    op_destory(ac->ioch, cpl);
    return ac->errcode;
}

int _do_write(admin_context_t *ac, uint32_t oid, const char *str) {
    if (strlen(str) >= 4096) {
        log_err("String is too long\n");
        return -1;
    }
    // uint32_t fsz = _fstat.st_size;
    uint32_t fsz = 0x1000;
    void *iobuf;
    io_buffer_alloc(&iobuf, fsz); 
    
    strcpy((char*)iobuf, str);

    int opd = io_write(ac->ioch, oid, iobuf, 0, fsz);
    log_debug("opd=%d, prepare OK\n", opd);
    
    _sync_with_op(ac , opd);

    int cpl = opd;
    //
    int status, op_type;
    void *data_buffer;
    uint32_t data_len;
    op_claim_result(ac->ioch, cpl, &status, &op_type, &data_buffer, &data_len);
    log_info("Execute result of op(%d), status_code=(%d)\n", cpl, status);    
    data_buffer == NULL ? ({(void)0;}) :({log_warn("Write op response data_buffer is not NULL\n"); 0 ;});
    op_destory(ac->ioch, cpl);
    io_buffer_free(iobuf);
    return status;
}

int _do_get(admin_context_t *ac, uint32_t oid) {
    int opd = io_read(ac->ioch, oid , 0 , 0x1000);
    _sync_with_op(ac , opd);
    //
    int cpl = opd;
    int status, op_type;
    void *data_buffer;
    uint32_t data_len;
    op_claim_result(ac->ioch, cpl, &status, &op_type, &data_buffer, &data_len);
    log_info("Execute result of op(%d), status_code=(%d)\n", cpl, status);    
    data_buffer != NULL ? ({(void)0;}):({log_warn("Read op response data_buffer is NULL\n");});
    op_destory(ac->ioch, cpl);

    char *_str = data_buffer;
    _str[4095] = '\0';
    log_info("\n%s\n", _str);

    io_buffer_free(data_buffer);
    return status;
}

int _do_stat(admin_context_t *ac) {
    int opd = io_stat(ac->ioch);
    log_debug("opd=%d, prepare OK\n", opd);

    _sync_with_op(ac, opd);

    int cpl = opd;
    int status, op_type;
    void *data_buffer;
    uint32_t data_len;
    op_claim_result(ac->ioch, cpl, &status, &op_type, &data_buffer, &data_len);

    log_debug("Execute result of op(%d), status_code=(%d)\n", cpl, status);    

    if(data_buffer) {
        log_info("***Result cannot be parsed now, TODO***\n");
        io_buffer_free(data_buffer);
    } else {
        log_err("Some errors ocurred\n");
    }
    op_destory(ac->ioch, cpl);
    return 0;
}


int _run(admin_context_t *ac, int argc , char **argv) {
    if (argc < 3 ) {
        print_usage_and_exit();
    } 
    ac->ip = argv[1];
    ac->port = atoi(argv[2]);

    tls_io_ctx_init(0);
    log_debug("liboss env init done\n");

    ac->ioch = get_io_channel_with(ac->ip, ac->port,256);

    if(!ac->ioch) {
        log_info("Cannot establish channel with %s:%d\n", ac->ip, ac->port);
        return -1;
    }
    log_debug("IO channel setup done\n");
 
    const char *cmd = argv[3];
    if(!strcmp(cmd, "stat")) {
        log_debug("Process [stat] command\n ");
        _do_stat(ac);
    } else if (!strcmp(cmd, "put")) {
        uint32_t oid = atoi(argv[4]);
        const char *file = argv[5];
        if(_do_create(ac, oid)) {
            log_err("Create object %u failed\n" , oid);
            return -1;
        }
        if (_do_write(ac, oid, file)) {
            log_err("Write to object %u failed\n" , oid);
            return -1;
        }
        return 0;
    } else if (!strcmp(cmd, "get")){
        uint32_t oid = atoi(argv[4]);
        if(_do_get(ac, oid)) {
            log_err("Read object %u failed\n" ,oid);
            return -1;
        }
        return 0;
    } else if (!strcmp(cmd, "delete")){
        uint32_t oid = atoi(argv[4]);
        if(_do_delete(ac, oid)) {
            log_err("Delete object %u failed\n" ,oid);
            return -1;
        }
        return 0;
    } else if (!strcmp(cmd, "perf")){
        int qd = atoi(argv[4]);
        if(_do_perf(ac ,qd)) {
            log_err("Perf failed\n");
            return -1;
        }
        return 0;
    } else {
        log_info("Invalid cmd [%s]\n" , cmd);
        return -1;
    }
    return 0;
    put_io_channel(ac->ioch);

    tls_io_ctx_fini();
}


int main(int argc, char **argv) {
    admin_context_t *ac = calloc(1,sizeof(admin_context_t));
    
    _run(ac ,argc, argv);

    free(ac);
    return 0;
}