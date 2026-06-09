// node_scheduler
// Round-robin scheduler: selects first unvisited node that is ready or in pending queue.
//
// v2 changes:
//   - Removed discovery_mode and dirty_mask.
//   - Added pending_queue FIFO fed by reverse_index_lookup.
//   - Queue head has priority over round-robin scan.

module node_scheduler #(
    parameter int NUM_NODES = gbp_pkg::NUM_NODES_PER_PE,
    parameter int NODE_ID_W = gbp_pkg::NODE_ID_W,
    parameter int PENDING_Q_DEPTH = 16
)(
    input  logic clk_i,
    input  logic rst_n_i,

    input  logic phase_factor_first_i,
    input  logic [NUM_NODES-1:0] node_ready_i,
    input  logic [NUM_NODES-1:0] visited_mask_i,

    // From reverse_index_lookup
    input  logic                 affected_valid_i,
    input  logic [NODE_ID_W-1:0] affected_local_id_i,

    input  logic sched_ready_i,
    output logic sched_valid_o,
    output logic [NODE_ID_W-1:0] sched_node_id_o,
    output logic sched_is_factor_o,
    output logic no_schedulable_nodes_o,

    // Debug: queue overflow indication (sticky)
    output logic pending_q_overflow_o
);

  localparam Q_PTR_W = $clog2(PENDING_Q_DEPTH);

  // =====================================================================
  // Pending Queue (FIFO)
  // =====================================================================
  logic [NODE_ID_W-1:0] pending_q [PENDING_Q_DEPTH];
  logic [Q_PTR_W:0]     q_wr_ptr, q_rd_ptr;
  logic                 q_full, q_empty;

  assign q_full  = (q_wr_ptr[Q_PTR_W] != q_rd_ptr[Q_PTR_W])
                    && (q_wr_ptr[Q_PTR_W-1:0] == q_rd_ptr[Q_PTR_W-1:0]);
  assign q_empty = (q_wr_ptr == q_rd_ptr);

  // Sticky overflow flag: set when affected_valid arrives but queue is full
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      pending_q_overflow_o <= 1'b0;
    end else if (affected_valid_i && q_full) begin
      pending_q_overflow_o <= 1'b1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      q_wr_ptr <= '0;
    end else begin
      if (affected_valid_i && !q_full) begin
        pending_q[q_wr_ptr[Q_PTR_W-1:0]] <= affected_local_id_i;
        q_wr_ptr <= q_wr_ptr + 1;
      end
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      q_rd_ptr <= '0;
    end else begin
      if (sched_ready_i && !q_empty) begin
        q_rd_ptr <= q_rd_ptr + 1;
      end
    end
  end

  logic [NODE_ID_W-1:0] q_head;
  assign q_head = pending_q[q_rd_ptr[Q_PTR_W-1:0]];

  // Head valid only if not empty and not already visited
  logic q_head_valid;
  assign q_head_valid = !q_empty && !visited_mask_i[q_head];

  // =====================================================================
  // Round-robin fallback: scan for ready & unvisited nodes
  // =====================================================================
  logic [NODE_ID_W-1:0] next_index_r;
  logic [NODE_ID_W-1:0] rr_selected;
  logic                 rr_found;

  always_comb begin
    rr_selected = '0;
    rr_found = 1'b0;
    for (int i = 0; i < NUM_NODES; i++) begin
      logic [NODE_ID_W-1:0] idx;
      idx = next_index_r + NODE_ID_W'(i);
      if (!rr_found && !visited_mask_i[idx] && node_ready_i[idx]) begin
        rr_found = 1'b1;
        rr_selected = idx;
      end
    end
  end

  // =====================================================================
  // Output selection
  // =====================================================================
  logic [NODE_ID_W-1:0] selected_node;
  logic                 found;

  always_comb begin
    if (q_head_valid) begin
      selected_node = q_head;
      found = 1'b1;
    end else if (rr_found) begin
      selected_node = rr_selected;
      found = 1'b1;
    end else begin
      selected_node = '0;
      found = 1'b0;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      next_index_r   <= '0;
      sched_valid_o  <= 1'b0;
      sched_node_id_o<= '0;
    end else begin
      if (sched_ready_i) begin
        next_index_r  <= selected_node + 1'b1;
        sched_valid_o   <= found;
        sched_node_id_o <= selected_node;
      end
    end
  end

  assign sched_is_factor_o = phase_factor_first_i;
  assign no_schedulable_nodes_o = !found;

endmodule
