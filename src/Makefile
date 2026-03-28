CC = gcc
CXX = g++
CFLAGS = -g -Wall -O3 -fPIC
#CFLAGS = -g3 -Wall -O0 -fPIC
CXXFLAGS = -g -Wall -O3 -fPIC -std=c++20 -I./libvulcan/include
LDFLAGS = -shared
INCLUDES = -I../linux/usr/include/
LIBS = -lm -lpthread
HEMEM_LIBS = $(LIBS) -ldl -lsyscall_intercept -L../Hoard/src -lhoard -lstdc++

default: libhemem.so

all: hemem-libs libllmcode.so

hemem-libs: libhemem-lru.so libhemem-simple.so libhemem-lru-swap.so libhemem.so

libllmcode.so: llmcode_shlib.o
	$(CC) $(LDFLAGS) -o libllmcode.so llmcode_shlib.o $(LIBS)

LIBVULCAN_SRCS = libvulcan/src/vulcan.cpp libvulcan/src/feature_registry.cpp libvulcan/src/feature_store.cpp
LIBVULCAN_OBJS = $(LIBVULCAN_SRCS:.cpp=.o)

libhemem.so: hemem.o pebs.o pebs_vulcan.o timer.o interpose.o fifo.o spsc-ring.o $(LIBVULCAN_OBJS)
	$(CC) $(LDFLAGS) -o libhemem.so hemem.o timer.o interpose.o pebs.o pebs_vulcan.o fifo.o spsc-ring.o $(LIBVULCAN_OBJS) $(HEMEM_LIBS)

libhemem-lru.so: policies/hemem-lru.o policies/lru.o timer.o interpose.o policies/paging.o fifo.o spsc-ring.o
	$(CC) $(LDFLAGS) -o libhemem-lru.so policies/hemem-lru.o timer.o policies/lru.o interpose.o policies/paging.o fifo.o spsc-ring.o $(HEMEM_LIBS)

libhemem-simple.so: policies/hemem-simple.o policies/simple.o timer.o interpose.o policies/paging.o fifo.o spsc-ring.o
	$(CC) $(LDFLAGS) -o libhemem-simple.so policies/hemem-simple.o timer.o policies/simple.o interpose.o policies/paging.o fifo.o spsc-ring.o $(HEMEM_LIBS)

libhemem-lru-swap.so: policies/hemem-lru-swap.o policies/lru_swap.o timer.o interpose.o policies/paging.o fifo.o spsc-ring.o
	$(CC) $(LDFLAGS) -o libhemem-lru-swap.so policies/hemem-lru-swap.o timer.o policies/lru_swap.o interpose.o policies/paging.o fifo.o spsc-ring.o $(HEMEM_LIBS)

hemem.o: hemem.c hemem.h pebs.h interpose.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_HEMEM -c hemem.c -o hemem.o
	
policies/hemem-lru.o: hemem.c hemem.h policies/lru.h interpose.h policies/paging.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_LRU -c hemem.c -o policies/hemem-lru.o

policies/hemem-simple.o: hemem.c hemem.h policies/simple.h interpose.h policies/paging.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_SIMPLE -c hemem.c -o policies/hemem-simple.o

policies/hemem-lru-swap.o: hemem.c hemem.h policies/lru.h interpose.h policies/paging.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_LRU -D LRU_SWAP -c hemem.c -o policies/hemem-lru-swap.o

interpose.o: interpose.c interpose.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c interpose.c

timer.o: timer.c timer.h
	$(CC) $(CFLAGS) $(INCLUDES) -c timer.c

policies/lru.o: policies/lru.c policies/lru.h hemem.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_LRU -c policies/lru.c -o policies/lru.o

policies/simple.o: policies/simple.c policies/simple.h hemem.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -c policies/simple.c -o policies/simple.o

policies/lru_swap.o: policies/lru.c policies/lru.h hemem.h fifo.h spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -D LRU_SWAP -D ALLOC_LRU -c policies/lru.c -o policies/lru_swap.o

policies/paging.o: policies/paging.c policies/paging.h
	$(CC) $(CFLAGS) $(INCLUDES) -c policies/paging.c -o policies/paging.o

pebs.o: pebs.c pebs.h hemem.h fifo.h
	$(CC) $(CFLAGS) $(INCLUDES) -c pebs.c

fifo.o: fifo.c fifo.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c fifo.c

spsc-ring.o: spsc-ring.c spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -c spsc-ring.c

pebs_vulcan.o: pebs_vulcan.cpp pebs_vulcan.h
	$(CXX) $(CXXFLAGS) -c pebs_vulcan.cpp -o pebs_vulcan.o

libvulcan/src/%.o: libvulcan/src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

llmcode_shlib.o: llmcode_shlib.c hemem.h pebs.h
	$(CC) $(CFLAGS) $(INCLUDES) -c llmcode_shlib.c

clean:
	$(RM) *.o *.so policies/*.o libvulcan/src/*.o pebs_vulcan.o
