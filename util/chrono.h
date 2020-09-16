
#ifndef CHRONO_H
#define CHRONO_H

#include <sys/time.h>
#include <stdint.h>

static inline uint64_t now() {
    struct timeval t;
    gettimeofday(&t,0);
    return t.tv_sec * 1000000 + t.tv_usec;
}



#endif
