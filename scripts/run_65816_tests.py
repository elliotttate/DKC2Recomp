#!/usr/bin/env python3
"""Run external SingleStepTests/65816 state vectors against dkc2_cpu_step.

The multi-gigabyte JSON corpus and compiled shared library remain external to
this repository. This script uses only Python's standard library.
"""

import argparse
import ctypes
import json
import pathlib
import subprocess
import sys


READ8 = ctypes.CFUNCTYPE(ctypes.c_uint8, ctypes.c_void_p, ctypes.c_uint32)
WRITE8 = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint8
)


class Cpu(ctypes.Structure):
    _fields_ = [
        ("a", ctypes.c_uint16),
        ("x", ctypes.c_uint16),
        ("y", ctypes.c_uint16),
        ("s", ctypes.c_uint16),
        ("d", ctypes.c_uint16),
        ("pc", ctypes.c_uint16),
        ("dbr", ctypes.c_uint8),
        ("pbr", ctypes.c_uint8),
        ("p", ctypes.c_uint8),
        ("e", ctypes.c_bool),
        ("waiting", ctypes.c_bool),
        ("stopped", ctypes.c_bool),
        ("instructions", ctypes.c_uint64),
    ]


class MemoryInterface(ctypes.Structure):
    _fields_ = [
        ("context", ctypes.c_void_p),
        ("read8", READ8),
        ("write8", WRITE8),
    ]


CURRENT_MEMORY = {}
BLOCK_MOVE_OPCODES = {0x44, 0x54}


@READ8
def read8(_context, address):
    return CURRENT_MEMORY.get(address & 0xFFFFFF, 0)


@WRITE8
def write8(_context, address, value):
    CURRENT_MEMORY[address & 0xFFFFFF] = value


def parse_opcode_spec(text):
    values = []
    for item in text.split(","):
        if "-" in item:
            start, end = item.split("-", 1)
            values.extend(range(int(start, 16), int(end, 16) + 1))
        else:
            values.append(int(item, 16))
    if any(value < 0 or value > 0xFF for value in values):
        raise ValueError("opcode must be between 00 and ff")
    return sorted(set(values))


def load_vectors(repository, relative_path):
    path = repository / relative_path
    if path.is_file() and path.stat().st_size:
        with path.open("r", encoding="utf-8") as source:
            return json.load(source)
    completed = subprocess.run(
        ["git", "-C", str(repository), "show", f"HEAD:{relative_path}"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def load_cpu_library(path):
    library = ctypes.CDLL(str(path))
    library.dkc2_cpu_step.argtypes = [
        ctypes.POINTER(Cpu),
        ctypes.POINTER(MemoryInterface),
    ]
    library.dkc2_cpu_step.restype = ctypes.c_int
    return library


def cpu_from_state(state):
    cpu = Cpu()
    for field in ("a", "x", "y", "s", "d", "pc", "dbr", "pbr", "p"):
        setattr(cpu, field, state[field])
    cpu.e = bool(state["e"])
    return cpu


def state_differences(cpu, expected):
    differences = []
    for field in ("a", "x", "y", "s", "d", "pc", "dbr", "pbr", "p"):
        actual = getattr(cpu, field)
        if actual != expected[field]:
            differences.append(
                f"{field}: actual={actual:#x} expected={expected[field]:#x}"
            )
    if int(cpu.e) != expected["e"]:
        differences.append(
            f"e: actual={int(cpu.e)} expected={expected['e']}"
        )
    for address, expected_value in expected["ram"]:
        actual = CURRENT_MEMORY.get(address, 0)
        if actual != expected_value:
            differences.append(
                f"ram[{address:#08x}]: actual={actual:#04x} "
                f"expected={expected_value:#04x}"
            )
    return differences


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--opcodes", default="00-ff")
    parser.add_argument(
        "--limit", type=int, default=10000, help="vectors per opcode/mode"
    )
    parser.add_argument("--max-failures", type=int, default=20)
    parser.add_argument(
        "--include-block-move",
        action="store_true",
        help=(
            "include MVP/MVN even though this cycle-capped corpus stops "
            "them in the middle of a logical instruction"
        ),
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="print only failures, exclusions, and the final total",
    )
    args = parser.parse_args()

    try:
        opcodes = parse_opcode_spec(args.opcodes)
    except ValueError as error:
        parser.error(str(error))
    skipped = []
    if not args.include_block_move:
        skipped = [opcode for opcode in opcodes if opcode in BLOCK_MOVE_OPCODES]
        opcodes = [opcode for opcode in opcodes if opcode not in BLOCK_MOVE_OPCODES]
    if skipped:
        print(
            "excluded cycle-capped block-move vectors: "
            + ", ".join(f"{opcode:02X}" for opcode in skipped)
        )

    library = load_cpu_library(args.library.resolve())
    memory_interface = MemoryInterface(None, read8, write8)
    passed = 0
    failures = 0

    for opcode in opcodes:
        for mode in ("e", "n"):
            relative_path = f"v1/{opcode:02x}.{mode}.json"
            vectors = load_vectors(args.corpus.resolve(), relative_path)
            for vector in vectors[: args.limit]:
                CURRENT_MEMORY.clear()
                CURRENT_MEMORY.update(vector["initial"]["ram"])
                cpu = cpu_from_state(vector["initial"])
                result = library.dkc2_cpu_step(
                    ctypes.byref(cpu), ctypes.byref(memory_interface)
                )
                differences = state_differences(cpu, vector["final"])
                if result == 3:
                    differences.append("step returned invalid argument")
                if differences:
                    failures += 1
                    print(f"FAIL {vector['name']}: {'; '.join(differences)}")
                    if failures >= args.max_failures:
                        print(f"stopped after {failures} failures", file=sys.stderr)
                        return 1
                else:
                    passed += 1
            if not args.quiet:
                print(
                    f"opcode {opcode:02X} {mode}: "
                    f"checked {min(len(vectors), args.limit)}"
                )

    print(f"passed={passed} failed={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
