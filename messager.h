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

    //后端网络类型，POSIX 或者 RDMA
    int sock_type;
    
    // 对于服务端类型的messager需要指定 ip
    // char ip[46];
    const char *ip;
    

    // 对于服务端类型的messager需要指定 server
    int port;

    //msg 参数指向的空间以及内部的 meta buffer 与 data buffer 将在回调执行完成后释放
    //如果要避免释放 meta buffer 和 data buffer 请使用如下方法：
    // 1. 手动将 msg->data_buffer msg->meta_buffer 置 0 
    // 2. 使用 message_move 函数将源消息的正文（所属session,header+meta+data) 转移到目的 message
    //建议使用第2个方法
    void (*on_recv_message)(message_t *msg);
    
    //msg 参数指向的空间以及内部的 meta buffer 与 data buffer 将在回调执行完成后释放
    //如果要避免释放 meta buffer 和 data buffer 请使用如下方法：
    // 1. 手动将 msg->data_buffer msg->meta_buffer 置 0 
    // 2. 使用 message_move 函数将源消息的正文（所属session,header+meta+data) 转移到目的 message
    //建议使用第2个方法
    void (*on_send_message)(message_t *msg);


    //如果不为 NULL，重载 data buffer 的内存分配函数
    void* (*data_buffer_alloc)(uint32_t sz);

    //如果不为 NULL， 重载 data buffer 的内存释放函数
    void (*data_buffer_free)(void *buffer);

}messager_conf_t;

typedef struct msgr_server_if_t {
    
    //初始化 messager内部的数据结构
    int  (*messager_init)(messager_conf_t *conf);

    //启动 messager 内部的 Poller 
    int  (*messager_start)();
    
    //停止messager 内部的 Poller 
    void (*messager_stop)();
    
    //销毁 messager内部的数据结构
    void (*messager_fini)();
    
    //把一个消息放到发送队列
    //返回值：0 成功
    //返回值：-EAGAIN，内部缓存已满，需要等待 reply_poller 将 inflight message 刷回
    int  (*messager_sendmsg)(const message_t *_msg);
} msgr_server_if_t;






// **messager**
//messager 是一个 Per-thread 结构
//用于维护与当前 reactor 关联的所有session
//并负责所有 session 上的消息收发
typedef struct msgr_client_if_t {
    // messager 构造
    int  (*messager_init)(messager_conf_t *conf);
    
    // messager 析构
    void (*messager_fini)();

    ///返回一个透明的 session 指针
    void* (*messager_connect)(const char *ip , int port);
    
    //关闭一个session
    void  (*messager_close)(void *sess);

    //把一个消息放到发送队列
    //返回值：0 成功
    //返回值：-EAGAIN，内部缓存达到上限，需要调用 flush 
    int   (*messager_sendmsg)(const message_t *_msg);
    

    //把所有发送队列中的消息flush
    //返回值：>=0 成功发送的消息个数
    //返回值：errno 不可挽救的内部错误
    int   (*messager_flush)(); 


    //轮询所有session，试图接收消息
    //返回值：>=0 ，收到的消息个数
    //返回值：errno 不可挽救的内部错误
    int   (*messager_wait_msg)(); 
    // Poll once for income messages, return income messages number 

} msgr_client_if_t;

extern int msgr_server_if_init(msgr_server_if_t *sif); 
extern int msgr_client_if_init(msgr_client_if_t *cif); 

#endif