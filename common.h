#ifndef _COMMON_H_
#define _COMMON_H_

#include <emmintrin.h>
#include "stdint.h"
#include "memory.h"
#include "string.h"
#include "stdbool.h"

#include "errcode.h"


/**
 * 
 * DEBUG LEVEL
 * 
 * 
 */
#ifndef NDEBUG
#define MSGR_DEBUG 
#define MSGR_DEBUG_LEVEL 10
#else
#define MSGR_DEBUG 
#define MSGR_DEBUG_LEVEL 1
#endif




typedef void (*cb_func_t) (void* , int status_code);




//Okay... X86 only, little endian
typedef uint8_t  _u8;
typedef uint16_t _le16;
typedef uint32_t _le32;
typedef uint64_t _le64;

#define le16_to_cpu(u) (u)
#define cpu_to_le16(u) (u)
#define le32_to_cpu(u) (u)
#define cpu_to_le32(u) (u)
#define le64_to_cpu(u) (u)
#define cpu_to_le64(u) (u)


//Cancel struct aligned
#define _packed __attribute__((packed))


//Mfence
#define mb()    asm volatile("mfence":::"memory")
#define rmb()   asm volatile("lfence":::"memory")
#define wmb()   asm volatile("sfence" ::: "memory")
#endif