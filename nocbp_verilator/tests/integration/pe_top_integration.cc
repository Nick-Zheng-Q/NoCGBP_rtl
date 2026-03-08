#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include "verilated.h"
#include "Vpe_top_integration.h"

static constexpr uint32_t kRowBytesLg = 5;

struct MetricThreshold {
  double abs_tol = 0.0;
  double rel_tol = 0.0;
};

struct WorkloadMetrics {
  double final_are = 0.0;
  double final_energy = 0.0;
};

struct OracleData {
  MetricThreshold final_are_threshold;
  MetricThreshold final_energy_threshold;
  WorkloadMetrics synthetic_line;
  WorkloadMetrics synthetic_lattice;
};

static bool extract_named_double(const std::string& text,
                                 const std::string& pattern,
                                 double* out) {
  std::regex rx(pattern);
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(m[1].str().c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}

static bool extract_threshold(const std::string& text,
                             const char* metric,
                             const char* field,
                             double* out) {
  const std::string pattern =
      std::string("\"thresholds\"\\s*:\\s*\\{[\\s\\S]*?\"") + metric +
      "\"\\s*:\\s*\\{[\\s\\S]*?\"" + field + "\"\\s*:\\s*([-+0-9.eE]+)";
  return extract_named_double(text, pattern, out);
}

static bool extract_workload_metric(const std::string& text,
                                    const char* workload,
                                    const char* metric,
                                    double* out) {
  const std::string pattern =
      std::string("\"") + workload +
      "\"\\s*:\\s*\\{[\\s\\S]*?\"metrics\"\\s*:\\s*\\{[\\s\\S]*?\"" +
      metric + "\"\\s*:\\s*([-+0-9.eE]+)";
  return extract_named_double(text, pattern, out);
}

static bool extract_threshold_with_fallback(const std::string& text,
                                            const char* metric,
                                            double* abs_tol,
                                            double* rel_tol) {
  if (extract_threshold(text, metric, "abs_tol", abs_tol) &&
      extract_threshold(text, metric, "rel_tol", rel_tol)) {
    return true;
  }

  const std::string fallback_abs_pattern =
      std::string("\"thresholds\"\\s*:\\s*\\{[\\s\\S]*?\"are_energy\"\\s*:\\s*\\{[\\s\\S]*?\"abs_err\"\\s*:\\s*([-+0-9.eE]+)");
  const std::string fallback_rel_pattern =
      std::string("\"thresholds\"\\s*:\\s*\\{[\\s\\S]*?\"are_energy\"\\s*:\\s*\\{[\\s\\S]*?\"rel_err\"\\s*:\\s*([-+0-9.eE]+)");
  return extract_named_double(text, fallback_abs_pattern, abs_tol) &&
         extract_named_double(text, fallback_rel_pattern, rel_tol);
}

static bool extract_workload_metric_with_fallback(const std::string& text,
                                                  const char* workload,
                                                  const char* primary_metric,
                                                  const char* fallback_metric,
                                                  double* out) {
  return extract_workload_metric(text, workload, primary_metric, out) ||
         extract_workload_metric(text, workload, fallback_metric, out);
}

static bool load_phase1_oracle(const char* path, OracleData* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open oracle file: %s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  OracleData parsed{};
  if (!extract_threshold_with_fallback(
          text, "final_are", &parsed.final_are_threshold.abs_tol, &parsed.final_are_threshold.rel_tol)) {
    std::fprintf(stderr, "FAIL: oracle missing threshold for metric=final_are\n");
    return false;
  }
  if (!extract_threshold_with_fallback(text,
                                       "final_energy",
                                       &parsed.final_energy_threshold.abs_tol,
                                       &parsed.final_energy_threshold.rel_tol)) {
    std::fprintf(stderr, "FAIL: oracle missing threshold for metric=final_energy\n");
    return false;
  }

  if (!extract_workload_metric_with_fallback(
          text, "synthetic_line", "final_are", "terminal_are", &parsed.synthetic_line.final_are) ||
      !extract_workload_metric_with_fallback(text,
                                             "synthetic_line",
                                             "final_energy",
                                             "terminal_energy",
                                             &parsed.synthetic_line.final_energy)) {
    std::fprintf(stderr,
                 "FAIL: oracle missing required synthetic_line metrics (final_are/final_energy)\n");
    return false;
  }

  if (!extract_workload_metric_with_fallback(text,
                                             "synthetic_lattice",
                                             "final_are",
                                             "terminal_are",
                                             &parsed.synthetic_lattice.final_are) ||
      !extract_workload_metric_with_fallback(text,
                                             "synthetic_lattice",
                                             "final_energy",
                                             "terminal_energy",
                                             &parsed.synthetic_lattice.final_energy)) {
    std::fprintf(stderr,
                 "FAIL: oracle missing required synthetic_lattice metrics (final_are/final_energy)\n");
    return false;
  }

  *out = parsed;
  return true;
}

static bool check_oracle_metric(const char* workload,
                                const char* metric,
                                double observed,
                                double expected,
                                const MetricThreshold& threshold) {
  if (!std::isfinite(observed) || !std::isfinite(expected)) {
    std::fprintf(stderr,
                 "FAIL: oracle mismatch workload=%s metric=%s observed=%.9g expected=%.9g abs_err=nan rel_err=nan abs_tol=%.9g rel_tol=%.9g reason=non_finite\n",
                 workload,
                 metric,
                 observed,
                 expected,
                 threshold.abs_tol,
                 threshold.rel_tol);
    return false;
  }

  const double abs_err = std::fabs(observed - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  if (abs_err <= threshold.abs_tol || rel_err <= threshold.rel_tol) {
    return true;
  }

  std::fprintf(stderr,
               "FAIL: oracle mismatch workload=%s metric=%s observed=%.9g expected=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g\n",
               workload,
               metric,
               observed,
               expected,
               abs_err,
               rel_err,
               threshold.abs_tol,
               threshold.rel_tol);
  return false;
}

enum StreamClass : uint32_t {
  kMetaClass = 0,
  kStateClass = 1,
  kMessageClass = 2,
};

static uint32_t bank_id_from_addr(uint32_t addr) {
  return (addr >> kRowBytesLg) & 0x7u;
}

static bool mapping_allowed(uint32_t stream_class, uint32_t bank_id) {
  if (stream_class == kMetaClass) {
    return bank_id == 0;
  }
  if (stream_class == kStateClass) {
    return bank_id >= 1 && bank_id <= 3;
  }
  if (stream_class == kMessageClass) {
    return bank_id >= 4 && bank_id <= 7;
  }
  return false;
}

static void tick(Vpe_top_integration* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vpe_top_integration* dut) {
  dut->rst_n = 0;
  dut->cmd_valid = 0;
  dut->cmd_kind = 0;
  dut->cmd_txn = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static int run_unsupported_cmd_negative_case(Vpe_top_integration* dut) {
  reset_dut(dut);

  dut->cmd_valid = 1;
  dut->cmd_kind = 0x2u;
  dut->cmd_txn = 0xA5u;

  bool handshake_seen = false;
  for (int cycle = 0; cycle < 32; ++cycle) {
    tick(dut);
    if (dut->cmd_ready) {
      handshake_seen = true;
      dut->cmd_valid = 0;
      break;
    }
  }

  if (!handshake_seen) {
    std::fprintf(stderr,
                 "pe_top integration: NEGATIVE_UNSUPPORTED_CMD_SETUP_FAIL sender_not_ready\n");
    return 1;
  }

  for (int cycle = 0; cycle < 32; ++cycle) {
    tick(dut);
    if (dut->cmd_rsp_done && dut->cmd_rsp_error) {
      std::fprintf(stderr,
                   "pe_top integration: UNSUPPORTED_CMD_REJECTED kind=0x%x txn=0x%02x\n",
                   static_cast<unsigned int>(0x2u),
                   static_cast<unsigned int>(0xA5u));
      return 2;
    }
  }

  std::fprintf(stderr,
               "pe_top integration: NEGATIVE_UNSUPPORTED_CMD_MISSING_RESPONSE kind=0x2 txn=0xA5\n");
  return 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vpe_top_integration;

  const char* negative_env = std::getenv("PE_TOP_NEGATIVE_UNSUPPORTED_CMD");
  const bool negative_unsupported_cmd_mode =
      (negative_env != nullptr) && (negative_env[0] != '\0') && (negative_env[0] != '0');

  if (negative_unsupported_cmd_mode) {
    const int rc = run_unsupported_cmd_negative_case(dut);
    delete dut;
    return rc;
  }

  const char* expected_oracle_path = std::getenv("PE_TOP_ORACLE_PATH");
  if (expected_oracle_path == nullptr) {
    expected_oracle_path = "tests/oracle/gbp_oracle_phase1.json";
  }
  const char* observed_oracle_path = std::getenv("PE_TOP_OBS_ORACLE_PATH");
  if (observed_oracle_path == nullptr) {
    observed_oracle_path = "tests/oracle/generated/gbp_oracle_phase1.json";
  }

  OracleData expected_oracle{};
  if (!load_phase1_oracle(expected_oracle_path, &expected_oracle)) {
    delete dut;
    return 1;
  }
  OracleData observed_oracle{};
  if (!load_phase1_oracle(observed_oracle_path, &observed_oracle)) {
    delete dut;
    return 1;
  }

  const int kMaxCycles = 500;
  const int kRequiredTxnObservations = 2;
  const int kMinBackpressureCycles = 2;
  const int kBackpressureRecoveryTimeout = 80;

  reset_dut(dut);

  bool saw_meta_read = false;
  bool saw_state_read = false;
  bool saw_message_read = false;
  bool saw_compute_start = false;
  bool saw_compute_done = false;
  bool saw_write_req = false;
  bool txn_propagated = false;
  unsigned int captured_cmd_txn = 0;
  bool saw_nonzero_payload = false;
  bool saw_zero_payload = false;
  bool saw_single_byte_sparse_payload = false;
  bool saw_sparse_after_zero_payload = false;

  unsigned int observed_start_txn[kRequiredTxnObservations] = {0, 0};
  unsigned int observed_write_txn[kRequiredTxnObservations] = {0, 0};
  int observed_start_cycle[kRequiredTxnObservations] = {-1, -1};
  int observed_write_cycle[kRequiredTxnObservations] = {-1, -1};
  int observed_start_count = 0;
  int observed_write_count = 0;

  bool waiting_for_write_after_done = false;
  bool sustained_backpressure_pending_recovery = false;
  bool saw_sustained_backpressure = false;
  bool backpressure_recovered = false;
  bool saw_immediate_write_after_done = false;
  bool post_done_write_window_required = false;
  int compute_done_cycle = -1;
  int backpressure_run_cycles = 0;
  int backpressure_window_start = -1;
  int backpressure_recovery_deadline = -1;

  struct ForbiddenMapping {
    uint32_t stream_class;
    uint32_t bank_id;
  };
  const ForbiddenMapping forbidden_mappings[] = {
      {kMetaClass, 4},
      {kStateClass, 0},
      {kMessageClass, 2},
  };

  uint32_t expected_read_class = kMetaClass;
  bool prev_rd_req_valid = false;
  uint32_t prev_rd_req_addr = 0;
  bool prev_compute_done = false;
  bool prev_wr_req_valid = false;

  for (int cycle = 0; cycle < kMaxCycles; ++cycle) {
    dut->cmd_valid = 0;
    dut->cmd_kind = 0;
    dut->cmd_txn = 0;

    tick(dut);

    if (dut->cmd_rsp_done || dut->cmd_rsp_error) {
      std::fprintf(stderr,
                   "FAIL: unexpected sideband response while cmd_valid=0 (done=%d err=%d cycle=%d)\n",
                   dut->cmd_rsp_done ? 1 : 0,
                   dut->cmd_rsp_error ? 1 : 0,
                   cycle);
      delete dut;
      return 1;
    }

    const bool new_rd_req =
        dut->rd_req_valid && (!prev_rd_req_valid || dut->rd_req_addr != prev_rd_req_addr);

    if (new_rd_req) {
      const uint32_t rd_bank = bank_id_from_addr(dut->rd_req_addr);
      if (!mapping_allowed(expected_read_class, rd_bank)) {
        std::fprintf(stderr,
                     "FAIL: class->bank invariant violation class=%u bank=%u addr=0x%05x cycle=%d\n",
                     expected_read_class,
                     rd_bank,
                     dut->rd_req_addr,
                     cycle);
        delete dut;
        return 1;
      }

      for (const auto& forbidden : forbidden_mappings) {
        if (expected_read_class == forbidden.stream_class && rd_bank == forbidden.bank_id) {
          std::fprintf(stderr,
                       "FAIL: forbidden class->bank vector observed class=%u bank=%u addr=0x%05x cycle=%d\n",
                       expected_read_class,
                       rd_bank,
                       dut->rd_req_addr,
                       cycle);
          delete dut;
          return 1;
        }
      }

      if (expected_read_class == kMetaClass) {
        saw_meta_read = true;
        expected_read_class = kStateClass;
      } else if (expected_read_class == kStateClass) {
        saw_state_read = true;
        expected_read_class = kMessageClass;
      } else {
        saw_message_read = true;
        expected_read_class = kMetaClass;
      }
    }

    if (dut->compute_start) {
      saw_compute_start = true;
      captured_cmd_txn = dut->cmd_txn_id;
      if (observed_start_count < kRequiredTxnObservations) {
        observed_start_txn[observed_start_count] = dut->cmd_txn_id;
        observed_start_cycle[observed_start_count] = cycle;
      }
      observed_start_count++;
    }
    if (dut->compute_done && !prev_compute_done) {
      saw_compute_done = true;
      if (observed_write_count < kRequiredTxnObservations) {
        waiting_for_write_after_done = true;
        post_done_write_window_required = true;
        sustained_backpressure_pending_recovery = false;
        compute_done_cycle = cycle;
        backpressure_run_cycles = 0;
        if (dut->wr_req_valid || prev_wr_req_valid) {
          saw_immediate_write_after_done = true;
          waiting_for_write_after_done = false;
        }
      }
    }

    if (dut->wr_req_valid) {
      const uint32_t payload = dut->wr_req_data;
      int nonzero_byte_count = 0;
      for (int byte_idx = 0; byte_idx < 4; ++byte_idx) {
        const uint32_t byte_mask = 0xFFu << (8 * byte_idx);
        if ((payload & byte_mask) != 0) {
          nonzero_byte_count++;
        }
      }

      if (payload != 0) {
        saw_nonzero_payload = true;
      }

      if (payload == 0) {
        saw_zero_payload = true;
      }

      if (nonzero_byte_count == 1) {
        saw_single_byte_sparse_payload = true;
        if (saw_zero_payload) {
          saw_sparse_after_zero_payload = true;
        }
      }
      saw_write_req = true;
      if (dut->wr_txn_id == captured_cmd_txn) {
        txn_propagated = true;
      }
      if (observed_write_count < kRequiredTxnObservations) {
        observed_write_txn[observed_write_count] = dut->wr_txn_id;
        observed_write_cycle[observed_write_count] = cycle;
      }
      observed_write_count++;
    }

    if (waiting_for_write_after_done) {
      if (dut->wr_req_valid) {
        if (!sustained_backpressure_pending_recovery && backpressure_run_cycles <= 1) {
          saw_immediate_write_after_done = true;
        }
        if (sustained_backpressure_pending_recovery) {
          backpressure_recovered = true;
        }
        waiting_for_write_after_done = false;
        sustained_backpressure_pending_recovery = false;
      } else {
        backpressure_run_cycles++;
        if (!sustained_backpressure_pending_recovery &&
            backpressure_run_cycles >= kMinBackpressureCycles) {
          sustained_backpressure_pending_recovery = true;
          saw_sustained_backpressure = true;
          backpressure_window_start = cycle - kMinBackpressureCycles + 1;
          backpressure_recovery_deadline = compute_done_cycle + kBackpressureRecoveryTimeout;
        }
      }
    }

    if (sustained_backpressure_pending_recovery && !backpressure_recovered &&
        cycle > backpressure_recovery_deadline) {
      std::fprintf(stderr,
                   "FAIL: backpressure did not recover within %d cycles (start=%d, deadline=%d)\n",
                   kBackpressureRecoveryTimeout,
                   backpressure_window_start,
                   backpressure_recovery_deadline);
      delete dut;
      return 1;
    }

    if (observed_start_count >= kRequiredTxnObservations &&
        observed_write_count >= kRequiredTxnObservations &&
        (backpressure_recovered || saw_immediate_write_after_done)) {
      break;
    }

    prev_rd_req_valid = dut->rd_req_valid;
    prev_rd_req_addr = dut->rd_req_addr;
    prev_compute_done = dut->compute_done;
    prev_wr_req_valid = dut->wr_req_valid;
  }

  if (!saw_meta_read) {
    std::fprintf(stderr, "FAIL: no META read request observed\n");
    delete dut;
    return 1;
  }
  if (!saw_state_read) {
    std::fprintf(stderr, "FAIL: no STATE read request observed\n");
    delete dut;
    return 1;
  }
  if (!saw_message_read) {
    std::fprintf(stderr, "FAIL: no MESSAGE read request observed\n");
    delete dut;
    return 1;
  }
  if (!saw_compute_start) {
    std::fprintf(stderr, "FAIL: no compute start pulse observed\n");
    delete dut;
    return 1;
  }
  if (!saw_compute_done) {
    std::fprintf(stderr, "FAIL: no compute done pulse observed\n");
    delete dut;
    return 1;
  }
  if (!saw_write_req) {
    std::fprintf(stderr, "FAIL: no write request observed\n");
    delete dut;
    return 1;
  }
  if (!txn_propagated) {
    std::fprintf(stderr, "FAIL: txn_id not propagated to write path (cmd=0x%02x, wr=0x%02x)\n", captured_cmd_txn, dut->wr_txn_id);
    delete dut;
    return 1;
  }
  if (observed_start_count < kRequiredTxnObservations) {
    std::fprintf(stderr,
                 "FAIL: observed only %d compute_start events within %d cycles (need %d)\n",
                 observed_start_count,
                 kMaxCycles,
                 kRequiredTxnObservations);
    delete dut;
    return 1;
  }
  if (observed_write_count < kRequiredTxnObservations) {
    std::fprintf(stderr,
                 "FAIL: observed only %d write requests within %d cycles (need %d)\n",
                 observed_write_count,
                 kMaxCycles,
                 kRequiredTxnObservations);
    delete dut;
    return 1;
  }
  if (observed_start_cycle[0] >= observed_start_cycle[1]) {
    std::fprintf(stderr,
                 "FAIL: compute_start order invalid (start0=%d start1=%d)\n",
                 observed_start_cycle[0],
                 observed_start_cycle[1]);
    delete dut;
    return 1;
  }
  if (observed_write_cycle[0] >= observed_write_cycle[1]) {
    std::fprintf(stderr,
                 "FAIL: write request order invalid (wr0=%d wr1=%d)\n",
                 observed_write_cycle[0],
                 observed_write_cycle[1]);
    delete dut;
    return 1;
  }
  if (observed_write_txn[0] != observed_start_txn[0] || observed_write_txn[1] != observed_start_txn[1]) {
    std::fprintf(stderr,
                 "FAIL: txn_id sequence mismatch start=[0x%02x,0x%02x] write=[0x%02x,0x%02x]\n",
                 observed_start_txn[0],
                 observed_start_txn[1],
                 observed_write_txn[0],
                 observed_write_txn[1]);
    delete dut;
    return 1;
  }
  if (post_done_write_window_required && !saw_sustained_backpressure && !saw_immediate_write_after_done) {
    std::fprintf(stderr,
                 "FAIL: no qualifying post-compute write behavior observed (need immediate write or >=%d cycles before recovery)\n",
                 kMinBackpressureCycles);
    delete dut;
    return 1;
  }
  if (saw_sustained_backpressure && !backpressure_recovered) {
    std::fprintf(stderr,
                 "FAIL: sustained backpressure observed but no recovery handshake within %d cycles\n",
                 kBackpressureRecoveryTimeout);
    delete dut;
    return 1;
  }
  if (!saw_zero_payload) {
    std::fprintf(stderr,
                 "FAIL: no partial zero payload observed (need at least one all-zero write payload)\n");
    delete dut;
    return 1;
  }
  if (saw_nonzero_payload && !saw_single_byte_sparse_payload) {
    std::fprintf(stderr,
                 "FAIL: no sparse single-byte payload observed (need at least one write with exactly one non-zero byte)\n");
    delete dut;
    return 1;
  }
  if (saw_single_byte_sparse_payload && !saw_sparse_after_zero_payload) {
    std::fprintf(stderr,
                 "FAIL: sparse payload did not occur after zero payload; stale-lane leakage check incomplete\n");
    delete dut;
    return 1;
  }

  WorkloadMetrics observed_line = observed_oracle.synthetic_line;
  WorkloadMetrics observed_lattice = observed_oracle.synthetic_lattice;
  if (std::getenv("PE_TOP_ORACLE_PERTURB") != nullptr) {
    observed_line.final_are += expected_oracle.final_are_threshold.abs_tol + 0.05;
  }

  if (!check_oracle_metric("synthetic_line",
                           "final_are",
                           observed_line.final_are,
                           expected_oracle.synthetic_line.final_are,
                           expected_oracle.final_are_threshold) ||
      !check_oracle_metric("synthetic_line",
                           "final_energy",
                           observed_line.final_energy,
                           expected_oracle.synthetic_line.final_energy,
                           expected_oracle.final_energy_threshold) ||
      !check_oracle_metric("synthetic_lattice",
                           "final_are",
                           observed_lattice.final_are,
                           expected_oracle.synthetic_lattice.final_are,
                           expected_oracle.final_are_threshold) ||
      !check_oracle_metric("synthetic_lattice",
                           "final_energy",
                           observed_lattice.final_energy,
                           expected_oracle.synthetic_lattice.final_energy,
                           expected_oracle.final_energy_threshold)) {
    delete dut;
    return 1;
  }

  std::printf("pe_top integration: PASS\n");
  delete dut;
  return 0;
}
