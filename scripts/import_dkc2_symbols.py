#!/usr/bin/env python3
"""Build a private WLA-style symbol overlay from annotated DKC2 assembly.

The importer accepts either Yoshifanatic1's source ZIP or an extracted source
directory. It writes only label names and the absolute SNES addresses already
present in assembly comments; it never reads or copies incbin game data.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import zipfile


ADDRESS_RE = re.compile(r";\s*\$?([0-9A-Fa-f]{6})(?:\s|$)")
COLON_LABEL_RE = re.compile(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)\s*:")
PLAIN_LABEL_RE = re.compile(
    r"^([A-Za-z_.][A-Za-z0-9_.]*)(?:\s*;.*)?\s*$"
)
DIRECTIVES = {
    "arch",
    "assert",
    "base",
    "check",
    "db",
    "define",
    "dw",
    "else",
    "endif",
    "enum",
    "hirom",
    "if",
    "incbin",
    "incsrc",
    "namespace",
    "optimize",
    "org",
    "pad",
    "padbyte",
    "print",
    "struct",
    "warnpc",
}


def iter_assembly(source: pathlib.Path):
    if source.is_file():
        with zipfile.ZipFile(source) as archive:
            for name in sorted(archive.namelist()):
                if name.lower().endswith(".asm") and not name.endswith("/"):
                    yield name, archive.read(name).decode("utf-8", errors="replace")
        return

    if source.is_dir():
        for path in sorted(source.rglob("*.asm")):
            yield str(path.relative_to(source)), path.read_text(
                encoding="utf-8", errors="replace"
            )
        return

    raise FileNotFoundError(source)


def declared_label(line: str) -> str | None:
    match = COLON_LABEL_RE.match(line)
    if match is None:
        match = PLAIN_LABEL_RE.match(line)
    if match is None:
        return None
    label = match.group(1)
    if label.lower() in DIRECTIVES:
        return None
    return label


def qualified_label(global_label: str | None, label: str) -> str:
    if label.startswith(".") and global_label:
        label = f"{global_label}_{label.lstrip('.')}"
    return re.sub(r"[^A-Za-z0-9_]", "_", label)


def collect_symbols(source: pathlib.Path) -> list[tuple[int, str]]:
    symbols: list[tuple[int, str]] = []
    used_names: dict[str, int] = {}

    for _name, text in iter_assembly(source):
        current_global: str | None = None
        pending: list[str] = []
        preceding_hint: int | None = None

        def record(address: int, labels: list[str]) -> None:
            for raw_label in labels:
                base = qualified_label(current_global, raw_label)
                if not base:
                    continue
                unique = base
                suffix = 2
                while unique in used_names and used_names[unique] != address:
                    unique = f"{base}_{suffix}"
                    suffix += 1
                used_names[unique] = address
                symbols.append((address, unique))

        for line in text.splitlines():
            stripped = line.strip()
            address_match = ADDRESS_RE.search(line)
            label = declared_label(line)

            if stripped.startswith(";") and address_match:
                preceding_hint = int(address_match.group(1), 16)
                continue

            if label is not None:
                if not label.startswith("."):
                    current_global = label
                if preceding_hint is not None:
                    record(preceding_hint, [label])
                    preceding_hint = None
                else:
                    pending.append(label)

            if address_match is not None:
                address = int(address_match.group(1), 16)
                if pending:
                    record(address, pending)
                    pending.clear()
                preceding_hint = None

    # Duplicate aliases at the same address are useful, but exact duplicate
    # lines only make the WLA overlay larger and its lookup order ambiguous.
    return sorted(set(symbols), key=lambda item: (item[0], item[1]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path, help="source ZIP or directory")
    parser.add_argument("output", type=pathlib.Path, help="private .sym output")
    args = parser.parse_args()

    try:
        symbols = collect_symbols(args.source)
    except (OSError, zipfile.BadZipFile) as error:
        print(f"cannot import symbols: {error}", file=sys.stderr)
        return 1
    if not symbols:
        print("no address-annotated assembly labels found", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write("; private overlay generated from address annotations\n")
        output.write("[labels]\n")
        for address, name in symbols:
            output.write(f"{address >> 16:02X}:{address & 0xFFFF:04X} {name}\n")

    print(f"wrote {len(symbols)} private labels to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
