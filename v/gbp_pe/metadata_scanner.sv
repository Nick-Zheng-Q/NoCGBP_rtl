// metadata_scanner.sv
// Reads NodeHeader from SPM, extracts metadata, scans AdjEntry list.
// Classifies neighbors as local vs remote based on (x, y) coordinates.

module metadata_scanner
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W
    , parameter int DOF_W       = gbp_pkg::DOF_W
    , parameter int ADJ_COUNT_W = gbp_pkg::ADJ_COUNT_W
    , parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W
    , parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W
    , parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS
    , parameter int X_CORD_W    = gbp_pkg::X_CORD_W
    , parameter int Y_CORD_W    = gbp_pkg::Y_CORD_W
) (
    input  logic clk_i
    , input  logic rst_i

    // Command from scheduler
    , input  logic                 cmd_valid_i
    , input  logic [NODE_ID_W-1:0] cmd_node_id_i
    , input  logic                 cmd_is_factor_i
    , output logic                 cmd_ready_o

    // SPM read port (to SPM Arbiter)
    , output logic                 spm_rd_valid_o
    , output logic [SPM_ADDR_W-1:0] spm_rd_addr_o
    , input  logic                 spm_rd_ready_i
    , input  logic [BEAT_BITS-1:0] spm_rd_data_i

    // AdjEntry output stream
    , output logic                 adj_valid_o
    , input  logic                 adj_ready_i
    , output logic [NODE_ID_W-1:0] adj_neighbor_id_o
    , output logic [X_CORD_W-1:0]  adj_neighbor_x_o
    , output logic [Y_CORD_W-1:0]  adj_neighbor_y_o
    , output logic                 adj_is_local_o
    , output logic                 adj_last_o
    , output logic [ADJ_COUNT_W-1:0] adj_edge_idx_o

    // Node info output
    , output logic                 info_valid_o
    , output logic [DOF_W-1:0]     info_dof_o
    , output logic [ADJ_COUNT_W-1:0] info_adj_count_o
    , output logic [SPM_ADDR_W-1:0] info_state_base_o
    , output logic [STATE_WORDS_W-1:0] info_state_words_o

    // My coordinates (for local/remote classification)
    , input  logic [X_CORD_W-1:0]  my_x_i
    , input  logic [Y_CORD_W-1:0]  my_y_i
);

  // NodeHeader layout (packed in a beat):
  // [NODE_ID_W-1:0]                    node_id
  // [NODE_ID_W+DOF_W-1:NODE_ID_W]     dof
  // [+: ADJ_COUNT_W]                   adj_count
  // [+: SPM_ADDR_W]                    adj_base
  // [+: SPM_ADDR_W]                    state_base
  // [+: STATE_WORDS_W]                 state_words
  localparam int HEADER_WORDS = 1;  // 1 beat for header

  // AdjEntry layout:
  // [NODE_ID_W-1:0]                    neighbor_id
  // [NODE_ID_W+X_CORD_W-1:NODE_ID_W]  neighbor_x
  // [+: Y_CORD_W]                      neighbor_y

  // FSM states
  localparam S_IDLE       = 3'd0;
  localparam S_RD_HEADER  = 3'd1;
  localparam S_PARSE_HDR  = 3'd2;
  localparam S_RD_ADJ     = 3'd3;
  localparam S_OUTPUT_ADJ = 3'd4;
  localparam S_DONE       = 3'd5;

  logic [2:0] state_r;

  // Latched command
  logic [NODE_ID_W-1:0] cmd_node_id_r;
  logic                  cmd_is_factor_r;

  // Latched header fields
  logic [DOF_W-1:0]         hdr_dof_r;
  logic [ADJ_COUNT_W-1:0]   hdr_adj_count_r;
  logic [SPM_ADDR_W-1:0]    hdr_adj_base_r;
  logic [SPM_ADDR_W-1:0]    hdr_state_base_r;
  logic [STATE_WORDS_W-1:0] hdr_state_words_r;

  // AdjEntry scan counter
  logic [ADJ_COUNT_W-1:0] adj_idx_r;

  // Latched AdjEntry data
  logic [NODE_ID_W-1:0] adj_neighbor_id_r;
  logic [X_CORD_W-1:0]  adj_neighbor_x_r;
  logic [Y_CORD_W-1:0]  adj_neighbor_y_r;

  // SPM read request register
  logic                 spm_rd_pending_r;
  logic [SPM_ADDR_W-1:0] spm_rd_addr_r;

  // Header address: header_base + node_id * HEADER_WORDS
  // For simplicity, header_base = 0 (META region starts at address 0)
  wire [SPM_ADDR_W-1:0] header_addr_w = SPM_ADDR_W'(cmd_node_id_i) * HEADER_WORDS;

  // AdjEntry address: adj_base + adj_idx
  wire [SPM_ADDR_W-1:0] adj_addr_w = hdr_adj_base_r + SPM_ADDR_W'(adj_idx_r);

  // Local/remote classification
  wire is_local_w = (adj_neighbor_x_r == my_x_i) && (adj_neighbor_y_r == my_y_i);

  // Output assignments
  assign cmd_ready_o = (state_r == S_IDLE);

  assign spm_rd_valid_o = spm_rd_pending_r;
  assign spm_rd_addr_o  = spm_rd_addr_r;

  assign adj_valid_o       = (state_r == S_OUTPUT_ADJ);
  assign adj_neighbor_id_o = adj_neighbor_id_r;
  assign adj_neighbor_x_o  = adj_neighbor_x_r;
  assign adj_neighbor_y_o  = adj_neighbor_y_r;
  assign adj_is_local_o    = is_local_w;
  assign adj_last_o        = (adj_idx_r == hdr_adj_count_r - 1);
  assign adj_edge_idx_o    = adj_idx_r;

  assign info_valid_o        = (state_r == S_OUTPUT_ADJ) && (adj_idx_r == 0);
  assign info_dof_o          = hdr_dof_r;
  assign info_adj_count_o    = hdr_adj_count_r;
  assign info_state_base_o   = hdr_state_base_r;
  assign info_state_words_o  = hdr_state_words_r;

  // FSM
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r <= S_IDLE;
      spm_rd_pending_r <= 1'b0;
      cmd_node_id_r <= '0;
      cmd_is_factor_r <= 1'b0;
      hdr_dof_r <= '0;
      hdr_adj_count_r <= '0;
      hdr_adj_base_r <= '0;
      hdr_state_base_r <= '0;
      hdr_state_words_r <= '0;
      adj_idx_r <= '0;
      adj_neighbor_id_r <= '0;
      adj_neighbor_x_r <= '0;
      adj_neighbor_y_r <= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          if (cmd_valid_i) begin
            cmd_node_id_r <= cmd_node_id_i;
            cmd_is_factor_r <= cmd_is_factor_i;
            spm_rd_addr_r <= header_addr_w;
            spm_rd_pending_r <= 1'b1;
            state_r <= S_RD_HEADER;
          end
        end

        S_RD_HEADER: begin
          if (spm_rd_ready_i) begin
            spm_rd_pending_r <= 1'b0;
            // Parse header from spm_rd_data_i
            hdr_dof_r          <= spm_rd_data_i[NODE_ID_W +: DOF_W];
            hdr_adj_count_r    <= spm_rd_data_i[NODE_ID_W + DOF_W +: ADJ_COUNT_W];
            hdr_adj_base_r     <= spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W +: SPM_ADDR_W];
            hdr_state_base_r   <= spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W + SPM_ADDR_W +: SPM_ADDR_W];
            hdr_state_words_r  <= spm_rd_data_i[NODE_ID_W + DOF_W + ADJ_COUNT_W + 2*SPM_ADDR_W +: STATE_WORDS_W];
            state_r <= S_PARSE_HDR;
          end
        end

        S_PARSE_HDR: begin
          if (hdr_adj_count_r == 0) begin
            // No neighbors
            state_r <= S_DONE;
          end else begin
            adj_idx_r <= '0;
            spm_rd_addr_r <= hdr_adj_base_r;
            spm_rd_pending_r <= 1'b1;
            state_r <= S_RD_ADJ;
          end
        end

        S_RD_ADJ: begin
          if (spm_rd_ready_i) begin
            spm_rd_pending_r <= 1'b0;
            // Parse AdjEntry
            adj_neighbor_id_r <= spm_rd_data_i[NODE_ID_W-1:0];
            adj_neighbor_x_r  <= spm_rd_data_i[NODE_ID_W +: X_CORD_W];
            adj_neighbor_y_r  <= spm_rd_data_i[NODE_ID_W + X_CORD_W +: Y_CORD_W];
            state_r <= S_OUTPUT_ADJ;
          end
        end

        S_OUTPUT_ADJ: begin
          if (adj_ready_i) begin
            if (adj_idx_r == hdr_adj_count_r - 1) begin
              // Last entry, done
              state_r <= S_DONE;
            end else begin
              // Next entry
              adj_idx_r <= adj_idx_r + 1;
              spm_rd_addr_r <= hdr_adj_base_r + SPM_ADDR_W'(adj_idx_r + 1);
              spm_rd_pending_r <= 1'b1;
              state_r <= S_RD_ADJ;
            end
          end
        end

        S_DONE: begin
          state_r <= S_IDLE;
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
