# BaseJump STL bsg_dataflow FIFO Summary

| Module | Type/Depth | Implementation Notes | Use/Notes |
| --- | --- | --- | --- |
| `bsg_one_fifo.sv` | 1 element | Minimal area, max throughput 1word/2cycle | Low-rate buffering |
| `bsg_one_fifo_bypass.sv` | 1 element + bypass | Does not break data/v combinational path | Round-robin isolation |
| `bsg_two_fifo.sv` | 2 element | ready/valid in, valid/yumi out | General-purpose FIFO |
| `bsg_fifo_bypass.sv` | Bypass | External FIFO sideband | Select bypass vs FIFO |
| `bsg_relay_fifo.sv` | 2 element | ready/valid on both sides | Link extension |
| `bsg_fifo_1r1w_small.sv` | Small FIFO | Wrapper; hardened or unhardened | Small capacity |
| `bsg_fifo_1r1w_small_unhardened.sv` | Small FIFO | Async-read 1R1W RF | Low latency |
| `bsg_fifo_1r1w_small_hardened.sv` | Small FIFO | Sync-read 1R1W SRAM | Block RAM / medium depth |
| `bsg_fifo_1r1w_small_credit_on_input.sv` | Small FIFO | Credit-in to ready/valid out | Credit protocol input |
| `bsg_fifo_1r1w_narrowed.sv` | Small FIFO | Output width narrowing | Width conversion |
| `bsg_fifo_1rw_large.sv` | Large FIFO | Single-port sync RAM | 1RW-only storage |
| `bsg_fifo_1r1w_large.sv` | Large FIFO | 1RW double-width + small FIFOs | 1RW area optimization |
| `bsg_fifo_1r1w_pseudo_large.sv` | Large FIFO | 1RW RAM + small FIFO | Read/write duty <= 50% |
| `bsg_fifo_1r1w_large_banked.sv` | Large FIFO | Banked pseudo-large | Marked not tested |
| `bsg_fifo_1r1w_multi.sv` | Multi FIFO | Shared storage + pointer lists | Demux endpoints |
| `bsg_fifo_1r1w_small_hardened_multi.sv` | Multi FIFO | Shared sync SRAM, multi-virtual | `yumi_i` one-hot |
| `bsg_fifo_reorder.sv` | Reorder FIFO | In-order alloc/deq, out-of-order write | ROB/store buffer |
| `bsg_round_robin_fifo_to_fifo.sv` | Transfer engine | FIFO-to-FIFO round robin | Channel transfer |
| `bsg_1_to_n_tagged_fifo.sv` | Tagged FIFO | 1-to-N demux, per-channel FIFO | Tagged fan-out |
| `bsg_1_to_n_tagged_fifo_shared.sv` | Tagged FIFO | Shared RAM, per-channel small buffers | Low-rate shared storage |
| `bsg_fifo_tracker.sv` | Helper | Full/empty tracking | Internal component |
| `bsg_fifo_shift_datapath.sv` | Helper | Shift-register array | Internal datapath |
