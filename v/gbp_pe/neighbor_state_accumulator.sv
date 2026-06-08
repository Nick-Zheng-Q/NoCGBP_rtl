// neighbor_state_accumulator.sv
// Merges local and remote neighbor state data into a single stream.
// Local data has priority (consumed first), then remote data.

module neighbor_state_accumulator
  import gbp_pkg::*;
(
    input  logic clk_i
    , input  logic rst_i

    // Local SPM read
    , input  logic                 local_valid_i
    , output logic                 local_ready_o
    , input  logic [FP32_W-1:0]   local_data_i
    , input  logic                 local_last_i

    // Remote response
    , input  logic                 remote_valid_i
    , output logic                 remote_ready_o
    , input  logic [FP32_W-1:0]   remote_data_i
    , input  logic                 remote_last_i

    // To Compute Unit
    , output logic                 out_valid_o
    , input  logic                 out_ready_i
    , output logic [FP32_W-1:0]   out_data_o
    , output logic                 out_last_o

    // Pipeline control
    , input  logic                 start_i
    , output logic                 accumulator_done_o
);

  localparam S_LOCAL  = 2'b00;
  localparam S_REMOTE = 2'b01;
  localparam S_DONE   = 2'b10;

  logic [1:0] state_r;

  // Output mux
  assign out_valid_o = (state_r == S_LOCAL) ? local_valid_i :
                       (state_r == S_REMOTE) ? remote_valid_i :
                       1'b0;
  assign out_data_o  = (state_r == S_LOCAL) ? local_data_i :
                       (state_r == S_REMOTE) ? remote_data_i :
                       '0;
  assign out_last_o  = (state_r == S_LOCAL) ? local_last_i :
                       (state_r == S_REMOTE) ? remote_last_i :
                       1'b0;
  assign local_ready_o  = (state_r == S_LOCAL) ? out_ready_i : 1'b0;
  assign remote_ready_o = (state_r == S_REMOTE) ? out_ready_i : 1'b0;
  assign accumulator_done_o = (state_r == S_DONE);

  // State transition
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r <= S_LOCAL;
    end else begin
      case (state_r)
        S_LOCAL: begin
          if (local_valid_i && out_ready_i && local_last_i) begin
            state_r <= S_REMOTE;
          end
        end
        S_REMOTE: begin
          if (remote_valid_i && out_ready_i && remote_last_i) begin
            state_r <= S_DONE;
          end
        end
        S_DONE: begin
          if (start_i) begin
            state_r <= S_LOCAL;
          end
        end
        default: state_r <= S_LOCAL;
      endcase
    end
  end

endmodule
