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


def adapt_constant_block(
        text: str, start_label: str, end_label: str,
        literal: str, helper: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]
    expression = f"{helper}({literal})"
    if expression in block:
        if block.count(expression) != 1:
            raise ValueError(
                f"ambiguous existing adaptation in {start_label}")
        return text

    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    block, count = re.subn(pattern, rf"\1{expression};", block)
    if count != 1:
        raise ValueError(
            f"expected one {literal} constant in {start_label}; "
            f"found {count}")
    return text[:start] + block + text[end:]


def adapt_nth_accumulator_write(
        text: str, start_label: str, end_label: str,
        write_index: int, helper: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]
    if helper in block:
        if block.count(helper) != 1:
            raise ValueError(
                f"ambiguous existing {helper} adaptation in {start_label}")
        return text

    pattern = re.compile(
        r"cpu_write_a_m\(cpu, \(uint16\)\((\w+)\)\);")
    matches = list(pattern.finditer(block))
    if write_index < 0 or write_index >= len(matches):
        raise ValueError(
            f"expected accumulator write {write_index} in {start_label}; "
            f"found {len(matches)} writes")
    match = matches[write_index]
    variable = match.group(1)
    replacement = (
        f"cpu_write_a_m(cpu, (uint16)({helper}({variable})));"
    )
    block = block[:match.start()] + replacement + block[match.end():]
    return text[:start] + block + text[end:]


def apply_overrides(generated_dir: Path) -> list[Path]:
    radius_path = find_unit(
        generated_dir, "check_placement_spawning_radius_M0X0")
    renderer_path = find_unit(
        generated_dir, "render_world_sprites_CODE_B59F40_M0X0")
    banana_index_path = find_unit(
        generated_dir, "update_banana_visibility_CODE_B5F3E9_M0X0")
    banana_renderer_path = find_unit(
        generated_dir, "prepare_banana_render_bounds_CODE_B5F540_M0X0")
    banana_clip_path = find_unit(
        generated_dir, "render_banana_tiles_CODE_B5F5E1_M0X0")
    despawn_path = find_unit(generated_dir, "CODE_B59C52_M0X0")

    paths = {
        radius_path,
        renderer_path,
        banana_index_path,
        banana_renderer_path,
        banana_clip_path,
        despawn_path,
    }
    sources = {
        path: add_include(path.read_text(encoding="utf-8"))
        for path in paths
    }

    radius = sources[radius_path]
    radius = wrap_single_read(
        radius, "0xbbb92f", "Dkc2VideoExpandCullLeft")
    radius = wrap_single_read(
        radius, "0xbbb931", "Dkc2VideoExpandCullSpan")
    sources[radius_path] = radius

    renderer = sources[renderer_path]
    renderer = adapt_trace_block(
        renderer, "L_9FC9_M0X0:", "L_9FDA_M0X0:")
    renderer = adapt_trace_block(
        renderer, "L_A00E_M0X0:", "L_A021_M0X0:")
    sources[renderer_path] = renderer

    banana_index = sources[banana_index_path]
    banana_index = adapt_constant_block(
        banana_index, "L_F3C5_M0X0:", "L_F3CE_M0X0:",
        "0x107", "Dkc2VideoExpandCullLeft")
    sources[banana_index_path] = banana_index

    banana_renderer = sources[banana_renderer_path]
    banana_renderer = adapt_constant_block(
        banana_renderer, "L_F51C_M0X0:", "L_F534_M0X0:",
        "0x100", "Dkc2VideoExpandCullLeft")
    banana_renderer = adapt_constant_block(
        banana_renderer, "L_F545_M0X0:", "L_F54E_M0X0:",
        "0x10f", "Dkc2VideoExpandCullSpan")
    sources[banana_renderer_path] = banana_renderer

    banana_clip = sources[banana_clip_path]
    banana_clip = adapt_constant_block(
        banana_clip, "L_F5F4_M0X0:", "L_F5F9_M0X0:",
        "0xf", "Dkc2VideoExpandCullLeft")
    banana_clip = adapt_constant_block(
        banana_clip, "L_F61B_M0X0:", "L_F62B_M0X0:",
        "0x107", "Dkc2VideoExpandCullLeft")
    banana_clip = adapt_nth_accumulator_write(
        banana_clip, "L_F672_M0X1:", "L_F6A4_M1X1:", 1,
        "Dkc2VideoPromoteOamXHigh")
    banana_clip = adapt_nth_accumulator_write(
        banana_clip, "L_F6D5_M0X1:", "L_F707_M1X1:", 1,
        "Dkc2VideoPromoteOamXHigh")
    sources[banana_clip_path] = banana_clip

    # $B5:9C52 walks the live sprite list every frame and releases any
    # sprite whose camera-relative X leaves [-$30, $130) (Y: [-$10, $120)).
    # Its X window is built as (x - camera - $80 + $B0) < $160, so the
    # left slack is the $B0 and the span the $160; widen both like the
    # renderer's cull, or an object standing in the widened margin is
    # released the moment it leaves the 4:3 span (a Bramble Blast barrel
    # cannon vanished from the right margin on a small step left).
    despawn = sources[despawn_path]
    despawn = adapt_constant_block(
        despawn, "L_9C79_M0X0:", "L_9C8F_M0X0:",
        "0xb0", "Dkc2VideoExpandCullLeft")
    despawn = adapt_constant_block(
        despawn, "L_9C79_M0X0:", "L_9C8F_M0X0:",
        "0x160", "Dkc2VideoExpandCullSpan")
    sources[despawn_path] = despawn

    for path in sorted(paths):
        path.write_text(sources[path], encoding="utf-8", newline="\n")
    return sorted(paths)


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
