CC=gcc
CXX=g++

CXX_FLAGS= -D_GNU_SOURCE -Wall  -std=gnu++11 -O2 -march=native -fno-strict-aliasing 
C_FLAGS = -D_GNU_SOURCE -Wall  -std=gnu11 -O2 -march=native -fno-strict-aliasing 
SPDK_INCLUDE_FLAGS=-I ./spdk/include
SPDK_LINK_FLAGS= -Wl,--whole-archive  -Lspdk/build/lib  -lspdk_env_dpdk  -lspdk_env_dpdk_rpc \
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
	-Wl,--no-whole-archive  -lpthread -lrt -lnuma -ldl -luuid -lm -ltcmalloc


BENCH_TGT= bin_bh_tmpfs bin_bh_objectstore
EX_TGT= bin_server bin_client 

BIN_TGT= $(BENCH_TGT) $(EX_TGT)


.PHONY: all clean

all: $(BIN_TGT) 

bin_server:main.c msg.o msgr.o objectstore.o 
	@$(CC) $^  $(C_FLAGS) $(SPDK_INCLUDE_FLAGS) $(SPDK_LINK_FLAGS) -o $@
	@echo "CC $< -o $@"

bin_client:client_demo.c msg.o msgr.o
	@$(CC) $^  $(C_FLAGS) $(SPDK_INCLUDE_FLAGS) $(SPDK_LINK_FLAGS) -o $@
	@echo "CC $< -o $@"



### benchmark binary
bin_bh_tmpfs:bench_tmpfs.c
	@$(CC) $^  $(C_FLAGS) $(SPDK_INCLUDE_FLAGS) $(SPDK_LINK_FLAGS) -o $@
	@echo "CC $< -o $@"

### benchmark binary
bin_bh_objectstore:bench_objectstore.c objectstore.o 
	@$(CC) $^  $(C_FLAGS) $(SPDK_INCLUDE_FLAGS) $(SPDK_LINK_FLAGS) -o $@
	@echo "CC $< -o $@"

%.o : %.c %.h
	@$(CC) -c $< $(C_FLAGS) $(SPDK_INCLUDE_FLAGS) -o $@
	@echo "CC $< -o $@"

clean:
	@rm -rf $(BIN_TGT) *.o core*





