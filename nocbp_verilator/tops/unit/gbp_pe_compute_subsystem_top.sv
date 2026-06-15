// gbp_pe_compute_subsystem_top.sv
// Unit-test wrapper for the new descriptor-driven compute subsystem.
// Provides a fake dual-read/write SPM with C++ backdoor write/read ports.

module gbp_pe_compute_subsystem_top (
    input logic clk
    , input logic rst_n

    // Command
    , input logic        cmd_valid_i
    , input logic [gbp_pkg::NODE_ID_W-1:0] cmd_node_id_i
    , input logic        cmd_is_factor_i
    , input logic [gbp_pkg::DOF_W-1:0]     cmd_dof_i
    , input logic [gbp_pkg::ADJ_COUNT_W-1:0] cmd_adj_count_i
    , input logic [gbp_pkg::STATE_WORDS_W-1:0] cmd_state_words_i
    , input logic [gbp_pkg::SPM_ADDR_W-1:0]  cmd_state_base_i
    , input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::DOF_W-1:0] cmd_neighbor_dofs_i
    , output logic       cmd_ready_o

    // SPM read backpressure
    , input logic        spm_rd0_ready_i

    // Fake SPM backdoor (word address, 32-bit write)
    , input logic [gbp_pkg::SPM_ADDR_W-1:0] spm_backdoor_wr_addr_i
    , input logic [31:0]                   spm_backdoor_wr_data_i
    , input logic                           spm_backdoor_wr_valid_i
    , input logic [gbp_pkg::SPM_ADDR_W-1:0] spm_backdoor_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0] spm_backdoor_rd_data_o

    // Done / batch done
    , output logic       done_valid_o
    , output logic [gbp_pkg::NODE_ID_W-1:0] done_node_id_o
    , output logic       done_is_factor_o
    , output logic       batch_done_o
);

  localparam int MEM_DEPTH_LG2 = 12; // 4k 64-bit words
  localparam int MEM_DEPTH = 1 << MEM_DEPTH_LG2;

  // Fake SPM memory
  logic [gbp_pkg::BEAT_BITS-1:0] fake_mem [0:MEM_DEPTH-1];

  // -------------------------------------------------------------------------
  // Compute subsystem instance
  // -------------------------------------------------------------------------
  logic                  spm_rd0_valid, spm_rd1_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_rd0_addr, spm_rd1_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_rd0_data, spm_rd1_data;

  logic                  spm_wr_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_wr_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_wr_data;
  logic [gbp_pkg::WSTRB_W-1:0]   spm_wr_wstrb;

  // Read ports: fake one-cycle latency to match read_stream_engine's
  // delayed-ready protocol.  The address is sampled when valid is high and
  // data is returned one cycle later, with ready acting as the response valid.
  // SPM addresses are 32-bit word addresses; fake_mem is a 64-bit beat array.
  logic spm_rd0_valid_r, spm_rd1_valid_r;
  logic spm_rd0_ready_int, spm_rd1_ready_int;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_rd0_sample_addr, spm_rd1_sample_addr;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      spm_rd0_valid_r <= 1'b0;
      spm_rd1_valid_r <= 1'b0;
    end else begin
      spm_rd0_valid_r <= spm_rd0_valid;
      spm_rd1_valid_r <= spm_rd1_valid;
    end
    if (spm_rd0_valid) spm_rd0_sample_addr <= spm_rd0_addr;
    if (spm_rd1_valid) spm_rd1_sample_addr <= spm_rd1_addr;
  end

  assign spm_rd0_ready_int = spm_rd0_valid_r & spm_rd0_ready_i;
  assign spm_rd1_ready_int = spm_rd1_valid_r;

  assign spm_rd0_data = fake_mem[spm_rd0_sample_addr[MEM_DEPTH_LG2-1:1]];
  assign spm_rd1_data = fake_mem[spm_rd1_sample_addr[MEM_DEPTH_LG2-1:1]];

  gbp_pe_compute_subsystem u_dut (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.cmd_valid_i(cmd_valid_i)
    ,.cmd_ready_o(cmd_ready_o)
    ,.cmd_node_id_i(cmd_node_id_i)
    ,.cmd_is_factor_i(cmd_is_factor_i)
    ,.cmd_dof_i(cmd_dof_i)
    ,.cmd_adj_count_i(cmd_adj_count_i)
    ,.cmd_state_words_i(cmd_state_words_i)
    ,.cmd_state_base_i(cmd_state_base_i)
    ,.cmd_neighbor_dofs_i(cmd_neighbor_dofs_i)
    ,.ns_valid_i(1'b0)
    ,.ns_ready_o()
    ,.ns_data_i('0)
    ,.ns_last_i(1'b0)
    ,.spm_rd0_valid_o(spm_rd0_valid)
    ,.spm_rd0_ready_i(spm_rd0_ready_int)
    ,.spm_rd0_addr_o(spm_rd0_addr)
    ,.spm_rd0_data_i(spm_rd0_data)
    ,.spm_rd1_valid_o(spm_rd1_valid)
    ,.spm_rd1_ready_i(spm_rd1_ready_int)
    ,.spm_rd1_addr_o(spm_rd1_addr)
    ,.spm_rd1_data_i(spm_rd1_data)
    ,.spm_wr_valid_o(spm_wr_valid)
    ,.spm_wr_ready_i(1'b1)
    ,.spm_wr_addr_o(spm_wr_addr)
    ,.spm_wr_data_o(spm_wr_data)
    ,.spm_wr_wstrb_o(spm_wr_wstrb)
    ,.done_valid_o(done_valid_o)
    ,.done_node_id_o(done_node_id_o)
    ,.done_is_factor_o(done_is_factor_o)
    ,.batch_done_o(batch_done_o)
  );

  // -------------------------------------------------------------------------
  // Fake SPM write (compute subsystem + backdoor)
  // -------------------------------------------------------------------------
  logic [gbp_pkg::SPM_ADDR_W-1:0] wr_beat_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  wr_beat_data;
  logic                           wr_beat_valid;

  // Backdoor writes a 32-bit word into the lower/upper half of a 64-bit beat.
  // It takes precedence over compute writes in the same cycle.
  logic [gbp_pkg::BEAT_BITS-1:0] backdoor_beat_data;
  logic [gbp_pkg::WSTRB_W-1:0]   backdoor_beat_wstrb;

  always_comb begin
    if (spm_backdoor_wr_valid_i) begin
      backdoor_beat_data = fake_mem[spm_backdoor_wr_addr_i[MEM_DEPTH_LG2-1:1]];
      backdoor_beat_wstrb = '0;
      if (spm_backdoor_wr_addr_i[0]) begin
        backdoor_beat_data[63:32] = spm_backdoor_wr_data_i;
        backdoor_beat_wstrb[7:4]  = 4'b1111;
      end else begin
        backdoor_beat_data[31:0]  = spm_backdoor_wr_data_i;
        backdoor_beat_wstrb[3:0]  = 4'b1111;
      end
      wr_beat_addr  = spm_backdoor_wr_addr_i[MEM_DEPTH_LG2-1:1];
      wr_beat_data  = backdoor_beat_data;
      wr_beat_valid = 1'b1;
    end else begin
      wr_beat_addr  = spm_wr_addr[MEM_DEPTH_LG2-1:1];
      wr_beat_data  = spm_wr_data;
      wr_beat_valid = spm_wr_valid;
    end
  end

  always_ff @(posedge clk) begin
    if (wr_beat_valid) begin
      for (int i = 0; i < gbp_pkg::WSTRB_W; i++) begin
        if (spm_backdoor_wr_valid_i ? backdoor_beat_wstrb[i] : spm_wr_wstrb[i]) begin
          fake_mem[wr_beat_addr][8*i +: 8] <= wr_beat_data[8*i +: 8];
        end
      end
    end
  end

  assign spm_backdoor_rd_data_o = fake_mem[spm_backdoor_rd_addr_i[MEM_DEPTH_LG2-1:1]];

endmodule
