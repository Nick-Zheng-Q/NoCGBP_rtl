# Address Generation Unit (AGU) Unit Test

## 1. Test Objective

Verify that `agu` correctly:
- Generates sequential word addresses starting from `base_addr`
- Produces exactly `word_count` addresses
- Asserts `last_addr` on the final address
- Advances only on `addr_valid && addr_ready`
- Handles reset cleanly

---

## 2. Preconditions

- Module: `agu`
- Parameters: `SPM_ADDR_W = 18`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE, no active descriptor

---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Address Sequence

**Scenario**: Generate 4 addresses starting from 0x0100.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | start | 1 | Trigger AGU |
| T+1   | base_addr | 0x0100 | Start address |
| T+1   | word_count | 4 | 4 addresses |
| T+2   | start | 0 | Clear |
| T+2   | ready_i | 1 | Downstream ready |
| T+3   | ready_i | 1 | Downstream ready |
| T+4   | ready_i | 1 | Downstream ready |
| T+5   | ready_i | 1 | Downstream ready |

### 3.2 Test Case 2: Backpressure from Downstream

**Scenario**: Downstream stalls every other cycle.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | start | 1 | Trigger |
| T+1   | base_addr | 0x0200 | Start address |
| T+1   | word_count | 4 | 4 addresses |
| T+2   | ready_i | 1 | Ready |
| T+3   | ready_i | 0 | Not ready |
| T+4   | ready_i | 0 | Still not ready |
| T+5   | ready_i | 1 | Ready |

### 3.3 Test Case 3: Single Word (word_count = 1)

**Scenario**: Only one address.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | start | 1 | Trigger |
| T+1   | base_addr | 0x0300 | Start address |
| T+1   | word_count | 1 | 1 address |
| T+2   | ready_i | 1 | Ready |

---

## 4. Expected Output

### 4.1 Test Case 1: Normal Address Sequence

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | addr_valid | 1 | First address valid |
| T+1   | addr | 0x0100 | Base address |
| T+1   | last_addr | 0 | Not last |
| T+2   | addr_valid | 1 | Second address |
| T+2   | addr | 0x0101 | Incremented |
| T+2   | last_addr | 0 | Not last |
| T+3   | addr_valid | 1 | Third address |
| T+3   | addr | 0x0102 | Incremented |
| T+3   | last_addr | 0 | Not last |
| T+4   | addr_valid | 1 | Fourth address |
| T+4   | addr | 0x0103 | Incremented |
| T+4   | last_addr | 1 | Last address |
| T+5   | addr_valid | 0 | Done |

### 4.2 Test Case 2: Backpressure

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | addr_valid | 1 | Address 0x0200 |
| T+3   | addr_valid | 1 | Hold 0x0200 (not consumed) |
| T+4   | addr_valid | 1 | Still holding 0x0200 |
| T+5   | addr_valid | 1 | Now 0x0201 consumed, 0x0201 valid |

### 4.3 Test Case 3: Single Word

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | addr_valid | 1 | Address 0x0300 |
| T+1   | last_addr | 1 | Immediately last |
| T+2   | addr_valid | 0 | Done |

---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
start     ___|        |__________________________________
                  ________    ________    ________    ________
addr      XXXXXX| 0x0100 |XX| 0x0101 |XX| 0x0102 |XX| 0x0103 |
                  ________    ________    ________    ________
valid     _______|        |__|        |__|        |__|        |__
                                                    ________
last      _________________________________________|        |__
```

---

## 6. Pass/Fail Criteria

- [ ] `addr_valid` asserted on first cycle after `start`
- [ ] `addr` = `base_addr` on first cycle
- [ ] `addr` increments by 1 each accepted cycle
- [ ] Exactly `word_count` addresses generated
- [ ] `last_addr = 1` on the final address only
- [ ] `addr_valid = 0` after sequence complete
- [ ] Backpressure: address held stable when `ready_i = 0`
- [ ] Reset: returns to IDLE immediately

---

## 7. Corner Cases

1. **word_count = 0**: No addresses generated
2. **word_count = 1**: Single address, `last_addr = 1` on first cycle
3. **Maximum word_count**: 65535 addresses (verify no overflow)
4. **Backpressure on last address**: `last_addr` stays asserted until consumed
5. **Start while active**: New `start` should be ignored or abort current
6. **Reset during sequence**: Clean abort, no spurious addresses

---

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../02_SPM_AND_METADATA.md` | SPM address space, bank mapping |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` §2.13 | AGU port definition |
| `../README.md` | Verification documentation index |
