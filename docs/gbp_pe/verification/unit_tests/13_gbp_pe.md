# gbp_pe Unit Test (Top-Level Shell)

## 1. Test Objective

Verify that the `gbp_pe` top-level module correctly:
- Wraps `gbp_pe_endpoint_adapter` and `pe_top`
- Connects to manycore `link_sif` interface
- Handles incoming requests from NoC
- Sends outgoing responses/completions
- Manages barrier synchronization

## 2. Preconditions

- Module: `gbp_pe`
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: Reset, all outputs deasserted

## 3. Test Stimulus

### 3.1 Test Case 1: Incoming Store Request

**Scenario**: External PE sends a store to this PE's address space.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | link_sif_i.fwd.v | 1 | Forward packet valid |
| T+1   | link_sif_i.fwd.data | PACKET | Store packet |
| T+1   | link_sif_i.fwd.data.addr | 0x1000 | Target address |
| T+1   | link_sif_i.fwd.data.op_v2 | e_remote_store | Store operation |
| T+1   | link_sif_i.fwd.data.payload | 0xDEAD | Store data |
| T+1   | link_sif_i.fwd.data.src_x_cord | 3 | Source X |
| T+1   | link_sif_i.fwd.data.src_y_cord | 2 | Source Y |
| T+2   | link_sif_i.fwd.v | 0 | Clear |

### 3.2 Test Case 2: Compute Done Response

**Scenario**: Internal compute completes, sends done packet to NoC.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | wb_cmd_valid_i | 1 | Whitebox command (if enabled) |
| T+1   | wb_cmd_kind_i | 2'b01 | Compute done kind |
| T+2   | wb_cmd_valid_i | 0 | Clear |

### 3.3 Test Case 3: Barrier Synchronization

**Scenario**: Barrier signal propagation across PEs.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | barrier_data_i | 1 | Barrier signal from neighbor |
| T+2   | barrier_data_i | 0 | Clear barrier |

## 4. Expected Output

### 4.1 Test Case 1: Incoming Store Request

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | link_sif_o.fwd.ready_and_rev | 1 | Ready to accept |
| T+2   | core_req_v_o | 1 | Request to PE logic |
| T+2   | core_req_addr_o | 0x1000 | Request address |
| T+2   | core_req_data_o | 0xDEAD | Request data |
| T+2   | core_req_we_o | 1 | Write enable |

### 4.2 Test Case 2: Compute Done Response

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | link_sif_o.fwd.v | 1 | Response packet valid |
| T+2   | link_sif_o.fwd.data.op_v2 | e_remote_store | Store operation |
| T+2   | out_credit_or_ready_o | 1 | Credits available |

### 4.3 Test Case 3: Barrier Synchronization

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | barrier_data_o | 1 | Barrier propagated |
| T+1   | barrier_src_r_o | DIR | Source direction |
| T+1   | barrier_dest_r_o | DIR | Destination direction |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___
         _____
reset_i      |___________________________________
              ________
link_fwd  ___|        |__________________________
              ________
link_ready|        |__________________________
                  ________
core_req  _______|        |______________________
```

## 6. Pass/Fail Criteria

- [ ] Incoming stores decoded correctly
- [ ] `core_req_yumi_i` handshake works
- [ ] Response packets formed correctly
- [ ] Credit-based flow control respected
- [ ] Barrier propagation correct
- [ ] Coordinate extraction from incoming requests

## 7. Corner Cases

1. **Reset during transaction**: Verify clean state
2. **Back-to-back requests**: No gap between packets
3. **Credit exhaustion**: NoC backpressure
4. **Simultaneous fwd and rev**: Both directions active
5. **Invalid address decode**: Error handling
