// mesh_NxM_gbp.cc — End-to-end GBP convergence testbench
// Loads graph data, initializes SPM, runs GBP iterations, computes ARE.
//
// Usage: ./Vmesh_2x2_gbp_top --graph <partition.json> --dataset <fr1desk.txt>
//
// Flow:
//   1. Parse partition JSON + dataset
//   2. Initialize SPM for each PE (NodeHeaders, AdjEntries, STATE)
//   3. Release reset
//   4. Wait for convergence or timeout
//   5. Read beliefs from SPM
//   6. Compute ARE
//   7. Report PASS/FAIL

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <svdpi.h>
#include "verilated.h"
#include "Vmesh_2x2_gbp_top.h"

// Include ARE calculator
#include "../tools/are_calculator.hpp"

using namespace std;
using namespace gbp;

// ============================================================================
// Constants
// ============================================================================
static constexpr int kNumPEs        = 4;
static constexpr int kNumBanks      = 8;
static constexpr int kWordsPerBeat  = 8;
static constexpr int kRowsPerBank   = 4096;
static constexpr int kBankWords     = kRowsPerBank * kWordsPerBeat;

// ============================================================================
// DPI-C SPM memory
// ============================================================================
static array<array<vector<uint32_t>, kNumBanks>, kNumPEs> g_dpi_bank_mem;

static bool resolve_pe_and_bank_from_scope(int& pe, int& bank) {
  svScope scope = svGetScope();
  if (scope == nullptr) return false;
  const char* name_c = svGetNameFromScope(scope);
  if (name_c == nullptr) return false;
  string name(name_c);

  auto extract_index = [&](const string& label) -> int {
    size_t pos = name.find(label);
    if (pos == string::npos) return 0;
    size_t bra = name.find('[', pos);
    if (bra != string::npos) {
      size_t ket = name.find(']', bra);
      if (ket != string::npos) return stoi(name.substr(bra + 1, ket - bra - 1));
    }
    bra = name.find("__BRA__", pos);
    if (bra != string::npos) {
      size_t ket = name.find("__KET__", bra);
      if (ket != string::npos) return stoi(name.substr(bra + 7, ket - bra - 7));
    }
    return 0;
  };

  int row = extract_index("g_pe_r");
  int col = extract_index("g_pe_c");
  pe = row * 2 + col;
  bank = extract_index("banks");
  return true;
}

extern "C" int pmem_read(int raddr) {
  int pe = 0, bank = 0;
  resolve_pe_and_bank_from_scope(pe, bank);
  if (pe < 0 || pe >= kNumPEs) pe = 0;
  if (bank < 0 || bank >= kNumBanks) bank = 0;
  if (raddr < 0 || raddr >= static_cast<int>(g_dpi_bank_mem[pe][bank].size())) return 0;
  return static_cast<int>(g_dpi_bank_mem[pe][bank][raddr]);
}

extern "C" void pmem_write(int waddr, int wdata, char byte_num) {
  int pe = 0, bank = 0;
  resolve_pe_and_bank_from_scope(pe, bank);
  if (pe < 0 || pe >= kNumPEs) pe = 0;
  if (bank < 0 || bank >= kNumBanks) bank = 0;
  if (waddr < 0 || waddr >= static_cast<int>(g_dpi_bank_mem[pe][bank].size())) return;
  uint32_t shift = static_cast<uint32_t>(static_cast<unsigned char>(byte_num)) * 8u;
  uint32_t mask = 0xFFu << shift;
  g_dpi_bank_mem[pe][bank][waddr] = (g_dpi_bank_mem[pe][bank][waddr] & ~mask)
                                    | (static_cast<uint32_t>(wdata) & mask);
}

// ============================================================================
// Helpers
// ============================================================================
static inline void tick(Vmesh_2x2_gbp_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vmesh_2x2_gbp_top* dut, int cycles = 10) {
  dut->rst_n = 0;
  for (int i = 0; i < cycles; i++) tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

// ============================================================================
// SPM Helpers
// ============================================================================
static void spm_write_word(int pe, uint32_t word_addr, uint32_t data) {
  uint32_t bank = (word_addr >> 1) & 0x7;
  uint32_t row  = word_addr >> 4;
  uint32_t word_in_row = word_addr & 0x1;
  uint32_t bank_word_addr = row * 2 + word_in_row;
  if (bank_word_addr < g_dpi_bank_mem[pe][bank].size()) {
    g_dpi_bank_mem[pe][bank][bank_word_addr] = data;
  }
}

static uint32_t spm_read_word(int pe, uint32_t word_addr) {
  uint32_t bank = (word_addr >> 1) & 0x7;
  uint32_t row  = word_addr >> 4;
  uint32_t word_in_row = word_addr & 0x1;
  uint32_t bank_word_addr = row * 2 + word_in_row;
  if (bank_word_addr < g_dpi_bank_mem[pe][bank].size()) {
    return g_dpi_bank_mem[pe][bank][bank_word_addr];
  }
  return 0;
}

static void spm_write_beat(int pe, uint32_t word_addr, uint64_t data) {
  spm_write_word(pe, word_addr, (uint32_t)data);
  spm_write_word(pe, word_addr + 1, (uint32_t)(data >> 32));
}

static void spm_write_node_header(int pe, uint32_t node_id, uint32_t dof,
                                   uint32_t adj_count, uint32_t adj_base,
                                   uint32_t state_base, uint32_t state_words) {
  uint64_t header = 0;
  header |= (uint64_t)(node_id & 0x3FF);
  header |= (uint64_t)(dof & 0xF) << 10;
  header |= (uint64_t)(adj_count & 0xF) << 14;
  header |= (uint64_t)(adj_base & 0x3FFFF) << 18;
  header |= (uint64_t)(state_base & 0x3FFFF) << 36;
  header |= (uint64_t)(state_words & 0x3F) << 54;
  spm_write_beat(pe, node_id * 2, header);
}

static void spm_write_adj_entry(int pe, uint32_t adj_addr, uint32_t neighbor_id,
                                 uint32_t neighbor_x, uint32_t neighbor_y) {
  uint64_t entry = 0;
  entry |= (uint64_t)(neighbor_id & 0x3FF);
  entry |= (uint64_t)(neighbor_x & 0x3F) << 10;
  entry |= (uint64_t)(neighbor_y & 0x1F) << 16;
  spm_write_beat(pe, adj_addr, entry);
}

static void spm_write_state_word(int pe, uint32_t state_addr, float val) {
  spm_write_word(pe, state_addr, float_to_u32(val));
}

// ============================================================================
// Graph Data
// ============================================================================
struct GraphNode {
  int id;
  int pe;
  int dof;
  vector<float> prior_eta;
  vector<float> prior_lambda;
};

struct GraphEdge {
  int src;  // factor id
  int dst;  // variable id
};

struct GraphData {
  vector<GraphNode> nodes;
  vector<GraphEdge> edges;
  int n_pes;
  int mesh_x;
  int mesh_y;
};

// ============================================================================
// SPM Initialization
// ============================================================================
// SPM Layout per PE:
//   [0x0000..0x00FF] META region (NodeHeaders, AdjEntries)
//   [0x0100..0x7FFF] STATE region
//   [0x8000..0xFFFF] STAGING region (runtime)
//
// For simplicity, we use a flat layout:
//   NodeHeaders at word_addr = node_id * 2
//   AdjEntries at word_addr = 0x100 + edge_idx
//   State at word_addr = 0x200 + node_offset

static constexpr uint32_t kAdjBase   = 0x100;
static constexpr uint32_t kStateBase = 0x200;

static void init_pe_spm(int pe, const vector<GraphNode>& nodes,
                         const vector<GraphEdge>& edges, int pe_id) {
  // Find nodes assigned to this PE
  vector<const GraphNode*> pe_nodes;
  for (const auto& n : nodes) {
    if (n.pe == pe_id) pe_nodes.push_back(&n);
  }

  // Find edges connected to this PE's nodes
  vector<const GraphEdge*> pe_edges;
  for (const auto& e : edges) {
    for (const auto* n : pe_nodes) {
      if (n->id == e.src || n->id == e.dst) {
        pe_edges.push_back(&e);
        break;
      }
    }
  }

  // Write NodeHeaders
  uint32_t adj_offset = kAdjBase;
  uint32_t state_offset = kStateBase;

  for (const auto* node : pe_nodes) {
    int compact_words = node->dof + node->dof * (node->dof + 1) / 2;
    int adj_count = 0;
    for (const auto* e : pe_edges) {
      if (e->src == node->id || e->dst == node->id) adj_count++;
    }

    spm_write_node_header(pe, node->id, node->dof, adj_count,
                          adj_offset, state_offset, compact_words);

    // Write AdjEntries
    for (const auto* e : pe_edges) {
      if (e->src == node->id || e->dst == node->id) {
        int neighbor_id = (e->src == node->id) ? e->dst : e->src;
        // For now, all neighbors are local (same PE)
        // In real mesh, need to compute neighbor PE coordinates
        spm_write_adj_entry(pe, adj_offset, neighbor_id, 0, 0);
        adj_offset++;
      }
    }

    // Write state (prior eta + lambda)
    for (size_t i = 0; i < node->prior_eta.size(); i++) {
      spm_write_state_word(pe, state_offset + i, node->prior_eta[i]);
    }
    for (size_t i = 0; i < node->prior_lambda.size(); i++) {
      spm_write_state_word(pe, state_offset + node->dof + i, node->prior_lambda[i]);
    }

    state_offset += compact_words;
  }
}

// ============================================================================
// Result Reading
// ============================================================================
static vector<VariableBelief> read_beliefs(int pe, const vector<GraphNode>& nodes) {
  vector<VariableBelief> beliefs;
  for (const auto& node : nodes) {
    if (node.pe != pe) continue;

    VariableBelief b;
    b.node_id = node.id;
    b.dof = node.dof;

    // Read NodeHeader to get state_base
    uint32_t lo = spm_read_word(pe, node.id * 2);
    uint32_t hi = spm_read_word(pe, node.id * 2 + 1);
    NodeHeader hdr = decode_node_header(lo, hi);

    int compact_words = b.dof + b.dof * (b.dof + 1) / 2;
    for (int i = 0; i < b.dof; i++) {
      b.eta.push_back(u32_to_float(spm_read_word(pe, hdr.state_base + i)));
    }
    for (int i = 0; i < b.dof * (b.dof + 1) / 2; i++) {
      b.lambda.push_back(u32_to_float(spm_read_word(pe, hdr.state_base + b.dof + i)));
    }
    beliefs.push_back(b);
  }
  return beliefs;
}

// ============================================================================
// Simple ARE (for linear graphs without full factor model)
// ============================================================================
static double compute_simple_are(const vector<VariableBelief>& beliefs) {
  // Simple ARE: sum of |eta_i / lambda_ii - 0|^2 / n
  // This is a placeholder for the full BA ARE
  double total = 0.0;
  for (const auto& b : beliefs) {
    for (int i = 0; i < b.dof; i++) {
      float mu_i = b.eta[i] / b.lambda[i * (i + 1) / 2 + i];  // eta[i] / lambda[i,i]
      total += mu_i * mu_i;
    }
  }
  return total / beliefs.size();
}

// ============================================================================
// Main Test
// ============================================================================
int run_test(int argc, char** argv) {
  // Parse arguments
  string graph_path;
  string dataset_path;
  int max_iters = 10;
  int max_cycles = 10000;
  double are_threshold = 0.1;

  for (int i = 1; i < argc; i++) {
    string arg(argv[i]);
    if (arg == "--graph" && i + 1 < argc) graph_path = argv[++i];
    if (arg == "--dataset" && i + 1 < argc) dataset_path = argv[++i];
    if (arg == "--iters" && i + 1 < argc) max_iters = atoi(argv[++i]);
    if (arg == "--cycles" && i + 1 < argc) max_cycles = atoi(argv[++i]);
    if (arg == "--threshold" && i + 1 < argc) are_threshold = atof(argv[++i]);
  }

  printf("=== GBP End-to-End Convergence Test ===\n");
  printf("Graph: %s\n", graph_path.empty() ? "(default 4-node chain)" : graph_path.c_str());
  printf("Max iterations: %d\n", max_iters);
  printf("ARE threshold: %f\n", are_threshold);

  // Initialize DPI memory
  for (int pe = 0; pe < kNumPEs; pe++) {
    for (int b = 0; b < kNumBanks; b++) {
      g_dpi_bank_mem[pe][b].resize(kBankWords, 0);
    }
  }

  // Create default graph (4-node chain: F0→V1→F2→V3, all on PE 0)
  GraphData graph;
  graph.n_pes = 1;
  graph.mesh_x = 1;
  graph.mesh_y = 1;

  // Variable nodes
  graph.nodes.push_back({1, 0, 2, {1.0f, 2.0f}, {1.0f, 0.0f, 1.0f}});  // V1
  graph.nodes.push_back({3, 0, 2, {3.0f, 4.0f}, {1.0f, 0.0f, 1.0f}});  // V3

  // Factor nodes (simplified: just edges, no factor state for now)
  graph.edges.push_back({0, 1});  // F0→V1
  graph.edges.push_back({2, 3});  // F2→V3

  // Initialize SPM
  printf("\nInitializing SPM...\n");
  for (int pe = 0; pe < kNumPEs; pe++) {
    init_pe_spm(pe, graph.nodes, graph.edges, pe);
  }

  // Verify SPM initialization
  printf("Verifying SPM...\n");
  for (const auto& node : graph.nodes) {
    uint32_t lo = spm_read_word(node.pe, node.id * 2);
    uint32_t hi = spm_read_word(node.pe, node.id * 2 + 1);
    NodeHeader hdr = decode_node_header(lo, hi);
    printf("  Node %d: dof=%d adj=%d state_base=%d state_words=%d\n",
           hdr.node_id, hdr.dof, hdr.adj_count, hdr.state_base, hdr.state_words);
  }

  // Create and reset DUT
  auto* dut = new Vmesh_2x2_gbp_top;
  dut->wb_cmd_valid_i = 0;
  dut->wb_cmd_is_factor_i = 0;
  dut->wb_force_done_valid_i = 0;
  dut->wb_inject_fetch_resp_valid_i = 0;
  dut->wb_inject_fetch_resp_data_valid_i = 0;
  dut->wb_inject_fetch_resp_last_i = 0;
  dut->wb_inject_fetch_resp_done_valid_i = 0;
  reset_dut(dut, 10);
  printf("DUT reset complete.\n");

  // Run simulation
  // NOTE: Without proper DMA/GS, we use whitebox mode to inject commands
  // For self-scheduling mode, the PE reads SPM and schedules nodes automatically
  printf("\nRunning simulation (whitebox mode)...\n");

  int phase_switches = 0;
  int cycle;
  for (cycle = 0; cycle < max_cycles; cycle++) {
    tick(dut);

    // Count phase switches (indicator of GBP iterations)
    if (dut->reset_valid_o) {
      phase_switches++;
    }

    // Check if all PEs done
    if (dut->wb_done_valid_o) {
      printf("  PE done at cycle %d\n", cycle);
    }

    // Simple termination: after enough cycles
    if (cycle > 100) break;  // Placeholder
  }

  printf("\nSimulation complete after %d cycles.\n", cycle);
  printf("Phase switches: %d\n", phase_switches);

  // Read results
  printf("\nReading results...\n");
  vector<VariableBelief> all_beliefs;
  for (const auto& node : graph.nodes) {
    uint32_t lo = spm_read_word(node.pe, node.id * 2);
    uint32_t hi = spm_read_word(node.pe, node.id * 2 + 1);
    NodeHeader hdr = decode_node_header(lo, hi);

    VariableBelief b;
    b.node_id = node.id;
    b.dof = node.dof;
    for (int i = 0; i < b.dof; i++) {
      b.eta.push_back(u32_to_float(spm_read_word(node.pe, hdr.state_base + i)));
    }
    for (int i = 0; i < b.dof * (b.dof + 1) / 2; i++) {
      b.lambda.push_back(u32_to_float(spm_read_word(node.pe, hdr.state_base + b.dof + i)));
    }
    all_beliefs.push_back(b);

    printf("  Node %d: eta=[%.4f, %.4f] lam=[%.4f, %.4f, %.4f]\n",
           b.node_id, b.eta[0], b.eta[1],
           b.lambda[0], b.lambda[1], b.lambda[2]);
  }

  // Compute ARE
  double are = compute_simple_are(all_beliefs);
  printf("\nARE = %.6f (threshold = %.6f)\n", are, are_threshold);

  // Cleanup
  delete dut;

  // Report
  if (are < are_threshold) {
    printf("\n=== PASS === ARE %.6f < threshold %.6f\n", are, are_threshold);
    return 0;
  } else {
    printf("\n=== FAIL === ARE %.6f >= threshold %.6f\n", are, are_threshold);
    return 1;
  }
}
