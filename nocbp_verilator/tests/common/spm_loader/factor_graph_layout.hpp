// factor_graph_layout.hpp
// Layout constants and helpers for GBP factor graph data in SPM.
// Must stay in sync with control_unit_gbp.sv META parsing logic.

#ifndef FACTOR_GRAPH_LAYOUT_HPP_
#define FACTOR_GRAPH_LAYOUT_HPP_

#include "global_memory.hpp"
#include <cstdint>

namespace spm_loader {

// ---------------------------------------------------------------------------
// META row layout (64 bytes = 8 beats = 16 words)
// Row 0 = scheduler header
// Row 1..N = node descriptors
// ---------------------------------------------------------------------------
constexpr int kMetaRowBytes = 64;   // 1 << (BYTE_OFF_W + WORD_OFF_W + BANK_ID_W)
constexpr int kMetaWordsPerRow = kMetaRowBytes / kWordBytes;  // 16

// Header (row 0) word layout
// beat 0 (addr + 0):  word0[15:0]=var_count, word0[31:16]=fac_count
// beat 1 (addr + 8):  word2[31:12]=msg_base_addr_upper, word2[11:9]=msg_bank_hint
//                     word3[31:16]=state_xfer_bytes, word3[15:0]=message_xfer_bytes
// beat 2 (addr +16):  word4[31:24]=state_step_bytes, word4[23:16]=message_step_bytes

// Node entry (row >= 1) word layout
// beat 0 (addr + 0):  word0[7:0]=txn_id, word0[8]=is_factor,
//                     word0[11:9]=dofs, word0[15:12]=adj_count
//                     word1[31:12]=state_base_addr_upper, word1[11:9]=state_bank_hint
// beat 1 (addr + 8):  word2[31:12]=message_base_addr_upper, word2[11:9]=msg_bank_hint
//                     word3[31:16]=state_xfer_bytes, word3[15:0]=message_xfer_bytes
// beat 2 (addr +16):  word4[31:24]=state_step_bytes, word4[23:16]=message_step_bytes

// ---------------------------------------------------------------------------
// Helper to pack a META header into global memory at byte_addr.
// ---------------------------------------------------------------------------
void write_meta_header(GlobalMemorySpace& mem,
                       uint64_t byte_addr,
                       uint16_t var_row_count,
                       uint16_t fac_row_count,
                       uint32_t message_base_addr,   // byte address, upper 20 bits stored
                       uint8_t  message_bank_hint,
                       uint16_t state_xfer_bytes,
                       uint16_t message_xfer_bytes,
                       uint8_t  state_step_bytes,
                       uint8_t  message_step_bytes);

// ---------------------------------------------------------------------------
// Helper to pack a META node entry into global memory at byte_addr.
// ---------------------------------------------------------------------------
void write_meta_node(GlobalMemorySpace& mem,
                     uint64_t byte_addr,
                     uint8_t  txn_id,
                     bool     is_factor,
                     uint8_t  dofs,          // 1..6
                     uint8_t  adj_count,     // 0..8
                     uint32_t state_base_addr,
                     uint8_t  state_bank_hint,
                     uint32_t message_base_addr,
                     uint8_t  message_bank_hint,
                     uint16_t state_xfer_bytes,
                     uint16_t message_xfer_bytes,
                     uint8_t  state_step_bytes,
                     uint8_t  message_step_bytes);

// ---------------------------------------------------------------------------
// Helper to write a 32-bit FP32 payload (e.g. state vector element)
// ---------------------------------------------------------------------------
void write_fp32(GlobalMemorySpace& mem, uint64_t byte_addr, float value);

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------
inline void write_meta_header(GlobalMemorySpace& mem,
                              uint64_t byte_addr,
                              uint16_t var_row_count,
                              uint16_t fac_row_count,
                              uint32_t message_base_addr,
                              uint8_t  message_bank_hint,
                              uint16_t state_xfer_bytes,
                              uint16_t message_xfer_bytes,
                              uint8_t  state_step_bytes,
                              uint8_t  message_step_bytes) {
    uint32_t word0 = (static_cast<uint32_t>(fac_row_count) << 16)
                   | static_cast<uint32_t>(var_row_count);
    mem.write_word(byte_addr + 0, word0);

    uint32_t word2 = (static_cast<uint32_t>(message_base_addr >> 12) << 12)
                   | (static_cast<uint32_t>(message_bank_hint & 0x7) << 9);
    uint32_t word3 = (static_cast<uint32_t>(state_xfer_bytes) << 16)
                   | static_cast<uint32_t>(message_xfer_bytes);
    mem.write_word(byte_addr + 8, word2);
    mem.write_word(byte_addr + 12, word3);

    uint32_t word4 = (static_cast<uint32_t>(state_step_bytes) << 24)
                   | (static_cast<uint32_t>(message_step_bytes) << 16);
    mem.write_word(byte_addr + 16, word4);
}

inline void write_meta_node(GlobalMemorySpace& mem,
                            uint64_t byte_addr,
                            uint8_t  txn_id,
                            bool     is_factor,
                            uint8_t  dofs,
                            uint8_t  adj_count,
                            uint32_t state_base_addr,
                            uint8_t  state_bank_hint,
                            uint32_t message_base_addr,
                            uint8_t  message_bank_hint,
                            uint16_t state_xfer_bytes,
                            uint16_t message_xfer_bytes,
                            uint8_t  state_step_bytes,
                            uint8_t  message_step_bytes) {
    uint32_t word0 = (static_cast<uint32_t>(adj_count & 0xF) << 12)
                   | (static_cast<uint32_t>(dofs & 0x7) << 9)
                   | (static_cast<uint32_t>(is_factor ? 1 : 0) << 8)
                   | static_cast<uint32_t>(txn_id);
    uint32_t word1 = (static_cast<uint32_t>(state_base_addr >> 12) << 12)
                   | (static_cast<uint32_t>(state_bank_hint & 0x7) << 9);
    mem.write_word(byte_addr + 0, word0);
    mem.write_word(byte_addr + 4, word1);

    uint32_t word2 = (static_cast<uint32_t>(message_base_addr >> 12) << 12)
                   | (static_cast<uint32_t>(message_bank_hint & 0x7) << 9);
    uint32_t word3 = (static_cast<uint32_t>(state_xfer_bytes) << 16)
                   | static_cast<uint32_t>(message_xfer_bytes);
    mem.write_word(byte_addr + 8, word2);
    mem.write_word(byte_addr + 12, word3);

    uint32_t word4 = (static_cast<uint32_t>(state_step_bytes) << 24)
                   | (static_cast<uint32_t>(message_step_bytes) << 16);
    mem.write_word(byte_addr + 16, word4);
}

inline void write_fp32(GlobalMemorySpace& mem, uint64_t byte_addr, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(float));
    mem.write_word(byte_addr, bits);
}

} // namespace spm_loader

#endif // FACTOR_GRAPH_LAYOUT_HPP_
