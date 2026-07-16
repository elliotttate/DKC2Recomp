#!/usr/bin/env python3
"""Compare PNG or binary PPM frame captures without third-party packages."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys
import zlib


WHITESPACE = b" \t\r\n\v\f"


def read_token(data: bytes, position: int) -> tuple[bytes, int]:
    while position < len(data):
        if data[position] in WHITESPACE:
            position += 1
        elif data[position] == ord("#"):
            newline = data.find(b"\n", position)
            if newline < 0:
                raise ValueError("unterminated PPM header comment")
            position = newline + 1
        else:
            break
    start = position
    while position < len(data) and data[position] not in WHITESPACE:
        position += 1
    if start == position:
        raise ValueError("incomplete PPM header")
    return data[start:position], position


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    position = 0
    tokens: list[bytes] = []
    for _ in range(4):
        token, position = read_token(data, position)
        tokens.append(token)
    if tokens[0] != b"P6":
        raise ValueError(f"{path}: expected a binary P6 PPM")
    width = int(tokens[1])
    height = int(tokens[2])
    maximum = int(tokens[3])
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: unsupported PPM dimensions or maximum")
    if position >= len(data) or data[position] not in WHITESPACE:
        raise ValueError(f"{path}: missing separator after PPM header")
    position += 2 if data[position:position + 2] == b"\r\n" else 1
    pixels = data[position:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(
            f"{path}: expected {expected} RGB bytes, found {len(pixels)}"
        )
    return width, height, pixels


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    return above if above_distance <= upper_left_distance else upper_left


def read_png(path: Path, data: bytes) -> tuple[int, int, bytes]:
    position = 8
    width = height = channels = 0
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        chunk_type = data[position + 4:position + 8]
        start = position + 8
        end = start + length
        if end + 4 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        chunk = data[start:end]
        expected_crc = struct.unpack(">I", data[end:end + 4])[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(chunk, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValueError(f"{path}: invalid PNG chunk checksum")
        if chunk_type == b"IHDR":
            if length != 13:
                raise ValueError(f"{path}: invalid PNG header")
            (width, height, depth, color_type, compression,
             filter_method, interlace) = struct.unpack(">IIBBBBB", chunk)
            if (width <= 0 or height <= 0 or depth != 8 or
                    color_type not in (2, 6) or compression != 0 or
                    filter_method != 0 or interlace != 0):
                raise ValueError(
                    f"{path}: only non-interlaced 8-bit RGB/RGBA PNG is supported"
                )
            channels = 3 if color_type == 2 else 4
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            break
        position = end + 4
    if width == 0 or not compressed:
        raise ValueError(f"{path}: incomplete PNG")
    try:
        filtered = zlib.decompress(compressed)
    except zlib.error as error:
        raise ValueError(f"{path}: invalid compressed PNG data") from error
    stride = width * channels
    if len(filtered) != (stride + 1) * height:
        raise ValueError(f"{path}: unexpected PNG scanline size")
    reconstructed = bytearray(stride * height)
    source = 0
    for y in range(height):
        filter_type = filtered[source]
        source += 1
        if filter_type > 4:
            raise ValueError(f"{path}: unsupported PNG filter")
        row = y * stride
        for x in range(stride):
            raw = filtered[source]
            source += 1
            left = reconstructed[row + x - channels] if x >= channels else 0
            above = reconstructed[row - stride + x] if y > 0 else 0
            upper_left = (
                reconstructed[row - stride + x - channels]
                if y > 0 and x >= channels else 0
            )
            predictor = (0 if filter_type == 0 else
                         left if filter_type == 1 else
                         above if filter_type == 2 else
                         (left + above) // 2 if filter_type == 3 else
                         paeth(left, above, upper_left))
            reconstructed[row + x] = (raw + predictor) & 0xFF
    if channels == 3:
        pixels = bytes(reconstructed)
    else:
        pixels = bytes(
            reconstructed[offset + channel]
            for offset in range(0, len(reconstructed), 4)
            for channel in range(3)
        )
    return width, height, pixels


def read_frame(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return read_png(path, data)
    return read_ppm(path)


def collapse_doubled_width(pixels: bytes,
                           width: int,
                           height: int) -> tuple[bytes, int]:
    collapsed = bytearray((width // 2) * height * 3)
    pair_mismatches = 0
    target = 0
    for source in range(0, len(pixels), 6):
        first = pixels[source:source + 3]
        second = pixels[source + 3:source + 6]
        collapsed[target:target + 3] = first
        target += 3
        if first != second:
            pair_mismatches += 1
    return bytes(collapsed), pair_mismatches


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compare an exported native frame with a PNG or P6 PPM reference. "
            "A candidate exactly twice as wide is collapsed by taking one "
            "pixel from each horizontal pair."
        )
    )
    parser.add_argument("candidate", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument(
        "--diff",
        type=Path,
        help="optional P6 PPM containing absolute per-channel differences",
    )
    args = parser.parse_args()

    try:
        candidate_width, candidate_height, candidate = read_frame(args.candidate)
        reference_width, reference_height, reference = read_frame(args.reference)
        pair_mismatches = 0
        original_width = candidate_width
        if (candidate_width == reference_width * 2 and
                candidate_height == reference_height):
            candidate, pair_mismatches = collapse_doubled_width(
                candidate, candidate_width, candidate_height
            )
            candidate_width //= 2
        if (candidate_width != reference_width or
                candidate_height != reference_height):
            raise ValueError(
                "frame dimensions differ after optional doubled-width collapse: "
                f"{candidate_width}x{candidate_height} versus "
                f"{reference_width}x{reference_height}"
            )
    except (OSError, ValueError) as error:
        print(f"comparison failed: {error}", file=sys.stderr)
        return 2

    differing_pixels = 0
    differing_channels = 0
    total_error = 0
    maximum_error = 0
    difference = bytearray(len(candidate))
    for offset in range(0, len(candidate), 3):
        pixel_differs = False
        for channel in range(3):
            error = abs(candidate[offset + channel] - reference[offset + channel])
            difference[offset + channel] = error
            if error != 0:
                pixel_differs = True
                differing_channels += 1
                total_error += error
                maximum_error = max(maximum_error, error)
        if pixel_differs:
            differing_pixels += 1

    if original_width != candidate_width:
        print(
            f"Candidate: {original_width}x{candidate_height} -> "
            f"{candidate_width}x{candidate_height} "
            f"({pair_mismatches} unequal horizontal pixel pairs)"
        )
    else:
        print(f"Candidate: {candidate_width}x{candidate_height}")
    print(f"Reference: {reference_width}x{reference_height}")
    print(f"Candidate SHA-256: {hashlib.sha256(candidate).hexdigest()}")
    print(f"Reference SHA-256: {hashlib.sha256(reference).hexdigest()}")
    print(
        f"Differing pixels: {differing_pixels} / "
        f"{candidate_width * candidate_height}"
    )
    print(f"Differing channels: {differing_channels} / {len(candidate)}")
    print(f"Maximum channel error: {maximum_error}")
    print(f"Mean absolute channel error: {total_error / len(candidate):.9f}")
    if args.diff is not None:
        try:
            write_ppm(args.diff, candidate_width, candidate_height, difference)
        except OSError as error:
            print(f"cannot write diff image: {error}", file=sys.stderr)
            return 2
        print(f"Difference image: {args.diff}")

    exact = differing_channels == 0 and pair_mismatches == 0
    print("Result: exact RGB match" if exact else "Result: frames differ")
    return 0 if exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
