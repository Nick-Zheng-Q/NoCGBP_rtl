// global_memory.hpp
// Global DRAM space for full factor graph storage.
// This is the "source of truth" before subgraph scheduling extracts PE-local data.

#ifndef GLOBAL_MEMORY_HPP_
#define GLOBAL_MEMORY_HPP_

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>

namespace spm_loader {

// ---------------------------------------------------------------------------
// Configuration (must match RTL gbp_pkg.sv)
// ---------------------------------------------------------------------------
constexpr int kNumBanks = 8;
constexpr int kBeatBytes = 32;          // 256-bit beat
constexpr int kWordBytes = 4;           // 32-bit word
constexpr size_t kSPMBytesPerPE = 1ull << 20;  // 1 MB SPM per PE (RTL spec)
constexpr int kRowAddrW = 12;           // $clog2(SPM_BYTES_PER_PE / NUM_BANKS / BEAT_BYTES)
constexpr size_t kRowsPerBank = 1ull << kRowAddrW;  // 4096
constexpr size_t kBankBytes = kRowsPerBank * kBeatBytes;  // 131072 = 128KB

// Address mapping: interleave-by-beat (same as nocbp_simulator/pe/PESPM.hpp)
//   bank_id  = (byte_addr / kBeatBytes) % kNumBanks
//   bank_row = (byte_addr / kBeatBytes) / kNumBanks
inline int addr_to_bank(uint64_t byte_addr) {
    return static_cast<int>((byte_addr / kBeatBytes) % kNumBanks);
}
inline int addr_to_row(uint64_t byte_addr) {
    return static_cast<int>((byte_addr / kBeatBytes) / kNumBanks);
}
inline uint64_t bank_row_to_addr(int bank, int row) {
    return static_cast<uint64_t>(row * kNumBanks + bank) * kBeatBytes;
}

// ---------------------------------------------------------------------------
// GlobalMemorySpace
// A flat byte array representing the full graph data in host/DRAM.
// ---------------------------------------------------------------------------
class GlobalMemorySpace {
public:
    explicit GlobalMemorySpace(size_t total_bytes = kSPMBytesPerPE);

    // Byte-level access
    uint8_t read_byte(uint64_t addr) const;
    void write_byte(uint64_t addr, uint8_t val);

    // Beat-level (64-bit) access — handles bank interleave automatically
    uint64_t read_beat(uint64_t byte_addr) const;
    void write_beat(uint64_t byte_addr, uint64_t data);

    // Word-level (32-bit) access
    uint32_t read_word(uint64_t byte_addr) const;
    void write_word(uint64_t byte_addr, uint32_t data);

    // Multi-beat sequential write (starting at byte_addr, consecutive in *byte* space)
    void write_beats(uint64_t byte_addr, const uint64_t* data, size_t num_beats);

    // Zero-initialize
    void clear();

    // Raw pointer access (for direct serialization)
    const uint8_t* data() const { return mem_.data(); }
    uint8_t* data() { return mem_.data(); }
    size_t size() const { return mem_.size(); }

private:
    std::vector<uint8_t> mem_;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------
inline GlobalMemorySpace::GlobalMemorySpace(size_t total_bytes)
    : mem_(total_bytes, 0) {}

inline uint8_t GlobalMemorySpace::read_byte(uint64_t addr) const {
    if (addr >= mem_.size()) return 0;
    return mem_[addr];
}

inline void GlobalMemorySpace::write_byte(uint64_t addr, uint8_t val) {
    if (addr >= mem_.size()) return;
    mem_[addr] = val;
}

inline uint64_t GlobalMemorySpace::read_beat(uint64_t byte_addr) const {
    if (byte_addr + kBeatBytes > mem_.size()) return 0;
    uint64_t data = 0;
    std::memcpy(&data, &mem_[byte_addr], kBeatBytes);
    return data;
}

inline void GlobalMemorySpace::write_beat(uint64_t byte_addr, uint64_t data) {
    if (byte_addr + kBeatBytes > mem_.size()) return;
    std::memcpy(&mem_[byte_addr], &data, kBeatBytes);
}

inline uint32_t GlobalMemorySpace::read_word(uint64_t byte_addr) const {
    if (byte_addr + kWordBytes > mem_.size()) return 0;
    uint32_t data = 0;
    std::memcpy(&data, &mem_[byte_addr], kWordBytes);
    return data;
}

inline void GlobalMemorySpace::write_word(uint64_t byte_addr, uint32_t data) {
    if (byte_addr + kWordBytes > mem_.size()) return;
    std::memcpy(&mem_[byte_addr], &data, kWordBytes);
}

inline void GlobalMemorySpace::write_beats(uint64_t byte_addr, const uint64_t* data, size_t num_beats) {
    for (size_t i = 0; i < num_beats; ++i) {
        uint64_t addr = byte_addr + i * kBeatBytes;
        if (addr + kBeatBytes > mem_.size()) break;
        std::memcpy(&mem_[addr], &data[i], kBeatBytes);
    }
}

inline void GlobalMemorySpace::clear() {
    std::fill(mem_.begin(), mem_.end(), 0);
}

} // namespace spm_loader

#endif // GLOBAL_MEMORY_HPP_
