#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

#define 

/**

 * 

*/
typedef struct msg_hdr_t {
    _le64 seq;  //unique seq in a session
    _le16 type; // message type
    _le16 prio; //
    _le32 rsv;  //
    
} msg_hdr_t;

typedef struct message_t {

    char *data;
}message_t;

#endif