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


# $BB:BB07 loads the camera X from $17BA. The helpers take it from WRAM
# directly, which has no bus side effects.
SPAWN_CAMERA = "(uint16)(cpu->ram[0x17ba] | (cpu->ram[0x17bb] << 8))"
SPAWN_NATIVE_LEFT = "_dkc2_native_spawn_left"
SPAWN_LEFT = "0xbbb92f"
SPAWN_SPAN = "0xbbb931"


def adapt_spawn_radius(text: str) -> str:
    """Widen $BB:BB07's left allowance and span without wrapping.

    The left read becomes FitSpawnLeft(camera, left). The span read becomes
    FitSpawnSpan(camera, left, span), with the left row re-read just before
    the native span read so the bus is left as the cartridge leaves it.
    """
    fit_left = "Dkc2VideoFitSpawnLeft("
    fit_span = "Dkc2VideoFitSpawnSpan("
    if fit_left in text or fit_span in text:
        if (text.count(fit_left) != 1 or text.count(fit_span) != 1 or
                text.count(f"uint16 {SPAWN_NATIVE_LEFT} =") != 1):
            raise ValueError("ambiguous existing spawn radius adaptation")
        for address in (SPAWN_LEFT, SPAWN_SPAN):
            if re.search(
                    rf"Dkc2Video(?:Expand|Cull)\w*\(\s*cpu_read16\("
                    rf"[^;\n]*{address}", text):
                raise ValueError(
                    f"stale helper on {address} beside the spawn fit")
        return text

    # Peel a single-argument helper from an earlier adaptation.
    for address, helpers in (
            (SPAWN_LEFT, ("Dkc2VideoExpandSpawnLeft",
                          "Dkc2VideoExpandCullLeft")),
            (SPAWN_SPAN, ("Dkc2VideoExpandSpawnSpan",
                          "Dkc2VideoExpandCullSpan"))):
        for helper in helpers:
            text = re.sub(
                rf"{helper}\((\s*cpu_read16\([^;\n]*{address}[^;\n]*\))"
                rf"\s*\)",
                r"\1", text)

    left = re.compile(
        rf"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*{SPAWN_LEFT}[^;\n]*\));")
    span = re.compile(
        rf"^([ \t]*)(uint16\s+\w+\s*=\s*)"
        rf"(cpu_read16\([^;\n]*{SPAWN_SPAN}[^;\n]*\));",
        re.MULTILINE)
    if len(left.findall(text)) != 1:
        raise ValueError(f"expected one read from {SPAWN_LEFT}")
    if len(span.findall(text)) != 1:
        raise ValueError(f"expected one read from {SPAWN_SPAN}")

    text = left.sub(
        lambda match: f"{match.group(1)}Dkc2VideoFitSpawnLeft("
                      f"{SPAWN_CAMERA}, {match.group(2)});", text)

    def fit(match: re.Match[str]) -> str:
        indent, target, read = match.groups()
        native_left = read.replace(SPAWN_SPAN, SPAWN_LEFT)
        return (f"{indent}uint16 {SPAWN_NATIVE_LEFT} = {native_left};\n"
                f"{indent}{target}Dkc2VideoFitSpawnSpan({SPAWN_CAMERA}, "
                f"{SPAWN_NATIVE_LEFT}, {read});")

    return span.sub(fit, text)


def adapt_trace_block(
        text: str, start_label: str, end_label: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]

    # The renderer cull carries the sprite-cull lead so a sprite entering
    # the widened view has its graphics queued before it is presented. A
    # directory adapted with the plain cull helpers is upgraded in place.
    replacements = (
        (r"(uint16\s+\w+\s*=\s*)0x30;",
         "Dkc2VideoExpandSpriteCullLeft(0x30)",
         "Dkc2VideoExpandCullLeft(0x30)"),
        (r"(uint16\s+\w+\s*=\s*)0x160;",
         "Dkc2VideoExpandSpriteCullSpan(0x160)",
         "Dkc2VideoExpandCullSpan(0x160)"),
    )
    for pattern, expression, stale in replacements:
        if block.count(stale) > 1:
            raise ValueError(
                f"ambiguous existing adaptation in {start_label}")
        block = block.replace(stale, expression)
        if expression in block:
            if block.count(expression) != 1:
                raise ValueError(
                    f"ambiguous existing adaptation in {start_label}")
            if re.search(pattern, block):
                raise ValueError(
                    f"native cull constant left beside {expression} in "
                    f"{start_label}")
            continue
        block, count = re.subn(pattern, rf"\1{expression};", block)
        if count != 1:
            raise ValueError(
                f"expected one native cull constant in {start_label}; "
                f"found {count}")
    return text[:start] + block + text[end:]


def adapt_constant_block(
        text: str, start_label: str, end_label: str,
        literal: str, helper: str, replace_helper: str | None = None) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]
    expression = f"{helper}({literal})"
    if replace_helper:
        # A directory adapted by an earlier, incorrect choice of helper is
        # corrected in place rather than rejected or double-wrapped.
        stale = f"{replace_helper}({literal})"
        if block.count(stale) > 1:
            raise ValueError(
                f"ambiguous existing adaptation in {start_label}")
        block = block.replace(stale, expression)
        text = text[:start] + block + text[end:]
    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    if expression in block:
        if block.count(expression) != 1:
            raise ValueError(
                f"ambiguous existing adaptation in {start_label}")
        if re.search(pattern, block):
            raise ValueError(
                f"native {literal} constant left beside {expression} in "
                f"{start_label}")
        return text

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
    klank_kart_path = find_unit(
        generated_dir, "spawn_and_setup_klank_kart_M0X0")

    paths = {
        radius_path,
        renderer_path,
        banana_index_path,
        banana_renderer_path,
        banana_clip_path,
        despawn_path,
        klank_kart_path,
    }
    sources = {
        path: add_include(path.read_text(encoding="utf-8"))
        for path in paths
    }

    radius = sources[radius_path]
    # The placement radius carries the spawn lead (narrow rows only) so an
    # object spawns before it reaches the widened screen edge. It computes
    # camera - left unsigned, so the widened left allowance is fitted to
    # the camera X: a chest in a bonus room fixed at camera $100 used the
    # $F8 row, which widened past the camera, wrapped, and released the
    # chest the first frame the check ran.
    radius = adapt_spawn_radius(radius)
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
    # camera + $100 is the right edge of the group window and the $10F span
    # reaches back to camera - $0F, so the edge widens by extra + bias and
    # the span by both margins.
    banana_renderer = adapt_constant_block(
        banana_renderer, "L_F51C_M0X0:", "L_F534_M0X0:",
        "0x100", "Dkc2VideoExpandCullRight",
        replace_helper="Dkc2VideoExpandCullLeft")
    banana_renderer = adapt_constant_block(
        banana_renderer, "L_F545_M0X0:", "L_F54E_M0X0:",
        "0x10f", "Dkc2VideoExpandCullSpan")
    sources[banana_renderer_path] = banana_renderer

    banana_clip = sources[banana_clip_path]
    banana_clip = adapt_constant_block(
        banana_clip, "L_F5F4_M0X0:", "L_F5F9_M0X0:",
        "0xf", "Dkc2VideoExpandCullLeft")
    # $F61B is the right-hand path: ($107 - screen_x) / 8 is how many banana
    # columns fit before the right edge, so it widens by extra + bias. The
    # left helper here added nothing at a level's left wall (bias +extra),
    # and bananas popped in well inside the right margin.
    banana_clip = adapt_constant_block(
        banana_clip, "L_F61B_M0X0:", "L_F62B_M0X0:",
        "0x107", "Dkc2VideoExpandCullRight",
        replace_helper="Dkc2VideoExpandCullLeft")
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
    # cannon vanished from the right margin on a small step left). It also
    # carries the full spawn lead, or an object the placement radius spawns
    # inside the lead would be released on its first frame.
    despawn = sources[despawn_path]
    despawn = adapt_constant_block(
        despawn, "L_9C79_M0X0:", "L_9C8F_M0X0:",
        "0xb0", "Dkc2VideoExpandDespawnLeft",
        replace_helper="Dkc2VideoExpandCullLeft")
    despawn = adapt_constant_block(
        despawn, "L_9C79_M0X0:", "L_9C8F_M0X0:",
        "0x160", "Dkc2VideoExpandDespawnSpan",
        replace_helper="Dkc2VideoExpandCullSpan")
    sources[despawn_path] = despawn

    # $BE:E84A walks Rickety Race's sorted Klank kart table and spawns the
    # next kart once (kart X - camera) falls below $120 (and is at least
    # $100). That is the native right edge, so at 16:9 a kart appeared
    # about 24 pixels inside the widened right margin while the Kongs'
    # kart was running. Widen only the upper bound, by the right margin
    # and the spawn lead. The table's spawn counter still spawns each
    # kart once, and keeping $100 as the lower bound spawns no kart
    # that the native game would skip.
    klank_kart = sources[klank_kart_path]
    klank_kart = adapt_constant_block(
        klank_kart, "L_E87B_M0X0:", "L_E880_M0X0:",
        "0x120", "Dkc2VideoExpandSpawnRight")
    sources[klank_kart_path] = klank_kart

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
