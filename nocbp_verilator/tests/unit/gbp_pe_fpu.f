-Wno-fatal
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_fpu
-I../imports/HardFloat/source
-I../imports/HardFloat/source/RISCV

../v/bsg_manycore_pkg.sv
../v/pe/gbp_pe.sv

../basejump_stl/bsg_fpu/bsg_fpu_add_sub.sv
../basejump_stl/bsg_fpu/bsg_fpu_mul.sv
../basejump_stl/bsg_fpu/bsg_fpu_preprocess.sv
../basejump_stl/bsg_fpu/bsg_fpu_sticky.sv
../basejump_stl/bsg_fpu/bsg_fpu_clz.sv

../basejump_stl/bsg_misc/bsg_less_than.sv
../basejump_stl/bsg_misc/bsg_reduce.sv
../basejump_stl/bsg_misc/bsg_mul_synth.sv
../basejump_stl/bsg_misc/bsg_priority_encode.sv
../basejump_stl/bsg_misc/bsg_priority_encode_one_hot_out.sv
../basejump_stl/bsg_misc/bsg_encode_one_hot.sv

../imports/HardFloat/source/HardFloat_primitives.v
../imports/HardFloat/source/HardFloat_rawFN.v
../imports/HardFloat/source/isSigNaNRecFN.v
../imports/HardFloat/source/fNToRecFN.v
../imports/HardFloat/source/recFNToFN.v
../imports/HardFloat/source/divSqrtRecFN_small.v
../imports/HardFloat/source/divSqrtRecFN_medium.v
../imports/HardFloat/source/divSqrtRecFN.v
../imports/HardFloat/source/divSqrtFN.v
