# Migrate `gbp_pe` compute datapath to `gbp_compute_core`

> **For agentic workers:** REQUIRED SUB-_SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the legacy `compute_unit`/`gbp_compute_engine` inside `gbp_pe_compute_subsystem` with the new `compute_unit_wrapper`/`gbp_compute_core`, and rewrite the `gbp_pe` unit test so it exercises the new architecture end-to-end.

**Architecture:** Keep the existing SPM read/write ports of `gbp_pe_compute_subsystem`. Insert two adapters between the existing stream engines and `compute_unit_wrapper`:
1. `operand_stream_assembler` — converts the existing 64-bit `read_stream_engine` beats plus descriptor metadata into 16-word `operand_stream_beat_t` beats.
2. `wb_to_wse_adapter` — converts the new `writeback_record_t` (32-bit words) into the existing `write_stream_engine` descriptor + 32-bit word interface.

Inside the subsystem, build a `cu_cmd_t` from the legacy scalar PE command fields and route the neighbor-state accumulator stream into `OST_BELIEF_MSG` operand beats.

**Tech Stack:** SystemVerilog, Verilator, BSG Manycore GBP PE.

---

## Key constraints discovered during exploration

1. **Dimension model mismatch.** The new core only supports `gbp_dim_e {DIM_1, DIM_3, DIM_6}`. The current `gbp_pe.cc` test uses `dof=2` for both factor and variable nodes. That test data **must be rewritten** for a supported dimension (recommend `DIM_3` for an end-to-end factor→variable case, or `DIM_1` for a minimal smoke test).
2. **Operand streams vs. neighbor-state accumulator.** The new belief path expects incoming messages as an `OST_BELIEF_MSG` operand stream read from SPM. The legacy `gbp_pe_compute_subsystem` receives local neighbor messages as a 32-bit streaming accumulator (`ns_valid_i`). The migration must either:
   - Route `ns_valid_i` into the operand assembler as a synthetic `OST_BELIEF_MSG` stream, or
   - Have the control subsystem pre-gather messages into SPM and pass the base address.
   This plan chooses the first option because it minimizes control-subsystem changes.
3. **Factor path streams.** `OP_MSG_F2V` requires four operand descriptors: `OST_MSG_STATIC`, `OST_CAV_FACTOR_O`, `OST_CAV_BELIEF_O`, `OST_CAV_OLD_TO_O`. The legacy SPM layout stores a single contiguous factor state. The test must lay out these four regions separately, or the subsystem must slice the legacy region on the fly. This plan rewrites the test layout.

---

## File structure

| File | Responsibility |
|------|----------------|
| `v/gbp_pe/compute_core/operand_stream_assembler.sv` *(create)* | Buffer 64-bit SPM beats, assemble 16-word operand beats, attach `kind/op_id/beat_idx/last`. |
| `v/gbp_pe/compute_core/wb_to_wse_adapter.sv` *(create)* | Take `writeback_record_t`, issue WSE descriptor, stream 32-bit payload words. |
| `v/gbp_pe/gbp_pe_compute_subsystem.sv` *(modify)* | Replace `compute_unit` with `compute_unit_wrapper` + adapters; build `cu_cmd_t`; route `ns_valid_i` into belief-message stream. |
| `v/gbp_pe/gbp_pe.sv` *(modify)* | Keep scalar whitebox/control command ports, forward unchanged into `gbp_pe_compute_subsystem`; remove stale debug `$display`. |
| `nocbp_verilator/tests/unit/gbp_pe.cc` *(modify)* | Rewrite SPM init and expected results for new-core dimensions and operand layout. |
| `nocbp_verilator/tops/unit/gbp_pe_top.sv` *(modify if needed)* | Add any new whitebox ports required by the rewritten test. |

---

## Task 1: Create `operand_stream_assembler`

**Files:**
- Create: `v/gbp_pe/compute_core/operand_stream_assembler.sv`
- Test: `nocbp_verilator/tests/unit/operand_stream_assembler.cc` *(create a minimal smoke test)*

### Step 1.1: Implement the assembler

```systemverilog
module operand_stream_assembler
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS  = gbp_pkg::BEAT_BITS,
    parameter int FP32_W     = gbp_pkg::FP32_W,
    localparam int WORDS_PER_RSE_BEAT = BEAT_BITS / FP32_W,          // 2
    localparam int RSE_BEATS_PER_OPERAND = OPERAND_STREAM_WIDTH
                                            / WORDS_PER_RSE_BEAT     // 8
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Descriptor from compute_unit_wrapper
    input  logic                       desc_valid_i,
    output logic                       desc_ready_o,
    input  operand_stream_kind_e       desc_kind_i,
    input  logic [31:0]                desc_op_id_i,
    input  logic [SPM_ADDR_W-1:0]      desc_base_addr_i,
    input  logic [15:0]                desc_nbeats_i,

    // Operand stream to compute_unit_wrapper
    output logic                       operand_valid_o,
    input  logic                       operand_ready_i,
    output operand_stream_beat_t       operand_o,

    // 64-bit SPM read port
    output logic                       spm_rd_valid_o,
    input  logic                       spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0]      spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0]       spm_rd_data_i
);

  // Internal RSE signals
  logic                 rse_desc_valid,  rse_desc_ready;
  logic [SPM_ADDR_W-1:0] rse_desc_base;
  logic [15:0]          rse_desc_word_count;
  logic                 rse_desc_is_staging;
  logic                 rse_beat_valid,  rse_beat_ready;
  logic [BEAT_BITS-1:0] rse_beat_data;

  read_stream_engine u_rse (
    .clk_i,
    .rst_n_i,
    .desc_valid_i       (rse_desc_valid),
    .desc_ready_o       (rse_desc_ready),
    .desc_base_addr_i   (rse_desc_base),
    .desc_word_count_i  (rse_desc_word_count),
    .desc_is_staging_i  (rse_desc_is_staging),
    .beat_valid_o       (rse_beat_valid),
    .beat_ready_i       (rse_beat_ready),
    .beat_data_o        (rse_beat_data),
    .spm_rd_valid_o,
    .spm_rd_ready_i,
    .spm_rd_addr_o,
    .spm_rd_data_i
  );

  // State
  typedef enum logic [1:0] { S_IDLE, S_ISSUE, S_STREAM, S_DONE } state_e;
  state_e state_r;
  logic [15:0] total_beats_r, beat_idx_r;
  logic [$clog2(RSE_BEATS_PER_OPERAND)-1:0] rse_beat_cnt_r;
  operand_stream_kind_e kind_r;
  logic [31:0] op_id_r;
  logic [OPERAND_STREAM_WIDTH-1:0][FP32_W-1:0] data_buffer_r;

  // Output assembly
  always_comb begin
    operand_o = '0;
    operand_o.kind     = kind_r;
    operand_o.ctx_id   = 1'b0;
    operand_o.op_id    = op_id_r;
    operand_o.beat_idx = beat_idx_r;
    operand_o.last     = (beat_idx_r + 16'd1 == total_beats_r);
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      operand_o.data[i] = data_buffer_r[i];
  end

  assign rse_desc_valid     = (state_r == S_ISSUE);
  assign rse_desc_base      = desc_base_addr_i;
  assign rse_desc_word_count= desc_nbeats_i * 16'd16;
  assign rse_desc_is_staging= 1'b0;

  assign desc_ready_o       = (state_r == S_IDLE);

  // Accept RSE beat unless we are holding a complete operand that hasn't been consumed
  assign rse_beat_ready     = (state_r == S_STREAM) && (!operand_valid_o || operand_ready_i);
  assign operand_valid_o    = (state_r == S_STREAM)
                              && (rse_beat_cnt_r == RSE_BEATS_PER_OPERAND-1)
                              && (rse_beat_valid || (beat_idx_r == total_beats_r));

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      state_r        <= S_IDLE;
      total_beats_r  <= '0;
      beat_idx_r     <= '0;
      rse_beat_cnt_r <= '0;
      kind_r         <= OST_MSG_STATIC;
      op_id_r        <= '0;
      data_buffer_r  <= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          if (desc_valid_i && desc_ready_o) begin
            total_beats_r  <= desc_nbeats_i;
            beat_idx_r     <= '0;
            rse_beat_cnt_r <= '0;
            kind_r         <= desc_kind_i;
            op_id_r        <= desc_op_id_i;
            state_r        <= S_ISSUE;
          end
        end

        S_ISSUE: begin
          if (rse_desc_valid && rse_desc_ready)
            state_r <= S_STREAM;
        end

        S_STREAM: begin
          if (operand_valid_o && operand_ready_i) begin
            beat_idx_r     <= beat_idx_r + 16'd1;
            rse_beat_cnt_r <= '0;
            if (beat_idx_r + 16'd1 == total_beats_r)
              state_r <= S_IDLE;
          end else if (rse_beat_valid && rse_beat_ready) begin
            for (int i = 0; i < WORDS_PER_RSE_BEAT; i++)
              data_buffer_r[rse_beat_cnt_r*WORDS_PER_RSE_BEAT + i]
                  <= rse_beat_data[i*FP32_W +: FP32_W];
            rse_beat_cnt_r <= rse_beat_cnt_r + 1'b1;
          end
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
```

### Step 1.2: Create a minimal smoke test

- One descriptor: `OST_BELIEF_PRIOR`, base `0x100`, `nbeats=1`.
- SPM backdoor init at `0x100` with 16 scalars `[0..15]`.
- Expect one `operand_valid_o` with `operand_o.data[i] == i`.

Run: `make run LEVEL=unit TEST=operand_stream_assembler VERILATOR_WARNINGS=none`
Expected: PASS.

---

## Task 2: Create `wb_to_wse_adapter`

**Files:**
- Create: `v/gbp_pe/compute_core/wb_to_wse_adapter.sv`
- Test: `nocbp_verilator/tests/unit/wb_to_wse_adapter.cc` *(optional minimal smoke test)*

### Step 2.1: Implement the adapter

```systemverilog
module wb_to_wse_adapter
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W
)(
    input  logic clk_i,
    input  logic rst_n_i,

    input  logic               wb_valid_i,
    output logic               wb_ready_o,
    input  writeback_record_t  wb_i,

    output logic                desc_valid_o,
    input  logic                desc_ready_i,
    output logic [SPM_ADDR_W-1:0] desc_base_addr_o,
    output logic [15:0]         desc_word_count_o,

    output logic                word_valid_o,
    input  logic                word_ready_i,
    output logic [FP32_W-1:0]   word_data_o
);

  typedef enum logic [1:0] { W_IDLE, W_DESC, W_STREAM } state_e;
  state_e state_r;
  logic [15:0] word_cnt_r;
  writeback_record_t wb_r;

  assign desc_valid_o       = (state_r == W_DESC);
  assign desc_base_addr_o   = wb_r.addr;
  assign desc_word_count_o  = wb_r.nwords;

  assign word_valid_o       = (state_r == W_STREAM) && (word_cnt_r < wb_r.nwords);
  assign word_data_o        = wb_r.payload[word_cnt_r];
  assign wb_ready_o         = (state_r == W_IDLE);

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      state_r   <= W_IDLE;
      word_cnt_r<= '0;
      wb_r      <= '0;
    end else begin
      case (state_r)
        W_IDLE: begin
          if (wb_valid_i && wb_ready_o) begin
            wb_r    <= wb_i;
            state_r <= W_DESC;
          end
        end
        W_DESC: begin
          if (desc_valid_o && desc_ready_i) begin
            word_cnt_r <= '0;
            state_r    <= W_STREAM;
          end
        end
        W_STREAM: begin
          if (word_valid_o && word_ready_i) begin
            word_cnt_r <= word_cnt_r + 16'd1;
            if (word_cnt_r + 16'd1 == wb_r.nwords)
              state_r <= W_IDLE;
          end
        end
        default: state_r <= W_IDLE;
      endcase
    end
  end

endmodule
```

### Step 2.2: Smoke test (optional)

- Drive a `writeback_record_t` with `addr=0x200`, `nwords=4`, payload `[1,2,3,4]`.
- Expect WSE descriptor then four words.

---

## Task 3: Rewrite `gbp_pe_compute_subsystem` for the new core

**Files:**
- Modify: `v/gbp_pe/gbp_pe_compute_subsystem.sv`

### Step 3.1: Change imports and internal signals

Add `import gbp_op_pkg::*;` and remove the old `compute_unit` signals:

```systemverilog
module gbp_pe_compute_subsystem
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    // keep existing parameters
)(...);
```

### Step 3.2: Build `cu_cmd_t`

```systemverilog
  cu_cmd_t cu_cmd;
  always_comb begin
    cu_cmd = '0;
    cu_cmd.op            = cmd_is_factor_i ? OP_MSG_F2V : OP_BELIEF;
    cu_cmd.factor_type   = FACTOR_SCALAR;
    cu_cmd.dim_i         = (cmd_dof_i == 4'd3) ? DIM_3
                         : (cmd_dof_i == 4'd6) ? DIM_6
                         : DIM_1;
    cu_cmd.dim_o         = cu_cmd.dim_i;        // uniform DOF for this test
    cu_cmd.direction     = 1'b0;
    cu_cmd.ctx_id        = 1'b0;
    cu_cmd.op_id         = {22'b0, cmd_node_id_i};
    cu_cmd.node_id       = {22'b0, cmd_node_id_i};
    cu_cmd.factor_id     = '0;
    cu_cmd.dst_addr      = SPM_ADDR_W'(cmd_node_id_i) << 4; // legacy WB address
    cu_cmd.aux_addr      = '0;
    cu_cmd.damping       = '0;
    cu_cmd.diag_lambda   = '0;
    cu_cmd.pivot_eps     = 32'h3DCCCCCD;        // 1e-12 approx
    cu_cmd.regularize_en = 1'b0;
    cu_cmd.degree        = 16'(cmd_adj_count_i);

    // Descriptor 0: state/prior (always used)
    cu_cmd.operand_desc[0].valid     = 1'b1;
    cu_cmd.operand_desc[0].kind      = cmd_is_factor_i ? OST_MSG_STATIC
                                                        : OST_BELIEF_PRIOR;
    cu_cmd.operand_desc[0].base_addr = {14'b0, cmd_state_base_i};
    cu_cmd.operand_desc[0].nbeats    = (cmd_state_words_i + 16'd15) / 16'd16;

    // For belief path, descriptor 1 is the message stream.
    // Base address will be provided by the control subsystem in V1.
    // In this migration we source OST_BELIEF_MSG from ns_valid_i instead.
    if (!cmd_is_factor_i) begin
      cu_cmd.operand_desc[1].valid = 1'b1;
      cu_cmd.operand_desc[1].kind  = OST_BELIEF_MSG;
      cu_cmd.operand_desc[1].base_addr = '0;
      cu_cmd.operand_desc[1].nbeats    = 16'(cmd_adj_count_i);
    end
  end
```

### Step 3.3: Instantiate the new datapath

- `compute_unit_wrapper`
- `operand_stream_assembler` on the read side, driven by `rd_req_o`.
- `wb_to_wse_adapter` on the write side, driving the existing `write_stream_engine`.
- A small `ns_to_operand_beat` converter for belief messages: when the wrapper requests `OST_BELIEF_MSG`, the assembler is bypassed and beats are assembled from `ns_valid_i`/`ns_data_i`.

Because this involves multiple files and careful back-pressure, implement it incrementally and verify with the new `gbp_pe.cc` test.

### Step 3.4: Tie off unused ports

- `spm_rd1_*` can be tied off (no staging reads in V0).
- `batch_done_o` can be tied to `done_valid_o` for V0 single-issue.

---

## Task 4: Update `gbp_pe.sv`

**Files:**
- Modify: `v/gbp_pe/gbp_pe.sv`

### Step 4.1: Remove debug prints

Delete or comment out the `GBP_PE_DBG` `$display` block around line 485.

### Step 4.2: Keep scalar command interface

No change required to the top-level scalar ports; `gbp_pe_compute_subsystem` now builds `cu_cmd_t` internally.

---

## Task 5: Rewrite `gbp_pe.cc` for the new core

**Files:**
- Modify: `nocbp_verilator/tests/unit/gbp_pe.cc`

### Step 5.1: Pick a supported dimension

Change TC3 and TC4 from `dof=2` to `dof=3` (`DIM_3`). If you want a quicker first milestone, start with `dof=1` (`DIM_1`).

### Step 5.2: Lay out SPM memory as new-core operand streams

For a `DIM_3` belief update (variable node), the SPM must contain:
- `OST_BELIEF_PRIOR` at some base: `eta[3]`, `L_packed[6]` (upper-triangular), `mu_old[3]` = 12 scalars.
- `OST_BELIEF_MSG` beats: one beat per neighbor, each `eta[3]`, `L_packed[6]` = 9 scalars.

For a `DIM_3` message update (factor node), the SPM must contain four separate regions:
- `OST_MSG_STATIC`: target state.
- `OST_CAV_FACTOR_O`, `OST_CAV_BELIEF_O`, `OST_CAV_OLD_TO_O`: cavity streams.

### Step 5.3: Compute golden expected values

Use a small Python script (similar to `nocbp_verilator/tests/tools/gbp_golden_ref.py`) to compute expected `mu`, `Lambda`, and residuals. Embed the values in the C++ test.

### Step 5.4: Update whitebox local-reader usage

The current test uses `wb_lr_valid_i` to inject the factor state directly into the accumulator. With the new core, local neighbor messages should be fed as `OST_BELIEF_MSG` operand beats. Either:
- Continue using `wb_lr_valid_i` and have the subsystem convert that stream, or
- Write the neighbor messages into SPM and let the subsystem read them.

This plan recommends keeping `wb_lr_valid_i` as a message-injection path and converting it inside the subsystem.

---

## Task 6: Iterate until `gbp_pe` unit test passes

**Run:**
```bash
cd nocbp_verilator
make run LEVEL=unit TEST=gbp_pe VERILATOR_WARNINGS=none
```

**Expected result:** All test cases pass.

---

## Spec coverage check

| Spec requirement | Task |
|------------------|------|
| New core wrapped by `compute_unit_wrapper` | Task 3 |
| 16-word operand stream from SPM | Task 1 + 3 |
| `OST_BELIEF_PRIOR` / `OST_BELIEF_MSG` protocol | Task 3 + 5 |
| `OP_MSG_F2V` operand windows/cavity streams | Task 5 (SPM layout) |
| Writeback via `writeback_record_t` | Task 2 + 3 |
| `gbp_pe` end-to-end regression | Task 6 |

**Open question / risk:** The legacy `gbp_pe_control_subsystem` still produces scalar command fields. If a future milestone needs the control subsystem to generate full `cu_cmd_t` with multiple operand descriptors, that subsystem will also need updating. This plan localizes that change inside `gbp_pe_compute_subsystem` to minimize blast radius.
