# Makefile for GBP compute engine

VERILATOR = verilator
VERILATOR_FLAGS = --lint-only -Wall -Wno-DECLFILENAME -Wno-UNUSED

RTL_DIR = .
IMPORTS_DIR = ../../../imports
BSG_STL_DIR = ../../../basejump_stl

# Source files
SV_SOURCES = \
	$(RTL_DIR)/staging_buffer.sv \
	$(RTL_DIR)/simd_array.sv \
	$(RTL_DIR)/matrix_fsm.sv \
	$(RTL_DIR)/gbp_control_fsm.sv \
	$(RTL_DIR)/gbp_compute_engine.sv \
	$(RTL_DIR)/matrix_alu/mat_inv_gauss_jordan.sv

INCLUDE_FLAGS = \
	-I$(RTL_DIR)/include \
	-I$(BSG_STL_DIR)/bsg_misc \
	-I$(BSG_STL_DIR)/bsg_fpu \
	-I$(IMPORTS_DIR)/HardFloat/source \
	-I$(IMPORTS_DIR)/HardFloat/source/RISCV

.PHONY: all lint clean

all: lint

lint: $(SV_SOURCES)
	@echo "Running Verilator lint check..."
	$(VERILATOR) $(VERILATOR_FLAGS) $(INCLUDE_FLAGS) $(SV_SOURCES) 2>&1 | head -50

clean:
	@echo "Cleaning up..."
	rm -rf obj_dir

help:
	@echo "Available targets:"
	@echo "  lint   - Run Verilator lint check on all RTL files"
	@echo "  clean  - Clean up generated files"
	@echo "  help   - Show this help message"
