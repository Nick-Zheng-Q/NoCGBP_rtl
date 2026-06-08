// spm_arbiter.cc
// Unit test for spm_arbiter (7 clients, 64-bit beat)
// Test cases from docs/gbp_pe/verification/unit_tests/11_spm_arbiter.md

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vspm_arbiter_top.h"

static void tick(Vspm_arbiter_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vspm_arbiter_top* dut, int cycles = 3) {
  dut->rst_n = 0;
  dut->rd_valid = 0;
  dut->rd_addr_0 = 0;
  dut->rd_addr_1 = 0;
  dut->rd_addr_2 = 0;
  dut->rd_addr_3 = 0;
  dut->rd_addr_4 = 0;
  dut->rd_addr_5 = 0;
  dut->rd_addr_6 = 0;
  dut->wr_valid = 0;
  dut->wr_addr_0 = 0;
  dut->wr_addr_1 = 0;
  dut->wr_addr_2 = 0;
  dut->wr_addr_3 = 0;
  dut->wr_addr_4 = 0;
  dut->wr_addr_5 = 0;
  dut->wr_addr_6 = 0;
  dut->wr_data_0 = 0;
  dut->wr_data_1 = 0;
  dut->wr_data_2 = 0;
  dut->wr_data_3 = 0;
  dut->wr_data_4 = 0;
  dut->wr_data_5 = 0;
  dut->wr_data_6 = 0;
  dut->wr_wstrb_0 = 0;
  dut->wr_wstrb_1 = 0;
  dut->wr_wstrb_2 = 0;
  dut->wr_wstrb_3 = 0;
  dut->wr_wstrb_4 = 0;
  dut->wr_wstrb_5 = 0;
  dut->wr_wstrb_6 = 0;
  for (int i = 0; i < 16; ++i) dut->bank_rd_data[i] = 0;
  for (int i = 0; i < cycles; ++i) tick(dut);
  dut->rst_n = 1;
}

static inline uint64_t get_rd_data(Vspm_arbiter_top* dut, int c) {
  switch (c) {
    case 0: return dut->rd_data_0;
    case 1: return dut->rd_data_1;
    case 2: return dut->rd_data_2;
    case 3: return dut->rd_data_3;
    case 4: return dut->rd_data_4;
    case 5: return dut->rd_data_5;
    case 6: return dut->rd_data_6;
    default: return 0;
  }
}

static inline void set_wr_data(Vspm_arbiter_top* dut, int c, uint64_t v) {
  switch (c) {
    case 0: dut->wr_data_0 = v; break;
    case 1: dut->wr_data_1 = v; break;
    case 2: dut->wr_data_2 = v; break;
    case 3: dut->wr_data_3 = v; break;
    case 4: dut->wr_data_4 = v; break;
    case 5: dut->wr_data_5 = v; break;
    case 6: dut->wr_data_6 = v; break;
  }
}

// Verilator flattens [NUM_BANKS-1:0][BEAT_BITS-1:0] to WData[BEAT_BITS/32 * NUM_BANKS].
// For 8 banks x 64 bits = 512 bits = 16 x 32-bit words.
// Bank b occupies words [b*2] (lo32) and [b*2+1] (hi32).
static inline void set_bank_rd_data(Vspm_arbiter_top* dut, int b, uint64_t v) {
  dut->bank_rd_data[b * 2]     = static_cast<uint32_t>(v);
  dut->bank_rd_data[b * 2 + 1] = static_cast<uint32_t>(v >> 32);
}

static inline uint64_t get_bank_wr_data(Vspm_arbiter_top* dut, int b) {
  uint64_t lo = static_cast<uint64_t>(dut->bank_wr_data[b * 2]);
  uint64_t hi = static_cast<uint64_t>(dut->bank_wr_data[b * 2 + 1]);
  return (hi << 32) | lo;
}

static inline void set_rd_addr(Vspm_arbiter_top* dut, int c, uint32_t addr) {
  switch (c) {
    case 0: dut->rd_addr_0 = addr; break;
    case 1: dut->rd_addr_1 = addr; break;
    case 2: dut->rd_addr_2 = addr; break;
    case 3: dut->rd_addr_3 = addr; break;
    case 4: dut->rd_addr_4 = addr; break;
    case 5: dut->rd_addr_5 = addr; break;
    case 6: dut->rd_addr_6 = addr; break;
  }
}

static inline void set_wr_addr(Vspm_arbiter_top* dut, int c, uint32_t addr) {
  switch (c) {
    case 0: dut->wr_addr_0 = addr; break;
    case 1: dut->wr_addr_1 = addr; break;
    case 2: dut->wr_addr_2 = addr; break;
    case 3: dut->wr_addr_3 = addr; break;
    case 4: dut->wr_addr_4 = addr; break;
    case 5: dut->wr_addr_5 = addr; break;
    case 6: dut->wr_addr_6 = addr; break;
  }
}

static inline void set_wr_wstrb(Vspm_arbiter_top* dut, int c, uint32_t wstrb) {
  switch (c) {
    case 0: dut->wr_wstrb_0 = wstrb; break;
    case 1: dut->wr_wstrb_1 = wstrb; break;
    case 2: dut->wr_wstrb_2 = wstrb; break;
    case 3: dut->wr_wstrb_3 = wstrb; break;
    case 4: dut->wr_wstrb_4 = wstrb; break;
    case 5: dut->wr_wstrb_5 = wstrb; break;
    case 6: dut->wr_wstrb_6 = wstrb; break;
  }
}

// Wait up to max_wait cycles for the given rd_ready mask.
static int wait_rd_ready(Vspm_arbiter_top* dut, int mask, int max_wait = 10) {
  int waited = 0;
  while ((dut->rd_ready & mask) != mask && waited < max_wait) {
    tick(dut);
    waited++;
  }
  return (dut->rd_ready & mask) == mask;
}

// ── Test Case 1: Single Client Read ──
static int test_single_read(Vspm_arbiter_top* dut) {
  printf("  Test Case 1: Single Read...");
  reset_dut(dut);
  int pass = 1;

  dut->rd_valid = 0x1; // client 0
  set_rd_addr(dut, 0, 0x0100); // bank = 0x100[3:1] = 0
  if (!wait_rd_ready(dut, 0x1)) {
    fprintf(stderr, "\n    FAIL: rd_ready[0] never asserted");
    pass = 0;
  }

  // Provide read data next cycle
  dut->rd_valid = 0;
  set_bank_rd_data(dut, 0, 0xDEADBEEFCAFEBABEULL);
  tick(dut);

  if (get_rd_data(dut, 0) != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data[0]=0x%016lX, expected 0xDEADBEEFCAFEBABE", (unsigned long long)get_rd_data(dut, 0));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Multiple Clients Different Banks ──
static int test_multiple_banks(Vspm_arbiter_top* dut) {
  printf("  Test Case 2: Multiple Clients Different Banks...");
  reset_dut(dut);
  int pass = 1;

  dut->rd_valid = 0x3; // clients 0,1
  set_rd_addr(dut, 0, 0x0100); // bank 0 (0x100[3:1] = 0)
  set_rd_addr(dut, 1, 0x0102); // bank 1 (0x102[3:1] = 1)
  if (!wait_rd_ready(dut, 0x3)) {
    fprintf(stderr, "\n    FAIL: rd_ready mismatch (got 0x%X)", dut->rd_ready);
    pass = 0;
  }

  dut->rd_valid = 0;
  set_bank_rd_data(dut, 0, 0xAAAA000000000000ULL);
  set_bank_rd_data(dut, 1, 0xBBBB000000000000ULL);
  tick(dut);

  if (get_rd_data(dut, 0) != 0xAAAA000000000000ULL) {
    fprintf(stderr, "\n    FAIL: rd_data[0] mismatch");
    pass = 0;
  }
  if (get_rd_data(dut, 1) != 0xBBBB000000000000ULL) {
    fprintf(stderr, "\n    FAIL: rd_data[1] mismatch");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Bank Conflict ──
static int test_bank_conflict(Vspm_arbiter_top* dut) {
  printf("  Test Case 3: Bank Conflict...");
  reset_dut(dut);
  int pass = 1;

  dut->rd_valid = 0x3; // clients 0,1
  set_rd_addr(dut, 0, 0x0100); // bank 0 (0x100[3:1] = 0)
  set_rd_addr(dut, 1, 0x0140); // bank 0 (0x140[3:1] = 0)

  // Wait for client 0 to be granted
  if (!wait_rd_ready(dut, 0x1)) {
    fprintf(stderr, "\n    FAIL T+1: rd_ready[0] never asserted");
    pass = 0;
  }
  if (dut->rd_ready & 0x2) {
    fprintf(stderr, "\n    FAIL T+1: rd_ready[1]=1 (should be 0 during conflict)");
    pass = 0;
  }

  // Clear client 0, client 1 granted next cycle
  dut->rd_valid = 0x2;
  tick(dut);

  if (dut->rd_ready & 0x1) {
    fprintf(stderr, "\n    FAIL T+2: rd_ready[0]=1");
    pass = 0;
  }
  if (!(dut->rd_ready & 0x2)) {
    fprintf(stderr, "\n    FAIL T+2: rd_ready[1]=0");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Read and Write Same Bank ──
static int test_read_write_same_bank(Vspm_arbiter_top* dut) {
  printf("  Test Case 4: Read and Write Same Bank...");
  reset_dut(dut);
  int pass = 1;

  dut->rd_valid = 0x1; // client 0 read
  set_rd_addr(dut, 0, 0x0100); // bank 0 (0x100[3:1] = 0)
  dut->wr_valid = 0x4; // client 2 write
  set_wr_addr(dut, 2, 0x0140); // bank 0 (0x140[3:1] = 0)
  set_wr_data(dut, 2, 0x1234567890ABCDEFULL);
  set_wr_wstrb(dut, 2, 0xFFFFFFFFU);

  // Write ready is combinational; read ready is delayed 1 cycle.
  int waited = 0;
  while ((!(dut->rd_ready & 0x1) || !(dut->wr_ready & 0x4)) && waited < 10) {
    tick(dut);
    waited++;
  }
  if (!(dut->rd_ready & 0x1)) {
    fprintf(stderr, "\n    FAIL: rd_ready[0]=0");
    pass = 0;
  }
  if (!(dut->wr_ready & 0x4)) {
    fprintf(stderr, "\n    FAIL: wr_ready[2]=0");
    pass = 0;
  }
  if (get_bank_wr_data(dut, 0) != 0x1234567890ABCDEFULL) {
    fprintf(stderr, "\n    FAIL: bank_wr_data mismatch");
    pass = 0;
  }

  // Read data returned next cycle
  dut->rd_valid = 0;
  dut->wr_valid = 0;
  set_bank_rd_data(dut, 0, 0xDEADBEEFCAFEBABEULL);
  tick(dut);

  if (get_rd_data(dut, 0) != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data[0] mismatch");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Seven Clients All Different Banks ──
static int test_seven_clients(Vspm_arbiter_top* dut) {
  printf("  Test Case 5: Seven Clients All Different Banks...");
  reset_dut(dut);
  int pass = 1;

  // Clients 0-6 read from banks 0-6
  dut->rd_valid = 0x7F; // clients 0-6
  for (int c = 0; c < 7; ++c) {
    set_rd_addr(dut, c, 0x0100 + c * 2); // bank c (addr[3:1] = c)
  }
  if (!wait_rd_ready(dut, 0x7F)) {
    fprintf(stderr, "\n    FAIL: rd_ready mismatch (got 0x%X)", dut->rd_ready);
    pass = 0;
  }

  dut->rd_valid = 0;
  for (int c = 0; c < 7; ++c) {
    set_bank_rd_data(dut, c, 0x1000 + c);
  }
  tick(dut);

  for (int c = 0; c < 7; ++c) {
    if (get_rd_data(dut, c) != (uint64_t)(0x1000 + c)) {
      fprintf(stderr, "\n    FAIL: rd_data[%d] mismatch", c);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Bank Conflict with 7th Client ──
static int test_seventh_client_conflict(Vspm_arbiter_top* dut) {
  printf("  Test Case 6: 7th Client Bank Conflict...");
  reset_dut(dut);
  int pass = 1;

  // Clients 0 and 6 both target bank 0
  dut->rd_valid = (1 << 0) | (1 << 6);
  set_rd_addr(dut, 0, 0x0100); // bank 0
  set_rd_addr(dut, 6, 0x0140); // bank 0

  // Wait for client 0 to be granted
  if (!wait_rd_ready(dut, 0x1)) {
    fprintf(stderr, "\n    FAIL T+1: rd_ready[0] never asserted");
    pass = 0;
  }
  if (dut->rd_ready & (1 << 6)) {
    fprintf(stderr, "\n    FAIL T+1: rd_ready[6]=1");
    pass = 0;
  }

  // Clear client 0, client 6 granted next cycle
  dut->rd_valid = (1 << 6);
  tick(dut);

  if (dut->rd_ready & 0x1) {
    fprintf(stderr, "\n    FAIL T+2: rd_ready[0]=1");
    pass = 0;
  }
  if (!(dut->rd_ready & (1 << 6))) {
    fprintf(stderr, "\n    FAIL T+2: rd_ready[6]=0");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: All Clients Active ──
static int test_all_clients_active(Vspm_arbiter_top* dut) {
  printf("  Test Case 7: All Clients Active...");
  reset_dut(dut);
  int pass = 1;

  // Idle cycles to let the round-robin pointer move past the
  // post-reset Verilator evaluation transition.
  tick(dut);
  tick(dut);

  // All 7 clients issue reads and writes to distinct banks
  dut->rd_valid = 0x7F;
  dut->wr_valid = 0x7F;
  for (int c = 0; c < 7; ++c) {
    set_rd_addr(dut, c, 0x0100 + c * 2); // bank c
    set_wr_addr(dut, c, 0x0140 + c * 2); // bank c
    set_wr_data(dut, c, (uint64_t)(0x1000 + c));
    set_wr_wstrb(dut, c, 0xFFU);
    set_bank_rd_data(dut, c, (uint64_t)(0x2000 + c));
  }

  const int mask = 0x7F;
  int granted = 0;
  for (int waited = 0; waited < 20 && !granted; ++waited) {
    tick(dut);
    int wr_ok    = (dut->wr_ready & mask) == mask;
    int rd_en_ok = ((unsigned)dut->bank_rd_en & mask) == mask;
    int wr_en_ok = ((unsigned)dut->bank_wr_en & mask) == mask;
    if (wr_ok && rd_en_ok && wr_en_ok) {
      for (int c = 0; c < 7; ++c) {
        uint64_t expected = (uint64_t)(0x1000 + c);
        if (get_bank_wr_data(dut, c) != expected) {
          fprintf(stderr, "\n    FAIL: bank_wr_data[%d]=0x%016lX, expected 0x%016lX",
                  c, (unsigned long long)get_bank_wr_data(dut, c), expected);
          pass = 0;
        }
      }
      granted = 1;
    }
  }
  if (!granted) {
    fprintf(stderr, "\n    FAIL: not all clients granted within 20 cycles");
    pass = 0;
  }

  // Registered read grants appear one cycle after the combinational grant
  tick(dut);
  if ((dut->rd_ready & mask) != mask) {
    fprintf(stderr, "\n    FAIL: rd_ready=0x%X, expected 0x7F", dut->rd_ready);
    pass = 0;
  }
  for (int c = 0; c < 7; ++c) {
    uint64_t expected = (uint64_t)(0x2000 + c);
    if (get_rd_data(dut, c) != expected) {
      fprintf(stderr, "\n    FAIL: rd_data[%d]=0x%016lX, expected 0x%016lX",
              c, (unsigned long long)get_rd_data(dut, c), expected);
      pass = 0;
    }
  }

  dut->rd_valid = 0;
  dut->wr_valid = 0;
  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: Same Bank Same Address ──
static int test_same_bank_same_address(Vspm_arbiter_top* dut) {
  printf("  Test Case 8: Same Bank Same Address...");
  reset_dut(dut);
  int pass = 1;

  // Idle cycles to avoid post-reset round-robin transition
  tick(dut);
  tick(dut);

  dut->rd_valid = 1 << 0;
  set_rd_addr(dut, 0, 0x0100); // bank 0
  dut->wr_valid = 1 << 2;
  set_wr_addr(dut, 2, 0x0100); // same bank 0, same address
  set_wr_data(dut, 2, 0x1234567890ABCDEFULL);
  set_wr_wstrb(dut, 2, 0xFFU);
  set_bank_rd_data(dut, 0, 0xDEADBEEFCAFEBABEULL);

  int ok = 0;
  for (int waited = 0; waited < 10 && !ok; ++waited) {
    tick(dut);
    ok = (dut->rd_ready & 1) && ((dut->wr_ready >> 2) & 1) &&
         ((unsigned)dut->bank_rd_en & 1U) && ((unsigned)dut->bank_wr_en & 1U) &&
         (get_rd_data(dut, 0) == 0xDEADBEEFCAFEBABEULL) &&
         (get_bank_wr_data(dut, 0) == 0x1234567890ABCDEFULL);
  }
  if (!ok) {
    fprintf(stderr, "\n    FAIL: grants or data mismatch (rd_ready=0x%X wr_ready=0x%X bank_rd_en=0x%X bank_wr_en=0x%X)",
            dut->rd_ready, dut->wr_ready, (unsigned)dut->bank_rd_en, (unsigned)dut->bank_wr_en);
    pass = 0;
  }

  dut->rd_valid = 0;
  dut->wr_valid = 0;
  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 9: Reset During Arbitration ──
static int test_reset_during_arbitration(Vspm_arbiter_top* dut) {
  printf("  Test Case 9: Reset During Arbitration...");
  reset_dut(dut);
  int pass = 1;

  // Idle cycles to avoid post-reset round-robin transition
  tick(dut);
  tick(dut);

  // Drive requests and wait for read grants to register
  dut->rd_valid = (1 << 0) | (1 << 1);
  set_rd_addr(dut, 0, 0x0100); // bank 0
  set_rd_addr(dut, 1, 0x0102); // bank 1
  dut->wr_valid = 1 << 2;
  set_wr_addr(dut, 2, 0x0140); // bank 0
  set_wr_data(dut, 2, 0xBAD0BAD0BAD0BAD0ULL);
  set_wr_wstrb(dut, 2, 0xFFU);
  if (!wait_rd_ready(dut, (1 << 0) | (1 << 1))) {
    fprintf(stderr, "\n    FAIL: rd_ready never asserted before reset");
    pass = 0;
  }
  if (!(dut->wr_ready & (1 << 2))) {
    fprintf(stderr, "\n    FAIL: wr_ready[2]=0 before reset");
    pass = 0;
  }

  // Assert reset while requests are active
  dut->rst_n = 0;
  tick(dut);
  if (dut->rd_ready != 0) {
    fprintf(stderr, "\n    FAIL: rd_ready=0x%X during reset, expected 0", dut->rd_ready);
    pass = 0;
  }

  // Deassert reset and verify clean resume
  dut->rst_n = 1;
  set_bank_rd_data(dut, 0, 0x1111111122222222ULL);
  set_bank_rd_data(dut, 1, 0x3333333344444444ULL);
  if (!wait_rd_ready(dut, (1 << 0) | (1 << 1))) {
    fprintf(stderr, "\n    FAIL: rd_ready did not resume after reset");
    pass = 0;
  }
  if (!(dut->wr_ready & (1 << 2))) {
    fprintf(stderr, "\n    FAIL: wr_ready[2]=0 after reset");
    pass = 0;
  }
  if (get_rd_data(dut, 0) != 0x1111111122222222ULL) {
    fprintf(stderr, "\n    FAIL: rd_data[0] mismatch after reset");
    pass = 0;
  }
  if (get_rd_data(dut, 1) != 0x3333333344444444ULL) {
    fprintf(stderr, "\n    FAIL: rd_data[1] mismatch after reset");
    pass = 0;
  }

  dut->rd_valid = 0;
  dut->wr_valid = 0;
  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 10: Back-to-Back Requests ──
static int test_back_to_back_requests(Vspm_arbiter_top* dut) {
  printf("  Test Case 10: Back-to-Back Requests...");
  reset_dut(dut);
  int pass = 1;

  // Idle cycles to avoid post-reset round-robin transition
  tick(dut);
  tick(dut);

  dut->rd_valid = 1 << 0;
  set_rd_addr(dut, 0, 0x0100); // bank 0

  set_bank_rd_data(dut, 0, 0xAAA0000000000000ULL);
  tick(dut);
  if (!(dut->rd_ready & 1) || get_rd_data(dut, 0) != 0xAAA0000000000000ULL) {
    fprintf(stderr, "\n    FAIL: cycle 1 rd_data[0] mismatch");
    pass = 0;
  }

  set_bank_rd_data(dut, 0, 0xBBB0000000000000ULL);
  tick(dut);
  if (!(dut->rd_ready & 1) || get_rd_data(dut, 0) != 0xBBB0000000000000ULL) {
    fprintf(stderr, "\n    FAIL: cycle 2 rd_data[0] mismatch");
    pass = 0;
  }

  set_bank_rd_data(dut, 0, 0xCCC0000000000000ULL);
  tick(dut);
  if (!(dut->rd_ready & 1) || get_rd_data(dut, 0) != 0xCCC0000000000000ULL) {
    fprintf(stderr, "\n    FAIL: cycle 3 rd_data[0] mismatch");
    pass = 0;
  }

  dut->rd_valid = 0;
  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vspm_arbiter_top;

  int failures = 0;
  printf("spm_arbiter unit tests (7 clients, 64-bit beat):\n");
  failures += test_single_read(dut);
  failures += test_multiple_banks(dut);
  failures += test_bank_conflict(dut);
  failures += test_read_write_same_bank(dut);
  failures += test_seven_clients(dut);
  failures += test_seventh_client_conflict(dut);
  failures += test_all_clients_active(dut);
  failures += test_same_bank_same_address(dut);
  failures += test_reset_during_arbitration(dut);
  failures += test_back_to_back_requests(dut);

  if (failures == 0) {
    printf("\nAll 10 tests PASSED\n");
  } else {
    printf("\n%d of 10 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
