RTL_F := tests/integration/gbp_pe_mesh_whitebox_convergence.f
INCDIRS := ../nocbp_simulator ../nocbp_simulator/gbp ../nocbp_simulator/utils
DEFINES := GBP_WHITEBOX_TEST GBP_PE_COUNT=$(PE_COUNT) GBP_MESH_X=$(MESH_X) GBP_MESH_Y=$(MESH_Y)
TOP := $(if $(RUN_CONFIG),gbp_pe_mesh_whitebox_convergence,$(error gbp_pe_mesh_whitebox_convergence 必须使用 RUN_CONFIG=<yaml> 启动；无迁移，直接替换))
TOP_SV := tops/integration/gbp_pe_mesh_whitebox.sv
TB := tests/integration/gbp_pe_mesh_whitebox_convergence.cc