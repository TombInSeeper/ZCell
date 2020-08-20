#ifndef _COMMON_H_
#define _COMMON_H_


#include "stdint.h"

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


#endif