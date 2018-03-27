CC=gcc -std=c11
CXX=g++ -std=c++11

#CC = clang-3.8
#CXX = clang++-3.8

########
#  Lib directories
########
LIBFS_DIR := $(CURDIR)/../../libfs
NVML_DIR := $(abspath $(LIBFS_DIR)/lib/nvml/src)
DPDK_DIR := $(abspath $(LIBFS_DIR)/lib/dpdk-16.07/x86_64-native-linuxapp-gcc)
SPDK_DIR := $(abspath $(LIBFS_DIR)/lib/spdk)
CUCKOO_DIR := $(abspath $(LIBFS_DIR)/lib/cuckoofilter)
#GLIB_DIR := $(abspath $(LIBFS_DIR)/lib/glib)
GLIB_DIR :=
LIBSPDK_DIR := $(abspath $(LIBFS_DIR)/src/storage/spdk/)
C_LIBCUCKOO_DIR := $(abspath $(LIBFS_DIR)/lib/libcuckoo-c/src)

INCLUDES := $(addprefix -I, .. . $(LIBSPDK_DIR) $(NVML_DIR)/include $(CUCKOO_DIR)/src $(C_LIBCUCKOO_DIR) $(GLIB_DIR) $(GLIB_DIR)/glib)

LD_FLAGS = -lpthread -lm -L$(NVML_DIR)/nondebug/ -lpmem -lrt \
	-L$(abspath $(C_LIBCUCKOO_DIR)/.libs) -lcuckoo_hash -Wl,-rpath=$(abspath $(C_LIBCUCKOO_DIR)/.libs) \
	-Wl,-rpath=$(abspath $(GLIB_DIR)/glib/.libs) -L$(abspath $(GLIB_DIR)/glib/.libs) #-lglib-2.0
LD_FLAGS_CXX = -lpthread -lm -L$(NVML_DIR)/nondebug/ -lpmem -lrt \
	-L$(abspath $(C_LIBCUCKOO_DIR)/.libs) -lcuckoo_hash -Wl,-rpath=$(abspath $(C_LIBCUCKOO_DIR)/.libs) \
	-Wl,-rpath=$(abspath $(GLIB_DIR)/glib/.libs) -L$(abspath $(GLIB_DIR)/glib/.libs) #-lglib-2.0
#DEBUG = -g -O0
DEBUG = -g -Ofast
#DEBUG = -O3

#MLFS_FLAGS = -DUSE_SLAB -DMLFS_INFO -DKERNFS
MLFS_FLAGS = -DMLFS_INFO -DKERNFS
#MLFS_FLAGS = -DKERNFS
#MLFS_FLAGS += -DBALLOC
#MLFS_FLAGS += -DDIGEST_OPT
#MLFS_FLAGS += -DIOMERGE
#MLFS_FLAGS += -DCONCURRENT
#MLFS_FLAGS += -DFCONCURRENT
#MLFS_FLAGS += -DUSE_SSD
#MLFS_FLAGS += -DUSE_HDD
#MLFS_FLAGS += -DMIGRATION
#MLFS_FLAGS += -DEXPERIMENTAL
#MLFS_FLAGS += -DHASHTABLE


########
#  Phony targets
########
.PHONY: kernfs all clean

BIN := kernfs fifo_cli extent_test

all: $(BIN)

clean:
	@rm -rfv *.o $(BIN)

scrub: clean
	@rm -rfv *.txt *.out *.log

time_stat.o: time_stat.c
	$(CC) time_stat.c -c -o time_stat.o -D_BSD_SOURCE

kernfs: kernfs.c
	$(CC) $^ $(DEBUG) -o $@ $(INCLUDES) -L../build -lkernfs -L$(LIBSPDK_DIR) -lspdk $(LD_FLAGS)  -Wl,-rpath=$(abspath $(LIBSPDK_DIR)) -Wl,-rpath=$(abspath $(NVML_DIR)/nondebug)

extent_test: extent_test.cc time_stat.o
	$(CXX) $^ $(DEBUG) -o $@ $(INCLUDES) -L../build -lkernfs -L$(LIBSPDK_DIR) -lspdk $(LD_FLAGS_CXX) -Wl,-rpath=$(abspath ../build) -Wl,-rpath=$(abspath $(LIBSPDK_DIR)) -Wl,-rpath=$(abspath $(NVML_DIR)/nondebug) $(MLFS_FLAGS)

fifo_cli: fifo_cli.c
	$(CC) -o $@ $^
