`include "bsg_manycore_defines.svh"

`ifndef GBP_PE_COUNT
`define GBP_PE_COUNT 1
`endif

`ifndef GBP_MESH_X
`define GBP_MESH_X 1
`endif

`ifndef GBP_MESH_Y
`define GBP_MESH_Y 1
`endif

module gbp_pe_mesh_whitebox_convergence
  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
  import gbp_pkg::*;
(
  input logic clk,
  input logic rst_n,

  input logic cmd_valid_i,
  input logic [1:0] cmd_kind_i,
  input logic [TXN_ID_W-1:0] cmd_txn_i,
  input logic [((`GBP_PE_COUNT) <= 1 ? 1 : $clog2(`GBP_PE_COUNT))-1:0] cmd_pe_i,
  output logic cmd_ready_o,

  input logic [((`GBP_PE_COUNT) <= 1 ? 1 : $clog2(`GBP_PE_COUNT))-1:0] observe_pe_i,
  output logic observe_wr_req_valid_o,
  output logic [SPM_ADDR_W-1:0] observe_wr_req_addr_o,
  output logic [31:0] observe_wr_req_data_o,
  output logic observe_ingress_wr_req_valid_o,
  output logic [SPM_ADDR_W-1:0] observe_ingress_wr_req_addr_o,
  output logic [31:0] observe_ingress_wr_req_data_o,
  output logic observe_bank0_wr_src0_valid_o,
  output logic [ROW_ADDR_W-1:0] observe_bank0_wr_src0_row_o,
  output logic observe_bank0_wr_src1_valid_o,
  output logic [ROW_ADDR_W-1:0] observe_bank0_wr_src1_row_o,
  output logic observe_compute_done_o,
  output logic observe_compute_start_o,
  output logic [2:0] observe_epoch_o,
  output logic observe_doorbell_o,
  output logic [7:0] observe_q_head_o,
  output logic [7:0] observe_q_tail_o,
  output logic [7:0] observe_q_credit_o,
  output logic [ROW_ADDR_W-1:0] observe_meta_row_o,
  output logic observe_scan_done_o,
  output logic [4:0] observe_ctrl_state_o,
  output logic observe_ctrl_phase_o,
  output logic observe_ctrl_compute_pending_o,
  output logic observe_ctrl_compute_running_o,
  output logic [31:0] observe_ctrl_var_cmd_accept_count_o,
  output logic [31:0] observe_ctrl_fac_cmd_accept_count_o,
  output logic [31:0] observe_ctrl_phase_flip_count_o,
  output logic [31:0] observe_ctrl_epoch_count_o,
  output logic observe_wr_desc_pending_o,
  output logic [1:0] observe_wrapper_state_o,
  output logic observe_wrapper_compute_done_o,
  output logic observe_wrapper_rsp_done_o,
  output logic observe_engine_compute_done_o,
  output logic observe_engine_rsp_done_o,
  output logic [2:0] observe_engine_cmd_dofs_o,
  output logic [3:0] observe_engine_cmd_msg_count_o,
  output logic [15:0] observe_engine_stream_in_beats_o,
  output logic [15:0] observe_engine_stream_out_beats_o,
  output logic [15:0] observe_engine_stream_target_beats_o,
  output logic observe_engine_stream_active_o,
  output logic observe_engine_stream_dir_out_o,
  output logic observe_engine_stream_out_valid_o,
  output logic observe_engine_stream_out_ready_o,
  output logic [3:0] observe_engine_fsm_state_o,
  output logic [3:0] observe_engine_fsm_accum_count_o,
  output logic [7:0] observe_write_fifo_occ_o,
  output logic observe_engine_stream_out_nonzero_o,
  output logic [31:0] observe_engine_stream_out_word0_o,
  output logic [7:0] observe_engine_stream_rd_addr_o,
  output logic observe_write_fifo_data_nonzero_o,
  output logic [31:0] observe_write_fifo_data_word0_o,
  output logic observe_mic_write_data_nonzero_o,
  output logic [31:0] observe_mic_write_data_word0_o,

  input logic [((`GBP_PE_COUNT) <= 1 ? 1 : $clog2(`GBP_PE_COUNT))-1:0] probe_pe_i,
  input logic [BANK_ID_W-1:0] probe_bank_i,
  input logic [ROW_ADDR_W-1:0] probe_row_i,
  output logic [31:0] probe_word0_o,
  output logic [31:0] probe_word1_o,
  output logic [31:0] probe_word2_o,
  output logic [31:0] probe_word3_o,
  output logic [31:0] probe_word4_o,
  output logic [31:0] probe_word5_o,
  output logic [31:0] probe_word6_o,
  output logic [31:0] probe_word7_o
);

  localparam int unsigned mesh_x_lp = `GBP_MESH_X;
  localparam int unsigned mesh_y_lp = `GBP_MESH_Y;
  localparam int unsigned pe_count_lp = `GBP_PE_COUNT;
  localparam int unsigned PE_SEL_W = (pe_count_lp <= 1) ? 1 : $clog2(pe_count_lp);
  localparam int unsigned x_subcord_width_lp = (mesh_x_lp <= 1) ? 1 : $clog2(mesh_x_lp);
  localparam int unsigned y_subcord_width_lp = (mesh_y_lp <= 1) ? 1 : $clog2(mesh_y_lp);
  localparam int unsigned pod_x_cord_width_lp = 1;
  localparam int unsigned pod_y_cord_width_lp = 1;
  localparam int unsigned x_cord_width_lp = x_subcord_width_lp + pod_x_cord_width_lp;
  localparam int unsigned y_cord_width_lp = y_subcord_width_lp + pod_y_cord_width_lp;
  localparam int unsigned addr_width_lp = 16;
  localparam int unsigned data_width_lp = 32;
  localparam int unsigned link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);
  localparam int unsigned barrier_ruche_factor_x_lp = 1;
  localparam int unsigned dmem_size_lp = 1024;
  localparam int unsigned row_bytes_lg_lp = BYTE_OFF_W + WORD_OFF_W;
  localparam int unsigned beat_words_lp = (1 << WORD_OFF_W);
  localparam int hetero_type_vec_lp [0:pe_count_lp-1] = '{default:8};

  logic reset_i;
  assign reset_i = ~rst_n;

  logic [mesh_x_lp-1:0] mesh_reset_i;
  logic [mesh_x_lp-1:0] mesh_reset_o;
  logic [E:W][mesh_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_i;
  logic [E:W][mesh_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_o;
  logic [S:N][mesh_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_i;
  logic [S:N][mesh_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_o;
  logic [S:N][mesh_x_lp-1:0] ver_barrier_link_i;
  logic [S:N][mesh_x_lp-1:0] ver_barrier_link_o;
  logic [E:W][mesh_y_lp-1:0] hor_barrier_link_i;
  logic [E:W][mesh_y_lp-1:0] hor_barrier_link_o;
  logic [E:W][mesh_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_i;
  logic [E:W][mesh_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_o;
  logic [mesh_x_lp-1:0][x_cord_width_lp-1:0] global_x_i;
  logic [mesh_x_lp-1:0][y_cord_width_lp-1:0] global_y_i;
  logic [mesh_x_lp-1:0][x_cord_width_lp-1:0] global_x_o;
  logic [mesh_x_lp-1:0][y_cord_width_lp-1:0] global_y_o;

  logic [mesh_y_lp-1:0][mesh_x_lp-1:0] wb_cmd_valid_lo;
  logic [mesh_y_lp-1:0][mesh_x_lp-1:0][1:0] wb_cmd_kind_lo;
  logic [mesh_y_lp-1:0][mesh_x_lp-1:0][TXN_ID_W-1:0] wb_cmd_txn_lo;
  logic [mesh_y_lp-1:0][mesh_x_lp-1:0] wb_cmd_ready_lo;

  logic [pe_count_lp-1:0] cmd_ready_by_pe_lo;
  logic [pe_count_lp-1:0] observe_wr_req_valid_by_pe_lo;
  logic [pe_count_lp-1:0][SPM_ADDR_W-1:0] observe_wr_req_addr_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_wr_req_data_by_pe_lo;
  logic [pe_count_lp-1:0] observe_ingress_wr_req_valid_by_pe_lo;
  logic [pe_count_lp-1:0][SPM_ADDR_W-1:0] observe_ingress_wr_req_addr_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_ingress_wr_req_data_by_pe_lo;
  logic [pe_count_lp-1:0] observe_bank0_wr_src0_valid_by_pe_lo;
  logic [pe_count_lp-1:0][ROW_ADDR_W-1:0] observe_bank0_wr_src0_row_by_pe_lo;
  logic [pe_count_lp-1:0] observe_bank0_wr_src1_valid_by_pe_lo;
  logic [pe_count_lp-1:0][ROW_ADDR_W-1:0] observe_bank0_wr_src1_row_by_pe_lo;
  logic [pe_count_lp-1:0] observe_compute_done_by_pe_lo;
  logic [pe_count_lp-1:0] observe_compute_start_by_pe_lo;
  logic [pe_count_lp-1:0][2:0] observe_epoch_by_pe_lo;
  logic [pe_count_lp-1:0] observe_doorbell_by_pe_lo;
  logic [pe_count_lp-1:0][7:0] observe_q_head_by_pe_lo;
  logic [pe_count_lp-1:0][7:0] observe_q_tail_by_pe_lo;
  logic [pe_count_lp-1:0][7:0] observe_q_credit_by_pe_lo;
  logic [pe_count_lp-1:0][ROW_ADDR_W-1:0] observe_meta_row_by_pe_lo;
  logic [pe_count_lp-1:0] observe_scan_done_by_pe_lo;
  logic [pe_count_lp-1:0][4:0] observe_ctrl_state_by_pe_lo;
  logic [pe_count_lp-1:0] observe_ctrl_phase_by_pe_lo;
  logic [pe_count_lp-1:0] observe_ctrl_compute_pending_by_pe_lo;
  logic [pe_count_lp-1:0] observe_ctrl_compute_running_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_ctrl_var_cmd_accept_count_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_ctrl_fac_cmd_accept_count_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_ctrl_phase_flip_count_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_ctrl_epoch_count_by_pe_lo;
  logic [pe_count_lp-1:0] observe_wr_desc_pending_by_pe_lo;
  logic [pe_count_lp-1:0][1:0] observe_wrapper_state_by_pe_lo;
  logic [pe_count_lp-1:0] observe_wrapper_compute_done_by_pe_lo;
  logic [pe_count_lp-1:0] observe_wrapper_rsp_done_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_compute_done_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_rsp_done_by_pe_lo;
  logic [pe_count_lp-1:0][2:0] observe_engine_cmd_dofs_by_pe_lo;
  logic [pe_count_lp-1:0][3:0] observe_engine_cmd_msg_count_by_pe_lo;
  logic [pe_count_lp-1:0][15:0] observe_engine_stream_in_beats_by_pe_lo;
  logic [pe_count_lp-1:0][15:0] observe_engine_stream_out_beats_by_pe_lo;
  logic [pe_count_lp-1:0][15:0] observe_engine_stream_target_beats_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_stream_active_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_stream_dir_out_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_stream_out_valid_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_stream_out_ready_by_pe_lo;
  logic [pe_count_lp-1:0][3:0] observe_engine_fsm_state_by_pe_lo;
  logic [pe_count_lp-1:0][3:0] observe_engine_fsm_accum_count_by_pe_lo;
  logic [pe_count_lp-1:0][7:0] observe_write_fifo_occ_by_pe_lo;
  logic [pe_count_lp-1:0] observe_engine_stream_out_nonzero_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_engine_stream_out_word0_by_pe_lo;
  logic [pe_count_lp-1:0][7:0] observe_engine_stream_rd_addr_by_pe_lo;
  logic [pe_count_lp-1:0] observe_write_fifo_data_nonzero_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_write_fifo_data_word0_by_pe_lo;
  logic [pe_count_lp-1:0] observe_mic_write_data_nonzero_by_pe_lo;
  logic [pe_count_lp-1:0][31:0] observe_mic_write_data_word0_by_pe_lo;
  logic [pe_count_lp-1:0][BEAT_BITS-1:0] probe_row_by_pe_lo;
  logic [BEAT_BITS-1:0] probe_row_lo;

  bsg_manycore_tile_compute_array_mesh #(
    .dmem_size_p(dmem_size_lp),
    .ipoly_hashing_p(0),
    .num_tiles_x_p(mesh_x_lp),
    .num_tiles_y_p(mesh_y_lp),
    .subarray_num_tiles_x_p(mesh_x_lp),
    .subarray_num_tiles_y_p(mesh_y_lp),
    .hetero_type_vec_p(hetero_type_vec_lp),
    .addr_width_p(addr_width_lp),
    .data_width_p(data_width_lp),
    .barrier_ruche_factor_X_p(barrier_ruche_factor_x_lp),
    .y_cord_width_p(y_cord_width_lp),
    .x_cord_width_p(x_cord_width_lp),
    .pod_y_cord_width_p(pod_y_cord_width_lp),
    .pod_x_cord_width_p(pod_x_cord_width_lp)
  ) mesh (
    .clk_i(clk),
    .reset_i(mesh_reset_i),
    .reset_o(mesh_reset_o),
    .hor_link_sif_i(hor_link_sif_i),
    .hor_link_sif_o(hor_link_sif_o),
    .ver_link_sif_i(ver_link_sif_i),
    .ver_link_sif_o(ver_link_sif_o),
    .ver_barrier_link_i(ver_barrier_link_i),
    .ver_barrier_link_o(ver_barrier_link_o),
    .hor_barrier_link_i(hor_barrier_link_i),
    .hor_barrier_link_o(hor_barrier_link_o),
    .barrier_ruche_link_i(barrier_ruche_link_i),
    .barrier_ruche_link_o(barrier_ruche_link_o),
    .global_x_i(global_x_i),
    .global_y_i(global_y_i),
    .global_x_o(global_x_o),
    .global_y_o(global_y_o),
    .wb_cmd_valid_i(wb_cmd_valid_lo),
    .wb_cmd_kind_i(wb_cmd_kind_lo),
    .wb_cmd_txn_id_i(wb_cmd_txn_lo),
    .wb_cmd_ready_o(wb_cmd_ready_lo)
  );

  always_comb begin
    mesh_reset_i = {mesh_x_lp{reset_i}};
    hor_link_sif_i = '{default:'0};
    ver_link_sif_i = '{default:'0};
    ver_barrier_link_i = '0;
    hor_barrier_link_i = '0;
    barrier_ruche_link_i = '0;

    wb_cmd_valid_lo = '0;
    wb_cmd_kind_lo = '0;
    wb_cmd_txn_lo = '0;

    for (int c = 0; c < mesh_x_lp; c++) begin
      global_x_i[c] = x_cord_width_lp'({pod_x_cord_width_lp'(0), x_subcord_width_lp'(c)});
      global_y_i[c] = y_cord_width_lp'({pod_y_cord_width_lp'(0), y_subcord_width_lp'(0)});
    end

    for (int r = 0; r < mesh_y_lp; r++) begin
      for (int c = 0; c < mesh_x_lp; c++) begin
        int pe_idx;
        pe_idx = (r * mesh_x_lp) + c;
        if (cmd_valid_i && (cmd_pe_i == PE_SEL_W'(pe_idx))) begin
          wb_cmd_valid_lo[r][c] = 1'b1;
          wb_cmd_kind_lo[r][c] = cmd_kind_i;
          wb_cmd_txn_lo[r][c] = cmd_txn_i;
        end
      end
    end
  end

  for (genvar r = 0; r < mesh_y_lp; r++) begin : g_probe_r
    for (genvar c = 0; c < mesh_x_lp; c++) begin : g_probe_c
      localparam int unsigned pe_idx_lp = (r * mesh_x_lp) + c;
      assign cmd_ready_by_pe_lo[pe_idx_lp] = wb_cmd_ready_lo[r][c];
      assign observe_wr_req_valid_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_wr_req_valid_lo;
      assign observe_wr_req_addr_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_wr_req_addr_lo;
      assign observe_wr_req_data_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_wr_req_data_low_lo;
      assign observe_ingress_wr_req_valid_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_ingress_wr_req_valid_lo;
      assign observe_ingress_wr_req_addr_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_ingress_wr_req_addr_lo;
      assign observe_ingress_wr_req_data_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_ingress_wr_req_data_low_lo;
      assign observe_bank0_wr_src0_valid_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.wr_req_by_bank[0][0];
      assign observe_bank0_wr_src0_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.wr_row_by_bank[0][0];
      assign observe_bank0_wr_src1_valid_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.wr_req_by_bank[0][1];
      assign observe_bank0_wr_src1_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.wr_row_by_bank[0][1];
      assign observe_compute_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_compute_done_lo;
      assign observe_compute_start_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe_compute_start_lo;
      assign observe_epoch_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.adapter.q_epoch_r[0];
      assign observe_doorbell_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.adapter.q_doorbell_r[0];
      assign observe_q_head_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.adapter.q_head_r[0];
      assign observe_q_tail_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.adapter.q_tail_r[0];
      assign observe_q_credit_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.adapter.q_credit_r[0];
      assign observe_meta_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.current_meta_row_r;
      assign observe_scan_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.scan_done_r;
      assign observe_ctrl_state_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.state_r;
      assign observe_ctrl_phase_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.phase_r;
      assign observe_ctrl_compute_pending_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.compute_pending_r;
      assign observe_ctrl_compute_running_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.compute_running_r;
      assign observe_ctrl_var_cmd_accept_count_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.var_cmd_accept_count_r;
      assign observe_ctrl_fac_cmd_accept_count_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.fac_cmd_accept_count_r;
      assign observe_ctrl_phase_flip_count_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.phase_flip_count_r;
      assign observe_ctrl_epoch_count_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_control_unit.epoch_count_r;
      assign observe_wr_desc_pending_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.wr_desc_pending_r;
      assign observe_wrapper_state_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.state_r[1:0];
      assign observe_wrapper_compute_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.gbp_compute_done;
      assign observe_wrapper_rsp_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.gbp_rsp_done;
      assign observe_engine_compute_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.compute_done_o;
      assign observe_engine_rsp_done_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.rsp_done_o;
      assign observe_engine_cmd_dofs_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.cmd_dofs_r;
      assign observe_engine_cmd_msg_count_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.cmd_msg_count_r;
      assign observe_engine_stream_in_beats_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_in_beats_r;
      assign observe_engine_stream_out_beats_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_out_beats_r;
      assign observe_engine_stream_target_beats_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_target_beats;
      assign observe_engine_stream_active_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_active_r;
      assign observe_engine_stream_dir_out_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_dir_out_r;
      assign observe_engine_stream_out_valid_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.gbp_stream_out_valid;
      assign observe_engine_stream_out_ready_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.gbp_stream_out_ready;
      assign observe_engine_fsm_state_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.u_gbp_control_fsm.state_r;
      assign observe_engine_fsm_accum_count_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.u_gbp_control_fsm.accum_count_r;
      assign observe_write_fifo_occ_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_write_stream_engine.data_fifo_occ_lo;
      assign observe_engine_stream_out_nonzero_by_pe_lo[pe_idx_lp] =
          |mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_out_data;
      assign observe_engine_stream_out_word0_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.stream_out_data[31:0];
      assign observe_engine_stream_rd_addr_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_compute_unit.u_gbp_engine.buf_stream_rd_addr;
      assign observe_write_fifo_data_nonzero_by_pe_lo[pe_idx_lp] =
          |mesh.y[r].x[c].tile.proc.h.z.pe.u_write_stream_engine.data_fifo_data_lo;
      assign observe_write_fifo_data_word0_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_write_stream_engine.data_fifo_data_lo[31:0];
      assign observe_mic_write_data_nonzero_by_pe_lo[pe_idx_lp] =
          |mesh.y[r].x[c].tile.proc.h.z.pe.u_write_stream_engine.u_mic_write.data_r;
      assign observe_mic_write_data_word0_by_pe_lo[pe_idx_lp] =
          mesh.y[r].x[c].tile.proc.h.z.pe.u_write_stream_engine.u_mic_write.data_r[31:0];

      always_comb begin
        unique case (probe_bank_i)
          BANK_ID_W'(0): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[0].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(1): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[1].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(2): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[2].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(3): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[3].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(4): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[4].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(5): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[5].u_spm_bank.mem_r[probe_row_i];
          BANK_ID_W'(6): probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[6].u_spm_bank.mem_r[probe_row_i];
          default: probe_row_by_pe_lo[pe_idx_lp] = mesh.y[r].x[c].tile.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[7].u_spm_bank.mem_r[probe_row_i];
        endcase
      end
    end
  end

  assign cmd_ready_o = cmd_ready_by_pe_lo[cmd_pe_i];
  assign observe_wr_req_valid_o = observe_wr_req_valid_by_pe_lo[observe_pe_i];
  assign observe_wr_req_addr_o = observe_wr_req_addr_by_pe_lo[observe_pe_i];
  assign observe_wr_req_data_o = observe_wr_req_data_by_pe_lo[observe_pe_i];
  assign observe_ingress_wr_req_valid_o = observe_ingress_wr_req_valid_by_pe_lo[observe_pe_i];
  assign observe_ingress_wr_req_addr_o = observe_ingress_wr_req_addr_by_pe_lo[observe_pe_i];
  assign observe_ingress_wr_req_data_o = observe_ingress_wr_req_data_by_pe_lo[observe_pe_i];
  assign observe_bank0_wr_src0_valid_o = observe_bank0_wr_src0_valid_by_pe_lo[observe_pe_i];
  assign observe_bank0_wr_src0_row_o = observe_bank0_wr_src0_row_by_pe_lo[observe_pe_i];
  assign observe_bank0_wr_src1_valid_o = observe_bank0_wr_src1_valid_by_pe_lo[observe_pe_i];
  assign observe_bank0_wr_src1_row_o = observe_bank0_wr_src1_row_by_pe_lo[observe_pe_i];
  assign observe_compute_done_o = observe_compute_done_by_pe_lo[observe_pe_i];
  assign observe_compute_start_o = observe_compute_start_by_pe_lo[observe_pe_i];
  assign observe_epoch_o = observe_epoch_by_pe_lo[observe_pe_i];
  assign observe_doorbell_o = observe_doorbell_by_pe_lo[observe_pe_i];
  assign observe_q_head_o = observe_q_head_by_pe_lo[observe_pe_i];
  assign observe_q_tail_o = observe_q_tail_by_pe_lo[observe_pe_i];
  assign observe_q_credit_o = observe_q_credit_by_pe_lo[observe_pe_i];
  assign observe_meta_row_o = observe_meta_row_by_pe_lo[observe_pe_i];
  assign observe_scan_done_o = observe_scan_done_by_pe_lo[observe_pe_i];
  assign observe_ctrl_state_o = observe_ctrl_state_by_pe_lo[observe_pe_i];
  assign observe_ctrl_phase_o = observe_ctrl_phase_by_pe_lo[observe_pe_i];
  assign observe_ctrl_compute_pending_o = observe_ctrl_compute_pending_by_pe_lo[observe_pe_i];
  assign observe_ctrl_compute_running_o = observe_ctrl_compute_running_by_pe_lo[observe_pe_i];
  assign observe_ctrl_var_cmd_accept_count_o = observe_ctrl_var_cmd_accept_count_by_pe_lo[observe_pe_i];
  assign observe_ctrl_fac_cmd_accept_count_o = observe_ctrl_fac_cmd_accept_count_by_pe_lo[observe_pe_i];
  assign observe_ctrl_phase_flip_count_o = observe_ctrl_phase_flip_count_by_pe_lo[observe_pe_i];
  assign observe_ctrl_epoch_count_o = observe_ctrl_epoch_count_by_pe_lo[observe_pe_i];
  assign observe_wr_desc_pending_o = observe_wr_desc_pending_by_pe_lo[observe_pe_i];
  assign observe_wrapper_state_o = observe_wrapper_state_by_pe_lo[observe_pe_i];
  assign observe_wrapper_compute_done_o = observe_wrapper_compute_done_by_pe_lo[observe_pe_i];
  assign observe_wrapper_rsp_done_o = observe_wrapper_rsp_done_by_pe_lo[observe_pe_i];
  assign observe_engine_compute_done_o = observe_engine_compute_done_by_pe_lo[observe_pe_i];
  assign observe_engine_rsp_done_o = observe_engine_rsp_done_by_pe_lo[observe_pe_i];
  assign observe_engine_cmd_dofs_o = observe_engine_cmd_dofs_by_pe_lo[observe_pe_i];
  assign observe_engine_cmd_msg_count_o = observe_engine_cmd_msg_count_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_in_beats_o = observe_engine_stream_in_beats_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_out_beats_o = observe_engine_stream_out_beats_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_target_beats_o = observe_engine_stream_target_beats_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_active_o = observe_engine_stream_active_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_dir_out_o = observe_engine_stream_dir_out_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_out_valid_o = observe_engine_stream_out_valid_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_out_ready_o = observe_engine_stream_out_ready_by_pe_lo[observe_pe_i];
  assign observe_engine_fsm_state_o = observe_engine_fsm_state_by_pe_lo[observe_pe_i];
  assign observe_engine_fsm_accum_count_o = observe_engine_fsm_accum_count_by_pe_lo[observe_pe_i];
  assign observe_write_fifo_occ_o = observe_write_fifo_occ_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_out_nonzero_o = observe_engine_stream_out_nonzero_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_out_word0_o = observe_engine_stream_out_word0_by_pe_lo[observe_pe_i];
  assign observe_engine_stream_rd_addr_o = observe_engine_stream_rd_addr_by_pe_lo[observe_pe_i];
  assign observe_write_fifo_data_nonzero_o = observe_write_fifo_data_nonzero_by_pe_lo[observe_pe_i];
  assign observe_write_fifo_data_word0_o = observe_write_fifo_data_word0_by_pe_lo[observe_pe_i];
  assign observe_mic_write_data_nonzero_o = observe_mic_write_data_nonzero_by_pe_lo[observe_pe_i];
  assign observe_mic_write_data_word0_o = observe_mic_write_data_word0_by_pe_lo[observe_pe_i];
  assign probe_row_lo = probe_row_by_pe_lo[probe_pe_i];

  assign probe_word0_o = probe_row_lo[31:0];
  assign probe_word1_o = probe_row_lo[63:32];
  assign probe_word2_o = probe_row_lo[95:64];
  assign probe_word3_o = probe_row_lo[127:96];
  assign probe_word4_o = probe_row_lo[159:128];
  assign probe_word5_o = probe_row_lo[191:160];
  assign probe_word6_o = probe_row_lo[223:192];
  assign probe_word7_o = probe_row_lo[255:224];

endmodule
