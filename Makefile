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

# Compile-time configuration matrix
MIN_MAX_HISTORY_VALUES := true false
HISTORY_LENGTH_VALUES := 4 8
SWITCH_SCALER_VALUES := 0.9 0.8
COMBOS := $(foreach mmh,$(MIN_MAX_HISTORY_VALUES),$(foreach hlen,$(HISTORY_LENGTH_VALUES),$(foreach scaler,$(SWITCH_SCALER_VALUES),$(mmh)_$(hlen)_$(scaler))))

# Models and outputs
MODELS := $(wildcard models/*.o)
LIB_OUTPUT_DIR := libraries
LIB_TARGETS := $(foreach combo,$(COMBOS),$(patsubst models/%.o,$(LIB_OUTPUT_DIR)/libhemem-%-$(combo).so,$(MODELS)))
TRAIN_LIB_TARGETS := $(foreach combo,$(COMBOS),$(patsubst models/%.o,$(LIB_OUTPUT_DIR)/libhemem-%_$(combo)_train.so,$(MODELS)))
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
NOMODEL_OBJS := $(addprefix $(OBJ_DIR)/nomodel/,$(OBJ_NAMES))
LOGGING_OBJS := $(addprefix $(OBJ_DIR)/logging/,$(OBJ_NAMES))
ARMS_TRAIN_OBJS := $(addprefix $(OBJ_DIR)/arms_train/,$(OBJ_NAMES))

BASE_DEFINES_model := -DUSE_MODEL=true
BASE_DEFINES_train := -DUSE_MODEL=true -DPRINT_TRAINING_DATA=true
BASE_DEFINES_nomodel := -DUSE_MODEL=false
BASE_DEFINES_logging := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -DMAX_LOGGED_SAMPLES=100000000 -DLOGGING_RUN=true
BASE_DEFINES_arms_train := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true

combo_mmh = $(word 1,$(subst _, ,$1))
combo_hlen = $(word 2,$(subst _, ,$1))
combo_scaler = $(word 3,$(subst _, ,$1))
combo_defs = -DMIN_MAX_HISTORY=$(call combo_mmh,$1) -DHISTORY_LENGTH=$(call combo_hlen,$1) -DSWITCH_SCALER=$(call combo_scaler,$1)

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean

all: $(LIB_TARGETS) $(TRAIN_LIB_TARGETS) $(ARMS_TARGET) $(LOGGING_TARGET) $(ARMS_TRAIN_TARGET)

$(TARGET_LIB): $(ARMS_TARGET) | $(LIB_OUTPUT_DIR)
	cp -f $< $@

define MAKE_COMBO_RULES
MODEL_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/model/$(1)/,$$(OBJ_NAMES))
TRAIN_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/train/$(1)/,$$(OBJ_NAMES))

# Build one ARMS library per model object under models/ and config combo
# Example: models/foo.o -> libraries/libhemem-foo-true_2_1.0.so
$$(LIB_OUTPUT_DIR)/libhemem-%-$(1).so: $$(MODEL_OBJS_$(1)) models/%.o | $$(LIB_OUTPUT_DIR)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) -shared -fPIC -g $$^ -o $$@ -O3 \
		$$(LIBS) \
		$$(EXTRA_COMPILE_ARGS)

# Build one ARMS library per model object under models with training data enabled
# Example: models/foo.o -> libraries/libhemem-foo_train-true_2_1.0.so
$$(LIB_OUTPUT_DIR)/libhemem-%_$(1)_train.so: $$(TRAIN_OBJS_$(1)) models/%.o | $$(LIB_OUTPUT_DIR)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) -shared -fPIC -g $$^ -o $$@ -O3 \
		$$(LIBS) \
		$$(EXTRA_COMPILE_ARGS)

# Compile C++ sources for USE_MODEL=true variants and config combo
$$(OBJ_DIR)/model/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) -fPIC $$(BASE_DEFINES_model) $$(call combo_defs,$(1)) -c $$< -o $$@

$$(OBJ_DIR)/train/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) -fPIC $$(BASE_DEFINES_train) $$(call combo_defs,$(1)) -c $$< -o $$@
endef

$(foreach combo,$(COMBOS),$(eval $(call MAKE_COMBO_RULES,$(combo))))

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

# Compile C++ sources for USE_MODEL=false variants (no combo specialization)
$(OBJ_DIR)/nomodel/%.o: %.cpp | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC $(BASE_DEFINES_nomodel) -c $< -o $@

$(OBJ_DIR)/logging/%.o: %.cpp | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC $(BASE_DEFINES_logging) -c $< -o $@

$(OBJ_DIR)/arms_train/%.o: %.cpp | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC $(BASE_DEFINES_arms_train) -c $< -o $@

$(LIB_OUTPUT_DIR):
	mkdir -p $(LIB_OUTPUT_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(LIB_OUTPUT_DIR)