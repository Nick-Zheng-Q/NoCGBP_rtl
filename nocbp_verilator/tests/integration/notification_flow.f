-Wno-fatal
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_dataflow
-I../basejump_stl/bsg_noc
-I../basejump_stl/bsg_mem
-I../v
-I../v/gbp_pe

../v/bsg_manycore_pkg.sv
../v/gbp_pe/gbp_pkg.sv

../v/bsg_manycore_reg_id_decode.sv
../v/bsg_manycore_endpoint.sv
../v/bsg_manycore_endpoint_fc.sv
../v/bsg_manycore_endpoint_standard.sv

../basejump_stl/bsg_misc/bsg_defines.sv
../basejump_stl/bsg_misc/bsg_counter_up_down.sv
../basejump_stl/bsg_misc/bsg_dff_reset_en.sv
../basejump_stl/bsg_misc/bsg_dff_en.sv
../basejump_stl/bsg_misc/bsg_dff_reset.sv
../basejump_stl/bsg_misc/bsg_dff.sv
../basejump_stl/bsg_misc/bsg_circular_ptr.sv

../basejump_stl/bsg_dataflow/bsg_fifo_tracker.sv
../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_small.sv
../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_small_unhardened.sv
../basejump_stl/bsg_dataflow/bsg_two_fifo.sv

../basejump_stl/bsg_mem/bsg_mem_1r1w.sv
../basejump_stl/bsg_mem/bsg_mem_1r1w_synth.sv

../v/gbp_pe/noc_adapter.sv
../v/gbp_pe/noc_adapter_tx.sv
../v/gbp_pe/noc_adapter_rx.sv
../v/gbp_pe/writeback_controller.sv
../v/gbp_pe/scoreboard_prefetcher.sv
