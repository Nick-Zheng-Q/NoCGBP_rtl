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
-I../v
-I../v/gbp_pe
-I../v/gbp_pe/compute/include
-I../v/pe
-I../basejump_stl
-I../basejump_stl/bsg_dataflow
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_mem
-I../basejump_stl/bsg_noc
-I../basejump_stl/bsg_fpu
-I../imports/HardFloat/source
-I../imports/HardFloat/source/RISCV

../basejump_stl/bsg_misc/bsg_defines.sv
../basejump_stl/bsg_noc/bsg_noc_pkg.sv
../basejump_stl/bsg_noc/bsg_mesh_router_pkg.sv
../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_large.sv
../basejump_stl/bsg_mem/bsg_mem_1rw_sync.sv
../basejump_stl/bsg_mem/bsg_mem_1rw_sync_synth.sv

../v/bsg_manycore_pkg.sv
../v/bsg_manycore_reg_id_decode.sv
../v/bsg_manycore_endpoint.sv
../v/bsg_manycore_endpoint_fc.sv
../v/bsg_manycore_endpoint_standard.sv
../v/bsg_manycore_eva_to_npa.sv
../v/gbp_pe/gbp_pkg.sv

../v/gbp_pe/noc_adapter_rx.sv
../v/gbp_pe/noc_adapter_tx.sv
../v/gbp_pe/noc_adapter.sv

../v/gbp_pe/phase_controller.sv
../v/gbp_pe/node_scheduler.sv
../v/gbp_pe/metadata_scanner.sv
../v/gbp_pe/scoreboard_prefetcher.sv
../v/gbp_pe/pull_client.sv
../v/gbp_pe/response_collector.sv
../v/gbp_pe/writeback_controller.sv
../v/gbp_pe/neighbor_state_accumulator.sv

../v/gbp_pe/agu.sv
../v/gbp_pe/read_stream_engine.sv
../v/gbp_pe/write_stream_engine.sv
../v/gbp_pe/compute_unit.sv
../v/gbp_pe/compute/gbp_compute_engine.sv
../v/gbp_pe/compute/gbp_control_fsm.sv
../v/gbp_pe/compute/matrix_fsm.sv
../v/gbp_pe/compute/simd_array.sv
../v/gbp_pe/compute/staging_buffer.sv
../v/gbp_pe/compute/matrix_alu/mat_inv_gauss_jordan.sv

../v/gbp_pe/spm_arbiter.sv
../v/gbp_pe/spm_bank.sv

../v/gbp_pe/gbp_pe_control_subsystem.sv
../v/gbp_pe/gbp_pe_compute_subsystem.sv
../v/gbp_pe/gbp_pe_memory_subsystem.sv
../v/gbp_pe/gbp_pe_fetch_subsystem.sv
../v/gbp_pe/gbp_pe.sv

../basejump_stl/bsg_misc/bsg_counter_up_down.sv
../basejump_stl/bsg_misc/bsg_dff_reset_en.sv
../basejump_stl/bsg_misc/bsg_dff_en.sv
../basejump_stl/bsg_misc/bsg_dff_reset.sv
../basejump_stl/bsg_misc/bsg_dff.sv
../basejump_stl/bsg_misc/bsg_decode_with_v.sv
../basejump_stl/bsg_misc/bsg_buf.sv

../basejump_stl/bsg_dataflow/bsg_fifo_tracker.sv
../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_small.sv
../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_small_unhardened.sv
../basejump_stl/bsg_dataflow/bsg_two_fifo.sv

../basejump_stl/bsg_mem/bsg_mem_1r1w.sv
../basejump_stl/bsg_mem/bsg_mem_1r1w_synth.sv

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
