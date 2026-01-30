/**
 *    bsg_manycore_pod_mesh.v
 *
 *    manycore pod with 2-D mesh network
 */


`include "bsg_manycore_defines.svh"

module bsg_manycore_pod_mesh
  import bsg_noc_pkg::*;
  import bsg_manycore_pkg::*;
  #(// number of tiles in a pod
    `BSG_INV_PARAM(num_tiles_x_p)
    , `BSG_INV_PARAM(num_tiles_y_p)
    , `BSG_INV_PARAM(pod_x_cord_width_p)
    , `BSG_INV_PARAM(pod_y_cord_width_p)
    , `BSG_INV_PARAM(x_cord_width_p)
    , `BSG_INV_PARAM(y_cord_width_p)
    , `BSG_INV_PARAM(addr_width_p)
    , `BSG_INV_PARAM(data_width_p)

    // This determines how to divide the pod into smaller hierarchical mid-level blocks.
    , `BSG_INV_PARAM(num_subarray_x_p)
    , `BSG_INV_PARAM(num_subarray_y_p)
    // number of tiles in a subarray
    , localparam subarray_num_tiles_x_lp = (num_tiles_x_p/num_subarray_x_p)
    , subarray_num_tiles_y_lp = (num_tiles_y_p/num_subarray_y_p)
    // coordinate width within a pod
    , x_subcord_width_lp=`BSG_SAFE_CLOG2(num_tiles_x_p)
    , y_subcord_width_lp=`BSG_SAFE_CLOG2(num_tiles_y_p)
  
    , parameter `BSG_INV_PARAM(dmem_size_p)
    , `BSG_INV_PARAM(ipoly_hashing_p) 

    , `BSG_INV_PARAM(barrier_ruche_factor_X_p)

    , localparam manycore_link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    `ifndef SYNTHESIS
    , parameter int hetero_type_vec_p [0:(num_tiles_y_p*num_tiles_x_p) - 1]  = '{default:0}
    `endif
  )
  (
    // manycore
    input clk_i
    , input [num_tiles_x_p-1:0] reset_i

    , input  [E:W][num_tiles_y_p-1:0][manycore_link_sif_width_lp-1:0] hor_link_sif_i
    , output [E:W][num_tiles_y_p-1:0][manycore_link_sif_width_lp-1:0] hor_link_sif_o

    , input  [S:N][num_tiles_x_p-1:0][manycore_link_sif_width_lp-1:0] ver_link_sif_i
    , output [S:N][num_tiles_x_p-1:0][manycore_link_sif_width_lp-1:0] ver_link_sif_o

    , input  [E:W][num_tiles_y_p-1:0] hor_barrier_link_i
    , output [E:W][num_tiles_y_p-1:0] hor_barrier_link_o

    , input  [E:W][num_tiles_y_p-1:0][barrier_ruche_factor_X_p-1:0] barrier_ruche_link_i
    , output [E:W][num_tiles_y_p-1:0][barrier_ruche_factor_X_p-1:0] barrier_ruche_link_o

    // pod cord (should be all same value for all columns)
    , input [num_tiles_x_p-1:0][x_cord_width_p-1:0] global_x_i
    , input [num_tiles_x_p-1:0][y_cord_width_p-1:0] global_y_i
  );

  `declare_bsg_manycore_link_sif_s(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p);

  // manycore subarray
  bsg_manycore_link_sif_s [num_subarray_y_p-1:0][num_subarray_x_p-1:0][E:W][subarray_num_tiles_y_lp-1:0] mc_hor_link_sif_li, mc_hor_link_sif_lo;
  bsg_manycore_link_sif_s [num_subarray_y_p-1:0][num_subarray_x_p-1:0][S:N][subarray_num_tiles_x_lp-1:0] mc_ver_link_sif_li, mc_ver_link_sif_lo;
  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][subarray_num_tiles_x_lp-1:0][x_cord_width_p-1:0] mc_global_x_li, mc_global_x_lo;
  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][subarray_num_tiles_x_lp-1:0][y_cord_width_p-1:0] mc_global_y_li, mc_global_y_lo;

  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][subarray_num_tiles_x_lp-1:0] mc_reset_li, mc_reset_lo;

  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][S:N][subarray_num_tiles_x_lp-1:0] mc_ver_barrier_link_li, mc_ver_barrier_link_lo;
  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][E:W][subarray_num_tiles_y_lp-1:0] mc_hor_barrier_link_li, mc_hor_barrier_link_lo;
  logic [num_subarray_y_p-1:0][num_subarray_x_p-1:0][E:W][subarray_num_tiles_y_lp-1:0][barrier_ruche_factor_X_p-1:0] mc_barrier_ruche_link_li, mc_barrier_ruche_link_lo;


  // Split the hetero_type_vec_p array into sub-arrays.
  `ifndef SYNTHESIS
  typedef int hetero_type_sub_vec[0:(subarray_num_tiles_y_lp*subarray_num_tiles_x_lp) - 1];
  function hetero_type_sub_vec get_subarray_hetero_type_vec(int y, int x);
    hetero_type_sub_vec vec;
    for (int sy_i = 0; sy_i < subarray_num_tiles_y_lp; sy_i++) begin
      for (int sx_i = 0; sx_i < subarray_num_tiles_x_lp; sx_i++) begin
        vec[sy_i*subarray_num_tiles_x_lp + sx_i] = hetero_type_vec_p[(sy_i + y * subarray_num_tiles_y_lp) * num_tiles_x_p + x * subarray_num_tiles_x_lp + sx_i];
      end
    end
    return vec;
  endfunction
  `endif



  for (genvar y = 0; y < num_subarray_y_p; y++) begin: mc_y
    for (genvar x = 0; x < num_subarray_x_p; x++) begin: mc_x
      bsg_manycore_tile_compute_array_mesh #(
        .dmem_size_p(dmem_size_p)
        ,.ipoly_hashing_p(ipoly_hashing_p)      
        ,.num_tiles_x_p(num_tiles_x_p)
        ,.num_tiles_y_p(num_tiles_y_p)

        ,.subarray_num_tiles_x_p(subarray_num_tiles_x_lp)
        ,.subarray_num_tiles_y_p(subarray_num_tiles_y_lp)

        ,.pod_x_cord_width_p(pod_x_cord_width_p)
        ,.pod_y_cord_width_p(pod_y_cord_width_p)
        ,.x_cord_width_p(x_cord_width_p)
        ,.y_cord_width_p(y_cord_width_p)
        ,.addr_width_p(addr_width_p)
        ,.data_width_p(data_width_p)
        ,.barrier_ruche_factor_X_p(barrier_ruche_factor_X_p)
          `ifndef SYNTHESIS
        ,.hetero_type_vec_p(get_subarray_hetero_type_vec(y, x))
          `endif
      ) mc (
        .clk_i(clk_i)

        ,.reset_i(mc_reset_li[y][x])
        ,.reset_o(mc_reset_lo[y][x])
    
        ,.hor_link_sif_i(mc_hor_link_sif_li[y][x])
        ,.hor_link_sif_o(mc_hor_link_sif_lo[y][x])

        ,.ver_link_sif_i(mc_ver_link_sif_li[y][x])
        ,.ver_link_sif_o(mc_ver_link_sif_lo[y][x])

        ,.ver_barrier_link_i(mc_ver_barrier_link_li[y][x])
        ,.ver_barrier_link_o(mc_ver_barrier_link_lo[y][x])
        ,.hor_barrier_link_i(mc_hor_barrier_link_li[y][x])
        ,.hor_barrier_link_o(mc_hor_barrier_link_lo[y][x])
        ,.barrier_ruche_link_i(mc_barrier_ruche_link_li[y][x])
        ,.barrier_ruche_link_o(mc_barrier_ruche_link_lo[y][x])

        ,.global_x_i(mc_global_x_li[y][x])
        ,.global_y_i(mc_global_y_li[y][x])
        ,.global_x_o(mc_global_x_lo[y][x])
        ,.global_y_o(mc_global_y_lo[y][x])
      );

      // connect to north
      if (y == 0) begin
        // ver link
        assign mc_ver_link_sif_li[y][x][N] = ver_link_sif_i[N][(x*subarray_num_tiles_x_lp)+:subarray_num_tiles_x_lp];
        assign ver_link_sif_o[N][(x*subarray_num_tiles_x_lp)+:subarray_num_tiles_x_lp] = mc_ver_link_sif_lo[y][x][N];
        // coordinates
        assign mc_global_x_li[y][x] = global_x_i[x*subarray_num_tiles_x_lp+:subarray_num_tiles_x_lp];
        assign mc_global_y_li[y][x] = global_y_i[x*subarray_num_tiles_x_lp+:subarray_num_tiles_x_lp];
        // reset
        assign mc_reset_li[y][x] = reset_i[(subarray_num_tiles_x_lp*x)+:subarray_num_tiles_x_lp];
        // tieoff ver barrier
        assign mc_ver_barrier_link_li[y][x][N] = '0;
      end
  
      // connect ver links to the next row
      if (y < num_subarray_y_p-1) begin
        // ver link
        assign mc_ver_link_sif_li[y+1][x][N] = mc_ver_link_sif_lo[y][x][S];
        assign mc_ver_link_sif_li[y][x][S] = mc_ver_link_sif_lo[y+1][x][N];
        // coordinates
        assign mc_global_x_li[y+1][x] = mc_global_x_lo[y][x];
        assign mc_global_y_li[y+1][x] = mc_global_y_lo[y][x];
        // reset
        assign mc_reset_li[y+1][x] = mc_reset_lo[y][x];
        // ver barrier
        assign mc_ver_barrier_link_li[y+1][x][N] = mc_ver_barrier_link_lo[y][x][S];
        assign mc_ver_barrier_link_li[y][x][S] = mc_ver_barrier_link_lo[y+1][x][N];
      end
    
      // last row
      if (y == num_subarray_y_p-1) begin
        // connect to south
        assign ver_link_sif_o[S][(x*subarray_num_tiles_x_lp)+:subarray_num_tiles_x_lp] = mc_ver_link_sif_lo[y][x][S];
        assign mc_ver_link_sif_li[y][x][S] = ver_link_sif_i[S][(x*subarray_num_tiles_x_lp)+:subarray_num_tiles_x_lp];
        // tieoff ver barrier
        assign mc_ver_barrier_link_li[y][x][S] = '0;
      end

      // connect to west
      if (x == 0) begin
        // local link
        assign hor_link_sif_o[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_hor_link_sif_lo[y][x][W];
        assign mc_hor_link_sif_li[y][x][W] = hor_link_sif_i[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
        // hor barrier
        assign hor_barrier_link_o[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_hor_barrier_link_lo[y][x][W];
        assign mc_hor_barrier_link_li[y][x][W] = hor_barrier_link_i[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
        // barrier ruche
        assign barrier_ruche_link_o[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_barrier_ruche_link_lo[y][x][W];
        assign mc_barrier_ruche_link_li[y][x][W] = barrier_ruche_link_i[W][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
      end

      // connect hor links to the next col
      if (x < num_subarray_x_p-1) begin
        // local
        assign mc_hor_link_sif_li[y][x+1][W] = mc_hor_link_sif_lo[y][x][E];
        assign mc_hor_link_sif_li[y][x][E] = mc_hor_link_sif_lo[y][x+1][W];
        // hor barrier
        assign mc_hor_barrier_link_li[y][x+1][W] = mc_hor_barrier_link_lo[y][x][E];
        assign mc_hor_barrier_link_li[y][x][E] = mc_hor_barrier_link_lo[y][x+1][W];
        // barrier ruche
        assign mc_barrier_ruche_link_li[y][x+1][W] = mc_barrier_ruche_link_lo[y][x][E];
        assign mc_barrier_ruche_link_li[y][x][E] = mc_barrier_ruche_link_lo[y][x+1][W];
      end

      // connect to east
      if (x == num_subarray_x_p-1) begin
        // local
        assign hor_link_sif_o[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_hor_link_sif_lo[y][x][E];
        assign mc_hor_link_sif_li[y][x][E] = hor_link_sif_i[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
        // hor barrier
        assign hor_barrier_link_o[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_hor_barrier_link_lo[y][x][E];
        assign mc_hor_barrier_link_li[y][x][E] = hor_barrier_link_i[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
        // barrier ruche
        assign barrier_ruche_link_o[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp] = mc_barrier_ruche_link_lo[y][x][E];
        assign mc_barrier_ruche_link_li[y][x][E] = barrier_ruche_link_i[E][y*subarray_num_tiles_y_lp+:subarray_num_tiles_y_lp];
      end

    end
  end


endmodule

`BSG_ABSTRACT_MODULE(bsg_manycore_pod_mesh)
