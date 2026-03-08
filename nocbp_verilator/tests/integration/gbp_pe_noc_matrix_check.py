#!/usr/bin/env python3

import argparse
import pathlib
import sys


REQUIRED_TOKENS = (
    "ROW:",
    "COMMAND:",
    "EXPECTED_EXIT:",
    "EXIT_CODE:",
    "REQUIRED_MARKER:",
    "MARKER_FOUND:",
    "ROW_STATUS:",
    "OUTPUT_BEGIN",
    "OUTPUT_END",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    if not input_path.is_file():
        print(f"GBP_PE_NOC_MATRIX_INTEGRITY_FAIL missing_file={input_path}", file=sys.stderr)
        return 1

    text = input_path.read_text(encoding="utf-8")
    rows = [block for block in text.split("\n\n") if block.strip()]
    if len(rows) < 6:
        print(f"GBP_PE_NOC_MATRIX_INTEGRITY_FAIL rows={len(rows)} expected_at_least=6", file=sys.stderr)
        return 1

    for index, row in enumerate(rows, start=1):
        for token in REQUIRED_TOKENS:
            if token not in row:
                print(
                    f"GBP_PE_NOC_MATRIX_INTEGRITY_FAIL row={index} missing_token={token}",
                    file=sys.stderr,
                )
                return 1

    print(f"GBP_PE_NOC_MATRIX_INTEGRITY_PASS rows={len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
