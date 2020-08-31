#ifndef MESSAGER_H
#define MESSAGER_H

#include "message.h"


#ifdef  MSGR_DEBUG
#define msgr_log(level, ...) do { \
        if((level) <= MSGR_DEBUG_LEVEL) {\
            printf("%s:%d:[%d](%s):" , __FILE__, __LINE__, (level), __func__ );\
            printf(__VA_ARGS__);\
        }\
    } while (0) 

#define msgr_err(...) msgr_log(0, __VA_ARGS__)
#define msgr_warn(...) msgr_log(1, __VA_ARGS__)
#define msgr_info(...) msgr_log(5, __VA_ARGS__)
#define msgr_debug(...) msgr_log(10,__VA_ARGS__)

#else
#define msgr_log(level, ...)
#define msgr_err(...) 
#define msgr_warn(...) 
#define msgr_info(...)
#define msgr_debug(...) 
#endif

typedef struct messager_conf_t {

    int sock_type;

    char ip[46];
    int port;
    //msg 参数指向的空间将在回调执行完成后释放
    void (*on_recv_message)(message_t *msg);
    
    //msg 参数指向的空间将在回调执行完成后释放
    void (*on_send_message)(message_t *msg);

    void * (*data_buffer_alloc)(uint32_t sz);

    void (*data_buffer_free)(void *buffer);

}messager_conf_t;

typedef struct msgr_server_if_t {
    int  (*messager_init)(messager_conf_t *conf);
    int  (*messager_start)();
    void (*messager_stop)();
    void (*messager_fini)();
    int  (*messager_sendmsg)(message_t *_msg_rvalue_ref);
} msgr_server_if_t;

typedef struct msgr_client_if_t {
    int  (*messager_init)(messager_conf_t *conf);
    void (*messager_fini)();
    ///return ptr to session
    void* (*messager_connect)(const char *ip , int port);
    //
    void (*messager_close)(void *sess);

    int  (*messager_sendmsg)(message_t *_msg_rvalue_ref);
    
    int  (*messager_flush)(); //flush all inflight msgs, return success number

    int  (*messager_wait_flush)(uint32_t max); //flush all inflight msgs, return success number

    int  (*messager_wait_msg)(uint32_t max); // Poll once for income messages, return income messages number 

} msgr_client_if_t;

extern int msgr_server_if_init(msgr_server_if_t * sif); 
extern int msgr_client_if_init(msgr_client_if_t * cif); 

#endif