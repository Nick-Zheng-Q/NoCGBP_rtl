# Neighbor State Accumulator Unit Test

## 1. Test Objective

Verify that the Neighbor State Accumulator correctly:
- Merges local and remote neighbor state data
- Streams data to Compute Unit
- Handles backpressure from Compute Unit
- Maintains data ordering

## 2. Preconditions

- Module: `neighbor_state_accumulator`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE

## 3. Test Stimulus

### 3.1 Test Case 1: Local State Only

**Scenario**: Single local neighbor state read.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | local_valid | 1 | Local data available |
| T+1   | local_data | WORD_0 | Local state word |
| T+1   | local_last | 1 | Last word |
| T+2   | local_valid | 0 | Clear |

### 3.2 Test Case 2: Remote State Only

**Scenario**: Single remote neighbor state from Response Collector.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | remote_valid | 1 | Remote data available |
| T+1   | remote_data | WORD_0 | Remote state word |
| T+1   | remote_last | 0 | Not last |
| T+2   | remote_valid | 1 | More data |
| T+2   | remote_data | WORD_1 | Remote state word |
| T+2   | remote_last | 1 | Last word |

### 3.3 Test Case 3: Mixed Local and Remote

**Scenario**: Local and remote data interleaved.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | local_valid | 1 | Local data |
| T+1   | local_data | LOCAL_0 | Local word |
| T+1   | local_last | 1 | Last local |
| T+2   | remote_valid | 1 | Remote data |
| T+2   | remote_data | REMOTE_0 | Remote word |
| T+2   | remote_last | 1 | Last remote |

## 4. Expected Output

### 4.1 Test Case 1: Local State Only

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | local_ready | 1 | Accept local data |
| T+2   | out_valid | 1 | Data to Compute Unit |
| T+2   | out_data | WORD_0 | Local state word |
| T+2   | out_last | 1 | Last word |

### 4.2 Test Case 2: Remote State Only

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | remote_ready | 1 | Accept remote data |
| T+2   | out_valid | 1 | Data to Compute Unit |
| T+2   | out_data | WORD_0 | Remote state word |
| T+2   | out_last | 0 | Not last |
| T+3   | remote_ready | 1 | Accept remote data |
| T+3   | out_valid | 1 | Data to Compute Unit |
| T+3   | out_data | WORD_1 | Remote state word |
| T+3   | out_last | 1 | Last word |

### 4.3 Test Case 3: Mixed Local and Remote

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | local_ready | 1 | Accept local data |
| T+2   | out_valid | 1 | Local data to CU |
| T+2   | out_data | LOCAL_0 | Local word |
| T+2   | out_last | 0 | Not last (more neighbors) |
| T+3   | remote_ready | 1 | Accept remote data |
| T+3   | out_valid | 1 | Remote data to CU |
| T+3   | out_data | REMOTE_0 | Remote word |
| T+3   | out_last | 1 | Last word |

## 5. Timing Diagram

```
Test Case 2:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________    ________
remote    ___|        |__|        |__________________________
              ________    ________
remote_ready|        |__|        |__________________________
                  ________    ________
out       _______|        |__|        |______________________
                  ________    ________
out_data  XXXXXXX| WORD_0 |XX| WORD_1 |XXXXXXXXXXXXXXXXXXXXXX
                              ________
out_last  ___________________|        |______________________
```

## 6. Pass/Fail Criteria

- [ ] Local and remote data merged correctly
- [ ] `out_valid` asserted for each input word
- [ ] `out_last` asserted on final word
- [ ] Backpressure: `local_ready`/`remote_ready` deasserted when `out_ready = 0`
- [ ] Data ordering preserved
- [ ] No data loss or duplication

## 7. Corner Cases

1. **Simultaneous local and remote**: Both valid same cycle (needs arbitration)
2. **Reset during transfer**: Verify clean state after reset
3. **Maximum backpressure**: Compute Unit busy for many cycles
4. **Single word neighbors**: Each neighbor has 1 state word
5. **Large state**: Neighbor with many state words
