#include "liboss.h"

#include "util/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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


const char *sub_cmds = {
    ""
}; 


void print_usage_and_exit() {
    const char *_usage = "client_admin [ip] [port] [sub_command(stat/put/get/delete)] [sub_args] \
    [NOTICE]:PUT/GET 传入文件必须是 4K(4096bytes) 大小对齐的\n\
    put [object_id] [content file]\n\
    get [object_id] [result file]\n\
    delete [object_id] \n\
    e.x.(put)  ./client_admin 10.0.12.10 6677 put 0 FOO.txt \n\
    e.x.(get)  ./client_admin 10.0.12.10 6677 get 0 FOO.txt \n\
    e.x.(delete)  ./client_admin 10.0.12.10 6677 delete 0 \n\
    \
    ";
    printf(_usage);
    exit(1);
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

int _do_write(admin_context_t *ac, uint32_t oid, const char *file) {
    struct stat _fstat;
    if (stat(file, &_fstat)) {
        log_err("Get attr of %s falied, errstr:%s \n", file, strerror(errno));
        return -1;
    }
    if (_fstat.st_size % 4096 != 0) {
        log_err("File size isn't aligned with 4K \n");
        return -1;
    }
    uint32_t fsz = _fstat.st_size;
    void *iobuf;
    io_buffer_alloc(&iobuf, fsz);    
    do {
        FILE *fp = fopen(file, "r");
        size_t read_bytes = fread(iobuf, fsz , 1, fp);
        if(read_bytes != fsz) {
            log_err(" File IO error\n");
            fclose(fp);
            return -1;        
        }
        fclose(fp);
    } while(0);
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

int _do_get(admin_context_t *ac, uint32_t oid, const char *file) {
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

    FILE *fp = fopen(file, "w");
    do {
        fwrite(data_buffer, 0x1000, 1, fp);
    } while(0);
    fclose(fp);

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

    ac->ioch = get_io_channel_with(ac->ip, ac->port);

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
        const char *file = argv[5];
        if(_do_get(ac, oid , file)) {
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
    }  else {
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