module gbp_pe_noc_bridge
  import gbp_pkg::*;
  #(
    parameter int unsigned data_width_p = 32,
    parameter int unsigned addr_width_p = 16
  )
  (
    input  logic clk_i,
    input  logic reset_i,

    input  logic [data_width_p-1:0] core_req_data_i,
    input  logic [addr_width_p-1:0] core_req_addr_i,
    input  logic core_req_we_i,
    input  logic core_req_v_i,
    output logic core_req_yumi_o,

    output logic [data_width_p-1:0] core_rsp_data_o,
    output logic core_rsp_v_o,

    output logic sideband_cmd_valid_o,
    output logic [1:0] sideband_cmd_kind_o,
    output logic [TXN_ID_W-1:0] sideband_cmd_txn_id_o,
    input  logic sideband_cmd_ready_i,
    input  logic sideband_rsp_done_i,
    input  logic sideband_rsp_error_i,

    output logic ingress_intent_v_o,
    output logic [addr_width_p-1:0] ingress_intent_addr_o,
    output logic [data_width_p-1:0] ingress_intent_data_o,
    output logic [GBP_INGRESS_BANK_W-1:0] ingress_intent_bank_o,
    output logic [3:0] ingress_intent_qid_o,
    input  logic ingress_intent_ready_i,

    output logic decode_error_o
  );

  localparam int unsigned row_bytes_lg_lp = GBP_INGRESS_ROW_BYTES_LG;
  localparam int unsigned bank_lsb_lp = row_bytes_lg_lp;
  localparam int unsigned qid_lsb_lp = 8;

  localparam logic [2:0] STATUS_FIELD_LP = GBP_MMIO_FIELD_Q_BASE_ADDR;
  localparam logic [2:0] LAST_ADDR_FIELD_LP = GBP_MMIO_FIELD_Q_DEPTH;
  localparam logic [2:0] LAST_CMD_FIELD_LP = GBP_MMIO_FIELD_Q_HEAD;

  typedef enum logic [1:0] {
    ORD_IDLE = 2'b00,
    ORD_PAYLOAD_WRITTEN = 2'b01,
    ORD_TAIL_WRITTEN = 2'b10
  } ordering_state_e;

  logic [3:0] status_bits_r;
  logic [addr_width_p-1:0] last_decode_addr_r;
  logic [1:0] last_cmd_kind_r;
  logic [TXN_ID_W-1:0] last_cmd_txn_id_r;
  ordering_state_e ordering_state_r;
  logic [addr_width_p-1:0] pending_payload_addr_r;
  logic [data_width_p-1:0] pending_payload_data_r;
  logic [GBP_INGRESS_BANK_W-1:0] pending_payload_bank_r;
  logic [3:0] pending_payload_qid_r;

  logic core_rsp_v_n;
  logic [data_width_p-1:0] core_rsp_data_n;
  logic [3:0] status_bits_n;
  logic [addr_width_p-1:0] last_decode_addr_n;
  logic [1:0] last_cmd_kind_n;
  logic [TXN_ID_W-1:0] last_cmd_txn_id_n;
  ordering_state_e ordering_state_n;
  logic [addr_width_p-1:0] pending_payload_addr_n;
  logic [data_width_p-1:0] pending_payload_data_n;
  logic [GBP_INGRESS_BANK_W-1:0] pending_payload_bank_n;
  logic [3:0] pending_payload_qid_n;

  logic [GBP_INGRESS_BANK_W-1:0] bank_id;
  logic [2:0] mmio_field;
  logic [3:0] qid;
  logic is_mmio_bank;
  logic is_payload_bank;
  logic decode_error_n;
  logic cmd_launch;
  logic req_blocked;
  logic req_accepted;
  logic ordering_doorbell_match;

  always_comb begin
    bank_id = core_req_addr_i[bank_lsb_lp +: GBP_INGRESS_BANK_W];
    mmio_field = core_req_addr_i[4:2];
    qid = core_req_addr_i[qid_lsb_lp +: 4];
    is_mmio_bank = (bank_id == GBP_INGRESS_MMIO_BANK_B0);
    is_payload_bank = bank_id[GBP_INGRESS_BANK_W-1];

    ordering_doorbell_match = core_req_we_i
                              & is_mmio_bank
                              & (mmio_field == GBP_MMIO_FIELD_Q_EPOCH_DOORBELL)
                              & core_req_data_i[0]
                              & (ordering_state_r == ORD_TAIL_WRITTEN)
                              & (pending_payload_qid_r == qid);

    req_blocked = core_req_v_i & ordering_doorbell_match & ~ingress_intent_ready_i;
    core_req_yumi_o = core_req_v_i & ~req_blocked;
    req_accepted = core_req_v_i & core_req_yumi_o;

    sideband_cmd_valid_o = 1'b0;
    sideband_cmd_kind_o = core_req_data_i[2:1];
    sideband_cmd_txn_id_o = core_req_data_i[15:8];

    ingress_intent_v_o = 1'b0;
    ingress_intent_addr_o = core_req_addr_i;
    ingress_intent_data_o = core_req_data_i;
    ingress_intent_bank_o = bank_id;
    ingress_intent_qid_o = qid;

    core_rsp_v_n = 1'b0;
    core_rsp_data_n = '0;
    decode_error_n = 1'b0;

    status_bits_n = status_bits_r;
    last_decode_addr_n = last_decode_addr_r;
    last_cmd_kind_n = last_cmd_kind_r;
    last_cmd_txn_id_n = last_cmd_txn_id_r;
    ordering_state_n = ordering_state_r;
    pending_payload_addr_n = pending_payload_addr_r;
    pending_payload_data_n = pending_payload_data_r;
    pending_payload_bank_n = pending_payload_bank_r;
    pending_payload_qid_n = pending_payload_qid_r;

    cmd_launch = 1'b0;

    if (req_accepted) begin
      if (core_req_we_i) begin
        if (is_mmio_bank) begin
          if (mmio_field == GBP_MMIO_FIELD_Q_TAIL) begin
            if ((ordering_state_r == ORD_PAYLOAD_WRITTEN) && (pending_payload_qid_r == qid)) begin
              ordering_state_n = ORD_TAIL_WRITTEN;
            end else begin
              decode_error_n = 1'b1;
              last_decode_addr_n = core_req_addr_i;
            end
          end else if ((mmio_field == GBP_MMIO_FIELD_Q_EPOCH_DOORBELL) && core_req_data_i[0]) begin
            if (ordering_doorbell_match) begin
              if (sideband_cmd_ready_i && ingress_intent_ready_i) begin
                sideband_cmd_valid_o = 1'b1;
                cmd_launch = 1'b1;
                ingress_intent_v_o = 1'b1;
                ingress_intent_addr_o = pending_payload_addr_r;
                ingress_intent_data_o = pending_payload_data_r;
                ingress_intent_bank_o = pending_payload_bank_r;
                ingress_intent_qid_o = pending_payload_qid_r;
                ordering_state_n = ORD_IDLE;
              end else begin
                decode_error_n = 1'b1;
                last_decode_addr_n = core_req_addr_i;
              end
            end else begin
              decode_error_n = 1'b1;
              last_decode_addr_n = core_req_addr_i;
            end
          end
        end else if (is_payload_bank) begin
          if (ordering_state_r == ORD_IDLE) begin
            pending_payload_addr_n = core_req_addr_i;
            pending_payload_data_n = core_req_data_i;
            pending_payload_bank_n = bank_id;
            pending_payload_qid_n = qid;
            ordering_state_n = ORD_PAYLOAD_WRITTEN;
          end else begin
            decode_error_n = 1'b1;
            last_decode_addr_n = core_req_addr_i;
          end
        end else begin
          decode_error_n = 1'b1;
          last_decode_addr_n = core_req_addr_i;
        end
      end else begin
        core_rsp_v_n = 1'b1;
        if (is_mmio_bank) begin
          unique case (mmio_field)
            STATUS_FIELD_LP: core_rsp_data_n = {{(data_width_p-4){1'b0}}, status_bits_r};
            LAST_ADDR_FIELD_LP: core_rsp_data_n = {{(data_width_p-addr_width_p){1'b0}}, last_decode_addr_r};
            LAST_CMD_FIELD_LP: core_rsp_data_n = {{(data_width_p-(TXN_ID_W+2)){1'b0}}, last_cmd_txn_id_r, last_cmd_kind_r};
            default: begin
              core_rsp_data_n = '0;
              decode_error_n = 1'b1;
              last_decode_addr_n = core_req_addr_i;
            end
          endcase
        end else begin
          core_rsp_data_n = '0;
          decode_error_n = 1'b1;
          last_decode_addr_n = core_req_addr_i;
        end
      end
    end

    if (cmd_launch) begin
      status_bits_n[0] = 1'b1;
      last_cmd_kind_n = sideband_cmd_kind_o;
      last_cmd_txn_id_n = sideband_cmd_txn_id_o;
    end
    if (sideband_rsp_done_i) begin
      status_bits_n[1] = 1'b1;
    end
    if (sideband_rsp_error_i) begin
      status_bits_n[2] = 1'b1;
    end
    if (decode_error_n) begin
      status_bits_n[3] = 1'b1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      core_rsp_v_o <= 1'b0;
      core_rsp_data_o <= '0;
      status_bits_r <= '0;
      last_decode_addr_r <= '0;
      last_cmd_kind_r <= '0;
      last_cmd_txn_id_r <= '0;
      ordering_state_r <= ORD_IDLE;
      pending_payload_addr_r <= '0;
      pending_payload_data_r <= '0;
      pending_payload_bank_r <= '0;
      pending_payload_qid_r <= '0;
    end else begin
      core_rsp_v_o <= core_rsp_v_n;
      core_rsp_data_o <= core_rsp_data_n;
      status_bits_r <= status_bits_n;
      last_decode_addr_r <= last_decode_addr_n;
      last_cmd_kind_r <= last_cmd_kind_n;
      last_cmd_txn_id_r <= last_cmd_txn_id_n;
      ordering_state_r <= ordering_state_n;
      pending_payload_addr_r <= pending_payload_addr_n;
      pending_payload_data_r <= pending_payload_data_n;
      pending_payload_bank_r <= pending_payload_bank_n;
      pending_payload_qid_r <= pending_payload_qid_n;
    end
  end

  assign decode_error_o = status_bits_r[3];

endmodule
