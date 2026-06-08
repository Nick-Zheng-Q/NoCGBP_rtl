# Metadata Scanner Unit Test

## 1. Test Objective

Verify that the Metadata Scanner correctly:
- Reads NodeHeader from SPM
- Extracts metadata fields (dof, adj_count, adj_base, state_base, state_words)
- Scans AdjEntry list
- Classifies neighbors as local vs remote


---

## 2. Preconditions

- Module: `metadata_scanner`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE, SPM contains valid NodeHeader and AdjEntry data


---

## 3. Test Stimulus

### 3.1 Test Case 1: Single Node Scan

**Scenario**: Scan node with 2 neighbors (1 local, 1 remote).

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Command received |
| T+1   | cmd_node_id | 0x10 | Node ID |
| T+1   | cmd_is_factor | 0 | Variable node |
| T+2   | cmd_valid | 0 | Clear command |
| T+3   | spm_rd_ready | 1 | SPM read ready |
| T+3   | spm_rd_data | HEADER | NodeHeader data |
| T+4   | spm_rd_ready | 1 | SPM read ready |
| T+4   | spm_rd_data | ADJ_ENTRY_0 | First AdjEntry |
| T+5   | spm_rd_ready | 1 | SPM read ready |
| T+5   | spm_rd_data | ADJ_ENTRY_1 | Second AdjEntry |


---

## 4. Expected Output

### 4.1 Test Case 1: Single Node Scan

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Accept command |
| T+2   | spm_rd_valid | 1 | Start SPM read |
| T+2   | spm_rd_addr | HEADER_ADDR | NodeHeader address |
| T+3   | info_valid | 1 | Node info valid |
| T+3   | info_dof | DOF_VALUE | Degree of freedom |
| T+3   | info_adj_count | 2 | Two neighbors |
| T+3   | info_state_base | STATE_ADDR | State base address |
| T+3   | info_state_words | STATE_WORDS | State word count |
| T+4   | adj_valid | 1 | AdjEntry valid |
| T+4   | adj_neighbor_id | NEIGHBOR_0 | First neighbor ID |
| T+4   | adj_neighbor_x | PE_ID_0 | First neighbor X coord |
| T+4   | adj_neighbor_y | 0x00 | First neighbor Y coord |
| T+4   | adj_is_local | 1 | Local neighbor |
| T+4   | adj_last | 0 | Not last |
| T+5   | adj_valid | 1 | AdjEntry valid |
| T+5   | adj_neighbor_id | NEIGHBOR_1 | Second neighbor ID |
| T+5   | adj_neighbor_x | PE_ID_1 | Second neighbor X coord |
| T+5   | adj_neighbor_y | 0x00 | Second neighbor Y coord |
| T+5   | adj_is_local | 0 | Remote neighbor |
| T+5   | adj_last | 1 | Last AdjEntry |


---

## 5. Timing Diagram

```
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
cmd       ___|        |_____________________________________
                  ________
spm_rd    _______|        |_________________________________
                      ________    ________    ________
info      _______|        |____|        |____|        |____
                                          ________    ________
adj       ___________________________|        |____|        |____
                                                    ________
last      _______________________________________|        |____
```


---

## 6. Pass/Fail Criteria

- [ ] NodeHeader read within 1 cycle of command
- [ ] All metadata fields extracted correctly
- [ ] AdjEntry scan completes in adj_count cycles
- [ ] `adj_last` asserted on last AdjEntry
- [ ] `adj_is_local` correct based on `(neighbor_x == self_x) && (neighbor_y == self_y)`
- [ ] FSM returns to IDLE after scan complete


---

## 7. Corner Cases

1. **Zero neighbors**: Node with adj_count = 0
2. **Maximum neighbors**: Node with adj_count = MAX_ADJ_COUNT
3. **All local neighbors**: No remote edges
4. **All remote neighbors**: No local edges
5. **SPM read error**: Invalid data in SPM

---


---

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../00_WRITING_GUIDE.md` | How to write architecture documents |
| `../../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../../02_SPM_AND_METADATA.md` | SPM layout, metadata structures |
| `../../03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` | Port-level interfaces, state machines |
| `../../06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages |
| `../README.md` | Verification documentation index |
