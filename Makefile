CC=gcc
CXX=g++
Q=@

SPDK_PATH_PREFIX=/home/wuyue


CFLAGS=-D_GNU_SOURCE -Wall -std=gnu99 -O3 -march=native -fno-strict-aliasing 
SPDK_INCLUDE_FLAGS=-I$(SPDK_PATH_PREFIX)/spdk/include
SPDK_LINK_FLAGS=-Wl,--whole-archive  -L$(SPDK_PATH_PREFIX)/spdk/build/lib  -lspdk_env_dpdk  -lspdk_env_dpdk_rpc \
	-L$(SPDK_PATH_PREFIX)/spdk/dpdk/build/lib -ldpdk  \
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
	-Wl,--no-whole-archive  -lpthread -lrt -lnuma -ldl -luuid -lm -ltcmalloc


###########################
MAKEFLAGS += --no-print-directory

C_SRCS += $(wildcard *.c)
# CXX_SRCS += $(CXX_SRCS-y)

OBJS = $(C_SRCS:.c=.o) 


DEPFLAGS = -MMD -MP -MF $*.d.tmp

# Compile first input $< (.c) into $@ (.o)
COMPILE_C=\
	$(Q)echo "  CC $S/$@"; \
	$(CC) -o $@  $(SPDK_INCLUDE_FLAGS) $(DEPFLAGS) $(CFLAGS) -c $< && \
	mv -f $*.d.tmp $*.d && touch -c $@

# Link $(OBJS) and $(LIBS) into $@ (app)
LINK_C=\
	$(Q)echo "  LINK $S/$@"; \
	$(CC) -o $@ $(SPDK_INCLUDE_FLAGS)  $(CFLAGS) $(LDFLAGS) $^ $(LIBS)  $(SPDK_LINK_FLAGS) $(SYS_LIBS)


MSGR_OBJS = messager.o net.o net_posix.o
OSTORE_OBJS = objectstore.o fakestore.o

DRAFT_BIN=a.out
TEST_BIN=test_server test_messager_client

BIN_TGT=server client $(TEST_BIN) $(DRAFT_BIN)

.PHONY: all clean test
	
all: $(BIN_TGT) 

test: $(TEST_BIN)

server:
	

client:

test_fixed_cache:test_fixed_cache.o
	$(LINK_C)

test_fake_store:test_fake_store.o objectstore.o fakestore.o
	$(LINK_C)

test_messager_server:test_messager_server.o $(MSGR_OBJS)
	$(LINK_C)

test_messager_client:test_messager_client.o $(MSGR_OBJS)
	$(LINK_C)

test_server:test_server_main.o $(MSGR_OBJS) $(OSTORE_OBJS)
	$(LINK_C)

a.out:draft.o
	$(LINK_C)

%.o: %.c %.d
	$(COMPILE_C)

%d: ;

.PRECIOUS: $(OBJS)

-include $(OBJS:.o=.d)

clean:
	@rm -rf $(BIN_TGT) *.o core* *.d





