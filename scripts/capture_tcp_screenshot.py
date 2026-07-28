#!/usr/bin/env python3
"""Capture a stable frame from a trace-enabled snesrecomp TCP server."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import sys
import time


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
