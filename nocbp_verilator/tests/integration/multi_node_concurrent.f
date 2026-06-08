-Wno-fatal
-I../basejump_stl/bsg_misc
-I../basejump_stl/bsg_dataflow
-I../basejump_stl/bsg_noc
-I../basejump_stl/bsg_mem
-I../v
-I../v/gbp_pe

../v/bsg_manycore_pkg.sv
../v/gbp_pe/gbp_pkg.sv

../basejump_stl/bsg_misc/bsg_defines.sv
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

../v/gbp_pe/scoreboard_prefetcher.sv
../v/gbp_pe/pull_client.sv
../v/gbp_pe/response_collector.sv
