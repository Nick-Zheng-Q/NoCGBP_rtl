// gbp_pe_control_subsystem.sv
// Encapsulates the foreground control pipeline:
//   phase_controller -> node_scheduler -> metadata_scanner -> local_neighbor_state_reader
//
// The local_neighbor_state_reader is internal to this subsystem. It receives the
// adjacency stream from metadata_scanner, stalls the scanner when a local neighbor
// is encountered, reads the neighbor's NodeHeader to obtain state_base/state_words,
// then reads the neighbor state from SPM STATE and forwards it to the accumulator.

module gbp_pe_control_subsystem
  import gbp_pkg::*;
#(
    parameter int NUM_NODES     = gbp_pkg::NUM_NODES_PER_PE,
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W,
    parameter int SPM_ADDR_W    = gbp_pkg::SPM_ADDR_W,
    parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W,
    parameter int ADJ_COUNT_W   = gbp_pkg::ADJ_COUNT_W,
    parameter int DOF_W         = gbp_pkg::DOF_W,
    parameter int BEAT_BITS     = gbp_pkg::BEAT_BITS,
    parameter int X_CORD_W      = gbp_pkg::X_CORD_W,
    parameter int Y_CORD_W      = gbp_pkg::Y_CORD_W
)(
    input  logic clk,
    input  logic rst_n,

    // From fetch subsystem: node readiness
    input  logic [NUM_NODES-1:0] node_ready_i,

    // From writeback controller: completion pulse
    input  logic wb_done_i,

    // To compute subsystem: command + metadata
    output logic                 cmd_valid_o,
    input  logic                 cmd_ready_i,
    output logic [NODE_ID_W-1:0] cmd_node_id_o,
    output logic                 cmd_is_factor_o,
    output logic [DOF_W-1:0]     cmd_dof_o,
    output logic [ADJ_COUNT_W-1:0] cmd_adj_count_o,
    output logic [STATE_WORDS_W-1:0] cmd_state_words_o,
    output logic [SPM_ADDR_W-1:0]  cmd_state_base_o,

    // Adjacency info to writeback controller (latched when cmd fires)
    output logic [ADJ_COUNT_W-1:0] wb_adj_count_o,
    output logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] wb_adj_neighbor_ids_o,
    output logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]  wb_adj_neighbor_xs_o,
    output logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]  wb_adj_neighbor_ys_o,
    output logic [MAX_ADJ_COUNT-1:0]                 wb_adj_is_local_o,

    // Adjacency stream to fetch subsystem
    output logic                 adj_valid_o,
    input  logic                 adj_ready_i,
    output logic [NODE_ID_W-1:0] adj_neighbor_id_o,
    output logic [X_CORD_W-1:0]  adj_neighbor_x_o,
    output logic [Y_CORD_W-1:0]  adj_neighbor_y_o,
    output logic                 adj_is_local_o,
    output logic                 adj_last_o,
    output logic [ADJ_COUNT_W-1:0] adj_edge_idx_o,

    // Scoreboard reset from writeback controller
    input  logic                 reset_valid_i,
    input  logic [NODE_ID_W-1:0] reset_node_id_i,
    input  logic                 reset_is_factor_i,

    // SPM read port (to memory subsystem) — shared by metadata_scanner and local_reader
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd_data_i,

    // Local neighbor state to accumulator
    output logic                 local_valid_o,
    input  logic                 local_ready_i,
    output logic [FP32_W-1:0]   local_data_o,
    output logic                 local_last_o,

    // My coordinates (for local/remote classification)
    input  logic [X_CORD_W-1:0] my_x_i,
    input  logic [Y_CORD_W-1:0] my_y_i
);

  // ========================================================================
  // Phase Controller
  // ========================================================================
  logic                 sched_valid;
  logic [NODE_ID_W-1:0] sched_node_id;
  logic                 sched_is_factor;
  logic                 no_schedulable_nodes;
  logic                 phase_factor_first;
  logic                 phase_switch_pulse;
  logic [NUM_NODES-1:0] visited_mask;

  phase_controller u_phase_controller (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.sched_valid_i(sched_valid)
    ,.sched_node_id_i(sched_node_id)
    ,.no_schedulable_nodes_i(no_schedulable_nodes)
    ,.wb_done_i(wb_done_i)
    ,.phase_factor_first_o(phase_factor_first)
    ,.phase_switch_pulse_o(phase_switch_pulse)
    ,.visited_mask_o(visited_mask)
  );

  // ========================================================================
  // Node Scheduler
  // ========================================================================
  logic sched_ready;

  node_scheduler u_node_scheduler (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.phase_factor_first(phase_factor_first)
    ,.node_ready(node_ready_i)
    ,.visited_mask(visited_mask)
    ,.sched_ready(sched_ready)
    ,.sched_valid(sched_valid)
    ,.sched_node_id(sched_node_id)
    ,.sched_is_factor(sched_is_factor)
    ,.no_schedulable_nodes(no_schedulable_nodes)
  );

  // ========================================================================
  // Metadata Scanner
  // ========================================================================
  logic                 ms_cmd_valid;
  logic [NODE_ID_W-1:0] ms_cmd_node_id;
  logic                 ms_cmd_is_factor;
  logic                 ms_cmd_ready;

  logic                 ms_spm_rd_valid;
  logic [SPM_ADDR_W-1:0] ms_spm_rd_addr;
  logic                 ms_spm_rd_ready;

  logic                 ms_adj_valid;
  logic                 ms_adj_ready;
  logic [NODE_ID_W-1:0] ms_adj_neighbor_id;
  logic [X_CORD_W-1:0]  ms_adj_neighbor_x;
  logic [Y_CORD_W-1:0]  ms_adj_neighbor_y;
  logic                 ms_adj_is_local;
  logic                 ms_adj_last;
  logic [ADJ_COUNT_W-1:0] ms_adj_edge_idx;

  logic                 ms_info_valid;
  logic [DOF_W-1:0]     ms_info_dof;
  logic [ADJ_COUNT_W-1:0] ms_info_adj_count;
  logic [SPM_ADDR_W-1:0]  ms_info_state_base;
  logic [STATE_WORDS_W-1:0] ms_info_state_words;

  metadata_scanner u_metadata_scanner (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.cmd_valid_i(ms_cmd_valid)
    ,.cmd_node_id_i(ms_cmd_node_id)
    ,.cmd_is_factor_i(ms_cmd_is_factor)
    ,.cmd_ready_o(ms_cmd_ready)
    ,.spm_rd_valid_o(ms_spm_rd_valid)
    ,.spm_rd_addr_o(ms_spm_rd_addr)
    ,.spm_rd_ready_i(ms_spm_rd_ready)
    ,.spm_rd_data_i(spm_rd_data_i)
    ,.adj_valid_o(ms_adj_valid)
    ,.adj_ready_i(ms_adj_ready)
    ,.adj_neighbor_id_o(ms_adj_neighbor_id)
    ,.adj_neighbor_x_o(ms_adj_neighbor_x)
    ,.adj_neighbor_y_o(ms_adj_neighbor_y)
    ,.adj_is_local_o(ms_adj_is_local)
    ,.adj_last_o(ms_adj_last)
    ,.adj_edge_idx_o(ms_adj_edge_idx)
    ,.info_valid_o(ms_info_valid)
    ,.info_dof_o(ms_info_dof)
    ,.info_adj_count_o(ms_info_adj_count)
    ,.info_state_base_o(ms_info_state_base)
    ,.info_state_words_o(ms_info_state_words)
    ,.my_x_i(my_x_i)
    ,.my_y_i(my_y_i)
  );

  assign ms_cmd_valid     = sched_valid;
  assign ms_cmd_node_id   = sched_node_id;
  assign ms_cmd_is_factor = sched_is_factor;
  assign sched_ready      = ms_cmd_ready;

  // ========================================================================
  // Local Neighbor State Reader
  // ========================================================================
  // For each local adjacency entry, reads neighbor NodeHeader, extracts
  // state_base/state_words, then reads state words sequentially from SPM.
  //
  // States:
  //   S_PASS      : passing adjacency entries through to fetch subsystem
  //   S_RD_HDR    : read neighbor NodeHeader
  //   S_PARSE_HDR : parse header fields
  //   S_RD_STATE  : read state words and stream to accumulator
  //
  localparam S_PASS     = 3'd0;
  localparam S_RD_HDR   = 3'd1;
  localparam S_PARSE_HDR= 3'd2;
  localparam S_RD_STATE = 3'd3;

  logic [2:0] lr_state_r;
  logic [NODE_ID_W-1:0] lr_neighbor_id_r;
  logic [ADJ_COUNT_W-1:0] lr_edge_idx_r;
  logic                   lr_adj_last_r;
  logic [SPM_ADDR_W-1:0]  lr_state_base_r;
  logic [STATE_WORDS_W-1:0] lr_state_words_r;
  logic [STATE_WORDS_W-1:0] lr_state_cnt_r;

  // NodeHeader layout (packed in 64-bit beat):
  // [NODE_ID_W-1:0]                    node_id
  // [NODE_ID_W+DOF_W-1:NODE_ID_W]     dof
  // [+: ADJ_COUNT_W]                   adj_count
  // [+: SPM_ADDR_W]                    adj_base
  // [+: SPM_ADDR_W]                    state_base
  // [+: STATE_WORDS_W]                 state_words

  // We use a separate SPM read port mux. When local reader is active, it wins.
  logic lr_spm_req;
  assign lr_spm_req = (lr_state_r != S_PASS);

  assign spm_rd_valid_o = lr_spm_req ? 1'b1 : ms_spm_rd_valid;
  assign spm_rd_addr_o  = lr_spm_req ? lr_spm_rd_addr_w : ms_spm_rd_addr;
  assign ms_spm_rd_ready = !lr_spm_req && spm_rd_ready_i;

  wire [SPM_ADDR_W-1:0] lr_spm_rd_addr_w;
  assign lr_spm_rd_addr_w = (lr_state_r == S_RD_HDR)
                              ? SPM_ADDR_W'(lr_neighbor_id_r)
                              : lr_state_base_r + SPM_ADDR_W'(lr_state_cnt_r);

  // adjacency backpressure: scanner stalls when we are reading local state
  assign ms_adj_ready = (lr_state_r == S_PASS);

  // Adjacency output to fetch subsystem: only remote entries pass through
  assign adj_valid_o       = ms_adj_valid && !ms_adj_is_local;
  assign adj_neighbor_id_o = ms_adj_neighbor_id;
  assign adj_neighbor_x_o  = ms_adj_neighbor_x;
  assign adj_neighbor_y_o  = ms_adj_neighbor_y;
  assign adj_is_local_o    = ms_adj_is_local;
  assign adj_last_o        = ms_adj_last;
  assign adj_edge_idx_o    = ms_adj_edge_idx;

  // =======================================================================
  // Adjacency capture buffer (for writeback_controller notifications)
  // =======================================================================
  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] adj_buf_neighbor_ids_r;
  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]  adj_buf_neighbor_xs_r;
  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]  adj_buf_neighbor_ys_r;
  logic [MAX_ADJ_COUNT-1:0]                adj_buf_is_local_r;
  logic [ADJ_COUNT_W-1:0]                  adj_buf_count_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      adj_buf_count_r <= '0;
      adj_buf_is_local_r <= '0;
    end else begin
      if (cmd_valid_o && cmd_ready_i) begin
        // Command consumed: clear buffer for next node
        adj_buf_count_r <= '0;
      end else if (ms_adj_valid && ms_adj_ready) begin
        adj_buf_neighbor_ids_r[ms_adj_edge_idx] <= ms_adj_neighbor_id;
        adj_buf_neighbor_xs_r[ms_adj_edge_idx]  <= ms_adj_neighbor_x;
        adj_buf_neighbor_ys_r[ms_adj_edge_idx]  <= ms_adj_neighbor_y;
        adj_buf_is_local_r[ms_adj_edge_idx]     <= ms_adj_is_local;
        if (ms_adj_last) begin
          adj_buf_count_r <= ADJ_COUNT_W'(ms_adj_edge_idx + 1);
        end
      end
    end
  end

  assign wb_adj_count_o         = adj_buf_count_r;
  assign wb_adj_neighbor_ids_o  = adj_buf_neighbor_ids_r;
  assign wb_adj_neighbor_xs_o   = adj_buf_neighbor_xs_r;
  assign wb_adj_neighbor_ys_o   = adj_buf_neighbor_ys_r;
  assign wb_adj_is_local_o      = adj_buf_is_local_r;

  // Local reader FSM
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      lr_state_r <= S_PASS;
      lr_neighbor_id_r <= '0;
      lr_edge_idx_r <= '0;
      lr_adj_last_r <= 1'b0;
      lr_state_base_r <= '0;
      lr_state_words_r <= '0;
      lr_state_cnt_r <= '0;
    end else begin
      case (lr_state_r)
        S_PASS: begin
          if (ms_adj_valid && ms_adj_ready && ms_adj_is_local) begin
            lr_neighbor_id_r <= ms_adj_neighbor_id;
            lr_edge_idx_r    <= ms_adj_edge_idx;
            lr_adj_last_r    <= ms_adj_last;
            lr_state_r       <= S_RD_HDR;
          end
        end

        S_RD_HDR: begin
          if (spm_rd_ready_i) begin
            lr_state_r <= S_PARSE_HDR;
          end
        end

        S_PARSE_HDR: begin
          lr_state_base_r  <= spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W + SPM_ADDR_W +: SPM_ADDR_W];
          lr_state_words_r <= spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W + 2*SPM_ADDR_W +: STATE_WORDS_W];
          lr_state_cnt_r   <= '0;
          lr_state_r       <= (spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W + 2*SPM_ADDR_W +: STATE_WORDS_W] == 0)
                                ? S_PASS : S_RD_STATE;
        end

        S_RD_STATE: begin
          if (spm_rd_ready_i && local_ready_i) begin
            if (lr_state_cnt_r + 1 == lr_state_words_r) begin
              lr_state_cnt_r <= '0;
              lr_state_r <= S_PASS;
            end else begin
              lr_state_cnt_r <= lr_state_cnt_r + 1;
            end
          end
        end

        default: lr_state_r <= S_PASS;
      endcase
    end
  end

  // Local state output to accumulator
  // In S_RD_STATE, every cycle we have a new word available (1-cycle SPM read latency)
  assign local_valid_o = (lr_state_r == S_RD_STATE);
  assign local_data_o  = spm_rd_data_i[FP32_W-1:0];  // lower 32b of 64b beat
  assign local_last_o  = (lr_state_r == S_RD_STATE) &&
                         (lr_state_cnt_r + 1 == lr_state_words_r);

  // ========================================================================
  // Command + Metadata to Compute Subsystem
  // ========================================================================
  // Latch scheduler info when metadata_scanner accepts the command,
  // and latch metadata info when metadata_scanner emits it.
  logic cmd_info_valid_r;
  logic [NODE_ID_W-1:0] cmd_node_id_r;
  logic                 cmd_is_factor_r;
  logic [DOF_W-1:0]     cmd_dof_r;
  logic [ADJ_COUNT_W-1:0] cmd_adj_count_r;
  logic [SPM_ADDR_W-1:0]  cmd_state_base_r;
  logic [STATE_WORDS_W-1:0] cmd_state_words_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      cmd_info_valid_r <= 1'b0;
    end else begin
      // Capture scheduler info when scanner accepts command
      if (ms_cmd_valid && ms_cmd_ready) begin
        cmd_node_id_r   <= sched_node_id;
        cmd_is_factor_r <= sched_is_factor;
      end
      // Capture metadata info when scanner emits it
      if (ms_info_valid) begin
        cmd_info_valid_r  <= 1'b1;
        cmd_dof_r         <= ms_info_dof;
        cmd_adj_count_r   <= ms_info_adj_count;
        cmd_state_base_r  <= ms_info_state_base;
        cmd_state_words_r <= ms_info_state_words;
      end else if (cmd_valid_o && cmd_ready_i) begin
        cmd_info_valid_r <= 1'b0;
      end
    end
  end

  // Command is valid when we have metadata info and all local neighbors consumed.
  assign cmd_valid_o       = cmd_info_valid_r && (lr_state_r == S_PASS);
  assign cmd_node_id_o     = cmd_node_id_r;
  assign cmd_is_factor_o   = cmd_is_factor_r;
  assign cmd_dof_o         = cmd_dof_r;
  assign cmd_adj_count_o   = cmd_adj_count_r;
  assign cmd_state_words_o = cmd_state_words_r;
  assign cmd_state_base_o  = cmd_state_base_r;

endmodule
