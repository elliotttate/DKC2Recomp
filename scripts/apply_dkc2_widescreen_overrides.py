#!/usr/bin/env python3
"""Apply source-owned DKC2 widescreen adaptations to private generated C."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


INCLUDE = '#include "dkc2_video.h"'


def find_unit(generated_dir: Path, symbol: str) -> Path:
    matches = [
        path for path in generated_dir.glob("*.c")
        if f"RecompReturn {symbol}(CpuState *cpu) {{" in
        path.read_text(encoding="utf-8")
    ]
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one generated unit defining {symbol}; "
            f"found {len(matches)}")
    return matches[0]


def add_include(text: str) -> str:
    if INCLUDE in text:
        return text
    marker = '#include "funcs.h"'
    if text.count(marker) != 1:
        raise ValueError("generated unit has an unexpected funcs.h include")
    return text.replace(marker, marker + "\n" + INCLUDE, 1)


def wrap_single_read(text: str, address: str, helper: str) -> str:
    already = re.compile(
        rf"{helper}\(\s*cpu_read16\([^;\n]*{address}[^;\n]*\)\s*\)")
    if len(already.findall(text)) == 1:
        return text
    if already.search(text):
        raise ValueError(f"ambiguous existing {helper} adaptation")

    pattern = re.compile(
        rf"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*{address}[^;\n]*\))"
        rf"(;)")
    text, count = pattern.subn(rf"\1{helper}(\2)\3", text)
    if count != 1:
        raise ValueError(
            f"expected one read from {address} for {helper}; found {count}")
    return text


def adapt_trace_block(
        text: str, start_label: str, end_label: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]

    replacements = (
        (r"(uint16\s+\w+\s*=\s*)0x30;", "Dkc2VideoExpandCullLeft(0x30)"),
        (r"(uint16\s+\w+\s*=\s*)0x160;", "Dkc2VideoExpandCullSpan(0x160)"),
    )
    for pattern, expression in replacements:
        if expression in block:
            if block.count(expression) != 1:
                raise ValueError(
                    f"ambiguous existing adaptation in {start_label}")
            continue
        block, count = re.subn(pattern, rf"\1{expression};", block)
        if count != 1:
            raise ValueError(
                f"expected one native cull constant in {start_label}; "
                f"found {count}")
    return text[:start] + block + text[end:]


def apply_overrides(generated_dir: Path) -> list[Path]:
    radius_path = find_unit(
        generated_dir, "check_placement_spawning_radius_M0X0")
    renderer_path = find_unit(generated_dir, "CODE_B59F40_M0X0")

    radius = add_include(radius_path.read_text(encoding="utf-8"))
    radius = wrap_single_read(
        radius, "0xbbb92f", "Dkc2VideoExpandCullLeft")
    radius = wrap_single_read(
        radius, "0xbbb931", "Dkc2VideoExpandCullSpan")

    renderer = add_include(renderer_path.read_text(encoding="utf-8"))
    renderer = adapt_trace_block(
        renderer, "L_9FC9_M0X0:", "L_9FDA_M0X0:")
    renderer = adapt_trace_block(
        renderer, "L_A00E_M0X0:", "L_A021_M0X0:")

    radius_path.write_text(radius, encoding="utf-8", newline="\n")
    renderer_path.write_text(renderer, encoding="utf-8", newline="\n")
    return [radius_path, renderer_path]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", required=True, type=Path)
    args = parser.parse_args()
    generated_dir = args.generated_dir.expanduser().resolve(strict=True)
    changed = apply_overrides(generated_dir)
    for path in changed:
        print(f"Applied DKC2 widescreen overrides: {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
