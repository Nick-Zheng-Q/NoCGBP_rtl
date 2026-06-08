// hex_writer.cpp

#include "hex_writer.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

namespace spm_loader {

bool HexWriter::write_banks(const GlobalMemorySpace& mem,
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

bool HexWriter::write_single_bank(const GlobalMemorySpace& mem,
                                  int bank_id,
                                  const std::string& filename) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) return false;

    // Only write non-zero rows to keep files small.
    // $readmemh treats missing addresses as zero.
    for (int row = 0; row < static_cast<int>(kRowsPerBank); ++row) {
        uint64_t byte_addr = bank_row_to_addr(bank_id, row);
        uint64_t beat = mem.read_beat(byte_addr);
        if (beat != 0) {
            ofs << "@" << std::hex << std::uppercase << std::setw(4)
                << std::setfill('0') << row << " "
                << std::setw(16) << std::setfill('0') << beat << "\n";
        }
    }
    return true;
}

} // namespace spm_loader
