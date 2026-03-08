module gbp_pe_noc_bridge_top
  import gbp_pkg::*;
(
  input  logic        clk,
  input  logic        rst_n,

  input  logic        req_v,
  input  logic        req_we,
  input  logic [15:0] req_addr,
  input  logic [31:0] req_data,
  input  logic        ingress_ready_block,
  output logic        req_yumi,

  output logic        rsp_v,
  output logic [31:0] rsp_data,

  output logic        sideband_cmd_valid_observe,
  output logic        sideband_cmd_seen_observe,
  output logic [1:0]  sideband_cmd_kind_observe,
  output logic [7:0]  sideband_cmd_txn_observe,
  output logic        ingress_intent_v_observe,
  output logic        ingress_intent_seen_observe,
  output logic        decode_error_observe,

  output logic [15:0] contract_mmio_cmd_addr,
  output logic [15:0] contract_mmio_status_addr,
  output logic [15:0] contract_invalid_addr
);

  localparam logic [15:0] mmio_base_addr_lp =
      16'(GBP_INGRESS_MMIO_BANK_B0 << GBP_INGRESS_ROW_BYTES_LG);
  localparam logic [15:0] invalid_class_addr_lp =
      16'(GBP_INGRESS_FWD_BANK_B1 << GBP_INGRESS_ROW_BYTES_LG);

  logic reset_i;
  assign reset_i = ~rst_n;

  logic sideband_cmd_ready_li;
  logic sideband_rsp_done_li;
  logic sideband_rsp_error_li;

  logic sideband_cmd_valid_lo;
  logic ingress_intent_v_lo;

  logic rsp_pending_r;
  logic sideband_cmd_seen_r;
  logic ingress_intent_seen_r;

  gbp_pe_noc_bridge
    #(
      .data_width_p(32)
      ,.addr_width_p(16)
    ) bridge
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.core_req_data_i(req_data)
      ,.core_req_addr_i(req_addr)
      ,.core_req_we_i(req_we)
      ,.core_req_v_i(req_v)
      ,.core_req_yumi_o(req_yumi)
      ,.core_rsp_data_o(rsp_data)
      ,.core_rsp_v_o(rsp_v)
      ,.sideband_cmd_valid_o(sideband_cmd_valid_lo)
      ,.sideband_cmd_kind_o(sideband_cmd_kind_observe)
      ,.sideband_cmd_txn_id_o(sideband_cmd_txn_observe)
      ,.sideband_cmd_ready_i(sideband_cmd_ready_li)
      ,.sideband_rsp_done_i(sideband_rsp_done_li)
      ,.sideband_rsp_error_i(sideband_rsp_error_li)
       ,.ingress_intent_v_o(ingress_intent_v_lo)
      ,.ingress_intent_addr_o()
      ,.ingress_intent_data_o()
      ,.ingress_intent_bank_o()
      ,.ingress_intent_qid_o()
       ,.ingress_intent_ready_i(~ingress_ready_block)
       ,.decode_error_o(decode_error_observe)
     );

  always_ff @(posedge clk) begin
    if (reset_i) begin
      rsp_pending_r <= 1'b0;
      sideband_cmd_seen_r <= 1'b0;
      ingress_intent_seen_r <= 1'b0;
    end else begin
      rsp_pending_r <= sideband_cmd_valid_lo & sideband_cmd_ready_li;
      if (sideband_cmd_valid_lo) begin
        sideband_cmd_seen_r <= 1'b1;
      end
      if (ingress_intent_v_lo) begin
        ingress_intent_seen_r <= 1'b1;
      end
    end
  end

  assign sideband_cmd_ready_li = 1'b1;
  assign sideband_rsp_done_li = rsp_pending_r;
  assign sideband_rsp_error_li = 1'b0;

  assign sideband_cmd_valid_observe = sideband_cmd_valid_lo;
  assign sideband_cmd_seen_observe = sideband_cmd_seen_r;
  assign ingress_intent_v_observe = ingress_intent_v_lo;
  assign ingress_intent_seen_observe = ingress_intent_seen_r;

  assign contract_mmio_cmd_addr = mmio_base_addr_lp +
      (16'(GBP_MMIO_FIELD_Q_EPOCH_DOORBELL) << 2);
  assign contract_mmio_status_addr = mmio_base_addr_lp +
      (16'(GBP_MMIO_FIELD_Q_BASE_ADDR) << 2);
  assign contract_invalid_addr = invalid_class_addr_lp;

endmodule
