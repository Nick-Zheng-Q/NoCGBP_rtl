// hex_writer.hpp
// Writes per-bank hex files for Verilator $readmemh initialization.

#ifndef HEX_WRITER_HPP_
#define HEX_WRITER_HPP_

#include "global_memory.hpp"
#include <string>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <iomanip>

namespace spm_loader {

class HexWriter {
public:
    // Write all 8 bank hex files from a GlobalMemorySpace.
    // Files are named: <prefix>_bank0.hex, <prefix>_bank1.hex, ...
    // Returns true on success.
    static bool write_banks(const GlobalMemorySpace& mem,
                            const std::string& prefix);

private:
    static bool write_single_bank(const GlobalMemorySpace& mem,
                                  int bank_id,
                                  const std::string& filename);
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------
inline bool HexWriter::write_banks(const GlobalMemorySpace& mem,
                                   const std::string& prefix) {
    for (int bank = 0; bank < kNumBanks; ++bank) {
        std::string filename = prefix + "_bank" + std::to_string(bank) + ".hex";
        if (!write_single_bank(mem, bank, filename)) {
            std::cerr << "[HexWriter] Failed to write " << filename << "\n";
            return false;
        }
    }
    std::cout << "[HexWriter] Wrote " << kNumBanks << " bank hex files with prefix: "
              << prefix << "\n";
    return true;
}

inline bool HexWriter::write_single_bank(const GlobalMemorySpace& mem,
                                         int bank_id,
                                         const std::string& filename) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) return false;

    for (int row = 0; row < static_cast<int>(kRowsPerBank); ++row) {
        uint64_t byte_addr = bank_row_to_addr(bank_id, row);
        bool non_zero = false;
        for (int w = 0; w < kBeatBytes / kWordBytes; ++w) {
            if (mem.read_word(byte_addr + w * kWordBytes) != 0) {
                non_zero = true;
                break;
            }
        }
        if (non_zero) {
            ofs << "@" << std::hex << std::uppercase << std::setw(4)
                << std::setfill('0') << row << " ";
            for (int w = (kBeatBytes / kWordBytes) - 1; w >= 0; --w) {
                uint32_t word = mem.read_word(byte_addr + w * kWordBytes);
                ofs << std::setw(8) << std::setfill('0') << word;
            }
            ofs << "\n";
        }
    }
    return true;
}

} // namespace spm_loader

#endif // HEX_WRITER_HPP_
