-Wno-EOFNEWLINE
-Wno-WIDTHCONCAT
-Wno-DECLFILENAME
-Wno-UNUSEDSIGNAL
-Wno-UNUSEDPARAM
-Wno-PINCONNECTEMPTY
-Wno-LATCH
-Wno-UNOPTFLAT
-Wno-CASEINCOMPLETE
-Wno-INITIALDLY
-Wno-WIDTH
-Wno-CASEOVERLAP
-Wno-SELRANGE
-Wno-VARHIDDEN
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_fpu
-I../v/gbp_pe/compute/include
-I../imports/HardFloat/source
-I../imports/HardFloat/source/RISCV

../basejump_stl/bsg_misc/bsg_defines.sv
../v/bsg_manycore_pkg.sv
../v/gbp_pe/gbp_pkg.sv
../v/gbp_pe/compute_core/gbp_op_pkg.sv

../v/gbp_pe/phase_controller.sv
../v/gbp_pe/node_scheduler.sv
../v/gbp_pe/metadata_scanner.sv
../v/gbp_pe/gbp_pe_control_subsystem.sv

../v/gbp_pe/agu.sv
../v/gbp_pe/read_stream_engine.sv
../v/gbp_pe/write_stream_engine.sv

../v/gbp_pe/compute_core/operand_stream_assembler.sv
../v/gbp_pe/compute_core/operand_stream_dispatcher.sv
../v/gbp_pe/compute_core/op_decoder.sv
../v/gbp_pe/compute_core/operand_window.sv
../v/gbp_pe/compute_core/cavity_builder.sv
../v/gbp_pe/compute_core/rhs_builder_for_message.sv
../v/gbp_pe/compute_core/ldlt_solve_core.sv
../v/gbp_pe/compute_core/schur_update_unit.sv
../v/gbp_pe/compute_core/damping_unit.sv
../v/gbp_pe/compute_core/belief_operand_unpacker.sv
../v/gbp_pe/compute_core/packed_accumulator.sv
../v/gbp_pe/compute_core/belief_solve_adapter.sv
../v/gbp_pe/compute_core/belief_result_builder.sv
../v/gbp_pe/compute_core/writeback_packer.sv
../v/gbp_pe/compute_core/wb_to_wse_adapter.sv
../v/gbp_pe/compute_core/gbp_compute_core.sv
../v/gbp_pe/compute_core/compute_unit_wrapper.sv

../v/gbp_pe/gbp_pe_compute_subsystem.sv

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
