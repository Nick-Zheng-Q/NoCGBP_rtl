`include "bsg_manycore_defines.svh"

module gbp_pe_mesh_2x2
  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
(
  input  logic        clk,
  input  logic        rst_n,
  input  logic        send_v,
  input  logic        send_we,
  input  logic [15:0] send_addr,
  input  logic [31:0] send_data,
  input  logic        force_stall_i,
  output logic        send_ready,
  output logic        link_activity_o,
  output logic        horizontal_activity_o,
  output logic        vertical_activity_o,
  output logic        decode_error_seen_o
);

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp = 32;
  localparam int addr_width_lp = 16;
  localparam int credit_counter_width_lp = 8;
  localparam int num_tiles_x_lp = 2;
  localparam int num_tiles_y_lp = 2;
  localparam int barrier_ruche_factor_x_lp = 1;
  localparam int dims_lp = 2;
  localparam int packet_width_lp =
      `bsg_manycore_packet_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);
  localparam int link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  `declare_bsg_manycore_packet_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  logic [link_sif_width_lp-1:0] tx_to_mesh;
  logic [link_sif_width_lp-1:0] mesh_to_tx;
  bsg_manycore_packet_s tx_pkt_s;
  logic [packet_width_lp-1:0] tx_pkt_bits;
  logic tx_ep_ready_lo;
  logic tx_send_v_li;

  localparam int stall_cycles_lp = 16;
  logic [$clog2(stall_cycles_lp+1)-1:0] stall_counter_r;
  logic stall_done_r;
  logic stall_active;

  `declare_bsg_manycore_link_sif_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  logic [E:W][num_tiles_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_i;
  logic [E:W][num_tiles_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_o;
  logic [S:N][num_tiles_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_i;
  logic [S:N][num_tiles_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_o;
  bsg_manycore_link_sif_s [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_link_i;
  bsg_manycore_link_sif_s [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_link_o;

  logic [S:N][num_tiles_x_lp-1:0] ver_barrier_link_i;
  logic [S:N][num_tiles_x_lp-1:0] ver_barrier_link_o;
  logic [E:W][num_tiles_y_lp-1:0] hor_barrier_link_i;
  logic [E:W][num_tiles_y_lp-1:0] hor_barrier_link_o;
  logic [E:W][num_tiles_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_i;
  logic [E:W][num_tiles_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_o;

  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][x_cord_width_lp-1:0] global_x_i;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][y_cord_width_lp-1:0] global_y_i;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][x_cord_width_lp-1:0] global_x_o;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][y_cord_width_lp-1:0] global_y_o;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0] tile_reset_o;

  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_barrier_link_i;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_barrier_link_o;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][barrier_ruche_factor_x_lp-1:0][E:W]
      tile_barrier_ruche_link_i;
  logic [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][barrier_ruche_factor_x_lp-1:0][E:W]
      tile_barrier_ruche_link_o;
  logic link_activity_r;
  logic horizontal_activity_r;
  logic vertical_activity_r;
  logic horizontal_link_activity;
  logic vertical_link_activity;
  logic decode_error_dest_lo;
  logic decode_error_seen_r;

  assign stall_active = force_stall_i & ~stall_done_r;
  assign tx_send_v_li = send_v & ~stall_active;
  assign send_ready = tx_ep_ready_lo & ~stall_active;

  always_comb begin
    tx_pkt_s = '0;
    tx_pkt_s.addr = send_addr;
    tx_pkt_s.op_v2 = send_we ? e_remote_store : e_remote_load;
    tx_pkt_s.reg_id = '1;
    tx_pkt_s.payload.data = send_data;
    tx_pkt_s.src_x_cord = 2'd0;
    tx_pkt_s.src_y_cord = 2'd0;
    tx_pkt_s.x_cord = 2'd1;
    tx_pkt_s.y_cord = 2'd1;
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
      ,.link_sif_i(mesh_to_tx)
      ,.link_sif_o(tx_to_mesh)
      ,.global_x_i(2'd0)
      ,.global_y_i(2'd0)
      ,.out_packet_i(tx_pkt_bits)
      ,.out_v_i(tx_send_v_li)
      ,.out_credit_or_ready_o(tx_ep_ready_lo)
      ,.returned_data_r_o()
      ,.returned_reg_id_r_o()
      ,.returned_v_r_o()
      ,.returned_pkt_type_r_o()
      ,.returned_yumi_i(1'b0)
      ,.returned_fifo_full_o()
      ,.returned_credit_v_r_o()
      ,.returned_credit_reg_id_r_o()
      ,.out_credits_used_o()
      ,.core_req_data_o()
      ,.core_req_addr_o()
      ,.core_req_we_o()
      ,.core_req_v_o()
      ,.core_req_yumi_i(1'b0)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  bsg_mesh_stitch
    #(
      .width_p(link_sif_width_lp)
      ,.x_max_p(num_tiles_x_lp)
      ,.y_max_p(num_tiles_y_lp)
    ) link_stitch
    (
      .outs_i(tile_link_o)
      ,.ins_o(tile_link_i)
      ,.hor_i(hor_link_sif_i)
      ,.hor_o(hor_link_sif_o)
      ,.ver_i(ver_link_sif_i)
      ,.ver_o(ver_link_sif_o)
    );

  for (genvar y = 0; y < num_tiles_y_lp; y++) begin : gen_row
    for (genvar x = 0; x < num_tiles_x_lp; x++) begin : gen_col
      bsg_manycore_tile_compute_mesh
        #(
          .dmem_size_p(1024)
          ,.x_cord_width_p(x_cord_width_lp)
          ,.y_cord_width_p(y_cord_width_lp)
          ,.pod_x_cord_width_p(1)
          ,.pod_y_cord_width_p(1)
          ,.num_tiles_x_p(num_tiles_x_lp)
          ,.num_tiles_y_p(num_tiles_y_lp)
          ,.data_width_p(data_width_lp)
          ,.addr_width_p(addr_width_lp)
          ,.barrier_ruche_factor_X_p(barrier_ruche_factor_x_lp)
          ,.ipoly_hashing_p(0)
          ,.dims_p(dims_lp)
          ,.barrier_dirs_p(7)
          ,.hetero_type_p(8)
          ,.debug_p(0)
        ) tile
        (
          .clk_i(clk)
          ,.reset_i(reset_i)
          ,.reset_o(tile_reset_o[y][x])
          ,.link_i(tile_link_i[y][x])
          ,.link_o(tile_link_o[y][x])
          ,.barrier_link_i(tile_barrier_link_i[y][x])
          ,.barrier_link_o(tile_barrier_link_o[y][x])
          ,.barrier_ruche_link_i(tile_barrier_ruche_link_i[y][x])
          ,.barrier_ruche_link_o(tile_barrier_ruche_link_o[y][x])
          ,.global_x_i(global_x_i[y][x])
          ,.global_y_i(global_y_i[y][x])
          ,.global_x_o(global_x_o[y][x])
          ,.global_y_o(global_y_o[y][x])
        );
    end
  end

  always_comb begin
    hor_link_sif_i = '{default:'0};
    ver_link_sif_i = '{default:'0};
    ver_barrier_link_i = '0;
    hor_barrier_link_i = '0;
    barrier_ruche_link_i = '0;

    tile_barrier_link_i = '0;
    tile_barrier_ruche_link_i = '0;

    global_x_i[0][0] = 2'd0;
    global_x_i[0][1] = 2'd1;
    global_x_i[1][0] = 2'd0;
    global_x_i[1][1] = 2'd1;

    global_y_i[0][0] = 2'd0;
    global_y_i[0][1] = 2'd0;
    global_y_i[1][0] = 2'd1;
    global_y_i[1][1] = 2'd1;

    hor_link_sif_i[W][0] = tx_to_mesh;

  end

  assign mesh_to_tx = hor_link_sif_o[W][0];
  assign decode_error_dest_lo = gen_row[1].gen_col[1].tile.proc.h.z.bridge_decode_error_lo;

  always_comb begin
    horizontal_link_activity = 1'b0;
    for (int y = 0; y < num_tiles_y_lp; y++) begin
      horizontal_link_activity |= (|hor_link_sif_o[E][y])
        | (|hor_link_sif_o[W][y])
        | (|hor_link_sif_i[E][y])
        | (|hor_link_sif_i[W][y]);
    end

    vertical_link_activity = 1'b0;
    for (int x = 0; x < num_tiles_x_lp; x++) begin
      vertical_link_activity |= (|ver_link_sif_o[N][x])
        | (|ver_link_sif_o[S][x])
        | (|ver_link_sif_i[N][x])
        | (|ver_link_sif_i[S][x]);
    end
  end

  always_ff @(posedge clk) begin
    if (reset_i) begin
      stall_counter_r <= '0;
      stall_done_r <= 1'b0;
      link_activity_r <= 1'b0;
      horizontal_activity_r <= 1'b0;
      vertical_activity_r <= 1'b0;
      decode_error_seen_r <= 1'b0;
    end else begin
      if (force_stall_i && !stall_done_r) begin
        if (stall_counter_r == stall_cycles_lp-1) begin
          stall_done_r <= 1'b1;
        end else begin
          stall_counter_r <= stall_counter_r + 1'b1;
        end
      end

      link_activity_r <= link_activity_r
        | (send_v & send_ready)
        | horizontal_link_activity
        | vertical_link_activity;
      horizontal_activity_r <= horizontal_activity_r | horizontal_link_activity;
      vertical_activity_r <= vertical_activity_r | vertical_link_activity;
      decode_error_seen_r <= decode_error_seen_r | decode_error_dest_lo;
    end
  end

  assign link_activity_o = link_activity_r;
  assign horizontal_activity_o = horizontal_activity_r;
  assign vertical_activity_o = vertical_activity_r;
  assign decode_error_seen_o = decode_error_seen_r;

endmodule
