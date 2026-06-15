-Wno-fatal
-Wno-WIDTH
-Wno-LATCH
-Wno-UNOPTFLAT
-I../v/gbp_pe
-I../v/gbp_pe/compute_core
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_fpu
-I../imports/HardFloat/source
-I../imports/HardFloat/source/RISCV

../v/gbp_pe/gbp_pkg.sv
../v/gbp_pe/compute_core/gbp_op_pkg.sv

../v/gbp_pe/compute_core/damping_unit.sv

../basejump_stl/bsg_fpu/bsg_fpu_add_sub.sv
../basejump_stl/bsg_fpu/bsg_fpu_mul.sv
../basejump_stl/bsg_fpu/bsg_fpu_preprocess.sv
../basejump_stl/bsg_fpu/bsg_fpu_sticky.sv
../basejump_stl/bsg_fpu/bsg_fpu_clz.sv

../imports/HardFloat/source/HardFloat_primitives.v
../imports/HardFloat/source/HardFloat_rawFN.v
../imports/HardFloat/source/isSigNaNRecFN.v
../imports/HardFloat/source/fNToRecFN.v
../imports/HardFloat/source/recFNToFN.v
../imports/HardFloat/source/divSqrtRecFN_small.v
../imports/HardFloat/source/divSqrtRecFN_medium.v
../imports/HardFloat/source/divSqrtRecFN.v
../imports/HardFloat/source/divSqrtFN.v
