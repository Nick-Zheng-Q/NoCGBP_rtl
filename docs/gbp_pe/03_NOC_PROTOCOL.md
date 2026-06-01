# NoC Protocol

## 1. Overview

GBP messages are transported over the manycore NoC using `e_remote_store` operations. Three message types are encoded via address-mapped mailboxes:

| Message | Mailbox Address | Payload | Purpose |
|---------|----------------|---------|---------|
| NOTIFICATION | `MBX_NOTIFICATION` | `{is_factor, source_node_id}` | Tell consumer "my state changed" |
| FETCH_REQUEST | `MBX_FETCH_REQ_*` (×3 stores) | `{is_factor, consumer_id}`, `{target_node_id}`, `{txn_id}` | Consumer requests current state |
| FETCH_RESPONSE | `MBX_RESP_META` + `MBX_RESP_DATA`×N + `MBX_RESP_DONE` | metadata, data words, done signal | Producer sends requested state |

All messages use `op = e_remote_store` in the `bsg_manycore_packet_s` format. See Section 6 for address map details.

---

## 2. Notification

Lightweight control message. Sent as a single `e_remote_store` to the consumer PE's notification mailbox.

### 2.1 Store Format

```
op      = e_remote_store
dst_x   = consumer PE x_cord
dst_y   = consumer PE y_cord
addr    = GBP_BASE_ADDR + MBX_NOTIFICATION
payload = {is_factor, source_node_id[NODE_ID_W-1:0]}
```

### 2.2 Send Logic (after producer updates node N)

```
for each consuming neighbor M (remote):
    dst_pe = lookup PE owning M
    dst_xy = pe_id_to_xy(dst_pe)
    send store(addr=MBX_NOTIFICATION, payload={is_factor, N}, dst=dst_xy)
```

### 2.3 Receive Logic (on consumer PE)

```
NoC Adapter decodes in_addr == GBP_BASE_ADDR + MBX_NOTIFICATION:
  → Extract source_node_id from in_data_o
  → Extract source PE from (in_src_x_cord_o, in_src_y_cord_o)
  → Assert rx_notif_valid to ScoreboardPrefetcher
  → Record notification on edge (M, N)
  → edge state: IDLE → NOTIFIED
```

---

## 3. Fetch Request

Consumer requests current state from producer. Uses 3 stores to carry all required fields.

### 3.1 Store Format (3-store sequence)

**Store 1:**
```
op      = e_remote_store
dst_x   = producer PE x_cord
dst_y   = producer PE y_cord
addr    = GBP_BASE_ADDR + MBX_FETCH_REQ_0
payload = {is_factor, consumer_node_id[NODE_ID_W-1:0]}
```

**Store 2:**
```
op      = e_remote_store (same destination)
addr    = GBP_BASE_ADDR + MBX_FETCH_REQ_1
payload = {target_node_id[NODE_ID_W-1:0]}
```

**Store 3:**
```
op      = e_remote_store (same destination)
addr    = GBP_BASE_ADDR + MBX_FETCH_REQ_2
payload = {txn_id[TXN_ID_W-1:0]}
```

`txn_id` is assigned by ScoreboardPrefetcher at request time. It is the edge_index within the per-node edge array. Used by Response Collector for STAGING lookup and by ScoreboardPrefetcher for edge matching on completion.

### 3.2 Send Logic (ScoreboardPrefetcher, on notification receipt)

```
for each edge with state == NOTIFIED:
    dst_pe = edge.source_pe
    dst_xy = pe_id_to_xy(dst_pe)
    txn_id = edge.edge_index   // per-node edge index
    send store1(addr=MBX_FETCH_REQ_0, payload={is_factor, consumer_id}, dst=dst_xy)
    send store2(addr=MBX_FETCH_REQ_1, payload={target_id}, dst=dst_xy)
    send store3(addr=MBX_FETCH_REQ_2, payload={txn_id}, dst=dst_xy)
    edge.state = IN_FLIGHT
    add to Scoreboard
```

### 3.3 Receive Logic (on producer PE)

```
NoC Adapter decodes in_addr:
  MBX_FETCH_REQ_0:     latch {is_factor, consumer_node_id}
  MBX_FETCH_REQ_1:     latch {target_node_id}
  MBX_FETCH_REQ_2:     latch {txn_id}
  → All three latched: assert rx_fetch_req_valid to Pull Server
  → Pull Server queues request for processing (max 4 per cycle)
  → txn_id is echoed in FETCH_RESPONSE for consumer-side matching
```

---

## 4. Fetch Response

Producer sends requested state data to consumer via multiple `e_remote_store` operations.

### 4.1 Response Metadata Store

First store carries metadata about the response:

```
op      = e_remote_store
dst_x   = consumer PE x_cord
dst_y   = consumer PE y_cord
addr    = GBP_BASE_ADDR + MBX_RESP_META
payload = {is_factor, state_words[STATE_WORDS_W-1:0]}
```

### 4.2 Response Data Stores

Subsequent stores stream the state data (one word per store):

```
Store 1: addr=GBP_BASE_ADDR + MBX_RESP_DATA, payload=word[0]
Store 2: addr=GBP_BASE_ADDR + MBX_RESP_DATA, payload=word[1]
...
Store N: addr=GBP_BASE_ADDR + MBX_RESP_DATA, payload=word[N-1]
```

**Signal semantics:**
- `tx_fetch_resp_last` / `rx_fetch_resp_last`: asserted on the last data word (Store N), before the MBX_RESP_DONE store
- `tx_fetch_resp_data_valid` / `rx_fetch_resp_data_valid`: asserted for each data word store
- `rx_fetch_resp_done_valid`: asserted when MBX_RESP_DONE store is received (completion signal)

### 4.3 Response Done Store

Final store signals completion:

```
op      = e_remote_store
dst_x   = consumer PE x_cord
dst_y   = consumer PE y_cord
addr    = GBP_BASE_ADDR + MBX_RESP_DONE
payload = {txn_id[TXN_ID_W-1:0], node_id[NODE_ID_W-1:0], consumer_node_id[NODE_ID_W-1:0]}
```

Consumer uses `txn_id` to directly index into the ScoreboardPrefetcher edge array and STAGING transaction table. `node_id + consumer_node_id` are carried for debug/validation.

**Signal semantics:**
- `rx_fetch_resp_done_valid`: asserted when NoC Adapter decodes a store to `MBX_RESP_DONE` address
- `rx_fetch_resp_txn_id`: extracted from payload, used for edge matching and STAGING lookup
- This signal triggers `complete_valid` in Response Collector, which notifies ScoreboardPrefetcher

### 4.4 Total Store Count

```
Total stores = 1 (metadata) + state_words (data) + 1 (done)
             = state_words + 2
```

### 4.5 Send Logic (on producer PE, after receiving FETCH_REQUEST)

```
1. Lookup local NodeHeader by target_node_id.
2. Read state_base and state_words from header.
3. Latch txn_id from FETCH_REQUEST (store 3).
4. Send metadata store: addr=MBX_RESP_META, payload={is_factor, state_words}.
5. For i = 0 to state_words-1:
     Read word[i] from STATE region.
     Send data store: addr=MBX_RESP_DATA, payload=word[i].
6. Send done store: addr=MBX_RESP_DONE, payload={txn_id, node_id, consumer_node_id}.
```

Pull Server does not understand eta/lambda. Does not compute. Only does state readback.

---

## 5. Complete Pull Flow

### 5.1 Producer Side (after node update)

```
PE_A updates node N:
  1. Compute new state for N.
  2. Writeback N's STATE to local SPM.
  3. For each consuming neighbor M (remote):
     - Send NOTIFICATION store to M's PE: addr=MBX_NOTIFICATION, payload={is_factor, N}.
```

### 5.2 Consumer Side (before node update)

```
PE_B wants to update node M (adjacent to N on PE_A):
  1. ScoreboardPrefetcher receives NOTIFICATION from NoC Adapter.
     → Record on edge(M, N): notification = true.
  2. Prefetcher issues FETCH_REQUEST via NoC Adapter (3 stores to MBX_FETCH_REQ_*).
     → edge(M, N): in_flight = true, notification = false.
     → txn_id = edge_index(M, N).
     → Add Scoreboard entry.
  3. PE_A receives FETCH_REQUEST, reads N's STATE, sends FETCH_RESPONSE stores.
  4. PE_B receives FETCH_RESPONSE (metadata + data + done stores).
     → done store carries txn_id → match edge(M, N) directly.
     → edge(M, N): ready = true, in_flight = false.
     → Remove Scoreboard entry.
  5. When ALL edges of M are "ready" → M becomes schedulable.
  6. Scheduler selects M, Compute Unit reads neighbor states, performs update.
  7. After compute: reset remote edges to idle, keep local edges as ready.
```

---

## 6. NoC Adaptation Layer (bsg_manycore compatible)

GBP packets are transported over the manycore NoC using `bsg_manycore_endpoint_standard` as the NoC access point. GBP semantic messages are encoded into manycore `e_remote_store` operations via address-mapped mailboxes.

### 6.1 Architecture Overview

```
GBP PE Internal                     NoC
                                    ┌─────────────┐
┌─────────────────────┐            │             │
│ Writeback Controller ├──[notif]──┤             │
│                     │            │  noc_adapter │◄──► link_sif_i/o
│ Pull Client ────────├──[req]────┤  (wraps      │     (manycore)
│                     │            │   endpoint_  │
│ Pull Server ────────├──[resp]───┤   standard)  │
│                     │            │             │
│ Response Collector──├──[rx]─────┤             │
└─────────────────────┘            └─────────────┘
```

The `noc_adapter` module wraps `bsg_manycore_endpoint_standard` and provides:
- TX path: accepts GBP messages from internal modules, forms `bsg_manycore_packet_s`, sends via `out_v_i` / `out_packet_i`
- RX path: receives incoming stores from `in_v_o` / `in_addr_o` / `in_data_o`, demuxes to appropriate GBP module
- Credit management: uses `out_credit_or_ready_o` for flow control

### 6.2 Address Map (Mailbox Layout)

GBP reserves a region in the PE's local EPA (Endpoint Address) space:

```
Base: GBP_BASE_ADDR (e.g., 0x1000)

Offset  Size   Name                 Direction    Description
------  ----   ----                 ---------    -----------
0x00    4B     MBX_NOTIFICATION     RX           Notification mailbox
0x04    4B     MBX_FETCH_REQ_0      RX           Fetch request store 1: {is_factor, consumer_node_id}
0x08    4B     MBX_FETCH_REQ_1      RX           Fetch request store 2: {target_node_id}
0x0C    4B     MBX_FETCH_REQ_2      RX           Fetch request store 3: {txn_id}
0x10    4B     MBX_RESP_META        RX           Response metadata (header)
0x14    4B     MBX_RESP_DATA        RX           Response data (streamed)
0x18    4B     MBX_RESP_DONE        RX           Response complete signal
```

### 6.3 Message Encoding (TX Path)

GBP messages are encoded as `e_remote_store` packets:

#### 6.3.1 NOTIFICATION

```
op      = e_remote_store
dst_x   = target PE x_cord
dst_y   = target PE y_cord
addr    = GBP_BASE_ADDR + MBX_NOTIFICATION
payload = {is_factor, source_node_id[NODE_ID_W-1:0]}

// target_node_id derived from adjacency info, not in packet
// consumer PE uses addr to identify notification type
```

#### 6.3.2 FETCH_REQUEST

```
op      = e_remote_store
dst_x   = producer PE x_cord
dst_y   = producer PE y_cord
addr    = GBP_BASE_ADDR + MBX_FETCH_REQ_0
payload = {is_factor, consumer_node_id[NODE_ID_W-1:0]}

// target_node_id and txn_id sent in subsequent stores (see Section 3.1)
```

Since `payload` is only 32 bits and FETCH_REQUEST needs multiple fields, use a 3-store sequence:

```
Store 1: addr = MBX_FETCH_REQ_0, payload = {is_factor, consumer_node_id}
Store 2: addr = MBX_FETCH_REQ_1, payload = {target_node_id}
Store 3: addr = MBX_FETCH_REQ_2, payload = {txn_id}
```

#### 6.3.3 FETCH_RESPONSE (multi-store)

Response data is streamed via multiple stores:

```
// First store: metadata
op      = e_remote_store
dst_x   = consumer PE x_cord
dst_y   = consumer PE y_cord
addr    = GBP_BASE_ADDR + MBX_RESP_META
payload = {is_factor, state_words[STATE_WORDS_W-1:0]}

// Subsequent stores: data words
Store 2: addr = GBP_BASE_ADDR + MBX_RESP_DATA, payload = word[0]
Store 3: addr = GBP_BASE_ADDR + MBX_RESP_DATA, payload = word[1]
...
Store N+1: addr = GBP_BASE_ADDR + MBX_RESP_DATA, payload = word[N-1]

// Final store: done signal (carries txn_id for consumer matching)
Store N+2: addr = GBP_BASE_ADDR + MBX_RESP_DONE, payload = {txn_id, node_id, consumer_node_id}
```

The consumer PE uses repeated writes to `MBX_RESP_DATA` to stream data words. The `MBX_RESP_DONE` write signals completion and carries `txn_id` for direct edge/STAGING lookup.

### 6.4 Message Decoding (RX Path)

Incoming stores are decoded by address:

```
in_v_o asserted:
  case (in_addr_o - GBP_BASE_ADDR)
    MBX_NOTIFICATION:   → route to ScoreboardPrefetcher.notif_*
    MBX_FETCH_REQ_0:    → latch {is_factor, consumer_node_id} (Pull Server)
    MBX_FETCH_REQ_1:    → latch {target_node_id} (Pull Server)
    MBX_FETCH_REQ_2:    → latch {txn_id} → all three latched: assert rx_fetch_req_valid
    MBX_RESP_META:      → route to Response Collector (header)
    MBX_RESP_DATA:      → route to Response Collector (data stream)
    MBX_RESP_DONE:      → route to Response Collector (completion, extract txn_id)
    default:            → ignore or error
  endcase
```

### 6.5 Coordinate Extraction

The `endpoint_standard` provides source coordinates with each incoming request:

```
in_src_x_cord_o, in_src_y_cord_o  →  source PE coordinates
```

For FETCH_REQUEST, the source PE is needed to route the response back. The adapter converts `(in_src_x_cord_o, in_src_y_cord_o)` to a `PE_ID_W`-bit source PE identifier.

### 6.6 Credit Flow Control

The `bsg_manycore_endpoint_standard` handles credit management internally:

```
out_credit_or_ready_o: indicates if the forward FIFO has space
  - When 1: safe to assert out_v_i
  - When 0: must stall

The endpoint_fc tracks:
  - rev_fifo_credits: guaranteed space for response (default 3)
  - out_credits_used: outstanding request count
```

GBP modules must check `out_credit_or_ready_o` before sending. The `noc_adapter` provides a `tx_ready` signal back to internal modules based on this.

### 6.7 Return Path (FETCH_RESPONSE via rev channel)

Manycore has a dedicated reverse (`rev`) channel for return packets. FETCH_RESPONSE can optionally use this path:

```
Producer PE receives FETCH_REQUEST:
  1. Decode request from in_addr_o / in_data_o
  2. Read state from SPM
  3. Return data via returning_data_i / returning_v_i (rev channel)
     - pkt_type = e_return_int_wb
     - data = state word
     - reg_id = transaction ID (for matching)

Consumer PE receives return packet:
  1. returned_v_r_o asserted
  2. Decode returned_data_r_o as state word
  3. Feed into Response Collector
```

This is more efficient than multi-store for large responses, as it uses the dedicated rev channel.

### 6.8 Combined Strategy

| Message | TX Method | RX Method | Rationale |
|---------|-----------|-----------|-----------|
| NOTIFICATION | fwd store to MBX_NOTIFICATION | decode in_addr | Small, 1 store |
| FETCH_REQUEST | fwd store to MBX_FETCH_REQ_* (3 stores) | decode in_addr, latch 3 stores | Carries {is_factor, consumer_id}, {target_id}, {txn_id} |
| FETCH_RESPONSE (small, ≤4 words) | fwd store to MBX_RESP_DATA | decode in_addr | Simple |
| FETCH_RESPONSE (large, >4 words) | rev channel (returning_data_i) | returned_v_r_o | Efficient, uses dedicated channel |

### 6.9 Open Items

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | GBP_BASE_ADDR selection | Open | Must not conflict with existing PE address map |
| 2 | Response path selection threshold | Open | When to use fwd store vs rev channel |
| 3 | Multi-store atomicity | Open | What if FETCH_REQ stores 1, 2, and 3 are interrupted by another incoming store? |
| 4 | Credit count for GBP | Open | How many outstanding GBP requests per PE |

---

## 7. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization, STAGING design |
| `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, open items |
| `05_INTERFACES.md` | Port-level interfaces, state machines, timing paths |
| `06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
| `verification/README.md` | Verification documentation index and test templates |
