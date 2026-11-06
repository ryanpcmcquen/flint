#!/usr/bin/env python3
"""Repeatable wall-clock benchmarks for Flint.

This is a development helper, not part of the Flint runtime.
"""

import argparse
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

CASES = [
    ("summary", ["--summary", "--no-mcp"]),
    ("summary_7d", ["--summary", "--days", "7", "--no-mcp"]),
    ("resume_dry_run", ["-r", "-n", "--days", "7", "--no-mcp"]),
    ("summary_mcp", ["--summary"]),
]


def default_binary(root: Path) -> Path:
    if platform.system() == "Windows":
        return root / "flint.exe"
    return root / "flint"


def run_case(binary, args, runs, warmup):
    for _ in range(warmup):
        subprocess.run(
            [str(binary), *args],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )

    times = []
    for _ in range(runs):
        start = time.perf_counter()
        subprocess.run(
            [str(binary), *args],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        times.append(time.perf_counter() - start)

    return {
        "runs": [round(value, 4) for value in times],
        "min": round(min(times), 4),
        "median": round(statistics.median(times), 4),
        "mean": round(statistics.mean(times), 4),
        "max": round(max(times), 4),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark Flint commands")
    parser.add_argument("--binary", default="", help="Path to the flint binary")
    parser.add_argument("--runs", type=int, default=5, help="Measured runs per case")
    parser.add_argument("--warmup", type=int, default=1, help="Warmup runs per case")
    parser.add_argument("--json", action="store_true", help="Print JSON instead of a table")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    binary = Path(args.binary) if args.binary else default_binary(root)
    if not binary.exists():
        print(f"error: binary not found: {binary}", file=sys.stderr)
        print("run `make` first, or pass --binary <path>", file=sys.stderr)
        return 1

    results = {}
    for name, case_args in CASES:
        results[name] = run_case(binary, case_args, args.runs, args.warmup)

    if args.json:
        print(json.dumps(results, indent=2))
        return 0

    print(f"Flint benchmark: {binary}")
    print(f"{'case':<16} {'min':>8} {'median':>8} {'mean':>8} {'max':>8}")
    print("-" * 54)
    for name, result in results.items():
        print(
            f"{name:<16} {result['min']:>8.3f} {result['median']:>8.3f} "
            f"{result['mean']:>8.3f} {result['max']:>8.3f}"
        )
    print("\nAll times in seconds.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
