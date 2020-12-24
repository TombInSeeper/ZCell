
#ifndef CHRONO_H
#define CHRONO_H

#include <sys/time.h>
#include <stdint.h>

static inline uint64_t now() {
    struct timeval t;
    gettimeofday(&t,0);
    return t.tv_sec * 1000000 + t.tv_usec;
}

static inline uint64_t rdtsc() {
    union {
        uint64_t tsc_64;
        struct {
            uint32_t lo_32;
            uint32_t hi_32;
        };
    } tsc;
    asm volatile("rdtsc" :
             "=a" (tsc.lo_32),
             "=d" (tsc.hi_32));
    return tsc.tsc_64;
}

static inline double to_us(uint64_t end , uint64_t start , uint64_t hz)
{
    return ((double)(end-start)/( (double) hz / 1e6));
}


#endif
