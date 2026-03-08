#!/usr/bin/env python3

import pathlib
import sys


FORBIDDEN_SNIPPETS = (
    "assign out_v_li = 1'b0;",
    "assign out_packet_li = '0;",
    "assign core_rsp_data_li = '0;",
    "assign core_rsp_v_li = 1'b0;",
    "assign core_req_yumi_li = core_req_v_lo;",
)

REQUIRED_SNIPPETS = (
    "gbp_pe_noc_bridge",
    "pe_top pe",
    ".core_rsp_data_o(core_rsp_data_li)",
    ".cmd_valid_i(sideband_cmd_valid_lo)",
    "assign out_v_li = egress_pending_r;",
    "assign out_packet_li = egress_packet_r;",
)


def parse_file_arg(argv: list[str]) -> str:
    if len(argv) == 1:
        return "v/gbp_pe/gbp_pe.sv"
    if len(argv) == 3 and argv[1] == "--file":
        return argv[2]

    print("usage: gbp_pe_tieoff_guard.py [--file <path>]", file=sys.stderr)
    raise SystemExit(2)


def main(argv: list[str]) -> int:
    rtl_path = pathlib.Path(parse_file_arg(argv))
    if not rtl_path.is_file():
        print(f"gbp_pe tie-off guard: FAIL missing file: {rtl_path}", file=sys.stderr)
        return 1

    rtl = rtl_path.read_text(encoding="utf-8")

    for snippet in REQUIRED_SNIPPETS:
        if snippet not in rtl:
            print(f"gbp_pe tie-off guard: FAIL missing required snippet: {snippet}", file=sys.stderr)
            return 1

    for snippet in FORBIDDEN_SNIPPETS:
        if snippet in rtl:
            print(f"gbp_pe tie-off guard: FAIL forbidden tie-off present: {snippet}", file=sys.stderr)
            return 1

    print("gbp_pe tie-off guard: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
