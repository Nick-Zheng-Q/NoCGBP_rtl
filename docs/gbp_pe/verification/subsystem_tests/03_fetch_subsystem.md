# Fetch Subsystem Integration Test

## 1. Test Objective

Verify that the Fetch Subsystem correctly:
- Tracks remote edges via ScoreboardPrefetcher
- Issues FETCH_REQUEST via Pull Client
- Receives FETCH_RESPONSE via Response Collector
- Writes response data to STAGING
- Notifies ScoreboardPrefetcher of completion
- Handles batch closure and STAGING reset

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Fetch Subsystem                          │
│                                                              │
│   ┌──────────────────┐        ┌─────────────┐              │
│   │ Scoreboard       │──req──▶│ Pull Client │              │
│   │ Prefetcher       │        └──────┬──────┘              │
│   │                  │◀──comp───────┘                     │
│   │                  │                                       │
│   │  staging_reserve │──────────┐                           │
│   │  staging_batch_cl│◀─────────┘                           │
│   └──────────────────┘        ┌──────────────────┐          │
│                               │ Response Collector│          │
│                               │  (STAGING Allocator)│         │
│                               └─────────┬────────┘          │
│                                         │                   │
│   NoC Adapter ◀─────────────────────────┘                   │
│   (FETCH_RESPONSE)                                          │
└─────────────────────────────────────────────────────────────┘
```

**Modules under test:** `scoreboard_prefetcher`, `pull_client`, `response_collector`

**Mocked modules:** `noc_adapter` (TX/RX), `metadata_scanner` (adjacency injection)

## 3. Preconditions

- Clock: 100MHz
- Reset: Active low
- Scoreboard empty, STAGING empty
- `SCOREBOARD_DEPTH = 64`, `STATE_WORDS_W = 6`

## 4. Test Stimulus

### ✅ 4.1 Test Case 1: Remote Edge Fetch — IMPLEMENTED

**Scenario**: One remote edge, notification triggers fetch request.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | adj_valid | 1 | Register edge |
| T+1   | adj_current_node_id | 0x10 | Consumer |
| T+1   | adj_neighbor_id | 0x20 | Producer (remote) |
| T+1   | adj_is_local | 0 | Remote |
| T+1   | adj_last | 1 | Last edge of this node |
| T+2   | adj_valid | 0 | Clear |
| T+3   | rx_notif_valid | 1 | Notification from producer |
| T+3   | rx_notif_source_node_id | 0x20 | Match adj_neighbor_id |
| T+4   | rx_notif_valid | 0 | Clear |
| T+5   | tx_fetch_req_valid | 1 | Fetch request issued |
| T+5   | tx_fetch_req_target_node_id | 0x20 | Correct target |

> **Implementation note:** Current test uses top-level `gbp_pe_fetch_subsystem_top` where `rx_fetch_resp_*` is tied off. Only the request path (adj → notification → tx_fetch_req) is verified.

### ✅ 4.2 Test Case 2: Local Edge Filtered — IMPLEMENTED

**Scenario**: Local adjacency should NOT produce fetch request.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | adj_valid | 1 | Register edge |
| T+1   | adj_is_local | 1 | Local |
| T+2   | adj_valid | 0 | Clear |
| ...   | Wait 10 cycles | | |
| T+12  | tx_fetch_req_valid | 0 | No fetch issued |

### ✅ 4.3 Test Case 3: TX Backpressure — IMPLEMENTED

**Scenario**: `tx_fetch_req_ready_i = 0` stalls the fetch request.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | tx_fetch_req_ready_i | 0 | Stall TX |
| T+2   | adj_valid + rx_notif | | Issue remote edge + notification |
| T+N   | tx_fetch_req_valid | 1 | Valid asserts even if ready=0 |
| T+N+1 | tx_fetch_req_ready_i | 1 | Release ready |
| T+N+1 | tx_fetch_req_valid | 1 | Request accepted |

### ❌ 4.4 Test Case 4: Response Full Path — NOT YET IMPLEMENTED

**Scenario**: Simulate complete fetch response → STAGING → remote_valid.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | adj_valid + rx_notif | | Register remote edge |
| T+5   | tx_fetch_req_valid | 1 | Fetch issued |
| T+10  | rx_fetch_resp_valid | 1 | Response metadata |
| T+10  | rx_fetch_resp_state_words | 4 | 4 words |
| T+11  | rx_fetch_resp_data_valid | 1 | Data beat 0 |
| T+12  | rx_fetch_resp_data_valid | 1 | Data beat 1 |
| T+13  | rx_fetch_resp_done_valid | 1 | Response done |
| T+14  | spm_wr_valid | 1 | Written to STAGING |
| T+15  | remote_valid_o | 1 | Data to accumulator |

> **Blocker:** Current `gbp_pe_fetch_subsystem_top` ties off `rx_fetch_resp_*`. Need to drive these signals in testbench.

### ❌ 4.5 Test Case 5: Multi-Entry Deduplication — NOT YET IMPLEMENTED

**Expected**: Same `adj_neighbor_id` registered 3 times → only 1 fetch request.

### ❌ 4.6 Test Case 6: Scoreboard Full — NOT YET IMPLEMENTED

**Expected**: Fill `SCOREBOARD_DEPTH` slots → `scoreboard_full_o` asserts → new edges blocked.

### ❌ 4.7 Test Case 7: Notification Mismatch — NOT YET IMPLEMENTED

**Expected**: Notification with unmatched `source_node_id` → ignored, no fetch issued.

### ❌ 4.8 Test Case 8: Node Ready Bitmap — NOT YET IMPLEMENTED

**Expected**: After local edge registered + remote edge fetched, `node_ready_o[consumer]` asserts.

## 5. Expected Output

### 5.1 Test Case 1: Single Remote Edge

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | adj_ready | 1 | Edge registered |
| T+3   | staging_reserve_valid | 1 | Reserve STAGING |
| T+4   | tx_fetch_req_valid | 1 | Fetch issued |
| T+6   | staging_wr_valid | 1 | Write to STAGING |
| T+6   | staging_wr_addr | STAGING_BASE | Base address |
| T+10  | complete_valid | 1 | Completion |
| T+10  | complete_txn_id | 0x00 | Correct edge |
| T+10  | scoreboard_occupancy_o | 0 | Cleared |

### 5.2 Test Case 2: Batch Closure

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+N   | staging_batch_closed | 1 | Batch closed |
| T+N+1 | fetch_req_valid_o | 0 | No new fetches |
| T+M+1 | staging_bump reset | staging_base | Reset for new batch |

### 5.3 Test Case 3: Scoreboard Full

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | scoreboard_full_o | 1 | Full asserted |
| T+4   | tx_fetch_req_valid | 0 | No new fetch |
| T+7   | tx_fetch_req_valid | 1 | Resume after completion |

## 6. Timing Diagram

```
Single Edge Fetch:
           ___     ___     ___     ___     ___     ___     ___     ___
notif     ___|        |________________________________________________
                          ________
fetch_req _______________|        |____________________________________
                                  ________    ________    ________
resp_data _______________________|   W0   |__|   W1   |__|   W2   |__
                                          ________
complete  _______________________________|        |___________________
```

## 7. Pass/Fail Criteria

- [ ] Edge registered → notification → fetch request sequence correct
- [ ] STAGING reservation before fetch issue
- [ ] Response data written to correct STAGING address
- [ ] `complete_valid` matches `rx_fetch_resp_done_valid`
- [ ] `complete_txn_id` matches original edge
- [ ] Batch closure stops new fetches
- [ ] `batch_done` resets STAGING for next batch
- [ ] Scoreboard full blocks new fetches
- [ ] Completion frees scoreboard slot

## 8. Corner Cases

1. **Zero remote edges**: No fetches issued
2. **Notification before edge registered**: Notification dropped/ignored
3. **Duplicate notification**: Same edge notified twice
4. **Response arrives before reservation**: Should not happen (design invariant)
5. **Out-of-order responses**: Responses arrive in different order than requests
6. **Reset during in-flight fetch**: Clean abort, no STAGING leak

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../02_SPM_AND_METADATA.md` §5 | STAGING design, batching |
| `../../04_PE_MICROARCHITECTURE.md` §2.4 | ScoreboardPrefetcher |
| `../../05_INTERFACES.md` §2.4-2.7 | Scoreboard, Pull Client, Response Collector |
| `../../06_PE_CONTROL_FLOW.md` §2.2 | Background fetch issue |
| `../unit_tests/04_scoreboard_prefetcher.md` | Scoreboard unit test |
| `../unit_tests/05_pull_client.md` | Pull Client unit test |
| `../unit_tests/07_response_collector.md` | Response Collector unit test |
