# Makefile for ARMS Kernel-based Memory Tiering System

CXX = g++
CC = gcc

# Compiler flags
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread -g

# Include paths
INCLUDES = -I.

# Libraries
LIBS = -lnuma -lpthread -ldl

# ARMS-specific flags (can be overridden via command line)
FAST_MEMORY_SIZE_GB ?= 8

# Models and outputs
MODELS := $(wildcard models/*.so)
LIB_OUTPUT_DIR := libraries
LIB_TARGETS := $(patsubst models/%.so,$(LIB_OUTPUT_DIR)/libhemem-%.so,$(MODELS))
ARMS_TARGET := $(LIB_OUTPUT_DIR)/libhemem-arms.so
LOGGING_TARGET := $(LIB_OUTPUT_DIR)/libhemem-logging.so

# Target
TARGET_LIB = libarms_kernel.so

# Source files
SRCS = arms_kernel.cpp timer.cpp hook/hook.cpp groups.cpp page.cpp logging.cpp interpose.cpp model.cpp
OBJS = $(SRCS:.cpp=.o)

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean $(TARGET_LIB)

all: $(LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET)

$(TARGET_LIB): $(HOOK_SRC) arms_kernel.cpp | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $(TARGET_LIB) -O3 \
	    $(LIBS) \
	    -DFAST_MEMORY_SIZE_GB=$(FAST_MEMORY_SIZE_GB) \
	    $(EXTRA_COMPILE_ARGS)

# Build one ARMS library per model shared object under models/
# Example: models/foo.o -> libraries/libhemem-foo.o
$(LIB_OUTPUT_DIR)/libhemem-%.o: $(SRCS) models/%.o | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) models/$*.o -o $@ -O3 \
	    $(LIBS) \
	    -DFAST_MEMORY_SIZE_GB=$(FAST_MEMORY_SIZE_GB) -DUSE_MODEL=true \
	    -Wl,-rpath,'$$ORIGIN/../models' \
	    $(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false
$(ARMS_TARGET): $(SRCS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $@ -O3 \
	    $(LIBS) \
	    -DFAST_MEMORY_SIZE_GB=$(FAST_MEMORY_SIZE_GB) -DUSE_MODEL=false \
	    $(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false and PRINT_TRAINING_DATA=true
$(LOGGING_TARGET): $(SRCS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $@ -O3 \
	    $(LIBS) \
	    -DFAST_MEMORY_SIZE_GB=$(FAST_MEMORY_SIZE_GB) -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true \
	    $(EXTRA_COMPILE_ARGS)

$(LIB_OUTPUT_DIR):
	mkdir -p $(LIB_OUTPUT_DIR)

# Compile C++ sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET_LIB) $(LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET)