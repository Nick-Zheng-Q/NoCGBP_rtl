// agu
// address generation unit

module agu
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
(
    input  logic                   clk_i,
    input  logic                   reset_i,
    input  desc_t                  descriptor_i,
    input  logic                   ready_i,       // FIFO ready
    output logic  [SPM_ADDR_W-1:0] agu_addr,
    output logic                   valid_o,       // address valid
    output logic                   next_desc_o
);

  logic [  SPM_ADDR_W-1:0] base_addr;
  logic [XFER_BYTES_W-1:0] xfer_bytes;
  logic [STEP_BYTES_W-1:0] step_bytes;
  logic [    BEAT_MAX-1:0] beat_count;
  logic [    BEAT_MAX-1:0] beat_count_max;
  logic                    active_r;

  // Calculate max beats
  always_comb begin
    base_addr = descriptor_i.base_addr;
    xfer_bytes = descriptor_i.xfer_bytes;
    step_bytes = descriptor_i.addr_step_bytes;
    // xfer_bytes 不是 beat 对齐时必须向上取整，否则 108B 这类负载会少发最后一个 beat。
    beat_count_max = (xfer_bytes + XFER_BYTES_W'(BEAT_BYTES - 1)) >> $clog2(BEAT_BYTES);
  end

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      active_r <= 1'b0;
      beat_count <= '0;
      agu_addr <= '0;
      valid_o <= 1'b0;
      next_desc_o <= 1'b0;
    end else begin
      valid_o <= 1'b0;
      next_desc_o <= 1'b0;

      if (ready_i) begin
        if (!active_r) begin
          if (descriptor_i.start) begin
            agu_addr <= base_addr;
            valid_o <= 1'b1;

            if (beat_count_max <= 'd1) begin
              active_r <= 1'b0;
              beat_count <= '0;
              next_desc_o <= 1'b1;
            end else begin
              active_r <= 1'b1;
              beat_count <= 'd1;
            end
          end
        end else begin
          agu_addr <= agu_addr + step_bytes;
          valid_o <= 1'b1;

          if (beat_count + 1'b1 >= beat_count_max) begin
            active_r <= 1'b0;
            beat_count <= '0;
            next_desc_o <= 1'b1;
          end else begin
            beat_count <= beat_count + 1'b1;
          end
        end
      end
    end
  end

endmodule
