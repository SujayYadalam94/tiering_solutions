# Makefile for ARMS Kernel-based Memory Tiering System

CXX = g++
CC = gcc

# Compiler flags
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread -g

# Include paths
INCLUDES = -I.

# Libraries
LIBS = -lnuma -lpthread -ldl

# Models and outputs
MODELS := $(wildcard models/*.o)
LIB_OUTPUT_DIR := libraries
LIB_TARGETS := $(patsubst models/%.o,$(LIB_OUTPUT_DIR)/libhemem-%.so,$(MODELS))
ARMS_TARGET := $(LIB_OUTPUT_DIR)/libhemem-arms.so
LOGGING_TARGET := $(LIB_OUTPUT_DIR)/libhemem-logging.so

# Target
TARGET_LIB = libarms_kernel.so

# Source files
SRCS = arms_kernel.cpp \
	madvise_thread.cpp \
	pebs_scan_thread.cpp \
	pagemap_scan_thread.cpp \
	migration_worker.cpp \
	policy_thread.cpp \
	timer.cpp hook/hook.cpp groups.cpp page.cpp logging.cpp interpose.cpp model.cpp
OBJS = $(SRCS:.cpp=.o)

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean $(TARGET_LIB)

all: $(LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET)

$(TARGET_LIB): $(HOOK_SRC) arms_kernel.cpp | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $(TARGET_LIB) -O3 \
	    $(LIBS) \
	    $(EXTRA_COMPILE_ARGS)

# Build one ARMS library per model object under models/
# Example: models/foo.o -> libraries/libhemem-foo.so
$(LIB_OUTPUT_DIR)/libhemem-%.so: $(SRCS) models/%.o | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) models/$*.o -o $@ -O3 \
		$(LIBS) \
		-DUSE_MODEL=true \
		$(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false
$(ARMS_TARGET): $(SRCS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $@ -O3 \
	    $(LIBS) \
	    -DUSE_MODEL=false \
	    $(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false and PRINT_TRAINING_DATA=true
$(LOGGING_TARGET): $(SRCS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $(SRCS) -o $@ -O3 \
	    $(LIBS) \
	    -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -DMAX_LOGGED_SAMPLES=100000000 \
	    $(EXTRA_COMPILE_ARGS)

$(LIB_OUTPUT_DIR):
	mkdir -p $(LIB_OUTPUT_DIR)

# Compile C++ sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET_LIB) $(LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET)