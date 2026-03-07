RTL_F := tests/unit/gbp_pe_fpu.f
RTL_SV := ../v/gbp_pe/gbp_pkg.sv ../v/gbp_pe/interfaces.sv ../v/gbp_pe/compute_unit.sv
INCDIRS := ../v ../v/gbp_pe ../basejump_stl/bsg_misc ../basejump_stl/bsg_fpu ../imports/HardFloat/source ../imports/HardFloat/source/RISCV
TOP := compute_unit_top
TOP_SV := tops/unit/compute_unit_top.sv