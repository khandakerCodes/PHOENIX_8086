#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Line coverage of the host-tested kernel files

    coverage_report.py DIR SOURCE... [--markdown FILE]

Reads every .gcda file in DIR (written by the host tests built with
--coverage, see `make coverage`), merges what each test binary
executed, and prints the share of lines run for each kernel SOURCE.
A line counts as covered if any test ran it.

This covers only what the host tests exercise; the in-kernel
self-test and the QEMU integration test run far more of the kernel,
but on the target, where gcov cannot reach.
"""

import argparse
import glob
import json
import os
import subprocess
import sys


def gcov_records(directory):
    """Yield gcov's JSON record for every .gcda file in the directory."""
    for gcda in sorted(glob.glob(os.path.join(os.path.abspath(directory), "*.gcda"))):
        result = subprocess.run(["gcov", "--json-format", "--stdout", gcda],
                                cwd=directory, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"gcov failed on {gcda}: {result.stderr.strip()}")
        for line in result.stdout.splitlines():
            if line.strip():
                yield json.loads(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("directory")
    parser.add_argument("sources", nargs="+")
    parser.add_argument("--markdown")
    args = parser.parse_args()

    wanted = {os.path.normpath(os.path.abspath(s)): s for s in args.sources}
    lines = {s: {} for s in args.sources}       # source → {line number: covered?}

    for record in gcov_records(args.directory):
        cwd = record.get("current_working_directory", args.directory)
        for entry in record.get("files", []):
            path = os.path.normpath(os.path.join(cwd, entry["file"]))
            source = wanted.get(path)
            if source is None:
                continue
            for line in entry.get("lines", []):
                number = line["line_number"]
                lines[source][number] = lines[source].get(number, False) or line["count"] > 0

    rows = []
    for source in args.sources:
        total = len(lines[source])
        covered = sum(1 for hit in lines[source].values() if hit)
        rows.append((source, covered, total, 100.0 * covered / total if total else 0.0))
    all_covered = sum(r[1] for r in rows)
    all_total = sum(r[2] for r in rows)

    print("Host test line coverage")
    for source, covered, total, share in rows:
        print(f"  {source:<20} {covered:>4} of {total:>4} lines  {share:5.1f}%")
    print(f"  {'total':<20} {all_covered:>4} of {all_total:>4} lines  "
          f"{100.0 * all_covered / max(all_total, 1):5.1f}%")

    if args.markdown:
        with open(args.markdown, "a", encoding="utf-8") as f:
            f.write("### Host test line coverage\n\n| File | Lines run | Coverage |\n| --- | ---: | ---: |\n")
            for source, covered, total, share in rows:
                f.write(f"| `{source}` | {covered} of {total} | {share:.1f}% |\n")
            f.write(f"| **total** | {all_covered} of {all_total} | "
                    f"{100.0 * all_covered / max(all_total, 1):.1f}% |\n")

    if all_total == 0:
        sys.exit("no coverage data found; run the tests built with --coverage first")


if __name__ == "__main__":
    main()
