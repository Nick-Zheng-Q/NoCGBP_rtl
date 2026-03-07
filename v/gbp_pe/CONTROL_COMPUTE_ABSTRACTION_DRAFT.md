# GBP Accelerator Control/Compute Abstraction Draft (Revised)

## Goal
Define a hardware-realistic split for GBP execution, based on real simulator code paths, with detailed focus on:
- (3) choose node by priority + RR,
- (4) check node computability,
- (5) compute one node.

Reference code:
- `nocbp_simulator/pe/ProcessingElement.cpp:173` (`cycle`)
- `nocbp_simulator/pe/ProcessingElement.cpp:805` (`compute_variable_node`)
- `nocbp_simulator/pe/ProcessingElement.cpp:833` (`compute_factor_node`)
- `nocbp_simulator/pe/ProcessingElement.cpp:949` (`can_compute_variable_node`)
- `nocbp_simulator/pe/ProcessingElement.cpp:977` (`can_compute_factor_node`)

## Scope Clarification
From the simulator cycle list (1..8), this document details 3/4/5 as requested.

- 3/4/5 are the compute scheduler core.
- 1/2/6/7/8 are system glue around the core and are listed only as interfaces.

## Layer Split (Final)

### Task-level control (control_unit)
Owns:
- priority phase (`factor` / `variable`)
- RR pointer advance (`next_fac_index` / `next_var_index` equivalent)
- computability gating decision
- issue exactly one task command when eligible
- commit scheduler state on compute completion

Does not own:
- arithmetic micro-steps of factor/variable update

### Operator-level execution (compute_unit)
Owns:
- factor/variable kernel micro-sequencing on single SIMD
- robustify / optional relinearization / damping / message compute math
- write payload generation and completion status

Does not own:
- global phase/RR/pending scheduling policy

## Required Command Contract (Control -> Compute)
This contract must identify one selected node task. It should be narrow and index-based.


### Frozen v1 fields

| Field | Width | Driven by | Consumed by | Reset | Notes |
|---|---:|---|---|---|---|
| `cmd_valid` | 1 | control | compute | 0 | Command offer valid |
| `cmd_ready` | 1 | compute | control | 1 (idle) | Command acceptance backpressure |
| `cmd_kind` | 1 | control | compute | 0 | `0=variable`, `1=factor` |
| `cmd_node_idx` | `W_NODE` | control | compute | 0 | Local node index |
| `cmd_iter0` | 1 | control | compute | 0 | Factor first-iter fallback hint |
| `cmd_txn_id` | 8 | control | compute | 0 | Transaction association |
| `rsp_done` | 1 | compute | control | 0 (or 1 if idle-high impl) | Completion pulse/level per timing rules |

### Acceptance and commit events (canonical)
- **Command accept event**: `cmd_accept = cmd_valid && cmd_ready`
- **Completion commit event**: `rsp_done && inflight_match`

Where `inflight_match` means completion corresponds to the currently inflight command context tracked by control.

### Timing/handshake rules
1. v1 is **single-inflight**: at most one accepted command may be outstanding.
2. control may assert `cmd_valid` only when it has a fully-formed command tuple.
3. compute may deassert `cmd_ready` while busy; control must hold command fields stable while `cmd_valid=1` and `cmd_ready=0`.
4. scheduler state advance for dispatch-side fields happens only on `cmd_accept`.
5. scheduler commit-side updates happen only on `rsp_done && inflight_match`.
6. a second command must not be accepted before the prior command commit event.

### Ownership split (frozen)
- **control-owned**: `cmd_valid`, `cmd_kind`, `cmd_node_idx`, `cmd_iter0`, `cmd_txn_id`
- **compute-owned**: `cmd_ready`, `rsp_done`

### Legacy compatibility note (`start/done/mode`)
- Current RTL interface in `v/gbp_pe/interfaces.sv` exposes `start/done/mode`.
- v1 migration keeps behavior equivalent by mapping:
  - `start` -> `cmd_valid` (with implicit single-cycle offer)
  - `mode` -> `cmd_kind` (after width/type update)
  - `done` -> `rsp_done`
- During transition, implementation must keep one canonical acceptance/commit policy from this section; legacy names are transport aliases only.

Notes:
- `cmd_kind` is 1b (not 2b).
- Heavy config (beta, min_linear_iters, damping mode, adjacency sizes) should come from compute-side context tables indexed by `cmd_kind/cmd_node_idx`, not widened onto the command bus.

## Detailed Mapping: Simulator (3/4/5) -> Hardware

### 3) Choose node by priority + RR
Simulator behavior (`cycle`):
- if `priority_factor_first`, scan factor nodes from `next_fac_index` in RR order
- skip nodes already visited in this phase (`factor_visited_in_phase`)
- pick first node passing `can_compute_factor_node`
- if `priority_factor_first == false`, do symmetric scan on variable side
- process at most one node per eligible cycle, and only in current priority phase

Hardware mapping:
- maintain `phase` + `rr_ptr`
- per cycle, evaluate candidate window in RR order until first eligible node
- launch at most one command (`cmd_valid`) per scheduling turn
- update `rr_ptr = selected + 1` only on command accept

State in control:
- `phase` (1b)
- `rr_ptr_factor`, `rr_ptr_variable`
- `visited_in_phase` bitmaps (factor/variable)

## Scheduler State Ownership and Atomic Transition Rules (Frozen v1)

### Ownership matrix
| State group | Writer | Read by | Notes |
|---|---|---|---|
| `phase` | control | control, compute(cmd_kind source only) | Compute must not modify |
| `rr_ptr_factor`, `rr_ptr_variable` | control | control | Advanced on accept event only |
| `visited_in_phase` bitmaps | control | control | Updated on accept and phase reset |
| `active[]` | control | control, compute (indirect via command issue) | Updated on commit/policy events |
| `has_new[]` | control | control | Set by ingress path, cleared on commit |
| `has_stale[]` | control | control | Updated on commit/policy |
| `inflight` / inflight context | control | control | Set on accept, cleared on matching done |
| per-node `iter0`/iter counter | control | control | Compute reads via command hint only |

Single-owner invariant: scheduler state is control-owned; compute only consumes command fields and returns response.

### Atomic events
Only two events may mutate scheduler core state.

1) **Dispatch-accept event**
- Trigger: `cmd_accept = cmd_valid && cmd_ready`
- Allowed updates (same atomic update group):
  - latch inflight context (`kind`, `node_idx`, `txn_id`)
  - mark selected node visited
  - advance corresponding RR pointer
- Forbidden in this event:
  - phase switch
  - pending completion-side decrement/cleanup

2) **Done-commit event**
- Trigger: `rsp_done && inflight_match`
- Allowed updates (same atomic update group):
  - clear inflight context
  - completion-side updates (`active/has_new/has_stale`, pending counters)
  - phase switch decision and boundary updates
- Forbidden in this event:
  - changing selected command identity from another accept

### Race-prevention rules
- If `cmd_accept` and `rsp_done` coincide, process in deterministic order: commit old inflight first, then accept new command.
- No scheduler updates on `rsp_done` when `inflight_match==0`.
- No RR/visited updates without `cmd_accept`.
- No completion-side updates without `rsp_done && inflight_match`.

### 4) Check node computability
Simulator predicates:

Variable (`can_compute_variable_node`):
- node active
- no stale factor messages for that node
- at least one `new_messages[j] == true`

Factor (`can_compute_factor_node`):
- node active
- stale variable messages block compute, except first-iter fallback
  - fallback condition: `has_stale && fac_iter_times[idx] == 0`
- at least one `new_messages[j] == true`

Hardware mapping:
- keep per-node readiness bitfields in control-side scheduler state
- compute eligible bit as combinational predicate over:
  - `active[idx]`
  - `has_new[idx]`
  - `has_stale[idx]`
  - `iter0[idx]` (factor only)

Minimum tracked bits per node in control:
- `active`
- `has_new`
- `has_stale`
- `iter0` (or small iter counter) for factor
- `inflight`

## Control Scheduler FSM Spec (Phase + RR + Computability)

### State set (control-side only)
- `ST_SELECT_PHASE`: pick active phase (`factor`/`variable`) for this scheduling turn.
- `ST_SCAN_RR`: scan candidates from current phase RR pointer until first eligible node or full wrap.
- `ST_DISPATCH_CMD`: drive command fields and wait for `cmd_accept`.
- `ST_WAIT_DONE`: wait for `rsp_done && inflight_match`.
- `ST_PHASE_UPDATE`: handle phase-switch / wrap / no-eligible policy and return to `ST_SELECT_PHASE`.

### Guard conditions
- `eligible(node)`:
  - variable: `active && has_new && !has_stale`
  - factor: `active && has_new && (!has_stale || iter0)`
- `phase_empty`: no eligible node found after one full RR wrap in current phase.
- `phase_complete`: phase-level pending count reaches zero at done-commit boundary.

### Output actions by state
| State | Outputs driven | Transition on TRUE | Else |
|---|---|---|---|
| `ST_SELECT_PHASE` | none | `ST_SCAN_RR` | `ST_SCAN_RR` |
| `ST_SCAN_RR` | candidate index + eligibility eval | if eligible found -> `ST_DISPATCH_CMD` | if wrapped with none -> `ST_PHASE_UPDATE` |
| `ST_DISPATCH_CMD` | `cmd_valid=1`, command tuple stable | if `cmd_accept` -> `ST_WAIT_DONE` | stay `ST_DISPATCH_CMD` |
| `ST_WAIT_DONE` | none (hold inflight context) | if `rsp_done && inflight_match` -> `ST_PHASE_UPDATE` | stay `ST_WAIT_DONE` |
| `ST_PHASE_UPDATE` | apply commit-side updates and phase policy | always -> `ST_SELECT_PHASE` | n/a |

### RR wrap and no-eligible behavior (frozen)
- One scheduling turn scans at most one full ring in current phase.
- If no eligible node found (`phase_empty=1`), control enters `ST_PHASE_UPDATE` and applies phase policy:
  - toggle phase if opposite phase has pending/eligible work,
  - otherwise remain in phase and idle-scan next turn.

### Phase switch policy
- Phase switch decision is evaluated only in `ST_PHASE_UPDATE`.
- `iter_count` increments only after completing a full `FACTOR + VARIABLE` pair boundary.
- `visited_in_phase` bitmap is cleared when entering a new phase.

### 5) Compute one node (single SIMD)

#### Variable path (`compute_variable_node`)
Simulator sequence:
1. `var_node->update_belief(vars_message_list[idx])`
2. mark stale_flags true (consumed)
3. clear active/pending/inflight for this variable

Exact math in `VariableNode::update_belief(messages)`:
- `belief_eta = prior_eta + sum_i(msg_i.eta)`
- `belief_lam = prior_lam + sum_i(msg_i.lam)`
- `sigma = inverse(belief_lam)`
- `mu = sigma * belief_eta`

Hardware operator micro-flow:
1. load prior + inbound message blocks (`eta`, `lam`)
2. vector/matrix reduction accumulation:
   - vector add tree for `eta`
   - matrix add tree for `lam`
3. SPD solve stage:
   - preferred: factorize `belief_lam` once and solve for `mu`
   - optional: solve with identity RHS to produce `sigma`
4. write belief outputs, assert done

Commit in control after done:
- clear `active/has_new` as needed
- set `has_stale` per policy
- clear `inflight`
- decrement pending counters

#### Factor path (`compute_factor_node`)
Simulator sequence:
1. choose input source:
   - prior fallback when stale and first iter
   - else cached variable->factor messages
2. `robustify_loss()`
3. optional relinearization branch:
   - compute means + `norm_diff`
   - compare with `beta` and `min_linear_iters`
   - maybe `compute_factor(...)` and reset relin counter
4. damping selection (`eta_damping_to_use`)
5. `compute_messages(eta_damping_to_use)`
6. mark stale_flags true
7. clear active/pending/inflight for factor

Cycle-wrapper actions immediately after compute call (still part of item 5 end-to-end semantics):
- clear `new_messages` flags for that node
- `send_to_network(...)` for generated messages
- increment per-node iteration counter (`fac_iter_times` / `var_iter_times`)

Hardware operator micro-flow (single SIMD):
1. load node context and adjacency message vectors
2. source-select mux (prior fallback vs cached)
3. robustify stage:
   - residual/norm pipeline: subtract, dot, sqrt, divide
   - piecewise loss update for `adaptive_gauss_noise_var`
   - rescale existing factor (`eta`,`lam`) by variance ratio
4. relinearization decision stage:
   - compute means from each adjacent belief (`mu_i = solve(lam_i, eta_i)`)
   - concat means, compute `norm_diff`
   - compare against `beta` and `min_linear_iters`
5. optional relinearization compute (`compute_factor`):
   - Jacobian `J = jac_fn(linpoint)`
   - prediction `pred = meas_fn(linpoint)`
   - build weighted normal form:
     - `lambda_factor = J^T W J`
     - `eta_factor = J^T W (J*linpoint + measurement - pred)`
6. damping parameter select (`eta_damping_to_use`)
7. message compute loop per adjacent variable `v` (`compute_messages`):
   - build cavity-adjusted `eta_factor`, `lam_factor`
   - partition into blocks (`loo,lono,lnoo,lnono`)
   - Schur-form message update:
     - `new_lam_v = loo - lono * inv(lnono) * lnoo`
     - `new_eta_v = eo  - lono * inv(lnono) * eno`
   - apply eta damping:
     - `damped_eta_v = (1-d)*new_eta_v + d*old_eta_v`
8. pack outputs, done

## Compute Unit Operator Inventory (Detailed)

### A. Core linear algebra operators
- vector add/sub (eta accumulation and cavity update)
- matrix add/sub (lam accumulation and cavity update)
- matrix-vector multiply (`J*x`, `J^T*r`)
- small dense matrix-matrix multiply (`J^T*W*J`, Schur middle products)
- block extract/concat/repack (for Schur partitioning)

### B. Solve/inversion operators
- SPD factorization + triangular solve (preferred replacement for explicit inverse)
- optional explicit inverse path only for debug/parity mode

### C. Scalar/nonlinear operators
- norm2 + sqrt (residual and mahalanobis distance)
- divide / reciprocal (noise scaling, weighting)
- compare/select (robust loss branches, relin gate)
- damping blend (`a*x + b*y`)

## Functional Unit Reuse Plan (Single SIMD)

### Hardware-first reuse model (not opcode grouping)

Physical compute fabric should be minimized to one math backbone:

1. `VMA` (Vector MAC Array, N lanes)
- the only high-throughput arithmetic block
- executes add/mul/fma, dot, reduction fragments, tiled matrix ops

2. `SFU` (Shared Scalar Unit)
- reciprocal/div/sqrt/compare/select
- serves robustify and solve normalization steps

3. `Data Mover`
- stream-local staging buffers for tiles and message blocks
- supports overlap: load next tile while VMA computes current tile

Note: this is not a mandatory standalone compute-local LSU block in v1.
Current system already has read/write stream engines as global load/store path.

Important: `MAT_ENGINE` and `SOLVE_ENGINE` are **microcode modes** on top of the same `VMA+SFU`, not separate large arithmetic arrays.

### Time-multiplexing plan on the same fabric

#### Variable update reuse
- `belief_eta/lam` accumulation: `VMA`
- `mu = solve(lam, eta)`: `VMA` for factorization/triangular ops + `SFU` for reciprocal/normalization
- optional `sigma`: same solve microcode reusing `VMA+SFU`
- writeback through existing write stream path

#### Factor update reuse
- robustify norm pipeline: `VMA` (sumsq/dot) + `SFU` (sqrt/div)
- `J^T W J`, `J^T W r`: tiled on `VMA` (no extra matrix engine)
- Schur elimination solve (`lnono` blocks): same solve microcode on `VMA+SFU`
- damping blend and pack: `VMA`, then writeback

### Structural hazard policy (single-SIMD reality)
- only one heavy math op issues per cycle group on `VMA`
- solve microcode has priority over background accumulations once entered
- robustify scalar ops can overlap with data-mover transfers, but not with SFU-dependent solve step

### Microstep-to-fabric mapping example (factor path)
| Microstep | VMA | SFU | Data mover |
|---|---|---|---|
| load adjacency blocks | idle | idle | read current, prefetch next |
| residual + norm2 | active (sub/dot) | active (sqrt) | prefetch |
| robust weight update | active (scale) | active (div/compare) | idle |
| build `J^T W J` | active (tiled fma) | idle | stream tiles |
| build `J^T W r` | active (gemv) | idle | stream tiles |
| Schur solve | active (factor/solve kernels) | active (reciprocal/div) | hold working set |
| damping blend | active (axpby) | idle | stage output |
| store output | idle | idle | writeback |

### Reuse consequence
- area is controlled (one main arithmetic array)
- throughput depends on schedule quality and memory overlap, not extra engines
- control must provide deterministic microstep order to avoid VMA/SFU contention

## Parallelization Strategy Under Single SIMD

### 1) Intra-op lane parallelism
- pack vector/matrix rows across SIMD lanes
- keep symmetric matrix update in packed form to avoid redundant work

### 2) Inter-edge pipelining (factor message loop)
- while edge `v` is in solve stage, prefetch edge `v+1` cavity blocks
- overlap output pack of edge `v-1` with compute of edge `v`

### 3) Memory-compute overlap
- ping-pong staging buffers for adjacency blocks
- explicit prefetch window for next node context before current node commit

### 4) Control-compute overlap
- control performs next eligible-node scan while compute is running (no new dispatch until done)

## Numeric Stability and Throughput Notes
- Prefer solve-based implementation over explicit inverse in hot loops.
- Add small diagonal floor before solve when near-singular blocks are detected.
- Keep accumulation order deterministic to reduce run-to-run drift.
- Throughput bottleneck is usually Schur solve in factor path; prioritize reuse-friendly block sizes.

## Compute Command Decode and Admission Policy (Task 4)

### Decode rules
- `cmd_kind=0`: variable-node kernel path.
- `cmd_kind=1`: factor-node kernel path.
- `cmd_node_idx`: selects node context table row for the active phase domain.
- `cmd_iter0`: enables factor stale-message first-iteration fallback behavior only.
- `cmd_txn_id`: copied into inflight context and returned for completion association.

### Admission policy (`cmd_ready`)
- `cmd_ready=1` iff compute has no inflight command and internal microstep state is idle.
- `cmd_ready=0` during any active variable/factor microstep sequence.
- `cmd_ready` may return to 1 only after completion response is raised and internal commit boundary is crossed.

### Completion association
- Compute completion is accepted by control only when `rsp_done && inflight_match`.
- `inflight_match` must check at least `{cmd_kind, cmd_node_idx, cmd_txn_id}` against latched inflight context.

## Unit-Test Expansion Plan (Task 7/8)

### Control unit tests (`control_unit_top`)
- `CU_RR_WRAP`: RR pointer wrap-around with sparse eligible map.
- `CU_PHASE_EMPTY_SWITCH`: no eligible node in current phase forces policy-defined phase update.
- `CU_ACCEPT_ONLY_UPDATES`: RR/visited/inflight mutate only at `cmd_accept`.
- `CU_COMMIT_ONLY_UPDATES`: completion-side state mutates only at `rsp_done && inflight_match`.
- `CU_DONE_RACE`: done same-cycle as new offer follows deterministic ordering (commit old then accept new).
- `CU_LIVENESS_TIMEOUT`: under finite stalls, scheduler eventually dispatches or enters explicit no-work idle.

### Compute unit tests (`compute_unit`)
- `CP_DECODE_VAR`: `cmd_kind=0` enters variable path only.
- `CP_DECODE_FAC`: `cmd_kind=1` enters factor path only.
- `CP_ONE_INFLIGHT`: second command blocked while busy.
- `CP_READY_RECOVERY`: `cmd_ready` returns high after completion boundary.
- `CP_FACTOR_BRANCH_ON`: relin-enabled branch sequence reaches done.
- `CP_FACTOR_BRANCH_OFF`: relin-bypass branch sequence reaches done.
- `CP_DONE_ASSOC`: done with mismatched inflight tag is rejected by control-side checker.

## Integration Test Plan (Task 9)

### End-to-end checkpoints (`pe_top_integration`)
For each transaction, assert ordered checkpoints:
1. META dispatch accepted
2. follow-up reads issued
3. compute command accepted
4. compute completion observed
5. write emission observed

### Required scenarios
- `IT_SINGLE_TXN_ORDER`: one transaction strict ordering.
- `IT_MULTI_TXN_NO_CROSS`: two+ transactions no cross-association by txn/node.
- `IT_BACKPRESSURE_LIVENESS`: prolonged ready-low windows still complete within timeout.
- `IT_PHASE_SWITCH`: factor->variable transition boundary preserved.

## Unified Verification Matrix and Exit Gates (Task 10)

| Requirement | Unit Check | Integration Check | Gate |
|---|---|---|---|
| Command field semantics | CP_DECODE_VAR/CP_DECODE_FAC | IT_SINGLE_TXN_ORDER | Must pass |
| Single-inflight admission | CP_ONE_INFLIGHT | IT_BACKPRESSURE_LIVENESS | Must pass |
| Accept/commit atomicity | CU_ACCEPT_ONLY_UPDATES / CU_COMMIT_ONLY_UPDATES | IT_MULTI_TXN_NO_CROSS | Must pass |
| RR/phase policy | CU_RR_WRAP / CU_PHASE_EMPTY_SWITCH | IT_PHASE_SWITCH | Must pass |
| Liveness | CU_LIVENESS_TIMEOUT | IT_BACKPRESSURE_LIVENESS | Must pass |

### Regression command order
1. `make -C nocbp_verilator run LEVEL=unit TEST=control_unit_top`
2. `make -C nocbp_verilator run LEVEL=unit TEST=stream_dispatcher_top`
3. `make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"`

### Exit gates
- Any unit failure: stop immediately.
- Any integration timeout or ordering violation: fail release gate.
- No subjective/manual override for failed gates.

## Final Verification Wave (F1-F4)

### F1 Plan Compliance Audit
- Verify every plan checkbox maps to explicit section/content in this document.

### F2 Code Quality Review
- Ensure all terms are deterministic (no ambiguous "maybe" semantics), no conflicting ownership.

### F3 Manual QA (doc-level)
- Walk one variable and one factor transaction trace manually against sections above.

### F4 Scope Fidelity
- Confirm no quantization content and no compute-local standalone LSU requirement in v1.

## Cycle Example (Factor Task on Single SIMD)
Example timeline for one selected factor task `f7`:

- `C0`: control selects `f7` by phase+RR, computability passes, sets `inflight[f7]=1`
- `C1`: issue cmd `{valid=1, kind=factor, node_idx=f7, iter0=1/0, txn_id}`
- `C2`: compute loads input vectors/context
- `C3`: robustify stage
- `C4`: relin decision
- `C5..Ck`: optional relin + message compute loop on SIMD
- `Ck+1`: compute done + output payload valid
- `Ck+2`: control commit (clear inflight, update has_new/has_stale/active, RR advance)

Important rule:
- control must update scheduler state atomically on command accept and on done commit (avoid TOCTOU race between eligibility and dispatch).

## What Stays Outside 3/4/5 (Brief)
- receive from network
- pending/inflight load request management
- send messages to network
- priority switch coordination across PEs
- remove consumed messages

These are still needed, but they are not the compute scheduler core discussed here.

## Current RTL Gap vs This Model
Current `v/gbp_pe/control_unit.sv` drives only start/mode pulses for compute, which is insufficient for mapping `compute_factor_node`/`compute_variable_node` kernels.

The next RTL step is to add the command contract above and move factor/variable micro-steps into compute_unit internal FSM.

## Top Implementation Pitfalls
1. Duplicating computability logic in both control and compute (will diverge)
2. Letting compute mutate scheduler pointers/masks directly (breaks determinism)
3. Not latching inflight atomically at dispatch accept (race under backpressure)

## Review Decisions Needed
1. `iter_count` increment point: per phase switch or per factor+variable pair
2. first version status return: `done` only vs `done+error`
3. whether `visited_in_phase` is required in v1 or can be simplified to strict RR+pending bitmap
