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


def wrap_single_read(
        text: str, address: str, helper: str,
        extra_argument: str | None = None) -> str:
    argument = rf",\s*{re.escape(extra_argument)}" if extra_argument else ""
    already = re.compile(
        rf"{helper}\(\s*cpu_read16\([^;\n]*{address}[^;\n]*\)"
        rf"{argument}\s*\)")
    if len(already.findall(text)) == 1:
        return text
    if already.search(text):
        raise ValueError(f"ambiguous existing {helper} adaptation")

    pattern = re.compile(
        rf"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*{address}[^;\n]*\))"
        rf"(;)")
    def replacement(match: re.Match[str]) -> str:
        suffix = f", {extra_argument}" if extra_argument else ""
        return f"{match.group(1)}{helper}({match.group(2)}{suffix}){match.group(3)}"

    text, count = pattern.subn(replacement, text)
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


def restore_banana_render_scratch(text: str) -> str:
    """Keep widened banana bounds from leaking into gameplay scratch RAM."""
    marker = "/* DKC2 widescreen: restore native shared scratch $44. */"
    if marker in text:
        if text.count(marker) != 1:
            raise ValueError("ambiguous banana scratch restoration")
        return text

    start_label = "L_F5BC_M0X0:"
    start = text.find(start_label)
    if start < 0:
        raise ValueError("could not locate banana renderer return block")
    anchor = "    { uint16 _ret_s = cpu->S;  /* RTS pop hardware return frame */"
    insertion = text.find(anchor, start)
    if insertion < 0:
        raise ValueError("could not locate banana renderer RTS anchor")
    restoration = (
        "    /* DKC2 widescreen: restore native shared scratch $44. */\n"
        "    if (Dkc2VideoTerrainReady()) {\n"
        "      uint16 _dkc2_native_right = (uint16)(\n"
        "          cpu_read16(cpu, cpu->DB, (uint16)(0x17ba)) + 0x100u);\n"
        "      cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x0044),\n"
        "                  _dkc2_native_right);\n"
        "    }\n"
    )
    return text[:insertion] + restoration + text[insertion:]


def function_span(text: str, symbol: str) -> tuple[int, int]:
    marker = f"RecompReturn {symbol}(CpuState *cpu) {{"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"could not locate generated function {symbol}")
    brace = text.find("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise ValueError(f"unterminated generated function {symbol}")


def set_placement_context_at_entry(
        text: str, symbol: str, activation: bool) -> str:
    start, end = function_span(text, symbol)
    block = text[start:end]
    value = "true" if activation else "false"
    statement = f"Dkc2VideoSetPlacementRadiusActivation({value});"
    if statement in block:
        if block.count(statement) != 1:
            raise ValueError(f"ambiguous placement context in {symbol}")
        return text
    brace = block.find("{") + 1
    block = block[:brace] + f"\n  {statement}" + block[brace:]
    return text[:start] + block + text[end:]


def set_mixed_placement_activation_context(text: str) -> str:
    """CODE_BBBAB8 checks live despawn, then the original spawn point."""
    symbol = "CODE_BBBAB8_M0X0"
    text = set_placement_context_at_entry(text, symbol, False)
    start, end = function_span(text, symbol)
    block = text[start:end]
    statement = "Dkc2VideoSetPlacementRadiusActivation(true);"
    if statement in block:
        if block.count(statement) != 1:
            raise ValueError(f"ambiguous placement context in {symbol}")
        return text
    anchor = (
        "    { extern RecompReturn "
        "check_placement_spawning_radius_M0X0(CpuState *cpu);")
    insertion = block.find(anchor)
    if insertion < 0:
        raise ValueError("could not locate mixed placement activation tailcall")
    block = block[:insertion] + f"    {statement}\n" + block[insertion:]
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
    placement_contexts = {
        "default_activation_radius_check_M0X0": True,
        "CODE_BBBA92_M0X0": False,
        "default_deactivation_radius_check_M0X0": False,
        "CODE_BBBAF3_M0X0": True,
    }
    placement_context_paths = {
        symbol: find_unit(generated_dir, symbol)
        for symbol in placement_contexts
    }
    mixed_placement_path = find_unit(generated_dir, "CODE_BBBAB8_M0X0")

    paths = {
        radius_path,
        renderer_path,
        banana_index_path,
        banana_renderer_path,
        banana_clip_path,
        mixed_placement_path,
        *placement_context_paths.values(),
    }
    sources = {
        path: add_include(path.read_text(encoding="utf-8"))
        for path in paths
    }

    radius = sources[radius_path]
    radius = wrap_single_read(
        radius, "0xbbb92f", "Dkc2VideoExpandPlacementLeft", "cpu->X")
    radius = wrap_single_read(
        radius, "0xbbb931", "Dkc2VideoExpandPlacementSpan", "cpu->X")
    sources[radius_path] = radius

    for symbol, activation in placement_contexts.items():
        path = placement_context_paths[symbol]
        sources[path] = set_placement_context_at_entry(
            sources[path], symbol, activation)
    sources[mixed_placement_path] = set_mixed_placement_activation_context(
        sources[mixed_placement_path])

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
    banana_renderer = restore_banana_render_scratch(banana_renderer)
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
