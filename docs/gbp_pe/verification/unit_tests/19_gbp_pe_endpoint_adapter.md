# gbp_pe_endpoint_adapter Unit Test

## 1. Test Objective

Verify that `gbp_pe_endpoint_adapter` correctly:
- Wraps `bsg_manycore_endpoint_standard`
- Handles incoming requests from NoC
- Manages outgoing packets
- Provides memory-mapped queue subsystem

## 2. Preconditions

- Module: `gbp_pe_endpoint_adapter`
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: Reset, all outputs deasserted

## 3. Test Stimulus

### 3.1 Test Case 1: Incoming Store

**Scenario**: External PE sends a store to this PE.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | link_sif_i.fwd.v | 1 | Forward packet valid |
| T+1   | link_sif_i.fwd.data | PACKET | Store packet |
| T+1   | link_sif_i.fwd.data.addr | 0x1000 | Target address |
| T+1   | link_sif_i.fwd.data.op_v2 | e_remote_store | Store |
| T+1   | link_sif_i.fwd.data.payload | 0xDEAD | Data |
| T+1   | link_sif_i.fwd.data.src_x_cord | 3 | Source X |
| T+1   | link_sif_i.fwd.data.src_y_cord | 2 | Source Y |
| T+2   | link_sif_i.fwd.v | 0 | Clear |

### 3.2 Test Case 2: Outgoing Packet

**Scenario**: Internal logic sends a packet to NoC.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | out_v_i | 1 | Output valid |
| T+1   | out_packet_i | PACKET | Output packet |
| T+1   | out_packet_i.addr | 0x2000 | Destination address |
| T+1   | out_packet_i.op_v2 | e_remote_store | Store |
| T+1   | out_packet_i.payload | 0xBEEF | Data |
| T+1   | out_packet_i.dst_x_cord | 5 | Dest X |
| T+1   | out_packet_i.dst_y_cord | 4 | Dest Y |
| T+2   | out_v_i | 0 | Clear |

### 3.3 Test Case 3: Credit Flow Control

**Scenario**: NoC applies backpressure due to credit exhaustion.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | out_v_i | 1 | Try to send |
| T+1   | out_credit_or_ready_o | 0 | No credits |
| T+2   | out_credit_or_ready_o | 0 | Still no credits |
| T+3   | out_credit_or_ready_o | 1 | Credit available |

## 4. Expected Output

### 4.1 Test Case 1: Incoming Store

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | link_sif_o.fwd.ready_and_rev | 1 | Ready to accept |
| T+2   | core_req_v_o | 1 | Request to logic |
| T+2   | core_req_addr_o | 0x1000 | Address |
| T+2   | core_req_data_o | 0xDEAD | Data |
| T+2   | core_req_we_o | 1 | Write enable |

### 4.2 Test Case 2: Outgoing Packet

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | out_credit_or_ready_o | 1 | Credits available |
| T+2   | link_sif_o.fwd.v | 1 | Packet sent |
| T+2   | link_sif_o.fwd.data | PACKET | Output packet |

### 4.3 Test Case 3: Credit Flow Control

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | out_credit_or_ready_o | 0 | Stall |
| T+2   | out_credit_or_ready_o | 0 | Still stall |
| T+3   | out_credit_or_ready_o | 1 | Can send |
| T+4   | link_sif_o.fwd.v | 1 | Packet sent |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___
              ________
link_fwd  ___|        |__________________________
              ________
link_ready|        |__________________________
                  ________
core_req  _______|        |______________________
```

## 6. Pass/Fail Criteria

- [ ] Incoming requests decoded correctly
- [ ] `core_req_yumi_i` handshake works
- [ ] Outgoing packets formed correctly
- [ ] Credit-based flow control respected
- [ ] Source coordinates extracted
- [ ] Return packets handled correctly

## 7. Corner Cases

1. **Reset during transaction**: Clean state
2. **Credit exhaustion**: Backpressure handling
3. **Back-to-back packets**: No gap
4. **Simultaneous in/out**: Both directions active
5. **Invalid address**: Error handling
