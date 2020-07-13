#ifndef _MSGR_H_
#define _MSGR_H_

#include "spdk/stdinc.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/sock.h"
#include "spdk/util.h"

#include "msg.h"


extern int msgr_init(char *ip, int port);

extern int msgr_fini();

#endif