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
VFLAGS += --cc --exe --build -O2 -Wall -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-PINCONNECTEMPTY -Wno-DECLFILENAME -Wno-UNOPTFLAT -Wno-IMPORTSTAR -Wno-WIDTHCONCAT -Wno-EOFNEWLINE -Wno-LATCH -Wno-VARHIDDEN
else ifeq ($(VERILATOR_WARNINGS), none)
VFLAGS += --cc --exe --build -O2 -Wno-fatal -Wno-lint
else
# Default configuration - suppress common warnings but keep others
VFLAGS += --cc --exe --build -O2 -Wall -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-PINCONNECTEMPTY -Wno-DECLFILENAME -Wno-UNOPTFLAT -Wno-IMPORTSTAR
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

.PHONY: all build lint run clean show show-config list-tests clean-all

all: build

# Extract RTL source files from .f file (exclude flags/lines starting with -)
RTL_F_SRCS := $(if $(RTL_F),$(shell sed '/^-/d;/^$$/d' $(call abs_path,$(RTL_F))),)
RTL_ALL_SRCS := $(call abs_list,$(RTL_F_SRCS)) $(call abs_list,$(RTL_SV)) $(if $(TOP_SV),$(call abs_path,$(TOP_SV)),)

$(EXE): $(RTL_ALL_SRCS) $(call abs_path,$(TB)) $(call abs_path,common/sim_main.cc)
	mkdir -p $(OBJ_DIR)
	python3 common/update_clangd_includes.py .clangd $(abspath $(OBJ_DIR))
	$(VERILATOR) $(VFLAGS)

build: $(EXE)

# Syntax-only lint (no C++ compilation). Reuses the same file list / include dirs
# as a normal build but skips the testbench and sim_main.cc.
# Example: make lint LEVEL=integration TEST=new_pe_integration
# Example: make lint TOP=pe_top RTL_F=tests/integration/new_pe_integration.f
lint:
	mkdir -p $(OBJ_DIR)
	$(VERILATOR) --lint-only --top-module $(TOP) \
		$(if $(RTL_F),-f $(call abs_path,$(RTL_F)),) \
		$(call abs_list,$(RTL_SV)) \
		$(if $(TOP_SV),$(call abs_path,$(TOP_SV)),) \
		$(addprefix -I, $(call abs_list,$(INCDIRS))) \
		$(addprefix +define+, $(DEFINES)) \
		--Mdir $(OBJ_DIR)

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

# ── Regression suite: all verified tests ──
REGRESSION_UNIT_TESTS := agu compute_unit gbp_pe gbp_pe_compute_subsystem gbp_pe_control_subsystem gbp_pe_fetch_subsystem gbp_pe_memory_subsystem metadata_scanner neighbor_state_accumulator noc_adapter node_scheduler phase_controller pull_client pull_server read_stream_engine_top response_collector scoreboard_prefetcher spm_arbiter spm_bank spm_bank_array writeback_controller write_stream_engine_top
REGRESSION_INTEG_TESTS := control_subsystem fetch_request_flow fetch_response_flow fetch_subsystem full_pull_cycle multi_node_concurrent notification_flow phase_scheduling

.PHONY: regression
regression:
	@echo "========================================"
	@echo "  GBP PE Regression Suite"
	@echo "========================================"
	@PASS=0; FAIL=0; \
UNIT_LIST="$(REGRESSION_UNIT_TESTS)"; \
for t in $$UNIT_LIST; do \
	printf "  UNIT  %-40s " "$$t ..."; \
	if $(MAKE) LEVEL=unit TEST=$$t VERILATOR_WARNINGS=none run >/tmp/reg_$$t.log 2>&1; then \
		echo "PASS"; PASS=$$((PASS+1)); \
	else \
		echo "FAIL"; FAIL=$$((FAIL+1)); \
		tail -5 /tmp/reg_$$t.log | sed 's/^/    /'; \
	fi; \
done; \
INTEG_LIST="$(REGRESSION_INTEG_TESTS)"; \
for t in $$INTEG_LIST; do \
	printf "  INTEG %-40s " "$$t ..."; \
	if $(MAKE) LEVEL=integration TEST=$$t VERILATOR_WARNINGS=none run >/tmp/reg_$$t.log 2>&1; then \
		echo "PASS"; PASS=$$((PASS+1)); \
	else \
		echo "FAIL"; FAIL=$$((FAIL+1)); \
		tail -5 /tmp/reg_$$t.log | sed 's/^/    /'; \
	fi; \
done; \
echo "========================================"; \
echo "  PASSED: $$PASS  FAILED: $$FAIL"; \
echo "========================================"; \
if [ $$FAIL -gt 0 ]; then exit 1; fi

list-tests:
	@echo "=== Unit Tests ==="
	@find tests/unit -name "*.cc" -type f 2>/dev/null | sed 's|tests/unit/||;s|\.cc$$||' | sort
	@echo ""
	@echo "=== Integration Tests ==="
	@find tests/integration -name "*.cc" -type f 2>/dev/null | sed 's|tests/integration/||;s|\.cc$$||' | sort
	@echo ""
	@echo "=== System Tests ==="
	@find tests/system -name "*.cc" -type f 2>/dev/null | sed 's|tests/system/||;s|\.cc$$||' | sort || echo "(no tests yet)"
