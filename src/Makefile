CC = gcc
CFLAGS = -g -Wall -O3 -fPIC
#CFLAGS = -g3 -Wall -O0 -fPIC
LDFLAGS = -shared
INCLUDES = -I../linux/usr/include/
LIBS = -lm -lpthread
HEMEM_LIBS = $(LIBS) -ldl -lsyscall_intercept -L../Hoard/src -lhoard

# Generate list of all .so files from models/*.o files
MODEL_OBJS := $(wildcard models/*.o)
MODEL_TARGETS := $(patsubst models/%.o,libhemem-%.so,$(MODEL_OBJS))

default: $(MODEL_TARGETS)

libhemem-%.so: models/%.o defs.o hemem.o pebs.o timer.o interpose.o fifo.o spsc-ring.o groups.o hemem_page.o logging.o model.o
	$(CC) $(LDFLAGS) -o models/$@ defs.o hemem.o timer.o interpose.o pebs.o fifo.o spsc-ring.o groups.o hemem_page.o logging.o model.o models/$*.o $(HEMEM_LIBS)

hemem.o: hemem.c hemem.h pebs.h interpose.h fifo.h spsc-ring.h defs.h hemem_page.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_HEMEM -c hemem.c -o hemem.o

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

groups.o: groups.c groups.h defs.h
	$(CC) $(CFLAGS) $(INCLUDES) -c groups.c

hemem_page.o: hemem_page.c hemem_page.h defs.h pebs.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c hemem_page.c

logging.o: logging.c logging.h defs.h hemem_page.h groups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c logging.c

model.o: model.c model.h hemem_page.h logging.h
	$(CC) $(CFLAGS) $(INCLUDES) -c model.c

defs.o: defs.c defs.h 
	$(CC) $(CFLAGS) $(INCLUDES) -c defs.c

pebs.o: pebs.c pebs.h hemem.h fifo.h groups.h defs.h hemem_page.h logging.h model.h
	$(CC) $(CFLAGS) $(INCLUDES) -c pebs.c

fifo.o: fifo.c fifo.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c fifo.c

spsc-ring.o: spsc-ring.c spsc-ring.h
	$(CC) $(CFLAGS) $(INCLUDES) -c spsc-ring.c

clean:
	$(RM) *.o *.so policies/*.o models/*.so
