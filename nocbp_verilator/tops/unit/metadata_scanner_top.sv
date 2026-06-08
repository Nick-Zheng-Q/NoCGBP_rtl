// metadata_scanner_top.sv
// Unit test wrapper for metadata_scanner
// Uses fixed combinational SPM responses based on address

module metadata_scanner_top (
    input  logic        clk
    , input  logic        rst_n

    // Command
    , input  logic        cmd_valid_i
    , input  logic [9:0] cmd_node_id_i
    , input  logic        cmd_is_factor_i
    , output logic        cmd_ready_o

    // AdjEntry stream
    , output logic        adj_valid_o
    , input  logic        adj_ready_i
    , output logic [9:0] adj_neighbor_id_o
    , output logic [5:0]  adj_neighbor_x_o
    , output logic [4:0]  adj_neighbor_y_o
    , output logic        adj_is_local_o
    , output logic        adj_last_o
    , output logic [3:0]  adj_edge_idx_o

    // Node info
    , output logic        info_valid_o
    , output logic [3:0]  info_dof_o
    , output logic [3:0]  info_adj_count_o
    , output logic [17:0] info_state_base_o
    , output logic [3:0]  info_state_words_o

    // Exposed SPM ready control and FSM state for corner-case tests
    , input  logic        spm_rd_ready_i
    , output logic [2:0]  state_o
);

  // SPM read port (combinational response)
  logic         spm_rd_valid;
  logic [17:0]  spm_rd_addr;
  logic         spm_rd_ready;
  logic [63:0] spm_rd_data;

  // SPM ready can be forced low from the testbench to exercise error/stall paths.
  assign spm_rd_ready = spm_rd_valid & spm_rd_ready_i;

  // Fixed SPM responses based on {addr[7:4], addr[3:0]}.
  // High nibble selects the test scenario/node; low nibble selects the word.
  always_comb begin
    spm_rd_data = '0;
    case ({spm_rd_addr[7:4], spm_rd_addr[3:0]})
      // ---------- Node 0x00 : zero neighbors ----------
      8'h00: begin
        spm_rd_data[9:0]    = 10'h000;  // node_id
        spm_rd_data[13:10]  = 4'd1;      // dof
        spm_rd_data[17:14]  = 4'd0;      // adj_count
        spm_rd_data[35:18]  = 18'd4;     // adj_base
        spm_rd_data[53:36]  = 18'h100;   // state_base
        spm_rd_data[59:54]  = 6'd2;      // state_words
      end

      // ---------- Node 0x10 : 2 neighbors (original case) ----------
      8'h10: begin
        spm_rd_data[9:0]    = 10'h010;  // node_id
        spm_rd_data[13:10]  = 4'd3;      // dof
        spm_rd_data[17:14]  = 4'd2;      // adj_count
        spm_rd_data[35:18]  = 18'h14;    // adj_base (high nibble matches node)
        spm_rd_data[53:36]  = 18'd8;     // state_base
        spm_rd_data[59:54]  = 6'd6;      // state_words
      end
      8'h14: begin
        spm_rd_data[9:0]    = 10'h020;  // neighbor_id
        spm_rd_data[15:10]  = 6'd1;      // neighbor_x
        spm_rd_data[20:16]  = 5'd0;      // neighbor_y (local: matches my_x=1,my_y=0)
      end
      8'h15: begin
        spm_rd_data[9:0]    = 10'h030;  // neighbor_id
        spm_rd_data[15:10]  = 6'd2;      // neighbor_x
        spm_rd_data[20:16]  = 5'd1;      // neighbor_y (remote)
      end

      // ---------- Node 0x20 : max neighbors, all local ----------
      8'h20: begin
        spm_rd_data[9:0]    = 10'h020;  // node_id
        spm_rd_data[13:10]  = 4'd4;      // dof
        spm_rd_data[17:14]  = 4'd8;      // adj_count = MAX_ADJ_COUNT
        spm_rd_data[35:18]  = 18'h24;    // adj_base
        spm_rd_data[53:36]  = 18'h200;   // state_base
        spm_rd_data[59:54]  = 6'd8;      // state_words
      end
      8'h24, 8'h25, 8'h26, 8'h27,
      8'h28, 8'h29, 8'h2A, 8'h2B: begin
        spm_rd_data[9:0]    = 10'h020 + (spm_rd_addr[3:0] - 4'd4);
        spm_rd_data[15:10]  = 6'd1;      // neighbor_x = my_x
        spm_rd_data[20:16]  = 5'd0;      // neighbor_y = my_y
      end

      // ---------- Node 0x30 : max neighbors, all remote ----------
      8'h30: begin
        spm_rd_data[9:0]    = 10'h030;  // node_id
        spm_rd_data[13:10]  = 4'd4;      // dof
        spm_rd_data[17:14]  = 4'd8;      // adj_count = MAX_ADJ_COUNT
        spm_rd_data[35:18]  = 18'h34;    // adj_base
        spm_rd_data[53:36]  = 18'h300;   // state_base
        spm_rd_data[59:54]  = 6'd8;      // state_words
      end
      8'h34, 8'h35, 8'h36, 8'h37,
      8'h38, 8'h39, 8'h3A, 8'h3B: begin
        spm_rd_data[9:0]    = 10'h030 + (spm_rd_addr[3:0] - 4'd4);
        spm_rd_data[15:10]  = 6'd2;      // neighbor_x != my_x
        spm_rd_data[20:16]  = 5'd1;      // neighbor_y != my_y
      end

      // ---------- Node 0x40 : header for SPM error/reset tests ----------
      8'h40: begin
        spm_rd_data[9:0]    = 10'h040;  // node_id
        spm_rd_data[13:10]  = 4'd2;      // dof
        spm_rd_data[17:14]  = 4'd2;      // adj_count
        spm_rd_data[35:18]  = 18'h44;    // adj_base
        spm_rd_data[53:36]  = 18'h400;   // state_base
        spm_rd_data[59:54]  = 6'd4;      // state_words
      end

      default: spm_rd_data = '0;
    endcase
  end

  metadata_scanner #(
    .NODE_ID_W(10)
    ,.DOF_W(4)
    ,.ADJ_COUNT_W(4)
    ,.STATE_WORDS_W(6)
    ,.SPM_ADDR_W(18)
    ,.BEAT_BITS(gbp_pkg::BEAT_BITS)
    ,.X_CORD_W(6)
    ,.Y_CORD_W(5)
  ) dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.cmd_valid_i(cmd_valid_i)
    ,.cmd_node_id_i(cmd_node_id_i)
    ,.cmd_is_factor_i(cmd_is_factor_i)
    ,.cmd_ready_o(cmd_ready_o)
    ,.spm_rd_valid_o(spm_rd_valid)
    ,.spm_rd_addr_o(spm_rd_addr)
    ,.spm_rd_ready_i(spm_rd_ready)
    ,.spm_rd_data_i(spm_rd_data)
    ,.adj_valid_o(adj_valid_o)
    ,.adj_ready_i(adj_ready_i)
    ,.adj_neighbor_id_o(adj_neighbor_id_o)
    ,.adj_neighbor_x_o(adj_neighbor_x_o)
    ,.adj_neighbor_y_o(adj_neighbor_y_o)
    ,.adj_is_local_o(adj_is_local_o)
    ,.adj_last_o(adj_last_o)
    ,.adj_edge_idx_o(adj_edge_idx_o)
    ,.info_valid_o(info_valid_o)
    ,.info_dof_o(info_dof_o)
    ,.info_adj_count_o(info_adj_count_o)
    ,.info_state_base_o(info_state_base_o)
    ,.info_state_words_o(info_state_words_o)
    ,.my_x_i(3'd1)
    ,.my_y_i(3'd0)
  );

  assign state_o = dut.state_r;

endmodule
