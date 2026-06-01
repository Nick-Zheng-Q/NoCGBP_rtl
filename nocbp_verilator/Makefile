VERILATOR ?= verilator
LEVEL ?= unit
TEST ?= example
TOP ?= $(TEST)
TOP_SV ?=
TB ?= tests/$(LEVEL)/$(TEST).cc
RTL_F ?=
RTL_SV ?=
INCDIRS ?=
DEFINES ?=
TRACE ?= 0
TIMEOUT ?= 1800
VERILATOR_WARNINGS ?= default

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
abs_path = $(if $(filter /%,$(1)),$(1),$(abspath $(ROOT_DIR)/$(1)))
abs_list = $(foreach p,$(1),$(call abs_path,$(p)))

RUN_CONFIG ?=
RUN_CONFIG_MK := $(ROOT_DIR)/build/.run_config.mk
ifneq ($(RUN_CONFIG),)
$(shell mkdir -p $(ROOT_DIR)/build)
$(shell python3 common/run_config_to_make.py $(RUN_CONFIG) > $(RUN_CONFIG_MK))
-include $(RUN_CONFIG_MK)
endif

BUILD_DIR := build/$(LEVEL)/$(TEST)
OBJ_DIR := $(BUILD_DIR)/obj_dir

CONFIG ?= tests/$(LEVEL)/$(TEST).yaml
$(shell python3 common/yaml_to_make.py $(CONFIG) > $(CONFIG).mk)
-include $(CONFIG).mk

EXE := $(OBJ_DIR)/V$(TOP)
RUN_ENV = LEVEL='$(LEVEL)' TEST='$(TEST)' TOP='$(TOP)' RUN_CONFIG='$(RUN_CONFIG)' RUN_CONFIG_ABS='$(RUN_CONFIG_ABS)' PE_COUNT='$(PE_COUNT)' MESH_X='$(MESH_X)' MESH_Y='$(MESH_Y)' WORKLOAD='$(WORKLOAD)' DATASET='$(DATASET)' SEED='$(SEED)' PARTITION='$(PARTITION)'

# Verilator warning configuration
# Supported levels: strict, default, minimal, none
ifeq ($(VERILATOR_WARNINGS), strict)
VFLAGS += --cc --exe --build -O2 -Wall -Werror
else ifeq ($(VERILATOR_WARNINGS), minimal)
VFLAGS += --cc --exe --build -O2 -Wall -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-PINCONNECTEMPTY -Wno-DECLFILENAME -Wno-UNOPTFLAT -Wno-WIDTHCONCAT -Wno-EOFNEWLINE
else ifeq ($(VERILATOR_WARNINGS), none)
VFLAGS += --cc --exe --build -O2 -Wno-fatal
else
# Default configuration - suppress common warnings but keep others
VFLAGS += --cc --exe --build -O2 -Wall -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-PINCONNECTEMPTY -Wno-DECLFILENAME -Wno-UNOPTFLAT
endif
VFLAGS += --top-module $(TOP)
VFLAGS += --Mdir $(OBJ_DIR)
VFLAGS += $(if $(RTL_F),-f $(call abs_path,$(RTL_F)),)
VFLAGS += $(call abs_list,$(RTL_SV))
VFLAGS += $(if $(TOP_SV),$(call abs_path,$(TOP_SV)),)
VFLAGS += $(call abs_path,$(TB))
VFLAGS += $(call abs_path,common/sim_main.cc)
VFLAGS += $(addprefix -I, $(call abs_list,$(INCDIRS)))
VFLAGS += $(foreach d,$(call abs_list,$(INCDIRS)),-CFLAGS -I$(d))
VFLAGS += $(addprefix +define+, $(DEFINES))

ifeq ($(TRACE),1)
VFLAGS += --trace
endif

.PHONY: all build run clean show show-config list-tests clean-all

all: build

build:
	mkdir -p $(OBJ_DIR)
	python3 common/update_clangd_includes.py .clangd $(abspath $(OBJ_DIR))
	$(VERILATOR) $(VFLAGS)

run: build
	@if command -v timeout >/dev/null 2>&1; then \
		timeout $(TIMEOUT) env $(RUN_ENV) $(EXE); \
	else \
		env $(RUN_ENV) $(EXE); \
	fi

show:
	@echo LEVEL=$(LEVEL)
	@echo TEST=$(TEST)
	@echo TOP=$(TOP)
	@echo TOP_SV=$(TOP_SV)
	@echo TB=$(TB)
	@echo RTL_F=$(RTL_F)
	@echo RTL_SV=$(RTL_SV)

show-config:
	@echo "=== Verilator Test Configuration ==="
	@echo "VERILATOR=$(VERILATOR)"
	@echo "LEVEL=$(LEVEL)"
	@echo "TEST=$(TEST)"
	@echo "TOP=$(TOP)"
	@echo "TRACE=$(TRACE)"
	@echo "TIMEOUT=$(TIMEOUT)"
	@echo "VERILATOR_WARNINGS=$(VERILATOR_WARNINGS)"
	@echo "VFLAGS=$(VFLAGS)"
	@echo ""
	@echo "=== Available Warning Levels ==="
	@echo "  strict  - All warnings as errors"
	@echo "  default - Suppress common warnings (default)"
	@echo "  minimal - Suppress more warnings including WIDTHCONCAT, EOFNEWLINE"
	@echo "  none    - Disable all warnings"

clean:
	rm -rf build/$(LEVEL)/$(TEST)

clean-all:
	rm -rf build/
	rm -f tests/**/*.yaml.mk
	rm -f .clangd

list-tests:
	@echo "=== Unit Tests ==="
	@find tests/unit -name "*.cc" -type f 2>/dev/null | sed 's|tests/unit/||;s|\.cc$$||' | sort
	@echo ""
	@echo "=== Integration Tests ==="
	@find tests/integration -name "*.cc" -type f 2>/dev/null | sed 's|tests/integration/||;s|\.cc$$||' | sort
	@echo ""
	@echo "=== System Tests ==="
	@find tests/system -name "*.cc" -type f 2>/dev/null | sed 's|tests/system/||;s|\.cc$$||' | sort || echo "(no tests yet)"
