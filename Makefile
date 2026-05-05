# Makefile for ARMS Kernel-based Memory Tiering System

CXX = g++
CC = gcc

# Compiler flags
BASE_CXXFLAGS := -std=c++17 -Wall -Wextra -pthread

ifeq ($(DEBUG),1)
CXXFLAGS ?= $(BASE_CXXFLAGS) -O0 -g
else
CXXFLAGS ?= $(BASE_CXXFLAGS) -O3 -DNDEBUG
endif

# Include paths
INCLUDES = -I.

# Libraries
LIBS = -lnuma -lpthread -ldl

# Build directories for reusable objects
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
PLATFORMS := C220G5
DEFAULT_PLATFORM ?= C220G5

# Compile-time configuration matrix
MODEL_SCORE_HISTORY_SUMMARY_VALUES := 2
# 0=min_max, 1=average, 2=adjusted_moving_average
HISTORY_LENGTH_VALUES := 10
SWITCH_SCALER_VALUES := 0.0625 0.125 0.25 0.5 1.0 1.5
COMBOS := $(foreach summary,$(MODEL_SCORE_HISTORY_SUMMARY_VALUES),$(foreach hlen,$(HISTORY_LENGTH_VALUES),$(foreach scaler,$(SWITCH_SCALER_VALUES),$(summary)_$(hlen)_$(scaler))))

# Models and outputs
MODELS := $(wildcard models/*.o)
# Logging path model object (override with LOGGING_MODEL_OBJ if needed)
LOGGING_MODEL_OBJ ?= $(firstword $(MODELS))
LIB_OUTPUT_DIR := libraries
PLATFORM_LIB_DIRS := $(addprefix $(LIB_OUTPUT_DIR)/,$(PLATFORMS))
LIB_TARGETS := $(foreach platform,$(PLATFORMS),$(foreach combo,$(COMBOS),$(patsubst models/%.o,$(LIB_OUTPUT_DIR)/$(platform)/libhemem-%-$(combo).so,$(MODELS))))
TRAIN_LIB_TARGETS := $(foreach platform,$(PLATFORMS),$(foreach combo,$(COMBOS),$(patsubst models/%.o,$(LIB_OUTPUT_DIR)/$(platform)/libhemem-%_$(combo)_train.so,$(MODELS))))
ARMS_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms.so)
ARMS_NOMIGRATION_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms_nomigrations.so)
ARMS_PLAIN_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms_plain.so)
LOGGING_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-logging.so)
ARMS_TRAIN_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms_train.so)
ARMS_NEAR_TRAIN_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms_near_train.so)
ARMS_CXL_TRAIN_TARGETS := $(foreach platform,$(PLATFORMS),$(LIB_OUTPUT_DIR)/$(platform)/libhemem-arms_cxl_train.so)
ARMS_TARGET_DEFAULT := $(LIB_OUTPUT_DIR)/$(DEFAULT_PLATFORM)/libhemem-arms.so

# Target
TARGET_LIB = libarms_kernel.so

# Source files
SRCS = arms_kernel.cpp \
	pebs_scan_thread.cpp \
	pagemap_scan_thread.cpp \
	migration_worker.cpp \
	policy_thread.cpp \
	timer.cpp hook/hook.cpp groups.cpp page.cpp logging.cpp model.cpp
OBJ_NAMES = $(SRCS:.cpp=.o)

BASE_DEFINES_model := -DUSE_MODEL=true
BASE_DEFINES_train := -DUSE_MODEL=true -DPRINT_TRAINING_DATA=true
BASE_DEFINES_nomodel := -DUSE_MODEL=false
BASE_DEFINES_nomodel_nomigrations := -DUSE_MODEL=false -DMIGRATION_WORKERS_ENABLED=false
BASE_DEFINES_logging := -DUSE_MODEL=true -DPRINT_TRAINING_DATA=true -DLOGGING_RUN=true
BASE_DEFINES_arms_plain := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=false -DLOGGING_RUN=false -DNEAR_MEM_TRACING_RUN=true
BASE_DEFINES_arms_train := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true
BASE_DEFINES_arms_near_train := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -DNEAR_MEM_TRACING_RUN=true
BASE_DEFINES_arms_cxl_train := -DUSE_MODEL=false -DPRINT_TRAINING_DATA=true -DNEAR_MEM_TRACING_RUN=true -DFORCE_FAR_MEMORY_DEFAULT=true -DENABLE_MIGRATION_WORKERS=false

combo_summary = $(word 1,$(subst _, ,$1))
combo_hlen = $(word 2,$(subst _, ,$1))
combo_scaler = $(word 3,$(subst _, ,$1))
combo_defs = -DMODEL_SCORE_HISTORY_SUMMARY=$(call combo_summary,$1) -DHISTORY_LENGTH=$(call combo_hlen,$1) -DSWITCH_SCALER=$(call combo_scaler,$1)
platform_defs = -D$(1)

model_name_from_obj = $(patsubst %.o,%,$(notdir $1))
model_discount_suffix = $(patsubst model_discounted_reward_%,%,$(call model_name_from_obj,$1))
model_discount_percent = $(if $(filter model_discounted_reward_%,$(call model_name_from_obj,$1)),$(firstword $(subst _, ,$(call model_discount_suffix,$1))),0)
model_discount_define = -DMODEL_DISCOUNT_PERCENT=$(call model_discount_percent,$1)
variant_model_objects = $(foreach model,$(MODELS),$(addprefix $(OBJ_DIR)/$(1)/$(2)/$(3)/$(call model_name_from_obj,$(model))/,$(OBJ_NAMES)))

LOGGING_MODEL_NAME := $(call model_name_from_obj,$(LOGGING_MODEL_OBJ))
LOGGING_MODEL_DISCOUNT_PERCENT := $(call model_discount_percent,$(LOGGING_MODEL_NAME))
LOGGING_MAX_LOGGED_SAMPLES ?= 100000000

define LINK_SHARED_RECIPE
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -fPIC -g $^ -o $@ -O3 \
		$(LIBS) \
		$(EXTRA_COMPILE_ARGS)
endef

define COMPILE_OBJECT_RECIPE
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fPIC $(1) -c $< -o $@
endef

# System detection
UNAME_M := $(shell uname -m)
HOSTNAME := $(shell hostname)

.PHONY: all clean

all: $(LIB_TARGETS) $(TRAIN_LIB_TARGETS) $(ARMS_TARGETS) $(ARMS_NOMIGRATION_TARGETS) $(ARMS_PLAIN_TARGETS) $(LOGGING_TARGETS) $(ARMS_TRAIN_TARGETS) $(ARMS_NEAR_TRAIN_TARGETS) $(ARMS_CXL_TRAIN_TARGETS)

$(TARGET_LIB): $(ARMS_TARGET_DEFAULT) | $(LIB_OUTPUT_DIR)
	cp -f $< $@

define MAKE_MODEL_SOURCE_RULES
$$(OBJ_DIR)/model/$(1)/$(2)/%/$(patsubst %.cpp,%.o,$(3)): $(3) | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_model) $$(call combo_defs,$(2)) $$(call platform_defs,$(1)) $$(call model_discount_define,$$*))

$$(OBJ_DIR)/train/$(1)/$(2)/%/$(patsubst %.cpp,%.o,$(3)): $(3) | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_train) $$(call combo_defs,$(2)) $$(call platform_defs,$(1)) $$(call model_discount_define,$$*))
endef

define MAKE_PLATFORM_COMBO_RULES
MODEL_OBJS_$(1)_$(2) := $$(addprefix $$(OBJ_DIR)/model/$(1)/$(2)/%/,$$(OBJ_NAMES))
TRAIN_OBJS_$(1)_$(2) := $$(addprefix $$(OBJ_DIR)/train/$(1)/$(2)/%/,$$(OBJ_NAMES))

.NOTINTERMEDIATE: $$(call variant_model_objects,model,$(1),$(2)) $$(call variant_model_objects,train,$(1),$(2))
.PRECIOUS: $$(call variant_model_objects,model,$(1),$(2)) $$(call variant_model_objects,train,$(1),$(2))

# Build one ARMS library per model object under models/ and config combo
# Example: models/foo.o -> libraries/libhemem-foo-2_10_1.0.so
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-%-$(2).so: $$(MODEL_OBJS_$(1)_$(2)) models/%.o | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build one ARMS library per model object under models with training data enabled
# Example: models/foo.o -> libraries/libhemem-foo_2_10_1.0_train.so
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-%_$(2)_train.so: $$(TRAIN_OBJS_$(1)_$(2)) models/%.o | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

endef

$(foreach platform,$(PLATFORMS),$(foreach combo,$(COMBOS),$(eval $(call MAKE_PLATFORM_COMBO_RULES,$(platform),$(combo)))))
$(foreach platform,$(PLATFORMS),$(foreach combo,$(COMBOS),$(foreach src,$(SRCS),$(eval $(call MAKE_MODEL_SOURCE_RULES,$(platform),$(combo),$(src))))))

define MAKE_PLATFORM_BASE_RULES
NOMODEL_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/nomodel/$(1)/,$$(OBJ_NAMES))
NOMODEL_NOMIGRATION_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/nomodel_nomigrations/$(1)/,$$(OBJ_NAMES))
ARMS_PLAIN_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/arms_plain/$(1)/,$$(OBJ_NAMES))
LOGGING_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/logging/$(1)/,$$(OBJ_NAMES))
ARMS_TRAIN_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/arms_train/$(1)/,$$(OBJ_NAMES))
ARMS_NEAR_TRAIN_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/arms_near_train/$(1)/,$$(OBJ_NAMES))
ARMS_CXL_TRAIN_OBJS_$(1) := $$(addprefix $$(OBJ_DIR)/arms_cxl_train/$(1)/,$$(OBJ_NAMES))

# Build without linking a model; force USE_MODEL=false
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms.so: $$(NOMODEL_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build without linking a model or starting migration workers.
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms_nomigrations.so: $$(NOMODEL_NOMIGRATION_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)

# Build explicit non-logging, non-training ARMS variant
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms_plain.so: $$(ARMS_PLAIN_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build logging path with model inference enabled and a user-provided model object
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-logging.so: $$(LOGGING_OBJS_$(1)) $$(LOGGING_MODEL_OBJ) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build ARMS with training data logging enabled (no model linked)
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms_train.so: $$(ARMS_TRAIN_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build ARMS with training data logging + near-memory default + 250ms interval (no model linked)
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms_near_train.so: $$(ARMS_NEAR_TRAIN_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Build ARMS with the same non-virtual timestep path as near-train, but keep far-memory default and disable migration workers.
$$(LIB_OUTPUT_DIR)/$(1)/libhemem-arms_cxl_train.so: $$(ARMS_CXL_TRAIN_OBJS_$(1)) | $$(LIB_OUTPUT_DIR)/$(1)
	$$(LINK_SHARED_RECIPE)

# Compile C++ sources for USE_MODEL=false variants (platform specialization)
$$(OBJ_DIR)/nomodel/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_nomodel) $$(call platform_defs,$(1)))

$$(OBJ_DIR)/nomodel_nomigrations/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_nomodel_nomigrations) $$(call platform_defs,$(1)))

$$(OBJ_DIR)/arms_plain/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_arms_plain) $$(call platform_defs,$(1)))

$$(OBJ_DIR)/logging/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_logging) $$(call platform_defs,$(1)) -DMODEL_DISCOUNT_PERCENT=$$(LOGGING_MODEL_DISCOUNT_PERCENT) -DMAX_LOGGED_SAMPLES=$$(LOGGING_MAX_LOGGED_SAMPLES))

$$(OBJ_DIR)/arms_train/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_arms_train) $$(call platform_defs,$(1)))

$$(OBJ_DIR)/arms_near_train/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_arms_near_train) $$(call platform_defs,$(1)))

$$(OBJ_DIR)/arms_cxl_train/$(1)/%.o: %.cpp | $$(OBJ_DIR)
	$$(call COMPILE_OBJECT_RECIPE,$$(BASE_DEFINES_arms_cxl_train) $$(call platform_defs,$(1)))
endef

$(foreach platform,$(PLATFORMS),$(eval $(call MAKE_PLATFORM_BASE_RULES,$(platform))))

$(LIB_OUTPUT_DIR):
	mkdir -p $(LIB_OUTPUT_DIR)

$(PLATFORM_LIB_DIRS):
	mkdir -p $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(LIB_OUTPUT_DIR)