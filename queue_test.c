#include <stdio.h>
#include <stdlib.h>
#include <sys/cdefs.h>
#include <sys/queue.h>

#define MSGR_DEBUG
#ifdef  MSGR_DEBUG
#define msgr_log(level, ...) do {\
        if((level) <= 10) {\
            printf("[%d](%s):" , (level), __func__ );\
            printf(__VA_ARGS__);\
        }\
    } while (0) 

#define msgr_err(...) msgr_log(0, __VA_ARGS__)
#define msgr_warn(...) msgr_log(1, __VA_ARGS__)
#define msgr_info(...) msgr_log(5, __VA_ARGS__)
#define msgr_debug(...) msgr_log(10,__VA_ARGS__)

#else

#define MSGR_LOG(format, ...) \
    do  { \
    } while (0) 
#endif

typedef struct msg_t {
    int a;
    TAILQ_ENTRY(msg_t) msg_li_hook;
}msg_t;


int main ()
{
    msgr_err("1\n");
    return 0;
}
