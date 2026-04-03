`include "bsg_manycore_defines.svh"

package gbp_pe_endpoint_adapter_pkg;
  localparam int unsigned ROW_BYTES_LG = 5;
  localparam int unsigned BANK_ID_W = 3;

  localparam int unsigned B0 = 3'd0;
  localparam int unsigned B4 = 3'd4;
  localparam int unsigned B5 = 3'd5;
  localparam int unsigned B6 = 3'd6;
  localparam int unsigned B7 = 3'd7;

  localparam int unsigned QUEUE_NUM_LP = 16;
  localparam int unsigned QUEUE_ID_W_LP = 4;
  localparam int unsigned PAYLOAD_ROWS_LP = 256;
  localparam int unsigned PAYLOAD_ROW_W_LP = 8;
endpackage

module gbp_pe_endpoint_adapter
  import bsg_manycore_pkg::*;
  import gbp_pe_endpoint_adapter_pkg::*;
  #(
     parameter int x_cord_width_p = "inv"
     , parameter int y_cord_width_p = "inv"
     , parameter int data_width_p = "inv"
     , parameter int addr_width_p = "inv"
     , parameter int fifo_els_p = 4
     , parameter int rev_fifo_els_p = 4
     , parameter int credit_counter_width_p = 32
     , parameter bit forward_local_writes_p = 1'b0
   )
  (
    input  logic clk_i
    , input logic reset_i

    , input  logic [`bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)-1:0] link_sif_i
    , output logic [`bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)-1:0] link_sif_o

    , input logic [x_cord_width_p-1:0] global_x_i
    , input logic [y_cord_width_p-1:0] global_y_i

    , input  logic [`bsg_manycore_packet_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)-1:0] out_packet_i
    , input  logic out_v_i
    , output logic out_credit_or_ready_o

    , output logic [data_width_p-1:0] returned_data_r_o
    , output logic [bsg_manycore_reg_id_width_gp-1:0] returned_reg_id_r_o
    , output logic returned_v_r_o
    , output bsg_manycore_return_packet_type_e returned_pkt_type_r_o
    , input  logic returned_yumi_i
    , output logic returned_fifo_full_o
    , output logic returned_credit_v_r_o
    , output logic [bsg_manycore_reg_id_width_gp-1:0] returned_credit_reg_id_r_o
    , output logic [credit_counter_width_p-1:0] out_credits_used_o

    , output logic [data_width_p-1:0] core_req_data_o
    , output logic [addr_width_p-1:0] core_req_addr_o
    , output logic core_req_we_o
    , output logic core_req_v_o
    , input  logic core_req_yumi_i
    , input  logic [data_width_p-1:0] core_rsp_data_i
    , input  logic core_rsp_v_i
  );

  logic                            in_v_lo;
  logic [data_width_p-1:0]         in_data_lo;
  logic [(data_width_p>>3)-1:0]    in_mask_lo;
  logic [addr_width_p-1:0]         in_addr_lo;
  logic                            in_we_lo;
  bsg_manycore_load_info_s         in_load_info_lo;
  logic [x_cord_width_p-1:0]       in_src_x_lo;
  logic [y_cord_width_p-1:0]       in_src_y_lo;
  logic                            in_yumi_li;

  logic [data_width_p-1:0]         returning_data_li;
  logic                            returning_v_li;

  bsg_manycore_endpoint_standard
    #(
      .x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
      ,.fifo_els_p(fifo_els_p)
      ,.data_width_p(data_width_p)
      ,.addr_width_p(addr_width_p)
      ,.credit_counter_width_p(credit_counter_width_p)
      ,.rev_fifo_els_p(rev_fifo_els_p)
    ) ep_std
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.link_sif_i(link_sif_i)
      ,.link_sif_o(link_sif_o)

      ,.in_v_o(in_v_lo)
      ,.in_data_o(in_data_lo)
      ,.in_mask_o(in_mask_lo)
      ,.in_addr_o(in_addr_lo)
      ,.in_we_o(in_we_lo)
      ,.in_load_info_o(in_load_info_lo)
      ,.in_src_x_cord_o(in_src_x_lo)
      ,.in_src_y_cord_o(in_src_y_lo)
      ,.in_yumi_i(in_yumi_li)

      ,.returning_data_i(returning_data_li)
      ,.returning_v_i(returning_v_li)

      ,.out_v_i(out_v_i)
      ,.out_packet_i(out_packet_i)
      ,.out_credit_or_ready_o(out_credit_or_ready_o)

      ,.returned_data_r_o(returned_data_r_o)
      ,.returned_reg_id_r_o(returned_reg_id_r_o)
      ,.returned_v_r_o(returned_v_r_o)
      ,.returned_pkt_type_r_o(returned_pkt_type_r_o)
      ,.returned_yumi_i(returned_yumi_i)
      ,.returned_fifo_full_o(returned_fifo_full_o)
      ,.returned_credit_v_r_o(returned_credit_v_r_o)
      ,.returned_credit_reg_id_r_o(returned_credit_reg_id_r_o)
      ,.out_credits_used_o(out_credits_used_o)

      ,.global_x_i(global_x_i)
      ,.global_y_i(global_y_i)
    );

  logic [data_width_p-1:0] q_base_addr_r    [0:QUEUE_NUM_LP-1];
  logic [7:0]              q_depth_r        [0:QUEUE_NUM_LP-1];
  logic [7:0]              q_head_r         [0:QUEUE_NUM_LP-1];
  logic [7:0]              q_tail_r         [0:QUEUE_NUM_LP-1];
  logic [7:0]              q_credit_r       [0:QUEUE_NUM_LP-1];
  logic                    q_doorbell_r     [0:QUEUE_NUM_LP-1];
  logic [2:0]              q_epoch_r        [0:QUEUE_NUM_LP-1];

  logic [data_width_p-1:0] payload_mem_r [0:3][0:PAYLOAD_ROWS_LP-1];

  typedef enum logic [1:0] {
    ORD_IDLE = 2'b00,
    ORD_PAYLOAD_WRITTEN = 2'b01,
    ORD_TAIL_WRITTEN = 2'b10
  } ordering_state_e;

  ordering_state_e ordering_state_r;
  logic                    ordering_pending_r;
  logic                    ownership_err_r;

  wire [BANK_ID_W-1:0] bank_id_w = in_addr_lo[ROW_BYTES_LG +: BANK_ID_W];
  wire is_b0_w = (bank_id_w == B0);
  wire is_payload_w = (bank_id_w == B4) || (bank_id_w == B5) || (bank_id_w == B6) || (bank_id_w == B7);
  wire [QUEUE_ID_W_LP-1:0] qid_w = in_addr_lo[8 +: QUEUE_ID_W_LP];
  wire [2:0] field_w = in_addr_lo[4:2];
  wire [PAYLOAD_ROW_W_LP-1:0] row_w = in_addr_lo[(ROW_BYTES_LG+BANK_ID_W) +: PAYLOAD_ROW_W_LP];
  wire [1:0] plane_w = bank_id_w[1:0];

  logic [data_width_p-1:0] read_data_n;
  logic read_v_n;
  logic write_v_n;
  logic [data_width_p-1:0] payload_write_data;

  always_comb begin
    in_yumi_li = 1'b0;
    read_data_n = '0;
    read_v_n = 1'b0;
    write_v_n = 1'b0;

    core_req_v_o = 1'b0;
    core_req_we_o = in_we_lo;
    core_req_addr_o = in_addr_lo;
    core_req_data_o = in_data_lo;

    if (in_v_lo & ~in_we_lo) begin
      if (is_b0_w) begin
        read_v_n = 1'b1;
        unique case (field_w)
          3'd0: read_data_n = q_base_addr_r[qid_w];
          3'd1: read_data_n = {{(data_width_p-8){1'b0}}, q_depth_r[qid_w]};
          3'd2: read_data_n = {{(data_width_p-8){1'b0}}, q_head_r[qid_w]};
          3'd3: read_data_n = {{(data_width_p-8){1'b0}}, q_tail_r[qid_w]};
          3'd4: read_data_n = {{(data_width_p-8){1'b0}}, q_credit_r[qid_w]};
          3'd5: read_data_n = {{(data_width_p-4){1'b0}}, q_epoch_r[qid_w], q_doorbell_r[qid_w]};
          default: read_data_n = '0;
        endcase
      end
      else if (is_payload_w) begin
        read_v_n = 1'b1;
        read_data_n = payload_mem_r[plane_w][row_w];
      end
      else begin
        core_req_v_o = 1'b1;
        read_v_n = core_req_yumi_i;
        if (core_req_yumi_i & core_rsp_v_i) begin
          read_data_n = core_rsp_data_i;
        end
      end
    end
    else if (in_v_lo & in_we_lo) begin
      if (forward_local_writes_p || (~is_b0_w & ~is_payload_w)) begin
        core_req_v_o = 1'b1;
        write_v_n = core_req_yumi_i;
      end
      else begin
        write_v_n = 1'b1;
      end
    end

    in_yumi_li = in_v_lo & (read_v_n | write_v_n);
  end

  always_comb begin
    integer bi;
    payload_write_data = '0;
    for (bi = 0; bi < (data_width_p>>3); bi++) begin
      if (in_mask_lo[bi]) begin
        payload_write_data[(bi*8) +: 8] = in_data_lo[(bi*8) +: 8];
      end
    end
  end

  always_ff @(posedge clk_i) begin
    integer i;
    if (reset_i) begin
      ordering_state_r <= ORD_IDLE;
      ordering_pending_r <= 1'b0;
      ownership_err_r <= 1'b0;
      returning_v_li <= 1'b0;
      returning_data_li <= '0;
      for (i = 0; i < QUEUE_NUM_LP; i++) begin
        q_base_addr_r[i] <= '0;
        q_depth_r[i] <= 8'd0;
        q_head_r[i] <= 8'd0;
        q_tail_r[i] <= 8'd0;
        q_credit_r[i] <= 8'd0;
        q_doorbell_r[i] <= 1'b0;
        q_epoch_r[i] <= 3'd0;
      end
    end
    else begin
      returning_v_li <= read_v_n | write_v_n;
      returning_data_li <= read_data_n;

      if (in_yumi_li & in_we_lo & is_payload_w) begin
        if (ordering_state_r != ORD_IDLE) begin
          ownership_err_r <= 1'b1;
        end
        payload_mem_r[plane_w][row_w] <= payload_write_data;
        if (q_credit_r[qid_w] != 8'd0) begin
          q_credit_r[qid_w] <= q_credit_r[qid_w] - 8'd1;
        end
        else begin
          ownership_err_r <= 1'b1;
        end
        ordering_state_r <= ORD_PAYLOAD_WRITTEN;
        ordering_pending_r <= 1'b1;
      end

      if (in_yumi_li & in_we_lo & is_b0_w) begin
        unique case (field_w)
          3'd0: q_base_addr_r[qid_w] <= in_data_lo;
          3'd1: q_depth_r[qid_w] <= in_data_lo[7:0];
          3'd2: begin
            q_head_r[qid_w] <= in_data_lo[7:0];
          end
          3'd3: begin
            if (ordering_state_r == ORD_PAYLOAD_WRITTEN) begin
              q_tail_r[qid_w] <= in_data_lo[7:0];
              ordering_state_r <= ORD_TAIL_WRITTEN;
              ordering_pending_r <= 1'b0;
            end
            else begin
              ownership_err_r <= 1'b1;
            end
          end
          3'd4: begin
            q_credit_r[qid_w] <= in_data_lo[7:0];
          end
          3'd5: begin
            q_epoch_r[qid_w] <= in_data_lo[3:1];
            if (in_data_lo[0]) begin
              if (ordering_state_r == ORD_TAIL_WRITTEN) begin
                q_doorbell_r[qid_w] <= 1'b1;
                ordering_state_r <= ORD_IDLE;
                ordering_pending_r <= 1'b0;
              end
              else begin
                ownership_err_r <= 1'b1;
              end
            end
            else begin
              q_doorbell_r[qid_w] <= 1'b0;
            end
          end
          default: begin end
        endcase
      end
    end
  end

endmodule
