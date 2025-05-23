# You can generate the compile_commands.json file by using
# `make clean -j$(nproc)`
# `bear make all -j$(nproc)`
# `compdb -p ./ list > ../compile_commands.json`
# `cp ../compile_commands.json ./
# or
# `make compiledb`
#
# You can get `bear` from [link](https://github.com/rizsotto/Bear)
# You can get `compdb` from [link](https://github.com/Sarcasm/compdb)

# `-fsanitize=address` and `-lasan` for memory leakage checking
# `-g` and `-pg` for tracing the program
# So, you must delete all when you release the program

.SUFFIXES : .c .cpp .o
CC = gcc
AR = ar
CXX = g++
INTEGRATION_TEST_TARGET = integration-test.out
BENCHMARK_TARGET = benchmark.out
LIBRARY_TARGET = libftl.a

GLIB_INCLUDES = $(shell pkg-config --cflags glib-2.0)
DEVICE_INCLUDES = 

GLIB_LIBS = $(shell pkg-config --libs glib-2.0)
DOCKER_TAG_ROOT = ftl

# Device Module Setting
USE_ZONE_DEVICE = 0
USE_BLUEDBM_DEVICE = 1
# Debug Setting
USE_DEBUG = 0
USE_LOG_SILENT = 0
# Random Generator Setting
USE_LEGACY_RANDOM = 1

ifeq ($(USE_DEBUG), 1)
DEBUG_FLAGS = -g -pg \
              -DCONFIG_ENABLE_MSG \
              -DCONFIG_ENABLE_DEBUG
MACROS = -DUSE_GC_MESSAGE -DDEBUG
MEMORY_CHECK_LIBS = -lasan
MEMORY_CHECK_CFLAGS = 
MEMORY_CHECK_LIBS += -fsanitize=address
else
DEBUG_FLAGS =
MACROS = -DUSE_GC_MESSAGE
MEMORY_CHECK_LIBS =
MEMORY_CHECK_CFLAGS = 
endif

ifeq ($(USE_LOG_SILENT), 1)
DEBUG_FLAGS += -DENABLE_LOG_SILENT
endif

ifeq ($(USE_LEGACY_RANDOM), 1)
MACROS += -DUSE_LEGACY_RANDOM
endif

TEST_TARGET := lru-test.out \
              bits-test.out \
              ramdisk-test.out \
			  bluedbm-test.out

DEVICE_LIBS =

ifeq ($(USE_ZONE_DEVICE), 1)
# Zoned Device's Setting
DEVICE_INFO := -DDEVICE_NR_BUS_BITS=3 \
               -DDEVICE_NR_CHIPS_BITS=3 \
               -DDEVICE_NR_PAGES_BITS=5 \
               -DDEVICE_NR_BLOCKS_BITS=21

TEST_TARGET += zone-test.out
DEVICE_LIBS += -lzbd
else ifeq ($(USE_BLUEDBM_DEVICE), 1)
# BlueDBM Device's Setting
DEVICE_INFO := -DDEVICE_NR_BUS_BITS=3 \
               -DDEVICE_NR_CHIPS_BITS=3 \
               -DDEVICE_NR_PAGES_BITS=7 \
               -DDEVICE_NR_BLOCKS_BITS=19 \
               -DUSER_MODE \
               -DUSE_PMU \
               -DUSE_KTIMER \
               -DUSE_NEW_RMW \
               -D_LARGEFILE64_SOURCE \
               -D_GNU_SOURCE \
               -DNOHOST

DEVICE_LIBS += -lmemio
DEVICE_INCLUDES += -I/usr/local/include/memio
else
# Ramdisk Setting (1GiB)
DEVICE_INFO := -DDEVICE_NR_BUS_BITS=3 \
               -DDEVICE_NR_CHIPS_BITS=3 \
               -DDEVICE_NR_PAGES_BITS=7 \
               -DDEVICE_NR_BLOCKS_BITS=19
endif

ifeq ($(USE_ZONE_DEVICE), 1)
DEVICE_INFO += -DDEVICE_USE_ZONED \
               -DPAGE_FTL_USE_GLOBAL_RWLOCK
endif

ifeq ($(USE_BLUEDBM_DEVICE), 1)
DEVICE_INFO += -DDEVICE_USE_BLUEDBM
endif

ARFLAGS := rcs
CFLAGS := -Wall \
          -Wextra \
          -Wpointer-arith \
          -Wcast-align \
          -Wwrite-strings \
          -Wswitch-default \
          -Wunreachable-code \
          -Winit-self \
          -Wmissing-field-initializers \
          -Wno-unknown-pragmas \
          -Wundef \
          -Wconversion \
          $(DEVICE_INFO) \
          $(DEBUG_FLAGS) \
          $(MEMORY_CHECK_CFLAGS) \
          -O3

CXXFLAGS := $(CFLAGS) \
            -std=c++11

UNITY_ROOT := ./unity
LIBS := -lm -lpthread $(GLIB_LIBS) $(DEVICE_LIBS) $(MEMORY_CHECK_LIBS)

INCLUDES := -I./include -I./unity/src $(GLIB_INCLUDES) $(DEVICE_INCLUDES)

RAMDISK_SRCS = device/ramdisk/*.c
ZONED_SRCS =
BLUEDBM_SRCS = 

ifeq ($(USE_ZONE_DEVICE), 1)
ZONED_SRCS += device/zone/*.c
endif

ifeq ($(USE_BLUEDBM_DEVICE), 1)
BLUEDBM_SRCS += device/bluedbm/*.c
endif

DEVICE_SRCS := $(RAMDISK_SRCS) \
               $(BLUEDBM_SRCS) \
               $(ZONED_SRCS) \
               device/*.c

UTIL_SRCS := util/*.c

FTL_SRCS := ftl/page/*.c

INTERFACE_SRCS := interface/*.c

SRCS := $(DEVICE_SRCS) \
        $(UTIL_SRCS) \
        $(FTL_SRCS) \
        $(INTERFACE_SRCS)

OBJS := *.o

ifeq ($(PREFIX),)
PREFIX := /usr/local
endif

all: $(INTEGRATION_TEST_TARGET) $(BENCHMARK_TARGET)

test: $(TEST_TARGET)
	@for target in $(TEST_TARGET) ; do \
		./$$target ; \
	done
	# show coverage 
	@for target in $(TEST_TARGET) ; do \
		gcov ./$$target ; \
	done

integration-test: $(INTEGRATION_TEST_TARGET)
	./$(INTEGRATION_TEST_TARGET)

install:
	install -d $(DESTDIR)$(PREFIX)/lib/
	install -m 644 $(LIBRARY_TARGET) $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include/ftl
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/include/ftl

$(INTEGRATION_TEST_TARGET): integration-test.c $(LIBRARY_TARGET)
	$(CXX) $(MACROS) $(CXXFLAGS) -c integration-test.c $(INCLUDES) $(LIBS)
	$(CXX) $(MACROS) $(CXXFLAGS) -o $@ integration-test.o -L. -lftl -lpthread $(LIBS) $(INCLUDES)

$(BENCHMARK_TARGET): benchmark.c $(LIBRARY_TARGET)
	$(CXX) $(MACROS) $(CFLAGS) -g -c benchmark.c $(INCLUDES) $(LIBS)
	$(CXX) $(MACROS) $(CFLAGS) -g -o $@ benchmark.o -L. -lftl -lpthread -liberty $(INCLUDES) $(LIBS)

$(LIBRARY_TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): $(SRCS)
	$(CXX) $(MACROS) $(CFLAGS) -c $^ $(LIBS) $(INCLUDES)

lru-test.out: unity.o ./util/lru.c ./test/lru-test.c
	$(CXX) $(MACROS) $(CFLAGS) $(INCLUDES) -o $@ --coverage $^ $(LIBS)

bits-test.out: unity.o ./test/bits-test.c
	$(CXX) $(MACROS) $(CFLAGS) $(INCLUDES) -o $@ --coverage $^ $(LIBS)

ramdisk-test.out: $(OBJS) ./test/ramdisk-test.c
	$(CXX) $(MACROS) $(CFLAGS) $(INCLUDES) -o $@ --coverage $^ $(LIBS)

ifeq ($(USE_ZONE_DEVICE), 1)
zone-test.out: $(OBJS) ./test/zone-test.c
	$(CXX) $(MACROS) $(CFLAGS) -DENABLE_LOG_SILENT $(INCLUDES) -o $@ --coverage $^ $(LIBS)
endif

ifeq ($(USE_BLUEDBM_DEVICE), 1)
bluedbm-test.out : $(UNITY_ROOT)/src/unity.c $(DEVICE_SRCS) ./test/bluedbm-test.c
	$(CXX) $(MACROS) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)
endif


unity.o: $(UNITY_ROOT)/src/unity.c
	$(CXX) $(MACROS) $(CFLAGS) -DENABLE_LOG_SILENT $(INCLUDES) -c $^ $(LIBS)

docker-builder:
	docker build -t $(DOCKER_TAG_ROOT)/ftl-builder \
		-f docker/Dockerfile ./docker

docker-make-%:
	docker run --rm -v $(PWD):/ftl \
		$(DOCKER_TAG_ROOT)/ftl-builder /bin/bash -c "make clean && make $*"

docker-console:
	docker run --rm -it -v $(PWD):/ftl \
		-v ${HOME}/.zshrc:/root/.zshrc \
		-v ${HOME}/.vim:/root/.vim \
		-v ${HOME}/.vimrc:/root/.vimrc \
		$(DOCKER_TAG_ROOT)/ftl-builder /bin/bash

check:
	@echo "[[ CPPCHECK ROUTINE ]]"
	cppcheck --quiet --error-exitcode=0 --enable=all --inconclusive -I include/ $(SRCS) *.c
	@echo "[[ FLAWFINDER ROUTINE ]]"
	flawfinder $(SRCS) include/*.h
	@echo "[[ STATIC ANALYSIS ROUTINE ]]"
	lizard $(SRCS) include/*.h

documents:
	doxygen -s Doxyfile

flow:
	find . -type f -name '*.[ch]' ! -path "./unity/*" ! -path "./test/*" | xargs -i cflow {}

compiledb:
	bear $(MAKE) all
	compdb -p ./ list > ../compile_commands.json
	mv ../compile_commands.json ./

clean:
	find . -name '*.o' -exec rm -f {} +
	find . -name '*.gcov' -exec rm -f {} +
	find . -name '*.gcda' -exec rm -f {} +
	find . -name '*.gcno' -exec rm -f {} +
	rm -f $(TARGET) $(INTEGRATION_TEST_TARGET) $(TEST_TARGET) $(LIBRARY_TARGET) $(BENCHMARK_TARGET)
