# Memory Subsystem Integration Test

## 1. Test Objective

Verify that the Memory Subsystem correctly:
- Arbitrates between 7 SPM clients via round-robin
- Routes read/write requests to correct bank
- Handles bank conflicts with stall-and-retry
- Returns correct read data to requesting client
- Supports 64-bit beat width with byte-enable writes

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Memory Subsystem                         │
│                                                              │
│   Clients (7)                                                │
│   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐         │
│   │ Meta    │ │ CU_rd   │ │ CU_wr   │ │ PullSrv │         │
│   │ Scanner │ │ (RSE)   │ │ (WSE)   │ │         │         │
│   └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘         │
│   ┌─────────┐ ┌─────────┐ ┌─────────┐                    │
│   │ RespCol │ │ DMA     │ │ (spare) │                    │
│   └────┬────┘ └────┬────┘ └────┬────┘                    │
│        │           │           │                            │
│        └───────────┴───────────┴────────────────┐           │
│                                                   ▼           │
│                                          ┌─────────────┐     │
│                                          │  SPM Arbiter│     │
│                                          │  (RR, 7→8)  │     │
│                                          └──────┬──────┘     │
│                                                 │            │
│                              ┌──────────────────┼────────┐   │
│                              ▼                  ▼        ▼   │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │   │
│   │Bank 0   │  │Bank 1   │  │Bank 2   │  │Bank 3   │ ...│   │
│   │(8KB row)│  │(8KB row)│  │(8KB row)│  │(8KB row)│    │   │
│   └─────────┘  └─────────┘  └─────────┘  └─────────┘    │   │
│                                                           │   │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │   │
│   │Bank 4   │  │Bank 5   │  │Bank 6   │  │Bank 7   │    │   │
│   │(8KB row)│  │(8KB row)│  │(8KB row)│  │(8KB row)│    │   │
│   └─────────┘  └─────────┘  └─────────┘  └─────────┘    │   │
└─────────────────────────────────────────────────────────────┘
```

**Modules under test:** `spm_arbiter`, `spm_bank_array` (8 banks)

**Mocked modules:** 7 client modules

## 3. Preconditions

- Clock: 100MHz
- Reset: Active low
- SPM initialized with test pattern data
- `NUM_BANKS = 8`, `NUM_CLIENTS = 7`, `BEAT_BITS = 64`, `ROW_ADDR_W = 14`

## 4. Test Stimulus

### ✅ 4.1 Test Case 1: Write then Read Client 0 — IMPLEMENTED

**Scenario**: Client 0 writes data, then reads it back.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | wr_valid[0] | 1 | Client 0 write |
| T+1   | wr_addr[0] | 0x0100 | word address |
| T+1   | wr_data[0] | 0xDEADBEEFCAFEBABE | Write data |
| T+1   | wr_wstrb[0] | 0xFF | Full write enable |
| T+2   | wr_valid[0] | 0 | Clear |
| T+3   | rd_valid[0] | 1 | Client 0 read |
| T+3   | rd_addr[0] | 0x0100 | Same address |
| T+4   | rd_data[0] | 0xDEADBEEFCAFEBABE | Readback matches |

### ✅ 4.2 Test Case 2: Partial WSTRB Write — IMPLEMENTED

**Scenario**: Write only lower 4 bytes, read back verify upper bytes unchanged.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | wr_valid[0] | 1 | Write lower half |
| T+1   | wr_wstrb[0] | 0x0F | Only bytes [3:0] |
| T+1   | wr_data[0] | 0xFFFF_FFFF_0000_0000 | Upper = FF, lower = 00 |
| T+3   | rd_valid[0] | 1 | Read back |
| T+4   | rd_data[0][31:0] | 0x0000_0000 | Lower overwritten |
| T+4   | rd_data[0][63:32] | old_data | Upper unchanged |

### ✅ 4.3 Test Case 3: Concurrent Reads to Different Banks — IMPLEMENTED

**Scenario**: 3 clients read from banks 0, 1, 2 simultaneously.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | rd_valid[0] | 1 | Client 0 read bank 0 |
| T+1   | rd_addr[0] | 0x0000 | word_addr[3:1]=0 |
| T+1   | rd_valid[1] | 1 | Client 1 read bank 1 |
| T+1   | rd_addr[1] | 0x0002 | word_addr[3:1]=1 |
| T+1   | rd_valid[2] | 1 | Client 2 read bank 2 |
| T+1   | rd_addr[2] | 0x0004 | word_addr[3:1]=2 |

### ✅ 4.4 Test Case 4: Bank Conflict with Round-Robin — IMPLEMENTED

**Scenario**: 3 clients all target bank 0.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | rd_valid[0] | 1 | Client 0 read |
| T+1   | rd_addr[0] | 0x0000 | bank 0 |
| T+1   | rd_valid[1] | 1 | Client 1 read |
| T+1   | rd_addr[1] | 0x0010 | bank 0 |
| T+1   | rd_valid[2] | 1 | Client 2 read |
| T+1   | rd_addr[2] | 0x0020 | bank 0 |

### ❌ 4.5 Test Case 5: Write-then-Read Same Address — NOT YET IMPLEMENTED

**Expected**: Write to addr 0x100, immediately read same addr, verify new data.

### ❌ 4.6 Test Case 6: Zero WSTRB (No-Op Write) — NOT YET IMPLEMENTED

**Expected**: wstrb=0 → memory content unchanged.

### ❌ 4.7 Test Case 7: All 8 Clients Concurrent — NOT YET IMPLEMENTED

**Expected**: All clients granted within 8 cycles (round-robin fairness).

### ❌ 4.8 Test Case 8: Max Address Boundary — NOT YET IMPLEMENTED

**Expected**: Address 0x3FFFF (2^18 - 1) maps correctly without overflow.

## 5. Expected Output

### 5.1 Test Case 1: Concurrent Reads to Different Banks

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant client 0 |
| T+1   | rd_ready[1] | 1 | Grant client 1 |
| T+1   | rd_ready[2] | 1 | Grant client 2 |
| T+1   | bank_rd_en[0] | 1 | Bank 0 active |
| T+1   | bank_rd_en[1] | 1 | Bank 1 active |
| T+1   | bank_rd_en[2] | 1 | Bank 2 active |
| T+2   | rd_data[0] | DATA_0 | Client 0 data |
| T+2   | rd_data[1] | DATA_1 | Client 1 data |
| T+2   | rd_data[2] | DATA_2 | Client 2 data |

### 5.2 Test Case 2: Bank Conflict

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant client 0 (RR) |
| T+1   | rd_ready[1] | 0 | Stall client 1 |
| T+1   | rd_ready[2] | 0 | Stall client 2 |
| T+1   | bank_rd_en[0] | 1 | Bank 0 read |
| T+2   | rd_ready[0] | 0 | Stall client 0 |
| T+2   | rd_ready[1] | 1 | Grant client 1 |
| T+2   | bank_rd_en[0] | 1 | Bank 0 read |
| T+3   | rd_ready[2] | 1 | Grant client 2 |

### 5.3 Test Case 3: Read/Write Same Bank

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant read |
| T+1   | wr_ready[4] | 1 | Grant write |
| T+1   | bank_rd_en[3] | 1 | Bank 3 read |
| T+1   | bank_wr_en[3] | 1 | Bank 3 write |
| T+2   | rd_data[0] | OLD_DATA | Read returns old data (read-before-write) |

## 6. Timing Diagram

```
Bank Conflict (Test Case 2):
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________    ________    ________
rd_v[0]   ___|        |__|        |__________________________
              ________    ________    ________
rd_v[1]   ___|        |__|        |__________________________
              ________    ________    ________
rd_v[2]   ___|        |__|        |__________________________
              ________            ________            ________
rd_rdy[0] ____|        |__________|        |__________________
                      ________            ________
rd_rdy[1] ___________|        |__________|        |__________
                                  ________            ________
rd_rdy[2] _______________________|        |__________|        |
              ________    ________    ________
bank_en   ____|   0   |__|   0   |__|   0   |________________
```

## 7. Pass/Fail Criteria

- [ ] Different banks accessed in parallel (all grants asserted)
- [ ] Same bank conflict resolved with round-robin
- [ ] Read data returned to correct client (no cross-talk)
- [ ] Write data stored in correct bank at correct address
- [ ] Read-after-write to same address returns new data (next cycle)
- [ ] 64-bit beats preserved through entire path
- [ ] No deadlock under maximum contention

## 8. Corner Cases

1. **All 7 clients active**: Maximum arbitration stress
2. **All clients target same bank**: Serialization test
3. **Read-during-write same address**: Verify write-before-read or read-before-write policy
4. **Back-to-back requests**: Continuous arbitration fairness
5. **Reset during active transfer**: Clean state recovery
6. **Partial wstrb write**: Verify byte-enable masking in bank

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../02_SPM_AND_METADATA.md` | SPM layout, bank mapping |
| `../../04_PE_MICROARCHITECTURE.md` §2.11 | SPM Arbiter description |
| `../../05_INTERFACES.md` §2.9 | SPM Arbiter ports |
| `../unit_tests/11_spm_arbiter.md` | Arbiter unit test |
| `../unit_tests/16_spm_bank.md` | Bank unit test |
| `../unit_tests/17_spm_bank_array.md` | Bank array unit test |
