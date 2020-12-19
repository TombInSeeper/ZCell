CC=gcc
CXX=g++
Q=@


ver=release
ifeq ($(ver), debug)
CFLAGS=-D_GNU_SOURCE -Wall -std=gnu99 -fno-strict-aliasing  -g -O0 
else
CFLAGS=-D_GNU_SOURCE -DWY_NDEBUG -Wall -std=gnu99 -O3 -march=native -fno-strict-aliasing 
endif



PMDK_LINK_CFLAGS= -lpmem
DPDK_LINK_CFLAGS= `pkg-config libdpdk --libs --cflags`

SPDK_PATH_PREFIX=/home/wuyue
SPDK_INCLUDE_FLAGS=-I$(SPDK_PATH_PREFIX)/spdk/include
SPDK_LINK_FLAGS=-Wl,--whole-archive  -L$(SPDK_PATH_PREFIX)/spdk/build/lib  -lspdk_env_dpdk  -lspdk_env_dpdk_rpc \
	-lspdk_json -lspdk_jsonrpc  -lspdk_rpc \
	-lspdk_bdev_malloc  -lspdk_bdev_null \
	-lspdk_bdev_nvme\
	-lspdk_bdev\
	-lspdk_event_bdev  -lspdk_event_net -lspdk_event_vmd -lspdk_event \
	-lspdk_thread -lspdk_sock_posix -lspdk_sock -lspdk_notify\
	-lspdk_net\
	-lspdk_nvme\
	-lspdk_ftl\
	-lspdk_log -lspdk_trace -lspdk_util  -lspdk_conf\
	-lspdk_vmd \
	-L$(SPDK_PATH_PREFIX)/spdk/isa-l/.libs -lisal \
	-Wl,--no-whole-archive  -lpthread -lrt -lnuma -ldl -luuid -lm -ltcmalloc

# PMDK_LINK_CFLAGS=-lpmem2


###########################
MAKEFLAGS += --no-print-directory

C_SRCS += $(wildcard *.c)

OBJS = $(C_SRCS:.c=.o) 


DEPFLAGS = -MMD -MP -MF $*.d.tmp

# Compile first input $< (.c) into $@ (.o)
COMPILE_C=\
	$(Q)echo "  CC [$(ver)] $@"; \
	$(CC) -o $@  $(SPDK_INCLUDE_FLAGS) $(DEPFLAGS) $(CFLAGS) -c $< && \
	mv -f $*.d.tmp $*.d && touch -c $@

# Link $(OBJS) and $(LIBS) into $@ (app)
	# $(Q)echo "  LINK [$(ver)] $@" && 
LINK_C=\
	$(CC) -o $@ $(SPDK_INCLUDE_FLAGS) $(PMDK_LINK_CFLAGS) $(DPDK_LINK_CFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LIBS)  $(SPDK_LINK_FLAGS) $(SYS_LIBS)


MSGR_OBJS = messager.o net.o net_posix.o
OSTORE_OBJS = objectstore.o chunkstore.o nullstore.o  zstore.o pm.o


TEST_BIN= test_objstore
BIN_TGT=server client_msgr_tool client_admin_tool  

.PHONY: all clean test
	
all: $(BIN_TGT) 

test: $(TEST_BIN)

# test_messager_server:test_messager_server.o $(MSGR_OBJS)
# 	$(LINK_C)

# test_messager_client:test_messager_client.o $(MSGR_OBJS)
# 	$(LINK_C)

server:server_main.o $(MSGR_OBJS) $(OSTORE_OBJS)
	$(LINK_C)

client_msgr_tool:test_client_main.o $(MSGR_OBJS) $(OSTORE_OBJS)
	$(LINK_C)

client_admin_tool:client_admin_tool.o liboss.o $(MSGR_OBJS)
	$(LINK_C)


# test_nvme_md:test_nvme_md.o
# 	$(LINK_C)

# test_pm:test_pm.o pm.o
# 	$(LINK_C)

test_objstore: test_objstore.o $(OSTORE_OBJS)
	$(LINK_C)

test_ipc: test_spdk_ipc.o 
	$(LINK_C)

%.o: %.c %.d
	$(COMPILE_C)

%d: ;

.PRECIOUS: $(OBJS)

-include $(OBJS:.o=.d)

clean:
	@rm -rf $(BIN_TGT) *.o core* *.d





