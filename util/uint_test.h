#ifndef UINT_TEST_H
#define UINT_TEST_H
#include "assert.h"

#define ASSERT_EQ(exp1,exp2) \
    do{\
        typeof(exp1) _v1 = (exp1);\
        typeof(exp2) _v2 = (exp2);\
        assert(_v1 == _v2);\
    }while(0)
#endif