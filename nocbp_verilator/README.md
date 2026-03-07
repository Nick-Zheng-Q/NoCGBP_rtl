# Verilator Test Harness

Minimal C++-only test harness for unit/system tests.

## Quick start
- Unit example:
  - `make -C dv/verilator run LEVEL=unit TEST=example`

## How to add tests
- Create a DUT wrapper: `dv/verilator/tops/<level>/<test>_top.sv`
- Create a test: `dv/verilator/tests/<level>/<test>.cc`
- Run:
  - `make -C dv/verilator run LEVEL=<level> TEST=<test>`

## Notes
- Use `RTL_F` to point at a filelist (e.g. `rtl/compile.f`), or `RTL_SV` for extra SV sources.
- Use `DEFINES` and `INCDIRS` for compile-time switches.
