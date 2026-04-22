# Makefile for ARMS Kernel-based Memory Tiering System

CXX = g++
CC = gcc

# Compiler flags
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -pthread -g
CFLAGS = -O2 -Wall -Wextra -pthread -g

# Include paths
INCLUDES = -I.

# Libraries
LIBS = -lnuma -lpthread

# Target
TARGET_LIB = libarms_kernel.so

# Source files
SRCS = arms_kernel.cpp timer.cpp hook/hook.cpp
OBJS = $(SRCS:.cpp=.o)

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean $(TARGET_LIB)

all: $(TARGET_LIB)

$(TARGET_LIB): $(HOOK_SRC) arms_kernel.cpp
	@echo "Building $(TARGET_LIB) with ARMS Kernel integration..."
	$(CXX) -shared -fPIC -g hook/hook.cpp timer.cpp arms_kernel.cpp -o $(TARGET_LIB) -O3 \
	    -ldl -lpthread -lnuma \
	    $(EXTRA_COMPILE_ARGS)
	@echo "Hook library built successfully: $(TARGET_LIB)"
	@echo "Usage: LD_PRELOAD=./$(TARGET_LIB) ./your_application"

# Compile C++ sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET_LIB)

# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build both shared and static libraries (default)"
	@echo "  hook         - Build hook.so for LD_PRELOAD integration"
	@echo "  test         - Build the test program"
	@echo "  clean        - Remove all build artifacts"
	@echo "  install      - Install libraries and headers to /usr/local"
	@echo ""
	@echo "Hook target options (set via environment or make arguments):"
	@echo "  TARGET_EXE_NAME      - Name of target executable (default: test_app)"
	@echo "  EXTRA_COMPILE_ARGS   - Additional compiler flags"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                                    # Build libraries"
	@echo "  make test                               # Build test program"
	@echo "  make hook TARGET_EXE_NAME=\"myapp\"       # Build hook for specific app"
	@echo "  make clean all                          # Clean and rebuild"
	@echo ""
	@echo "Using the hook library:"
	@echo "  LD_PRELOAD=./hook.so ./your_application"
	@echo ""
	@echo "To use the library in your application:"
	@echo "  g++ -o myapp myapp.cpp -L. -larms_kernel -lnuma -lpthread"
