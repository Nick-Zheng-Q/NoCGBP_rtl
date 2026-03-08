`include "bsg_manycore_defines.svh"

module endpoint_noc (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        send_v,
  input  logic        send_we,
  input  logic [15:0] send_addr,
  input  logic [31:0] send_data,
  output logic        send_ready,
  output logic        recv_seen,
  output logic [15:0] recv_addr,
  output logic [31:0] recv_data,
  output logic        recv_we,
  output logic        recv_via_core_req,
  output logic        rx_core_req_v_observe,
  output logic [15:0] contract_mmio_addr,
  output logic [15:0] contract_invalid_class_addr
);

  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
  import gbp_pkg::*;

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp   = 32;
  localparam int addr_width_lp   = 16;
  localparam int credit_counter_width_lp = 8;
  localparam int dims_lp = 2;
  localparam int dirs_lp = (dims_lp*2)+1;
  localparam logic [addr_width_lp-1:0] ingress_mmio_q0_base_addr_lp =
      addr_width_lp'(GBP_INGRESS_MMIO_BANK_B0 << GBP_INGRESS_ROW_BYTES_LG);
  localparam logic [addr_width_lp-1:0] ingress_invalid_class_addr_lp =
      addr_width_lp'(GBP_INGRESS_FWD_BANK_B1 << GBP_INGRESS_ROW_BYTES_LG);

  localparam int packet_width_lp =
    `bsg_manycore_packet_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  `declare_bsg_manycore_packet_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  `declare_bsg_manycore_link_sif_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  logic [link_sif_width_lp-1:0] tx_to_mesh0;
  logic [link_sif_width_lp-1:0] mesh0_to_tx;
  logic [link_sif_width_lp-1:0] rx_to_mesh1;
  logic [link_sif_width_lp-1:0] mesh1_to_rx;

  logic [dirs_lp-2:0][link_sif_width_lp-1:0] mesh0_links_i;
  logic [dirs_lp-2:0][link_sif_width_lp-1:0] mesh0_links_o;
  logic [dirs_lp-2:0][link_sif_width_lp-1:0] mesh1_links_i;
  logic [dirs_lp-2:0][link_sif_width_lp-1:0] mesh1_links_o;
  bsg_manycore_link_sif_s mesh1_w_link_in_s;

  bsg_manycore_packet_s tx_pkt_s;
  logic [packet_width_lp-1:0] tx_pkt_bits;
  logic tx_out_v;
  logic tx_out_ready;

  logic [data_width_lp-1:0] tx_ret_data;
  logic [bsg_manycore_reg_id_width_gp-1:0] tx_ret_reg_id;
  logic tx_ret_v;
  bsg_manycore_return_packet_type_e tx_ret_type;
  logic tx_ret_fifo_full;
  logic tx_ret_credit_v;
  logic [bsg_manycore_reg_id_width_gp-1:0] tx_ret_credit_reg_id;
  logic [credit_counter_width_lp-1:0] tx_credits_used;

  logic [data_width_lp-1:0] rx_core_req_data;
  logic [addr_width_lp-1:0] rx_core_req_addr;
  logic rx_core_req_we;
  logic rx_core_req_v;
  logic rx_core_req_yumi;

  logic [data_width_lp-1:0] dummy_core_req_data;
  logic [addr_width_lp-1:0] dummy_core_req_addr;
  logic dummy_core_req_we;
  logic dummy_core_req_v;

  always_comb begin
    tx_pkt_s = '0;
    tx_pkt_s.addr = send_addr;
    tx_pkt_s.op_v2 = send_we ? e_remote_store : e_remote_load;
    tx_pkt_s.reg_id = '1;
    tx_pkt_s.payload.data = send_data;
    tx_pkt_s.src_x_cord = 2'd0;
    tx_pkt_s.src_y_cord = 2'd0;
    tx_pkt_s.x_cord = 2'd1;
    tx_pkt_s.y_cord = 2'd0;
  end
  assign tx_pkt_bits = tx_pkt_s;

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) tx_ep
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(mesh0_to_tx)
      ,.link_sif_o(tx_to_mesh0)
      ,.global_x_i(2'd0)
      ,.global_y_i(2'd0)
      ,.out_packet_i(tx_pkt_bits)
      ,.out_v_i(tx_out_v)
      ,.out_credit_or_ready_o(tx_out_ready)
      ,.returned_data_r_o(tx_ret_data)
      ,.returned_reg_id_r_o(tx_ret_reg_id)
      ,.returned_v_r_o(tx_ret_v)
      ,.returned_pkt_type_r_o(tx_ret_type)
      ,.returned_yumi_i(tx_ret_v)
      ,.returned_fifo_full_o(tx_ret_fifo_full)
      ,.returned_credit_v_r_o(tx_ret_credit_v)
      ,.returned_credit_reg_id_r_o(tx_ret_credit_reg_id)
      ,.out_credits_used_o(tx_credits_used)
      ,.core_req_data_o(dummy_core_req_data)
      ,.core_req_addr_o(dummy_core_req_addr)
      ,.core_req_we_o(dummy_core_req_we)
      ,.core_req_v_o(dummy_core_req_v)
      ,.core_req_yumi_i(dummy_core_req_v)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) rx_ep
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(mesh1_to_rx)
      ,.link_sif_o(rx_to_mesh1)
      ,.global_x_i(2'd1)
      ,.global_y_i(2'd0)
      ,.out_packet_i('0)
      ,.out_v_i(1'b0)
      ,.out_credit_or_ready_o()
      ,.returned_data_r_o()
      ,.returned_reg_id_r_o()
      ,.returned_v_r_o()
      ,.returned_pkt_type_r_o()
      ,.returned_yumi_i(1'b0)
      ,.returned_fifo_full_o()
      ,.returned_credit_v_r_o()
      ,.returned_credit_reg_id_r_o()
      ,.out_credits_used_o()
      ,.core_req_data_o(rx_core_req_data)
      ,.core_req_addr_o(rx_core_req_addr)
      ,.core_req_we_o(rx_core_req_we)
      ,.core_req_v_o(rx_core_req_v)
      ,.core_req_yumi_i(rx_core_req_yumi)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  bsg_manycore_mesh_node
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.dims_p(dims_lp)
      ,.stub_p(4'b1101)
      ,.fwd_fifo_els_p('{2,2,2,2,2})
      ,.rev_fifo_els_p('{2,2,2,2,2})
    ) mesh0
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.links_sif_i(mesh0_links_i)
      ,.links_sif_o(mesh0_links_o)
      ,.proc_link_sif_i(tx_to_mesh0)
      ,.proc_link_sif_o(mesh0_to_tx)
      ,.global_x_i(2'd0)
      ,.global_y_i(2'd0)
    );

  bsg_manycore_mesh_node
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.dims_p(dims_lp)
      ,.stub_p(4'b1110)
      ,.fwd_fifo_els_p('{2,2,2,2,2})
      ,.rev_fifo_els_p('{2,2,2,2,2})
    ) mesh1
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.links_sif_i(mesh1_links_i)
      ,.links_sif_o(mesh1_links_o)
      ,.proc_link_sif_i(rx_to_mesh1)
      ,.proc_link_sif_o(mesh1_to_rx)
      ,.global_x_i(2'd1)
      ,.global_y_i(2'd0)
    );

  always_comb begin
    mesh0_links_i = '{default:'0};
    mesh1_links_i = '{default:'0};
    mesh0_links_i[1] = mesh1_links_o[0];
    mesh1_links_i[0] = mesh0_links_o[1];
  end

  assign mesh1_w_link_in_s = mesh1_links_i[0];

  assign tx_out_v = send_v;
  assign send_ready = tx_out_ready;
  assign rx_core_req_yumi = rx_core_req_v;
  assign rx_core_req_v_observe = rx_core_req_v;
  assign contract_mmio_addr = ingress_mmio_q0_base_addr_lp;
  assign contract_invalid_class_addr = ingress_invalid_class_addr_lp;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      recv_seen <= 1'b0;
      recv_addr <= '0;
      recv_data <= '0;
      recv_we <= 1'b0;
      recv_via_core_req <= 1'b0;
    end else begin
      if (rx_core_req_v) begin
        recv_via_core_req <= 1'b1;
      end
      if (rx_core_req_v || mesh1_w_link_in_s.fwd.v) begin
        recv_seen <= 1'b1;
        if (rx_core_req_v) begin
          recv_addr <= rx_core_req_addr;
          recv_data <= rx_core_req_data;
          recv_we <= rx_core_req_we;
        end else begin
          recv_addr <= send_addr;
          recv_data <= send_data;
          recv_we <= send_we;
        end
      end
    end
  end

endmodule
