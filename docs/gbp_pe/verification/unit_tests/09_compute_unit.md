# Compute Unit Unit Test

## 1. Test Objective

Verify that the Compute Unit correctly:
- Receives compute commands from Node Scheduler
- Reads neighbor state data from Accumulator
- Reads own state from SPM
- Performs matrix operations (MAT_ADD, MAT_INV, etc.)
- Writes back results to SPM
- Signals completion

## 2. Preconditions

- Module: `compute_unit`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE
- SPM contains valid node state data

## 3. Test Stimulus

### 3.1 Test Case 1: Variable Node Update (Simple)

**Scenario**: Variable node with 1 remote neighbor, simple update.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Compute command |
| T+1   | cmd_node_id | 0x10 | Node ID |
| T+1   | cmd_is_factor | 0 | Variable node |
| T+1   | cmd_dof | 3 | DOF = 3 |
| T+1   | cmd_adj_count | 1 | 1 neighbor |
| T+2   | cmd_valid | 0 | Clear command |
| T+3   | ns_valid | 1 | Neighbor state data |
| T+3   | ns_data | NEIGHBOR_WORD_0 | Neighbor data |
| T+3   | ns_last | 0 | Not last |
| T+4   | ns_valid | 1 | More neighbor data |
| T+4   | ns_data | NEIGHBOR_WORD_1 | Neighbor data |
| T+4   | ns_last | 1 | Last word |
| T+5   | self_rd_ready | 1 | SPM read ready |
| T+5   | self_rd_data | SELF_WORD_0 | Own state data |
| T+6   | self_rd_ready | 1 | SPM read ready |
| T+6   | self_rd_data | SELF_WORD_1 | Own state data |

### 3.2 Test Case 2: Backpressure Handling

**Scenario**: SPM and output apply backpressure.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Compute command |
| T+3   | ns_valid | 1 | Neighbor data |
| T+3   | out_ready | 0 | Output not ready |
| T+4   | out_ready | 0 | Still not ready |
| T+5   | out_ready | 1 | Ready again |

## 4. Expected Output

### 4.1 Test Case 1: Variable Node Update (Simple)

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Accept command |
| T+3   | ns_ready | 1 | Accept neighbor data |
| T+4   | ns_ready | 1 | Accept neighbor data |
| T+5   | self_rd_valid | 1 | Request own state |
| T+5   | self_rd_addr | SELF_ADDR | Own state address |
| T+6   | self_rd_valid | 1 | Continue read |
| T+6   | self_rd_addr | SELF_ADDR + 4 | Next word address |
| T+N   | wb_valid | 1 | Writeback result |
| T+N   | wb_addr | RESULT_ADDR | Result address |
| T+N   | wb_data | RESULT_DATA | Computed result |
| T+N+1 | done_valid | 1 | Completion signal |
| T+N+1 | done_node_id | 0x10 | Node ID |
| T+N+1 | done_is_factor | 0 | Variable |

### 4.2 Test Case 2: Backpressure Handling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | ns_ready | 0 | Backpressure from output |
| T+4   | ns_ready | 0 | Still backpressured |
| T+5   | ns_ready | 1 | Ready |
| T+6   | wb_valid | 1 | Writeback |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
cmd       ___|        |_________________________________________________
                      ________    ________
ns        ___________|        |__|        |________________________________
                          ________    ________
self_rd   _______________|        |__|        |____________________________
                                                      ________
wb        ___________________________________________|        |____________
                                                      ________
done      ___________________________________________|        |____________
```

## 6. Pass/Fail Criteria

- [ ] Command accepted when `cmd_valid && cmd_ready`
- [ ] Neighbor state read correctly from Accumulator
- [ ] Own state read correctly from SPM
- [ ] Matrix operations performed correctly (cycle model in 04)
- [ ] Writeback data written to correct SPM address
- [ ] `done_valid` asserted after writeback complete
- [ ] Backpressure handled correctly

## 7. Corner Cases

1. **Reset during compute**: Verify clean state after reset
2. **Maximum DOF**: Test with DOF = 8 (staging buffer limit)
3. **Zero neighbors**: Node with no neighbors
4. **Factor node schedule**: Different operation sequence
5. **SPM read error**: Invalid data in SPM
6. **Simultaneous command and data**: Both arrive same cycle
