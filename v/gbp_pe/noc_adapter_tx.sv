// noc_adapter_tx.sv
// GBP PE NoC Adapter - TX Path
// Round-robin arbiter between 3 TX sources:
//   0: NOTIFICATION (single packet)
//   1: FETCH_REQUEST (single packet, Pull Client sends 3x for full request)
//   2: FETCH_RESPONSE (FSM: metadata → data×N → done, does not release arbiter mid-response)

`include "bsg_manycore_defines.svh"

module noc_adapter_tx
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    parameter int data_width_p     = 32
    , parameter int addr_width_p   = 16
    , parameter int x_cord_width_p = 6
    , parameter int y_cord_width_p = 5
    , parameter int NODE_ID_W      = gbp_pkg::NODE_ID_W
    , parameter int STATE_WORDS_W  = gbp_pkg::STATE_WORDS_W
    , parameter int TXN_ID_W       = 8
    , parameter int unsigned GBP_BASE_ADDR = 32'h0000_1000

    , localparam packet_width_lp =
        `bsg_manycore_packet_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
) (
    input  logic clk_i
    , input  logic rst_n_i

    // ── Manycore coordinates ──
    , input  logic [x_cord_width_p-1:0] my_x_i
    , input  logic [y_cord_width_p-1:0] my_y_i

    // ── Notification TX ──
    , input  logic                 tx_notif_valid_i
    , output logic                 tx_notif_ready_o
    , input  logic [NODE_ID_W-1:0] tx_notif_source_node_id_i
    , input  logic [NODE_ID_W-1:0] tx_notif_target_node_id_i
    , input  logic                 tx_notif_is_factor_i
    , input  logic [x_cord_width_p-1:0] tx_notif_target_x_i
    , input  logic [y_cord_width_p-1:0] tx_notif_target_y_i

    // ── Fetch request TX ──
    , input  logic                 tx_fetch_req_valid_i
    , output logic                 tx_fetch_req_ready_o
    , input  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_i
    , input  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_i
    , input  logic                 tx_fetch_req_is_factor_i
    , input  logic [x_cord_width_p-1:0] tx_fetch_req_target_x_i
    , input  logic [y_cord_width_p-1:0] tx_fetch_req_target_y_i
    , input  logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_i
    , input  logic [1:0]           tx_fetch_req_store_idx_i  // 0/1/2

    // ── Fetch response TX ──
    , input  logic                     tx_fetch_resp_valid_i
    , output logic                     tx_fetch_resp_ready_o
    , input  logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id_i
    , input  logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id_i
    , input  logic                     tx_fetch_resp_is_factor_i
    , input  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words_i
    , input  logic [data_width_p-1:0]  tx_fetch_resp_data_i
    , input  logic                     tx_fetch_resp_data_valid_i
    , input  logic                     tx_fetch_resp_last_i
    , input  logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id_i

    // ── To endpoint ──
    , output logic [packet_width_lp-1:0] out_packet_o
    , output logic                       out_v_o
    , input  logic                       out_credit_or_ready_i

    // ── Status ──
    , output logic tx_busy_o
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  // ── Mailbox offsets ──
  localparam int unsigned MBX_NOTIFICATION  = 'h00;
  localparam int unsigned MBX_FETCH_REQ_0   = 'h04;
  localparam int unsigned MBX_FETCH_REQ_1   = 'h08;
  localparam int unsigned MBX_FETCH_REQ_2   = 'h0C;
  localparam int unsigned MBX_RESP_META     = 'h10;
  localparam int unsigned MBX_RESP_DATA     = 'h14;
  localparam int unsigned MBX_RESP_DONE     = 'h18;

  // ── Response TX FSM ──
  typedef enum logic [1:0] {
    RESP_IDLE
    ,RESP_META
    ,RESP_DATA
    ,RESP_DONE
  } resp_state_e;

  resp_state_e resp_state_r, resp_state_n;
  logic [STATE_WORDS_W-1:0] resp_data_cnt_r, resp_data_cnt_n;
  logic [STATE_WORDS_W-1:0] resp_state_words_r, resp_state_words_n;
  logic [NODE_ID_W-1:0]     resp_node_id_r, resp_node_id_n;
  logic [NODE_ID_W-1:0]     resp_consumer_id_r, resp_consumer_id_n;
  logic                     resp_is_factor_r, resp_is_factor_n;
  logic [TXN_ID_W-1:0]      resp_txn_id_r, resp_txn_id_n;
  logic [data_width_p-1:0]  resp_data_r, resp_data_n;

  // ── Round-robin arbiter ──
  logic [1:0] rr_ptr_r, rr_ptr_n;

  // ── Source signals ──
  logic src_valid [3];
  logic src_grant [3];

  assign src_valid[0] = tx_notif_valid_i;
  assign src_valid[1] = tx_fetch_req_valid_i;
  assign src_valid[2] = (resp_state_r != RESP_IDLE) || tx_fetch_resp_valid_i;

  // Round-robin selection
  logic [1:0] grant_idx;
  logic       grant_valid;

  always_comb begin
    grant_valid = 1'b0;
    grant_idx = '0;
    // Check sources starting from rr_ptr, wrap around
    for (int i = 0; i < 3; i++) begin
      int idx;
      idx = rr_ptr_r + i;
      if (idx >= 3) idx = idx - 3;
      if (src_valid[idx] && !grant_valid) begin
        grant_valid = 1'b1;
        grant_idx = idx[1:0];
      end
    end
  end

  assign src_grant[0] = grant_valid && (grant_idx == 2'd0);
  assign src_grant[1] = grant_valid && (grant_idx == 2'd1);
  assign src_grant[2] = grant_valid && (grant_idx == 2'd2);

  // Ready signals
  assign tx_notif_ready_o     = src_grant[0] && out_credit_or_ready_i;
  assign tx_fetch_req_ready_o = src_grant[1] && out_credit_or_ready_i;
  assign tx_fetch_resp_ready_o = tx_fetch_resp_valid_i && (resp_state_r == RESP_IDLE)
                                 && !src_valid[0] && !src_valid[1];
  // Response is accepted when: resp FSM is idle, resp valid, and no higher-priority source waiting
  // (response gets lowest priority to start, but once started it runs to completion)

  assign out_v_o = grant_valid && out_credit_or_ready_i;
  assign tx_busy_o = grant_valid;

  // ── Packet formation ──
  `declare_bsg_manycore_packet_s(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p);
  bsg_manycore_packet_s packet_cast;

  // Default: zero
  always_comb begin
    packet_cast = '0;
    packet_cast.src_x_cord = my_x_i;
    packet_cast.src_y_cord = my_y_i;

    // Response FSM drives its own packet
    if (src_grant[2]) begin
      case (resp_state_r)
        RESP_IDLE: begin
          // Latch response info and send metadata
          packet_cast.x_cord = tx_fetch_resp_consumer_node_id_i[x_cord_width_p-1:0]; // placeholder
          packet_cast.y_cord = tx_fetch_resp_consumer_node_id_i[y_cord_width_p-1:0]; // placeholder
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_RESP_META);
          packet_cast.op_v2 = e_remote_sw;
          packet_cast.payload = data_width_p'({tx_fetch_resp_is_factor_i, tx_fetch_resp_state_words_i});
        end
        RESP_META: begin
          packet_cast.x_cord = resp_consumer_id_r[x_cord_width_p-1:0];
          packet_cast.y_cord = resp_consumer_id_r[y_cord_width_p-1:0];
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_RESP_META);
          packet_cast.op_v2 = e_remote_sw;
          packet_cast.payload = data_width_p'({resp_is_factor_r, resp_state_words_r});
        end
        RESP_DATA: begin
          packet_cast.x_cord = resp_consumer_id_r[x_cord_width_p-1:0];
          packet_cast.y_cord = resp_consumer_id_r[y_cord_width_p-1:0];
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_RESP_DATA);
          packet_cast.op_v2 = e_remote_sw;
          packet_cast.payload = resp_data_r;
        end
        RESP_DONE: begin
          packet_cast.x_cord = resp_consumer_id_r[x_cord_width_p-1:0];
          packet_cast.y_cord = resp_consumer_id_r[y_cord_width_p-1:0];
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_RESP_DONE);
          packet_cast.op_v2 = e_remote_sw;
          // Payload layout: {txn_id, unused[5:0], consumer_node_id, node_id}
          // Matches noc_adapter_rx extraction:
          //   txn_id    = data[31:26]
          //   consumer  = data[19:10]
          //   node_id   = data[9:0]
          packet_cast.payload = {resp_txn_id_r, 6'b0, resp_consumer_id_r, resp_node_id_r};
        end
        default: ;
      endcase
    end
    // Notification
    else if (src_grant[0]) begin
      packet_cast.x_cord = tx_notif_target_x_i;
      packet_cast.y_cord = tx_notif_target_y_i;
      packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_NOTIFICATION);
      packet_cast.op_v2 = e_remote_sw;
      packet_cast.payload = data_width_p'({tx_notif_is_factor_i, tx_notif_source_node_id_i});
    end
    // Fetch request: Pull Client sends 3 pulses with store_idx = 0, 1, 2
    else if (src_grant[1]) begin
      packet_cast.x_cord = tx_fetch_req_target_x_i;
      packet_cast.y_cord = tx_fetch_req_target_y_i;
      packet_cast.op_v2 = e_remote_sw;
      case (tx_fetch_req_store_idx_i)
        2'd0: begin
          // Store 0: {is_factor, consumer_node_id} → MBX_FETCH_REQ_0
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_FETCH_REQ_0);
          packet_cast.payload = data_width_p'({tx_fetch_req_is_factor_i, tx_fetch_req_consumer_node_id_i});
        end
        2'd1: begin
          // Store 1: {target_node_id} → MBX_FETCH_REQ_1
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_FETCH_REQ_1);
          packet_cast.payload = data_width_p'(tx_fetch_req_target_node_id_i);
        end
        2'd2: begin
          // Store 2: {txn_id} → MBX_FETCH_REQ_2
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_FETCH_REQ_2);
          packet_cast.payload = data_width_p'(tx_fetch_req_txn_id_i);
        end
        default: begin
          packet_cast.addr = addr_width_p'(GBP_BASE_ADDR + MBX_FETCH_REQ_0);
          packet_cast.payload = '0;
        end
      endcase
    end
  end

  assign out_packet_o = packet_width_lp'(packet_cast);

  // ── Response FSM ──
  always_comb begin
    resp_state_n = resp_state_r;
    resp_data_cnt_n = resp_data_cnt_r;
    resp_state_words_n = resp_state_words_r;
    resp_node_id_n = resp_node_id_r;
    resp_consumer_id_n = resp_consumer_id_r;
    resp_is_factor_n = resp_is_factor_r;
    resp_txn_id_n = resp_txn_id_r;
    resp_data_n = resp_data_r;

    case (resp_state_r)
      RESP_IDLE: begin
        if (tx_fetch_resp_valid_i && out_credit_or_ready_i && src_grant[2]) begin
          // Latch response info
          resp_state_words_n = tx_fetch_resp_state_words_i;
          resp_node_id_n = tx_fetch_resp_node_id_i;
          resp_consumer_id_n = tx_fetch_resp_consumer_node_id_i;
          resp_is_factor_n = tx_fetch_resp_is_factor_i;
          resp_txn_id_n = tx_fetch_resp_txn_id_i;
          resp_data_cnt_n = '0;
          if (tx_fetch_resp_state_words_i == '0) begin
            // No data words, go directly to DONE
            resp_state_n = RESP_DONE;
          end else begin
            resp_state_n = RESP_DATA;
            resp_data_n = tx_fetch_resp_data_i;
          end
        end
      end

      RESP_META: begin
        // Metadata already sent in IDLE→DATA transition
        // This state is unused in current flow (metadata sent from IDLE)
        resp_state_n = RESP_DATA;
      end

      RESP_DATA: begin
        if (out_credit_or_ready_i && src_grant[2]) begin
          resp_data_cnt_n = resp_data_cnt_r + 1;
          if (resp_data_cnt_r == resp_state_words_r - 1) begin
            // Last data word sent, next is DONE
            resp_state_n = RESP_DONE;
          end
          // Latch next data word from input
          resp_data_n = tx_fetch_resp_data_i;
        end
      end

      RESP_DONE: begin
        if (out_credit_or_ready_i && src_grant[2]) begin
          resp_state_n = RESP_IDLE;
        end
      end

      default: resp_state_n = RESP_IDLE;
    endcase
  end

  // ── Round-robin pointer update ──
  always_comb begin
    rr_ptr_n = rr_ptr_r;
    if (out_v_o && out_credit_or_ready_i) begin
      // Advance pointer after successful send
      if (grant_idx == 2'd2) begin
        rr_ptr_n = 2'd0;  // Wrap around
      end else begin
        rr_ptr_n = grant_idx + 2'd1;
      end
      // Don't advance during response FSM (it holds the channel)
      if (src_grant[2] && resp_state_r != RESP_DONE) begin
        rr_ptr_n = rr_ptr_r;  // Keep current pointer
      end
    end
  end

  // ── Registers ──
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      resp_state_r <= RESP_IDLE;
      resp_data_cnt_r <= '0;
      resp_state_words_r <= '0;
      resp_node_id_r <= '0;
      resp_consumer_id_r <= '0;
      resp_is_factor_r <= 1'b0;
      resp_txn_id_r <= '0;
      resp_data_r <= '0;
      rr_ptr_r <= '0;
    end else begin
      resp_state_r <= resp_state_n;
      resp_data_cnt_r <= resp_data_cnt_n;
      resp_state_words_r <= resp_state_words_n;
      resp_node_id_r <= resp_node_id_n;
      resp_consumer_id_r <= resp_consumer_id_n;
      resp_is_factor_r <= resp_is_factor_n;
      resp_txn_id_r <= resp_txn_id_n;
      resp_data_r <= resp_data_n;
      rr_ptr_r <= rr_ptr_n;
    end
  end

endmodule
