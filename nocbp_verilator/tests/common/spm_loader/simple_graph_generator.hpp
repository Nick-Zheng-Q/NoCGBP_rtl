// simple_graph_generator.hpp
// Generates a minimal synthetic factor graph into GlobalMemorySpace.
// This is a placeholder until real dataset parsing is implemented.

#ifndef SIMPLE_GRAPH_GENERATOR_HPP_
#define SIMPLE_GRAPH_GENERATOR_HPP_

#include "global_memory.hpp"
#include <cstring>

namespace spm_loader {

// Helper: write a 32-bit word at a byte address.
inline void write_word(GlobalMemorySpace& mem, uint64_t byte_addr, uint32_t data) {
    mem.write_word(byte_addr, data);
}

// Helper: write an IEEE-754 float as a raw 32-bit word.
inline void write_fp32(GlobalMemorySpace& mem, uint64_t byte_addr, float val) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
    std::memcpy(&bits, &val, sizeof(bits));
    mem.write_word(byte_addr, bits);
}

// Write a META scheduler header into a single 256-bit beat.
// Format (matching control_unit_gbp header parsing):
//   word0[15:0]  = var_row_count
//   word0[31:16] = fac_row_count
inline void write_meta_header(GlobalMemorySpace& mem, uint64_t byte_addr,
                              uint16_t var_count, uint16_t fac_count,
                              uint32_t /*message_base*/, uint8_t /*message_bank*/,
                              uint16_t /*state_xfer*/, uint16_t /*message_xfer*/,
                              uint8_t /*state_step*/, uint8_t /*message_step*/) {
    uint32_t word0 = (static_cast<uint32_t>(fac_count) << 16) | var_count;
    mem.write_word(byte_addr + 0, word0);
    // words 1-7 are zero / reserved
}

// Write a META node entry into a single 256-bit beat.
// Format (matching control_unit_gbp node entry parsing):
//   word0[7:0]   = txn_id
//   word0[8]     = is_factor
//   word0[11:9]  = dofs
//   word0[15:12] = adj_count
//   word1[31:12] = state_base_addr
//   word1[11:9]  = state_bank_hint
//   word2[31:12] = message_base_addr
//   word2[11:9]  = message_bank_hint
//   word3[31:16] = state_xfer_bytes
//   word3[15:0]  = message_xfer_bytes
//   word4[31:24] = state_step_bytes
//   word4[23:16] = message_step_bytes
inline void write_meta_node(GlobalMemorySpace& mem, uint64_t byte_addr,
                            uint8_t txn_id, bool is_factor, uint8_t dofs, uint8_t adj_count,
                            uint32_t state_base, uint8_t state_bank,
                            uint32_t message_base, uint8_t message_bank,
                            uint16_t state_xfer, uint16_t message_xfer,
                            uint8_t state_step, uint8_t message_step) {
    uint32_t word0 = (static_cast<uint32_t>(adj_count & 0xF) << 12)
                   | (static_cast<uint32_t>(dofs & 0x7) << 9)
                   | (static_cast<uint32_t>(is_factor ? 1 : 0) << 8)
                   | txn_id;
    uint32_t word1 = ((state_base & 0xFFFFF) << 12)
                   | (static_cast<uint32_t>(state_bank & 0x7) << 9);
    uint32_t word2 = ((message_base & 0xFFFFF) << 12)
                   | (static_cast<uint32_t>(message_bank & 0x7) << 9);
    uint32_t word3 = (static_cast<uint32_t>(state_xfer) << 16)
                   | message_xfer;
    uint32_t word4 = (static_cast<uint32_t>(state_step) << 24)
                   | (static_cast<uint32_t>(message_step) << 16);

    mem.write_word(byte_addr + 0 * kWordBytes, word0);
    mem.write_word(byte_addr + 1 * kWordBytes, word1);
    mem.write_word(byte_addr + 2 * kWordBytes, word2);
    mem.write_word(byte_addr + 3 * kWordBytes, word3);
    mem.write_word(byte_addr + 4 * kWordBytes, word4);
    // words 5-7 are zero / reserved
}

// Create a minimal graph: 1 variable node with 1 adjacent factor.
// Writes META header + node entry, plus dummy STATE and MESSAGE payloads.
// Returns true on success.
bool generate_minimal_variable_graph(GlobalMemorySpace& mem);

// ---------------------------------------------------------------------------
// Inline implementation
// ---------------------------------------------------------------------------
inline bool generate_minimal_variable_graph(GlobalMemorySpace& mem) {
    mem.clear();

    uint16_t var_count = 1;
    uint16_t fac_count = 1;

    // Word addresses for state/message base (SPM uses word addresses)
    uint32_t state_base_addr = (0 << (3 + 3)) | (1 << 3);    // bank=1, row=0
    uint32_t message_base_addr = (0 << (3 + 3)) | (4 << 3);  // bank=4, row=0

    // Byte addresses for payload data in GlobalMemorySpace
    uint32_t state_byte_addr = state_base_addr * 4;
    uint32_t message_byte_addr = message_base_addr * 4;

    // dofs=2 requires compact_payload_beats=1 (32 bytes) per state/message
    uint16_t state_xfer_bytes = 32;
    uint16_t message_xfer_bytes = 32;
    uint8_t  state_step_bytes = 32;
    uint8_t  message_step_bytes = 32;

    write_meta_header(mem, 0,
                      var_count, fac_count,
                      message_base_addr, 4,
                      state_xfer_bytes, message_xfer_bytes,
                      state_step_bytes, message_step_bytes);

    uint8_t txn_id = 0x40;
    bool is_factor = false;
    uint8_t dofs = 2;
    uint8_t adj_count = 1;

    // Variable node at row 1 (byte addr 256), factor node at row 2 (byte addr 512)
    write_meta_node(mem, 256,
                    txn_id, false, dofs, adj_count,
                    state_base_addr, 1,
                    message_base_addr, 4,
                    state_xfer_bytes, message_xfer_bytes,
                    state_step_bytes, message_step_bytes);

    write_meta_node(mem, 512,
                    txn_id + 1, true, dofs, adj_count,
                    state_base_addr, 1,
                    message_base_addr, 4,
                    state_xfer_bytes, message_xfer_bytes,
                    state_step_bytes, message_step_bytes);

    for (int i = 0; i < 8; ++i) {
        float val = 0.1f * static_cast<float>(i + 1);
        write_fp32(mem, state_byte_addr + i * 4, val);
    }

    for (int i = 0; i < 8; ++i) {
        float val = 0.05f * static_cast<float>(i + 1);
        write_fp32(mem, message_byte_addr + i * 4, val);
    }

    return true;
}

} // namespace spm_loader

#endif // SIMPLE_GRAPH_GENERATOR_HPP_
