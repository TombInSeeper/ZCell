CC=gcc
CXX=g++
Q=@
AR=ar

MAKEFLAGS += --no-print-directory
C_SRCS += ./bdev_zcell.c ./bdev_zcell_rpc.c
OBJS = $(C_SRCS:.c=.o) 
DEPFLAGS = -MMD -MP -MF $*.d.tmp

SPDK_PATH_PREFIX=/home/wuyue
SPDK_INCLUDE_FLAGS=-I$(SPDK_PATH_PREFIX)/spdk/include

ver=release
ifeq ($(ver), debug)
CFLAGS=-D_GNU_SOURCE -Wall -std=gnu11 -fno-strict-aliasing  -g -O0 
else
CFLAGS=-D_GNU_SOURCE -DWY_NDEBUG -Wall -std=gnu11 -O3 -march=native -fno-strict-aliasing 
endif

# Compile first input $< (.c) into $@ (.o)
COMPILE_C=\
	$(Q)echo "  CC [$(ver)] $@"; \
	$(CC) -o $@  $(SPDK_INCLUDE_FLAGS) $(DEPFLAGS) $(CFLAGS) -c $< && \
	mv -f $*.d.tmp $*.d && touch -c $@

.PHONY: all clean 
	
all: zcell_bdev


zcell_bdev: $(OBJS)
	$(AR) rcs ./libspdk_bdev_zcell.a $^

%.o: %.c %.d
	$(COMPILE_C)

%d: ;

.PRECIOUS: $(OBJS)

-include $(OBJS:.o=.d)

clean:
	rm -rf  $(OBJS) $(OBJS:.o=.d) *.a