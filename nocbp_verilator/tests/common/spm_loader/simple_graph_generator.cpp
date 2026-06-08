// simple_graph_generator.cpp

#include "simple_graph_generator.hpp"
#include "factor_graph_layout.hpp"
#include <cmath>

namespace spm_loader {

bool generate_minimal_variable_graph(GlobalMemorySpace& mem) {
    mem.clear();

    // -------------------------------------------------------------------------
    // META header at row 0 (byte addr 0)
    // -------------------------------------------------------------------------
    uint16_t var_count = 1;   // 1 variable node
    uint16_t fac_count = 1;   // 1 factor node (so scan continues past header)

    // Place STATE payload at bank 1, row 0  => byte addr = bank_row_to_addr(1, 0) = 8
    uint32_t state_base_addr = static_cast<uint32_t>(bank_row_to_addr(1, 0));
    // Place MESSAGE payload at bank 4, row 0 => byte addr = bank_row_to_addr(4, 0) = 32
    uint32_t message_base_addr = static_cast<uint32_t>(bank_row_to_addr(4, 0));

    uint16_t state_xfer_bytes = 32;    // 4 beats = 32 bytes of state data
    uint16_t message_xfer_bytes = 32;  // 4 beats = 32 bytes of message data
    uint8_t  state_step_bytes = 32;
    uint8_t  message_step_bytes = 32;

    write_meta_header(mem, 0,
                      var_count, fac_count,
                      message_base_addr, 4,  // message bank hint = 4
                      state_xfer_bytes, message_xfer_bytes,
                      state_step_bytes, message_step_bytes);

    // -------------------------------------------------------------------------
    // META node entry for variable node at row 1 (byte addr 64)
    // -------------------------------------------------------------------------
    uint8_t txn_id = 0x40;
    bool is_factor = false;
    uint8_t dofs = 3;
    uint8_t adj_count = 1;  // must be >=1 for MESSAGE read to happen

    write_meta_node(mem, 64,
                    txn_id, is_factor, dofs, adj_count,
                    state_base_addr, 1,   // state bank hint = 1
                    message_base_addr, 4, // message bank hint = 4
                    state_xfer_bytes, message_xfer_bytes,
                    state_step_bytes, message_step_bytes);

    // -------------------------------------------------------------------------
    // META node entry for factor node at row 2 (byte addr 128)
    // -------------------------------------------------------------------------
    write_meta_node(mem, 128,
                    txn_id + 1, true, dofs, adj_count,
                    state_base_addr, 1,
                    message_base_addr, 4,
                    state_xfer_bytes, message_xfer_bytes,
                    state_step_bytes, message_step_bytes);

    // -------------------------------------------------------------------------
    // Write dummy STATE payload at bank 1, row 0 (byte addr 8)
    // control_unit_gbp expects valid FP32 data. Fill with small non-zero values.
    // -------------------------------------------------------------------------
    for (int i = 0; i < 8; ++i) {  // 8 words = 32 bytes
        float val = 0.1f * static_cast<float>(i + 1);
        write_fp32(mem, state_base_addr + i * 4, val);
    }

    // -------------------------------------------------------------------------
    // Write dummy MESSAGE payload at bank 4, row 0 (byte addr 32)
    // -------------------------------------------------------------------------
    for (int i = 0; i < 8; ++i) {
        float val = 0.05f * static_cast<float>(i + 1);
        write_fp32(mem, message_base_addr + i * 4, val);
    }

    return true;
}

} // namespace spm_loader
