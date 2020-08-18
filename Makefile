CC=gcc
CXX=g++
Q=@

CFLAGS=-D_GNU_SOURCE -Wall -std=gnu99 -O2 -march=native -fno-strict-aliasing 
SPDK_INCLUDE_FLAGS=-Ispdk/include
SPDK_LINK_FLAGS=-Wl,--whole-archive  -Lspdk/build/lib  -lspdk_env_dpdk  -lspdk_env_dpdk_rpc \
	-Lspdk/dpdk/build/lib -ldpdk  \
	-lspdk_json -lspdk_jsonrpc -lspdk_log_rpc  -lspdk_app_rpc  -lspdk_rpc \
	-lspdk_bdev_malloc  -lspdk_bdev_rpc -lspdk_bdev_null \
	-lspdk_bdev_nvme\
	-lspdk_bdev\
	-lspdk_event_bdev -lspdk_event_copy -lspdk_event_net -lspdk_event_vmd -lspdk_event \
	-lspdk_thread -lspdk_sock_posix -lspdk_sock -lspdk_notify\
	-lspdk_net\
	-lspdk_nvme\
	-lspdk_ftl\
	-lspdk_log -lspdk_trace -lspdk_util -lspdk_copy -lspdk_conf\
	-lspdk_vmd\
	-Wl,--no-whole-archive  -lpthread -lrt -lnuma -ldl -luuid -lm 


###########################
MAKEFLAGS += --no-print-directory

C_SRCS += $(wildcard *.c)
# CXX_SRCS += $(CXX_SRCS-y)

OBJS = $(C_SRCS:.c=.o) 

DEPFLAGS = -MMD -MP -MF $*.d.tmp

# Compile first input $< (.c) into $@ (.o)
COMPILE_C=\
	$(Q)echo "  LINK $S/$@"; \
	$(CC) -o $@  $(SPDK_INCLUDE_FLAGS) $(DEPFLAGS) $(CFLAGS) -c $< && \
	mv -f $*.d.tmp $*.d && touch -c $@

# Link $(OBJS) and $(LIBS) into $@ (app)
LINK_C=\
	$(Q)echo "  LINK $S/$@"; \
	$(CC) -o $@ $(SPDK_INCLUDE_FLAGS)  $(CFLAGS) $(LDFLAGS) $^ $(LIBS)  $(SPDK_LINK_FLAGS) $(SYS_LIBS)


BIN_TGT=  server client


.PHONY: all clean

all: $(BIN_TGT) 

server:demo_server.o msg.o msgr.o objectstore.o 
	$(LINK_C)

client:demo_client.o msg.o msgr.o
	$(LINK_C)

%.o : %.c %.d
	$(COMPILE_C)

%d: ;

.PRECIOUS: $(OBJS)

-include $(OBJS:.o=.d)

clean:
	@rm -rf $(BIN_TGT) *.o core* *.d





