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

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
abs_path = $(if $(filter /%,$(1)),$(1),$(abspath $(ROOT_DIR)/$(1)))
abs_list = $(foreach p,$(1),$(call abs_path,$(p)))

BUILD_DIR := build/$(LEVEL)/$(TEST)
OBJ_DIR := $(BUILD_DIR)/obj_dir
EXE := $(OBJ_DIR)/V$(TOP)

CONFIG ?= tests/$(LEVEL)/$(TEST).yaml
$(shell python3 common/yaml_to_make.py $(CONFIG) > $(CONFIG).mk)
-include $(CONFIG).mk

VFLAGS += --cc --exe --build -O2 -Wall -Wno-UNDRIVEN -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND -Wno-PINCONNECTEMPTY -Wno-DECLFILENAME -Wno-UNOPTFLAT
VFLAGS += --top-module $(TOP)
VFLAGS += --Mdir $(OBJ_DIR)
VFLAGS += $(if $(RTL_F),-f $(call abs_path,$(RTL_F)),)
VFLAGS += $(call abs_list,$(RTL_SV))
VFLAGS += $(if $(TOP_SV),$(call abs_path,$(TOP_SV)),)
VFLAGS += $(call abs_path,$(TB))
VFLAGS += $(call abs_path,common/sim_main.cc)
VFLAGS += $(addprefix -I, $(call abs_list,$(INCDIRS)))
VFLAGS += $(addprefix +define+, $(DEFINES))

ifeq ($(TRACE),1)
VFLAGS += --trace
endif

.PHONY: all build run clean show

all: build

build:
	mkdir -p $(OBJ_DIR)
	python3 common/update_clangd_includes.py .clangd $(abspath $(OBJ_DIR))
	$(VERILATOR) $(VFLAGS)

run: build
	$(EXE)

show:
	@echo LEVEL=$(LEVEL)
	@echo TEST=$(TEST)
	@echo TOP=$(TOP)
	@echo TOP_SV=$(TOP_SV)
	@echo TB=$(TB)
	@echo RTL_F=$(RTL_F)
	@echo RTL_SV=$(RTL_SV)

clean:
	rm -rf build/$(LEVEL)/$(TEST)
