#!/usr/bin/env python3
"""Generate private DKC2 snesrecomp units on Windows, Linux, or macOS."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


EXPECTED_SHA256 = "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"
EXPECTED_SIZE = 0x400000


def integer(value: str) -> int:
    return int(value, 0)


def validate_rom(path: Path) -> None:
    size = path.stat().st_size
    if size != EXPECTED_SIZE:
        raise ValueError(f"Unsupported ROM size {size}; expected {EXPECTED_SIZE} bytes.")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported ROM SHA-256 {digest}.")


def run(command: list[str], description: str) -> None:
    result = subprocess.run(command, check=False)
    if result.returncode:
        raise RuntimeError(
            f"{description} failed with exit code {result.returncode}.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--snesrecomp-root", type=Path)
    parser.add_argument(
        "--analysis-backend", choices=("native", "python", "auto"),
        default="native")
    parser.add_argument("--max-instructions", type=integer, default=4096)
    parser.add_argument("--max-nodes", type=integer, default=100000)
    parser.add_argument("--bank-shard-threshold-kib", type=integer,
                        default=1024)
    parser.add_argument("--bank-shard-pc-span", type=integer, default=0x10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository = Path(__file__).resolve().parent.parent
    snesrecomp_root = (args.snesrecomp_root or
                       repository / "snesrecomp").resolve()
    emitter = snesrecomp_root / "tools" / "v2_emit.py"
    native_builder = snesrecomp_root / "tools" / "build_native_analyzer.py"
    native_name = "snesrecomp-analyze.exe" if os.name == "nt" else "snesrecomp-analyze"
    native_analyzer = (snesrecomp_root / "recompiler-rs" / "target" /
                       "release" / native_name)
    header_sync = snesrecomp_root / "tools" / "v2_sync_funcs_h.py"
    config_directory = repository / "recomp"
    output_directory = repository / "generated" / "snesrecomp"

    if not emitter.is_file():
        raise FileNotFoundError(
            "snesrecomp is not initialized; run git submodule update --init --recursive")
    if not header_sync.is_file():
        raise FileNotFoundError(
            f"snesrecomp header synchronizer is missing: {header_sync}")
    if args.analysis_backend == "native":
        if not native_builder.is_file():
            raise FileNotFoundError(
                f"snesrecomp native analyzer builder is missing: {native_builder}")
        if shutil.which("cargo"):
            run([sys.executable, str(native_builder)],
                "snesrecomp native analyzer build")
        elif not native_analyzer.is_file():
            raise FileNotFoundError(
                f"Cargo is unavailable and no native analyzer exists at {native_analyzer}")
        else:
            print(f"Cargo is unavailable; using native analyzer: {native_analyzer}")

    rom = args.rom.expanduser().resolve(strict=True)
    validate_rom(rom)
    output_directory.mkdir(parents=True, exist_ok=True)
    run([
        sys.executable, str(header_sync), "--cfg-dir", str(config_directory),
        "--out", str(config_directory / "funcs.h")],
        "snesrecomp funcs.h synchronization")
    run([
        sys.executable, str(emitter), "--rom", str(rom),
        "--cfg-dir", str(config_directory), "--out-dir", str(output_directory),
        "--no-host-root-scan", "--no-hle", "--cfg-roots",
        "--analysis-backend", args.analysis_backend,
        "--max-insns", str(args.max_instructions),
        "--max-nodes", str(args.max_nodes),
        "--bank-shard-threshold-kib", str(args.bank_shard_threshold_kib),
        "--bank-shard-pc-span", str(args.bank_shard_pc_span)],
        "snesrecomp generation")
    print(f"Generated private sources in {output_directory}")
    print("The ROM and generated game code remain ignored by Git.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
