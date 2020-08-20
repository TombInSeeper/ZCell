#ifndef _MSGR_H_
#define _MSGR_H_

#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/sock.h"
#include "spdk/util.h"

#include "msg.h"



typedef struct msgr_init_opt_t {
    int type;
    const char *ip;
    int port;
    void (*on_recv_message)(struct message *m);
    void (*on_send_message)(struct message *m);

} msgr_init_opt_t;

extern int msgr_init(msgr_init_opt_t *opts);


// message m should live until 'on_send_message' was called;

extern int msgr_push_msg(struct message *m);

extern int msgr_fini();


#endif