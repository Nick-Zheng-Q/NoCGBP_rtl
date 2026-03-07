#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vdata_fifo.h"

static void tick(Vdata_fifo* dut) {
  dut->clk_i = 0;
  dut->eval();
  dut->clk_i = 1;
  dut->eval();
}

static void reset_dut(Vdata_fifo* dut) {
  dut->reset_i = 1;
  tick(dut);
  tick(dut);
  dut->reset_i = 0;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vdata_fifo;
  reset_dut(dut);

  printf("Test 1: Single write\n");
  dut->data_i = 0x11111111;
  dut->valid_i = 1;
  dut->unqueue_i = 0;
  tick(dut);
  if (dut->occ != 1) {
    std::fprintf(stderr, "test1: expected occ=1, got %u\n", dut->occ);
    delete dut;
    return 1;
  }
  printf("  PASS: occ=%u\n", dut->occ);

  printf("Test 2: Single read\n");
  dut->valid_i = 0;
  dut->unqueue_i = 1;
  tick(dut);
  if (dut->occ != 0) {
    std::fprintf(stderr, "test2: expected occ=0, got %u\n", dut->occ);
    delete dut;
    return 1;
  }
  printf("  PASS: occ=%u\n", dut->occ);

  printf("Test 3: Continuous writes (8 words)\n");
  reset_dut(dut);
  for (int i = 0; i < 8; i++) {
    dut->data_i = i;
    dut->valid_i = 1;
    dut->unqueue_i = 0;
    tick(dut);
  }
  if (dut->occ != 8) {
    std::fprintf(stderr, "test3: expected occ=8, got %u\n", dut->occ);
    delete dut;
    return 1;
  }
  printf("  PASS: occ=%u\n", dut->occ);

  printf("Test 4: Continuous reads (8 words)\n");
  for (int i = 0; i < 8; i++) {
    dut->valid_i = 0;
    dut->unqueue_i = 1;
    tick(dut);
  }
  if (dut->occ != 0) {
    std::fprintf(stderr, "test4: expected occ=0, got %u\n", dut->occ);
    delete dut;
    return 1;
  }
  printf("  PASS: occ=%u\n", dut->occ);

  printf("Test 5: Write until afull\n");
  reset_dut(dut);
  dut->valid_i = 1;
  dut->unqueue_i = 0;
  while (!dut->afull) {
    dut->data_i++;
    tick(dut);
  }
  printf("  PASS: afull=1, occ=%u\n", dut->occ, dut->occ);

  printf("Test 6: Simultaneous write and read\n");
  reset_dut(dut);
  dut->data_i = 0xAAAA;
  dut->valid_i = 1;
  dut->unqueue_i = 1;
  tick(dut);
  printf("  PASS: occ stays 1, occ=%u\n", dut->occ);

  printf("Test 7: Full then read one\n");
  reset_dut(dut);
  dut->valid_i = 1;
  dut->unqueue_i = 0;
  while (!dut->afull) {
    dut->data_i++;
    tick(dut);
  }
  printf("  Full: occ=%u afull=%u\n", dut->occ, dut->afull);
  dut->valid_i = 0;
  dut->unqueue_i = 1;
  tick(dut);
  if (dut->afull != 0) {
    std::fprintf(stderr, "test7: expected afull=0 after read, got %u\n", dut->afull);
    delete dut;
    return 1;
  }
  printf("  PASS: after read one: occ=%u afull=%u\n", dut->occ, dut->afull);

  printf("\ndata_fifo: all tests passed\n");
  delete dut;
  return 0;
}
