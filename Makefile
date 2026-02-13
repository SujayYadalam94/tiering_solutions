# Makefile for ARMS Kernel-based Memory Tiering System

CXX = g++
CC = gcc

# Compiler flags
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread -g

# Include paths
INCLUDES = -I.

# Libraries
LIBS = -lnuma -lpthread -ldl

# Build directories for reusable objects
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
MODEL_OBJ_DIR := $(OBJ_DIR)/model
TRAIN_OBJ_DIR := $(OBJ_DIR)/train
NOMODEL_OBJ_DIR := $(OBJ_DIR)/nomodel
LOGGING_OBJ_DIR := $(OBJ_DIR)/logging
ARMS_TRAIN_OBJ_DIR := $(OBJ_DIR)/arms_train

# Models and outputs
MODELS := $(wildcard models/*.o)
LIB_OUTPUT_DIR := libraries
LIB_TARGETS := $(patsubst models/%.o,$(LIB_OUTPUT_DIR)/libhemem-%.so,$(MODELS))
TRAIN_LIB_TARGETS := $(patsubst models/%.o,$(LIB_OUTPUT_DIR)/libhemem-%_train.so,$(MODELS))
ARMS_TARGET := $(LIB_OUTPUT_DIR)/libhemem-arms.so
LOGGING_TARGET := $(LIB_OUTPUT_DIR)/libhemem-logging.so
ARMS_TRAIN_TARGET := $(LIB_OUTPUT_DIR)/libhemem-arms_train.so

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
OBJ_NAMES = $(SRCS:.cpp=.o)
MODEL_OBJS = $(addprefix $(MODEL_OBJ_DIR)/,$(OBJ_NAMES))
TRAIN_OBJS = $(addprefix $(TRAIN_OBJ_DIR)/,$(OBJ_NAMES))
NOMODEL_OBJS = $(addprefix $(NOMODEL_OBJ_DIR)/,$(OBJ_NAMES))
LOGGING_OBJS = $(addprefix $(LOGGING_OBJ_DIR)/,$(OBJ_NAMES))
ARMS_TRAIN_OBJS = $(addprefix $(ARMS_TRAIN_OBJ_DIR)/,$(OBJ_NAMES))

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean $(TARGET_LIB)

all: $(LIB_TARGETS) $(TRAIN_LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET) $(ARMS_TRAIN_TARGET)

$(TARGET_LIB): $(NOMODEL_OBJS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
	    $(LIBS) \
	    $(EXTRA_COMPILE_ARGS)

# Build one ARMS library per model object under models/
# Example: models/foo.o -> libraries/libhemem-foo.so
$(LIB_OUTPUT_DIR)/libhemem-%.so: $(MODEL_OBJS) models/%.o | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
		$(LIBS) \
		$(EXTRA_COMPILE_ARGS)

# Build one ARMS library per model object under models with logging enabled
# Example: models/foo.o -> libraries/libhemem-foo.so
$(LIB_OUTPUT_DIR)/libhemem-%_train.so: $(TRAIN_OBJS) models/%.o | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
		$(LIBS) \
		$(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false
$(ARMS_TARGET): $(NOMODEL_OBJS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
	    $(LIBS) \
	    $(EXTRA_COMPILE_ARGS)

# Build without linking a model; force USE_MODEL=false and PRINT_TRAINING_DATA=true
$(LOGGING_TARGET): $(LOGGING_OBJS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
	    $(LIBS) \
	    $(EXTRA_COMPILE_ARGS)

# Build ARMS with training data logging enabled (no model linked)
$(ARMS_TRAIN_TARGET): $(ARMS_TRAIN_OBJS) | $(LIB_OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
	    $(LIBS) \
	    $(EXTRA_COMPILE_ARGS)

$(LIB_OUTPUT_DIR):
	mkdir -p $(LIB_OUTPUT_DIR)

$(MODEL_OBJ_DIR) $(TRAIN_OBJ_DIR) $(NOMODEL_OBJ_DIR) $(LOGGING_OBJ_DIR) $(ARMS_TRAIN_OBJ_DIR): | $(OBJ_DIR)
	mkdir -p $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Compile C++ sources for each variant
$(MODEL_OBJ_DIR)/%.o: %.cpp | $(MODEL_OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -DUSE_MODEL=true -c $< -o $@

$(TRAIN_OBJ_DIR)/%.o: %.cpp | $(TRAIN_OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -DUSE_MODEL=true -DPRINT_TRAINING_DATA=true -c $< -o $@

$(NOMODEL_OBJ_DIR)/%.o: %.cpp | $(NOMODEL_OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -DUSE_MODEL=false -c $< -o $@

$(LOGGING_OBJ_DIR)/%.o: %.cpp | $(LOGGING_OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -DMAX_LOGGED_SAMPLES=100000000 -DLOGGING_RUN=true -c $< -o $@

$(ARMS_TRAIN_OBJ_DIR)/%.o: %.cpp | $(ARMS_TRAIN_OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(TARGET_LIB) $(LIB_TARGETS) $(TRAIN_LIB_TARGETS) $(ARMS_TARGET) $(ARMS_TRAIN_TARGET) $(LOGGING_TARGET)