# Makefile for HeMem Multi-Region Tests

CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -pthread -I../src -DALLOC_RUNTIME
LDFLAGS = -pthread -lm

# Source files
SRC_DIR = ../src
POLICY_DIR = ../src/policies

HEMEM_SRCS = \
	$(SRC_DIR)/policy_runtime.c \
	$(SRC_DIR)/fifo.c \
	$(SRC_DIR)/pebs.c \
	$(POLICY_DIR)/lru.c \
	$(POLICY_DIR)/simple.c \
	hemem_stubs.c

HEMEM_OBJS = $(HEMEM_SRCS:.c=.o)

# Test executables
test_regions: test_regions.o $(HEMEM_OBJS)
	$(CC) $(CFLAGS) -o test_regions test_regions.o $(HEMEM_OBJS) $(LDFLAGS)

# Compile test file
test_regions.o: test_regions.c
	$(CC) $(CFLAGS) -c -o test_regions.o test_regions.c

# Compile HeMem sources
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(POLICY_DIR)/%.o: $(POLICY_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Run the test
test: test_regions
	@echo "========================================="
	@echo "Running HeMem Multi-Region Core Tests"
	@echo "========================================="
	@./test_regions

# Quick smoke test  
smoke: test
	@echo "Tests complete"

# Clean
clean:
	rm -f test_regions test_simple *.o $(SRC_DIR)/*.o $(POLICY_DIR)/*.o

.PHONY: all test smoke clean

all: test_regions

# Help
help:
	@echo "HeMem Multi-Region Simple Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build test executable"
	@echo "  test   - Build and run tests"
	@echo "  smoke  - Quick smoke test"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this message"
