#!/usr/bin/env python3
"""Capture a stable frame from a trace-enabled snesrecomp TCP server."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import sys
import time

from dkc2_symbols_generated import (
    DKC2_BG_ACTION,
    DKC2_CAMERA_CONTROL,
    DKC2_CAMERA_MAX_X,
    DKC2_CAMERA_MAX_Y,
    DKC2_CAMERA_X,
    DKC2_CAMERA_Y,
    DKC2_GAME_SUB_MODE,
    DKC2_LAYOUT_NUMBER,
    DKC2_LEVEL_EFFECTS,
    DKC2_LEVEL_NUMBER,
    DKC2_LEVEL_TYPE,
    DKC2_METATILE_TABLE,
    DKC2_NMI_SUB_MODE,
    DKC2_PPU_CONFIG_NUMBER,
    DKC2_SPRITE_COUNT,
    DKC2_SPRITE_CURRENT_GRAPHIC_OFFSET,
    DKC2_SPRITE_DESPAWN_COUNTDOWN_OFFSET,
    DKC2_SPRITE_DESPAWN_TIME_OFFSET,
    DKC2_SPRITE_DISPLAY_MODE_OFFSET,
    DKC2_SPRITE_OAM_PROPERTY_OFFSET,
    DKC2_SPRITE_PLACEMENT_NUMBER_OFFSET,
    DKC2_SPRITE_PLACEMENT_PARAMETER_OFFSET,
    DKC2_SPRITE_RENDER_ORDER_OFFSET,
    DKC2_SPRITE_RENDER_TABLE,
    DKC2_SPRITE_SIZE,
    DKC2_SPRITE_STATE_OFFSET,
    DKC2_SPRITE_SUB_STATE_OFFSET,
    DKC2_SPRITE_TABLE,
    DKC2_SPRITE_TYPE_OFFSET,
    DKC2_SPRITE_WORLD_X_OFFSET,
    DKC2_SPRITE_WORLD_Y_OFFSET,
    DKC2_TERRAIN_VRAM,
    DKC2_TILESET_TYPE,
    DKC2_VRAM_PAYLOAD_NUMBER,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=4382)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--oam-margin-report", action="store_true",
        help="report sprites rendered in the 43-pixel widescreen margins")
    parser.add_argument(
        "--scan-oam-margins", action="store_true",
        help="scan render snapshots throughout the run, not only at the end")
    parser.add_argument(
        "--report-output", type=Path,
        help="write the JSON capture report to this diagnostic path")
    parser.add_argument(
        "--wram-output", type=Path,
        help="write the selected frame's 128 KiB WRAM snapshot")
    parser.add_argument(
        "--vram-output", type=Path,
        help="write the selected frame's 64 KiB VRAM snapshot")
    return parser.parse_args()


def receive_line(connection: socket.socket) -> str:
    chunks = bytearray()
    while not chunks.endswith(b"\n"):
        chunk = connection.recv(262144)
        if not chunk:
            raise ConnectionError("debug server closed the connection")
        chunks.extend(chunk)
    return chunks.decode("utf-8").strip()


def query_raw(connection: socket.socket, command: str) -> str:
    connection.sendall((command + "\n").encode("utf-8"))
    response = receive_line(connection)
    try:
        parsed = json.loads(response)
    except json.JSONDecodeError:
        parsed = {}
    if parsed.get("connected") is True:
        response = receive_line(connection)
    return response


def query(connection: socket.socket, command: str) -> dict:
    response = query_raw(connection, command)
    parsed = json.loads(response)
    return parsed


def write_hex_snapshot(response: dict, destination: Path,
                       expected_size: int) -> None:
    payload = bytes.fromhex(response.get("hex", ""))
    if len(payload) != expected_size:
        raise RuntimeError(
            f"snapshot is {len(payload)} bytes; expected {expected_size}")
    output = destination.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)


def connect(port: int, deadline: float) -> socket.socket:
    while True:
        try:
            connection = socket.create_connection(
                ("127.0.0.1", port), timeout=1.0)
            connection.settimeout(5.0)
            return connection
        except OSError:
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"TCP debug server did not open port {port}")
            time.sleep(0.05)


def collect_oam_margin_sprites(response: dict, destination: dict) -> None:
    for snapshot in response.get("snaps", []):
        for slot_index, slot in enumerate(snapshot.get("slot", [])):
            y, xlow, xhigh, tile, attributes = slot
            x = xlow + (256 if xhigh else 0)
            side = None
            presented_x = x
            if 256 <= x < 256 + 43:
                side = "right"
            elif 512 - 43 <= x < 512:
                side = "left"
                presented_x = x - 512
            if side and y < 224:
                item = {
                    "frame": snapshot.get("f"),
                    "slot": slot_index,
                    "side": side,
                    "x": presented_x,
                    "y": y,
                    "tile": tile,
                    "attributes": attributes,
                }
                key = (item["frame"], slot_index)
                destination[key] = item


def _le16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def _signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def classify_screen_x(x: int, margin_width: int = 43) -> str:
    if -margin_width <= x < 0:
        return "left_margin"
    if 0 <= x < 256:
        return "native_view"
    if 256 <= x < 256 + margin_width:
        return "right_margin"
    return "outside_view"


def collect_oam_sprites(response: dict, margin_width: int = 43) -> list[dict]:
    """Decode the newest render-consumed OAM snapshot.

    OAM X is a 9-bit coordinate. Values near 512 represent sprites immediately
    left of the authentic screen, so present them as negative coordinates.
    Parked slots at or below the SNES's hidden Y range remain excluded.
    """
    snapshots = response.get("snaps", [])
    if not snapshots:
        return []
    snapshot = snapshots[-1]
    decoded = []
    for slot_index, slot in enumerate(snapshot.get("slot", [])):
        y, xlow, xhigh, tile, attributes = slot
        raw_x = xlow + (256 if xhigh else 0)
        x = raw_x - 512 if raw_x >= 448 else raw_x
        if y >= 224:
            continue
        decoded.append({
            "frame": snapshot.get("f"),
            "slot": slot_index,
            "screen_x": x,
            "screen_y": y,
            "region": classify_screen_x(x, margin_width),
            "tile": tile,
            "attributes": attributes,
        })
    return decoded


def decode_dkc2_wram(wram: bytes, margin_width: int = 43) -> dict:
    """Decode proven DKC2 v1.0 screen, camera, and sprite fields."""
    if len(wram) != 0x20000:
        raise RuntimeError(
            f"WRAM snapshot is {len(wram)} bytes; expected 131072")
    camera_x = _le16(wram, DKC2_CAMERA_X)
    camera_y = _le16(wram, DKC2_CAMERA_Y)
    render_slots = [
        _le16(wram, DKC2_SPRITE_RENDER_TABLE + index * 2)
        for index in range(DKC2_SPRITE_COUNT)
    ]
    sprites = []
    for index in range(DKC2_SPRITE_COUNT):
        base = DKC2_SPRITE_TABLE + index * DKC2_SPRITE_SIZE
        sprite_type = _le16(wram, base + DKC2_SPRITE_TYPE_OFFSET)
        if sprite_type == 0:
            continue
        world_x = _le16(wram, base + DKC2_SPRITE_WORLD_X_OFFSET)
        world_y = _le16(wram, base + DKC2_SPRITE_WORLD_Y_OFFSET)
        screen_x = _signed16((world_x - camera_x) & 0xFFFF)
        screen_y = _signed16((world_y - camera_y) & 0xFFFF)
        sprites.append({
            "slot": index,
            "wram_address": f"0x{base:04x}",
            "type": f"0x{sprite_type:04x}",
            "render_order": _le16(wram, base + DKC2_SPRITE_RENDER_ORDER_OFFSET),
            "world_x": world_x,
            "world_y": world_y,
            "screen_x": screen_x,
            "screen_y": screen_y,
            "region": classify_screen_x(screen_x, margin_width),
            "oam_property": f"0x{_le16(wram, base + DKC2_SPRITE_OAM_PROPERTY_OFFSET):04x}",
            "current_graphic": f"0x{_le16(wram, base + DKC2_SPRITE_CURRENT_GRAPHIC_OFFSET):04x}",
            "display_mode": f"0x{wram[base + DKC2_SPRITE_DISPLAY_MODE_OFFSET]:02x}",
            "state": wram[base + DKC2_SPRITE_STATE_OFFSET],
            "sub_state": wram[base + DKC2_SPRITE_SUB_STATE_OFFSET],
            "placement_number": _le16(wram, base + DKC2_SPRITE_PLACEMENT_NUMBER_OFFSET),
            "placement_parameter": _le16(wram, base + DKC2_SPRITE_PLACEMENT_PARAMETER_OFFSET),
            "despawn_time": wram[base + DKC2_SPRITE_DESPAWN_TIME_OFFSET],
            "despawn_countdown": wram[base + DKC2_SPRITE_DESPAWN_COUNTDOWN_OFFSET],
        })
    return {
        "level_number": _le16(wram, DKC2_LEVEL_NUMBER),
        "camera": {"x": camera_x, "y": camera_y},
        "screen_configuration": {
            "level_type": _le16(wram, DKC2_LEVEL_TYPE),
            "tileset_type": _le16(wram, DKC2_TILESET_TYPE),
            "layout_number": _le16(wram, DKC2_LAYOUT_NUMBER),
            "nmi_sub_mode": _le16(wram, DKC2_NMI_SUB_MODE),
            "game_sub_mode": _le16(wram, DKC2_GAME_SUB_MODE),
            "level_effects": _le16(wram, DKC2_LEVEL_EFFECTS),
            "ppu_config_number": _le16(wram, DKC2_PPU_CONFIG_NUMBER),
            "vram_payload_number": _le16(
                wram, DKC2_VRAM_PAYLOAD_NUMBER),
            "camera_control": wram[DKC2_CAMERA_CONTROL],
            "camera_max_x": _le16(wram, DKC2_CAMERA_MAX_X),
            "camera_max_y": _le16(wram, DKC2_CAMERA_MAX_Y),
            "bg_action": f"0x{_le16(wram, DKC2_BG_ACTION):04x}",
            "metatile_table": f"0x{_le16(wram, DKC2_METATILE_TABLE):04x}",
            "terrain_vram_word_address": (
                f"0x{_le16(wram, DKC2_TERRAIN_VRAM):04x}"),
        },
        "sprite_render_table": render_slots,
        "active_sprites": sprites,
    }


def main() -> int:
    args = parse_args()
    output = args.output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.timeout
    connection = connect(args.port, deadline)
    try:
        oam_margin_sprites_by_key = {}
        next_oam_scan = 0
        while True:
            history = query(connection, "history").get("history", {})
            newest = int(history.get("newest", -1))
            if args.scan_oam_margins and newest >= next_oam_scan:
                collect_oam_margin_sprites(
                    query(connection, "oam_render_get 128 128"),
                    oam_margin_sprites_by_key)
                next_oam_scan = newest + 96
            if newest >= args.frame:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"frame {args.frame} was not reached; newest is {newest}")
            time.sleep(0.02)

        ppu = query(connection, "get_ppu_state")
        shadow = query(connection, "ws_shadow_stats")
        if args.wram_output:
            write_hex_snapshot(
                query(connection, f"dump_frame_wram {args.frame} 0 131072"),
                args.wram_output, 0x20000)
        if args.vram_output:
            write_hex_snapshot(
                query(connection, f"dump_frame_vram {args.frame} 0 65536"),
                args.vram_output, 0x10000)
        if args.oam_margin_report or args.scan_oam_margins:
            collect_oam_margin_sprites(
                query(connection, "oam_render_get 256 128"),
                oam_margin_sprites_by_key)
        oam_margin_sprites = [
            oam_margin_sprites_by_key[key]
            for key in sorted(oam_margin_sprites_by_key)
        ]
        # The upstream server currently emits Windows paths without JSON
        # backslash escaping. Treat this one response as protocol text while
        # retaining strict JSON parsing for all other commands.
        screenshot = query_raw(connection, f"screenshot {output}")
        if '"ok":true' not in screenshot:
            raise RuntimeError(f"screenshot failed: {screenshot}")
        report = {
            "screenshot_raw": screenshot,
            "ppu": ppu,
            "widescreen_shadow": shadow,
            "oam_margin_sprites": oam_margin_sprites,
        }
        report_text = json.dumps(report, indent=2)
        if args.report_output:
            report_output = args.report_output.expanduser().resolve()
            report_output.parent.mkdir(parents=True, exist_ok=True)
            report_output.write_text(
                report_text + "\n", encoding="utf-8", newline="\n")
            print(json.dumps({
                "report": str(report_output),
                "oam_margin_sprite_samples": len(oam_margin_sprites),
            }))
        else:
            print(report_text)
        query(connection, "continue")
    finally:
        connection.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ConnectionError, OSError, RuntimeError, TimeoutError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
