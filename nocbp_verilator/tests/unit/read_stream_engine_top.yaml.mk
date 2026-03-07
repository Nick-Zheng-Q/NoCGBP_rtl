RTL_SV := ../basejump_stl/bsg_misc/bsg_defines.sv ../basejump_stl/bsg_dataflow/bsg_fifo_1r1w_large.sv ../v/bsg_manycore_pkg.sv ../v/gbp_pe/gbp_pkg.sv ../v/gbp_pe/interfaces.sv ../v/gbp_pe/agu.sv ../v/gbp_pe/addr_fifo.sv ../v/gbp_pe/data_fifo.sv ../v/gbp_pe/mic_read.sv ../v/gbp_pe/read_stream_engine.sv
INCDIRS := ../v ../v/gbp_pe ../basejump_stl ../basejump_stl/bsg_dataflow ../basejump_stl/bsg_misc ../basejump_stl/bsg_mem
TOP := read_stream_engine_top
TOP_SV := tops/unit/read_stream_engine_top.sv