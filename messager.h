#ifndef MESSAGER_H
#define MESSAGER_H

#include "message.h"
#include "util/chrono.h"


typedef struct messager_conf_t {

    union {
        struct {
            //【必须填写】，后端网络类型，SOCK_POSIX 或者 SOCK_RDMA
            int sock_type;
            // 【server 类型必须填写】
            // 对于服务端类型的messager需要指定 ip
            // char ip[46];
            const char *ip;
            
            // 【server 类型必须填写】
            // 对于服务端类型的messager需要指定 server
            int port;
        };
        struct {           
            int shm_id;
            // const char *ring_name_prefix;
        };
    };

    //【必须填写】
    //msg 参数指向的空间以及内部的 meta buffer 与 data buffer 将在回调执行完成后释放
    //如果要避免释放 meta buffer 和 data buffer 请使用如下方法：
    // 1. 手动将 msg->data_buffer msg->meta_buffer 置 NULL 
    // 2. 使用 message_move 函数将源消息的正文（所属session,header+meta+data) 转移到新的 message
    void (*on_recv_message)(message_t *msg);
    
    //【必须填写】
    //对于网络传输 messager
    //msg 参数指向的空间以及内部的 meta buffer 与 data buffer 将在回调执行完成后释放
    //如果要避免释放 meta buffer 和 data buffer 请使用如下方法：
    // 1. 手动将 msg->data_buffer msg->meta_buffer 置 NULL 
    // 2. 使用 message_move 函数将源消息的正文（所属session,header+meta+data) 转移到目的 message
    void (*on_send_message)(message_t *msg);

    // 【此回调只有 network 类型的 client 需要填写】
    // 当 messager 在进行消息收发时，seesion 有可能被异常关闭
    // 此回调在 session 被异常关闭前调用
    // 上层使用此回调进行清理工作，比如把透明的 void* 指针从活跃列表里删除 
    // 注意不要在这个回调里再调用 messager_close()，会产生二次释放问题
    void (*on_shutdown_session)(void *session , const char *ip, int port);

    //【选填】
    //如果不为 NULL，重载 data buffer 的内存分配函数
    void* (*data_buffer_alloc)(uint32_t sz);

    //【选填】
    //如果不为 NULL，重载 data buffer 的内存释放函数
    void (*data_buffer_free)(void *buffer);


    //【选填】
    //如果不为 NULL，重载 meta buffer 的内存分配函数
    void* (*meta_buffer_alloc)(uint32_t sz);

    //【选填】
    //如果不为 NULL，重载 meta buffer 的内存释放函数
    void (*meta_buffer_free)(void *buffer);

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
    

    uint64_t (*messager_last_busy_ticks)();

    //把一个消息放到发送队列
    //返回值：0 成功
    //返回值：-1，内部缓存已满，需要等待 reply_poller 将 inflight message 刷回
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
    void* (*messager_connect)(const char *ip , int port , void *priv_ctx);

    ///返回一个透明的 session 指针
    void* (*messager_connect2)(uint32_t core , void *priv_ctx);


    //获得与一个session关联的前一级上下文
    void* (*messager_get_session_ctx)(void* session);

    
    //关闭一个session
    void  (*messager_close)(void *session);

    //把一个消息放到对应 session 的发送队列
    //返回值：0 成功
    //返回值：-1, 内部缓存达到上限，需要调用 flush 
    int   (*messager_sendmsg)(const message_t *_msg);
    

    int   (*messager_wait_msg_of) (void* session); 


    int   (*messager_flush_msg_of)(void* session); 

    //把所有发送队列中的消息flush
    //返回值：>=0 成功发送的消息个数
    //返回值：-1 内部错误
    int   (*messager_flush)(); 


    //轮询所有session，试图接收消息
    //返回值：>=0，收到的消息个数
    //返回值：-1 内部错误
    int   (*messager_wait_msg)(); 
    // Poll once for income messages, return income messages number 

} msgr_client_if_t;

extern const msgr_server_if_t *msgr_get_server_impl();

extern const msgr_server_if_t *msgr_get_ipc_server_impl();

extern const msgr_client_if_t *msgr_get_client_impl();

extern const msgr_client_if_t *msgr_get_ipc_client_impl();


#endif