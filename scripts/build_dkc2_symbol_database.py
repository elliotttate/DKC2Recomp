#!/usr/bin/env python3
"""Validate DKC2 semantic symbols and generate durable lookup artifacts.

The SNESRecomp bank CFG files remain the structural source of function
boundaries.  ``recomp/symbols.toml`` adds stable, human-readable names to
selected boundaries, while ``recomp/layouts.toml`` records confirmed WRAM
objects and structure fields.  This tool checks that the layers agree before
updating a CFG or generating diagnostic/documentation projections.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import pprint
import re
import sys

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python 3.10 compatibility
    try:
        import tomli as tomllib
    except ModuleNotFoundError as error:  # pragma: no cover
        raise SystemExit(
            "Python 3.11+ or the 'tomli' package is required") from error


IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
CFG_RE = re.compile(
    r"^(?P<prefix>\s*func\s+)"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?P<middle>\s+)(?P<start>[0-9A-Fa-f]{4})"
    r"(?P<suffix>\s+end:(?P<end>[0-9A-Fa-f]{4,5}).*)$"
)
BANK_RE = re.compile(
    r"^\s*bank\s*=\s*(?:0x(?P<hex>[0-9A-Fa-f]{1,2})|"
    r"(?P<decimal>[0-9]{1,3}))\s*$"
)
FUNCTION_ADDRESS_RE = re.compile(r"^(?P<bank>[0-9A-Fa-f]{2}):(?P<offset>[0-9A-Fa-f]{4})$")
WRAM_ADDRESS_RE = re.compile(r"^WRAM:(?P<offset>[0-9A-Fa-f]{4,5})$")
CONFIDENCE_VALUES = {"guessed", "contextual", "confirmed"}
TYPE_SIZES = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u24": 3, "u32": 4, "s32": 4}


@dataclass(frozen=True)
class CfgFunction:
    path: Path
    line_index: int
    bank: int
    start: int
    end: int
    name: str

    @property
    def address(self) -> int:
        return (self.bank << 16) | self.start

    @property
    def canonical_address(self) -> str:
        return f"{self.bank:02X}:{self.start:04X}"


def _load_toml(path: Path) -> dict:
    with path.open("rb") as stream:
        return tomllib.load(stream)


def _identifier(value: object, context: str) -> str:
    if not isinstance(value, str) or not IDENTIFIER_RE.fullmatch(value):
        raise ValueError(f"{context} is not a valid identifier: {value!r}")
    return value


def _text(value: object, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{context} must be non-empty text")
    return value.strip()


def _confidence(value: object, context: str) -> str:
    if value not in CONFIDENCE_VALUES:
        choices = ", ".join(sorted(CONFIDENCE_VALUES))
        raise ValueError(f"{context} must be one of: {choices}")
    return str(value)


def _tags(value: object, context: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ValueError(f"{context} must contain at least one tag")
    tags = [_text(tag, context) for tag in value]
    if len(tags) != len(set(tags)):
        raise ValueError(f"{context} contains duplicate tags")
    return tags


def load_cfg(cfg_dir: Path) -> list[CfgFunction]:
    paths = sorted(cfg_dir.glob("bank[0-9A-Fa-f][0-9A-Fa-f].cfg"))
    if not paths:
        raise ValueError(f"no bankXX.cfg files found in {cfg_dir}")
    functions: list[CfgFunction] = []
    names: set[str] = set()
    addresses: set[int] = set()
    for path in paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        bank = None
        for line in lines:
            match = BANK_RE.match(line)
            if match:
                bank = int(match.group("hex"), 16) if match.group("hex") else int(match.group("decimal"))
                break
        if bank is None or not 0 <= bank <= 0xFF:
            raise ValueError(f"invalid or missing bank declaration: {path}")
        for line_index, line in enumerate(lines):
            match = CFG_RE.match(line)
            if not match:
                continue
            name = match.group("name")
            start = int(match.group("start"), 16)
            end = int(match.group("end"), 16)
            address = (bank << 16) | start
            if name in names:
                raise ValueError(f"duplicate CFG function name: {name}")
            if address in addresses:
                raise ValueError(f"duplicate CFG function address: {bank:02X}:{start:04X}")
            if end <= start:
                raise ValueError(f"invalid CFG function range at {path}:{line_index + 1}")
            names.add(name)
            addresses.add(address)
            functions.append(CfgFunction(path, line_index, bank, start, end, name))
    return functions


def load_semantic_functions(path: Path) -> tuple[dict, list[dict]]:
    root = _load_toml(path)
    if root.get("schema_version") != 1:
        raise ValueError(f"unsupported symbols schema in {path}")
    revision = _text(root.get("rom_revision"), "rom_revision")
    sha256 = _text(root.get("rom_sha256"), "rom_sha256").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", sha256):
        raise ValueError("rom_sha256 must be 64 lowercase hexadecimal digits")
    result: list[dict] = []
    addresses: set[int] = set()
    names: set[str] = set()
    aliases: set[str] = set()
    for index, raw in enumerate(root.get("function", [])):
        context = f"function[{index}]"
        match = FUNCTION_ADDRESS_RE.fullmatch(str(raw.get("address", "")))
        if not match:
            raise ValueError(f"{context}.address must use BB:OOOO notation")
        address = (int(match.group("bank"), 16) << 16) | int(match.group("offset"), 16)
        name = _identifier(raw.get("name"), f"{context}.name")
        raw_aliases = raw.get("aliases", [])
        if not isinstance(raw_aliases, list):
            raise ValueError(f"{context}.aliases must be an array")
        entry_aliases = [_identifier(alias, f"{context}.aliases") for alias in raw_aliases]
        if address in addresses:
            raise ValueError(f"duplicate semantic function address: {raw['address']}")
        if name in names or name in aliases:
            raise ValueError(f"duplicate semantic function name or alias: {name}")
        for alias in entry_aliases:
            if alias == name or alias in names or alias in aliases:
                raise ValueError(f"duplicate semantic function name or alias: {alias}")
        addresses.add(address)
        names.add(name)
        aliases.update(entry_aliases)
        result.append({
            "address": address,
            "address_text": f"{address >> 16:02X}:{address & 0xFFFF:04X}",
            "name": name,
            "aliases": entry_aliases,
            "confidence": _confidence(raw.get("confidence"), f"{context}.confidence"),
            "tags": _tags(raw.get("tags"), f"{context}.tags"),
            "provenance": _text(raw.get("provenance"), f"{context}.provenance"),
            "note": _text(raw.get("note"), f"{context}.note"),
        })
    if not result:
        raise ValueError("symbols database contains no functions")
    return {"schema_version": 1, "rom_revision": revision, "rom_sha256": sha256}, result


def load_layouts(path: Path) -> tuple[dict, list[dict], list[dict], list[dict]]:
    root = _load_toml(path)
    if root.get("schema_version") != 1:
        raise ValueError(f"unsupported layouts schema in {path}")
    metadata = {"schema_version": 1, "rom_revision": _text(root.get("rom_revision"), "rom_revision")}
    objects: list[dict] = []
    object_addresses: set[int] = set()
    identifiers: set[str] = set()
    for index, raw in enumerate(root.get("object", [])):
        context = f"object[{index}]"
        match = WRAM_ADDRESS_RE.fullmatch(str(raw.get("address", "")))
        if not match:
            raise ValueError(f"{context}.address must use WRAM:OOOO notation")
        address = int(match.group("offset"), 16)
        size = raw.get("size")
        if not isinstance(size, int) or size <= 0 or address + size > 0x20000:
            raise ValueError(f"{context}.size extends outside 128 KiB WRAM")
        name = _identifier(raw.get("name"), f"{context}.name")
        constant = _identifier(raw.get("constant"), f"{context}.constant")
        if address in object_addresses:
            raise ValueError(f"duplicate object address: {raw['address']}")
        for identifier in (name, constant):
            if identifier in identifiers:
                raise ValueError(f"duplicate layout identifier: {identifier}")
            identifiers.add(identifier)
        entry = {
            "address": address,
            "address_text": f"WRAM:{address:04X}",
            "name": name,
            "constant": constant,
            "type": _text(raw.get("type"), f"{context}.type"),
            "size": size,
            "confidence": _confidence(raw.get("confidence"), f"{context}.confidence"),
            "tags": _tags(raw.get("tags"), f"{context}.tags"),
            "provenance": _text(raw.get("provenance"), f"{context}.provenance"),
            "note": _text(raw.get("note"), f"{context}.note"),
        }
        for optional in ("count", "stride"):
            if optional in raw:
                if not isinstance(raw[optional], int) or raw[optional] <= 0:
                    raise ValueError(f"{context}.{optional} must be positive")
                entry[optional] = raw[optional]
        if "count" in entry and "stride" in entry and entry["count"] * entry["stride"] > size:
            raise ValueError(f"{context} count * stride exceeds the object size")
        for optional in ("count_constant", "stride_constant"):
            if optional in raw:
                value = _identifier(raw[optional], f"{context}.{optional}")
                if value in identifiers:
                    raise ValueError(f"duplicate layout identifier: {value}")
                identifiers.add(value)
                entry[optional] = value
        object_addresses.add(address)
        objects.append(entry)

    structures: list[dict] = []
    structure_by_name: dict[str, dict] = {}
    for index, raw in enumerate(root.get("structure", [])):
        context = f"structure[{index}]"
        name = _identifier(raw.get("name"), f"{context}.name")
        size = raw.get("size")
        if not isinstance(size, int) or size <= 0:
            raise ValueError(f"{context}.size must be positive")
        if name in structure_by_name:
            raise ValueError(f"duplicate structure: {name}")
        entry = {
            "name": name,
            "size": size,
            "confidence": _confidence(raw.get("confidence"), f"{context}.confidence"),
            "provenance": _text(raw.get("provenance"), f"{context}.provenance"),
            "note": _text(raw.get("note"), f"{context}.note"),
        }
        structure_by_name[name] = entry
        structures.append(entry)

    fields: list[dict] = []
    ranges_by_structure: dict[str, list[tuple[int, int, str]]] = {}
    for index, raw in enumerate(root.get("field", [])):
        context = f"field[{index}]"
        structure = _identifier(raw.get("structure"), f"{context}.structure")
        if structure not in structure_by_name:
            raise ValueError(f"{context} refers to unknown structure: {structure}")
        offset = raw.get("offset")
        if not isinstance(offset, int) or offset < 0:
            raise ValueError(f"{context}.offset must be non-negative")
        field_type = _text(raw.get("type"), f"{context}.type")
        if field_type not in TYPE_SIZES:
            raise ValueError(f"{context}.type has unknown width: {field_type}")
        end = offset + TYPE_SIZES[field_type]
        if end > structure_by_name[structure]["size"]:
            raise ValueError(f"{context} extends outside {structure}")
        name = _identifier(raw.get("name"), f"{context}.name")
        constant = _identifier(raw.get("constant"), f"{context}.constant")
        if constant in identifiers:
            raise ValueError(f"duplicate layout identifier: {constant}")
        identifiers.add(constant)
        for existing_start, existing_end, existing_name in ranges_by_structure.setdefault(structure, []):
            if offset < existing_end and end > existing_start:
                raise ValueError(f"{context} overlaps field {existing_name}")
        ranges_by_structure[structure].append((offset, end, name))
        fields.append({
            "structure": structure,
            "offset": offset,
            "name": name,
            "constant": constant,
            "type": field_type,
            "confidence": _confidence(raw.get("confidence"), f"{context}.confidence"),
            "note": _text(raw.get("note"), f"{context}.note"),
        })
    if not objects or not structures or not fields:
        raise ValueError("layouts database must contain objects, structures, and fields")
    return metadata, objects, structures, fields


def reconcile_functions(cfg: list[CfgFunction], semantic: list[dict]) -> tuple[list[tuple[CfgFunction, dict]], list[str]]:
    by_address = {entry.address: entry for entry in cfg}
    cfg_names = {entry.name: entry.address for entry in cfg}
    reconciled = []
    mismatches = []
    for symbol in semantic:
        entry = by_address.get(symbol["address"])
        if entry is None:
            raise ValueError(f"semantic address is not a CFG function boundary: {symbol['address_text']}")
        collision = cfg_names.get(symbol["name"])
        if collision is not None and collision != symbol["address"]:
            raise ValueError(f"semantic name collides with CFG at {collision:06X}: {symbol['name']}")
        reconciled.append((entry, symbol))
        if entry.name != symbol["name"]:
            mismatches.append(f"{entry.canonical_address}: {entry.name} -> {symbol['name']}")
    return reconciled, mismatches


def apply_cfg_names(reconciled: list[tuple[CfgFunction, dict]]) -> None:
    changes: dict[Path, dict[int, tuple[str, str]]] = {}
    for entry, symbol in reconciled:
        if entry.name != symbol["name"]:
            changes.setdefault(entry.path, {})[entry.line_index] = (entry.name, symbol["name"])
    for path, line_changes in changes.items():
        original = path.read_text(encoding="utf-8")
        lines = original.splitlines()
        for line_index, (old_name, new_name) in line_changes.items():
            match = CFG_RE.match(lines[line_index])
            if not match or match.group("name") != old_name:
                raise ValueError(f"CFG changed while applying names: {path}:{line_index + 1}")
            lines[line_index] = f"{match.group('prefix')}{new_name}{match.group('middle')}{match.group('start')}{match.group('suffix')}"
        path.write_text("\n".join(lines) + ("\n" if original.endswith("\n") else ""), encoding="utf-8", newline="\n")


def _python_literal(value: object) -> str:
    return pprint.pformat(value, width=100, sort_dicts=True)


def render_python(metadata: dict, functions: list[dict], objects: list[dict], structures: list[dict], fields: list[dict]) -> str:
    constants: list[tuple[str, int]] = []
    for item in objects:
        constants.append((item["constant"], item["address"]))
        if "count_constant" in item:
            constants.append((item["count_constant"], item["count"]))
        if "stride_constant" in item:
            constants.append((item["stride_constant"], item["stride"]))
    constants.extend((item["constant"], item["offset"]) for item in fields)
    function_map = {item["address"]: {key: value for key, value in item.items() if key != "address"} for item in functions}
    alias_map = {alias: item["address"] for item in functions for alias in [item["name"], *item["aliases"]]}
    object_map = {item["address"]: {key: value for key, value in item.items() if key != "address"} for item in objects}
    structure_map = {item["name"]: dict(item) for item in structures}
    field_map: dict[str, dict[int, dict]] = {}
    for item in fields:
        field_map.setdefault(item["structure"], {})[item["offset"]] = {key: value for key, value in item.items() if key not in {"structure", "offset"}}
    lines = [
        '# Generated by scripts/build_dkc2_symbol_database.py; do not edit.',
        '"""Validated DKC2 USA v1.0 symbols used by diagnostic tooling."""',
        '',
        f"SCHEMA_VERSION = {metadata['schema_version']}",
        f"ROM_REVISION = {_python_literal(metadata['rom_revision'])}",
        f"ROM_SHA256 = {_python_literal(metadata['rom_sha256'])}",
        '',
    ]
    lines.extend(f"{name} = 0x{value:X}" for name, value in sorted(constants))
    lines.extend([
        '',
        f"FUNCTION_SYMBOLS = {_python_literal(function_map)}",
        '',
        f"FUNCTION_ALIASES = {_python_literal(alias_map)}",
        '',
        f"WRAM_OBJECTS = {_python_literal(object_map)}",
        '',
        f"STRUCTURES = {_python_literal(structure_map)}",
        '',
        f"STRUCTURE_FIELDS = {_python_literal(field_map)}",
        '',
        'def resolve_function(name_or_alias: str):',
        '    """Return a function record by semantic name or historical alias."""',
        '    address = FUNCTION_ALIASES.get(name_or_alias)',
        '    if address is None:',
        '        return None',
        '    return {"address": address, **FUNCTION_SYMBOLS[address]}',
        '',
    ])
    return "\n".join(lines)


def _escape_markdown(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_docs(metadata: dict, cfg: list[CfgFunction], functions: list[dict], objects: list[dict], structures: list[dict], fields: list[dict]) -> str:
    lines = [
        "# DKC2 semantic symbol database",
        "",
        "This file is generated by `scripts/build_dkc2_symbol_database.py`; edit",
        "`recomp/symbols.toml` or `recomp/layouts.toml`, then regenerate it.",
        "",
        f"- ROM: `{metadata['rom_revision']}`",
        f"- SHA-256: `{metadata['rom_sha256']}`",
        f"- Structural CFG inventory: **{len(cfg)} functions** across **{len({item.bank for item in cfg})} banks**",
        f"- Curated semantic functions: **{len(functions)}**",
        f"- Confirmed/contextual WRAM objects: **{len(objects)}**",
        "",
        "Addresses are the stable identifiers. A historical alias may contain an",
        "address from another assembly revision and therefore is never used as the",
        "identity of a supported-ROM CFG boundary.",
        "",
        "## Functions",
        "",
        "| Address | Name | Confidence | Tags | Aliases | Purpose |",
        "|---|---|---|---|---|---|",
    ]
    for item in sorted(functions, key=lambda value: value["address"]):
        lines.append("| " + " | ".join(_escape_markdown(value) for value in (
            f"`{item['address_text']}`", f"`{item['name']}`", item["confidence"],
            ", ".join(item["tags"]), ", ".join(f"`{alias}`" for alias in item["aliases"]) or "none", item["note"],
        )) + " |")
    lines.extend([
        "", "## WRAM objects", "",
        "| Address | Name / generated constant | Type | Size | Confidence | Purpose |",
        "|---|---|---|---:|---|---|",
    ])
    for item in sorted(objects, key=lambda value: value["address"]):
        lines.append("| " + " | ".join(_escape_markdown(value) for value in (
            f"`{item['address_text']}`", f"`{item['name']}` / `{item['constant']}`",
            item["type"], item["size"], item["confidence"], item["note"],
        )) + " |")
    fields_by_structure: dict[str, list[dict]] = {}
    for item in fields:
        fields_by_structure.setdefault(item["structure"], []).append(item)
    for structure in structures:
        lines.extend([
            "", f"## Structure `{structure['name']}`", "",
            f"Size: `0x{structure['size']:X}` bytes. {structure['note']}", "",
            "| Offset | Field / generated constant | Type | Confidence | Purpose |",
            "|---:|---|---|---|---|",
        ])
        for item in sorted(fields_by_structure.get(structure["name"], []), key=lambda value: value["offset"]):
            lines.append("| " + " | ".join(_escape_markdown(value) for value in (
                f"`0x{item['offset']:02X}`", f"`{item['name']}` / `{item['constant']}`",
                item["type"], item["confidence"], item["note"],
            )) + " |")
    lines.extend([
        "", "## Confidence vocabulary", "",
        "- `confirmed`: supported by repeatable revision-specific code or runtime evidence.",
        "- `contextual`: owning behavior is known, but the entry is an internal continuation.",
        "- `guessed`: useful discovery label that must not be treated as proven behavior.",
        "",
    ])
    return "\n".join(lines)


def render_json(metadata: dict, cfg: list[CfgFunction], functions: list[dict], objects: list[dict], structures: list[dict], fields: list[dict], root: Path) -> str:
    semantic_by_address = {item["address"]: item for item in functions}
    inventory = []
    for entry in sorted(cfg, key=lambda item: item.address):
        item = {
            "address": entry.canonical_address,
            "address_integer": entry.address,
            "bank": entry.bank,
            "start": entry.start,
            "end": entry.end,
            "cfg_name": entry.name,
            "cfg_path": entry.path.resolve().relative_to(root.resolve()).as_posix(),
            "cfg_line": entry.line_index + 1,
        }
        if entry.address in semantic_by_address:
            item["semantic"] = {key: value for key, value in semantic_by_address[entry.address].items() if key != "address"}
        inventory.append(item)
    payload = {
        **metadata,
        "cfg_function_count": len(cfg),
        "functions": inventory,
        "wram_objects": objects,
        "structures": structures,
        "fields": fields,
    }
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def _check(path: Path, expected: str) -> None:
    if not path.is_file():
        raise ValueError(f"generated output is missing: {path}")
    if path.read_text(encoding="utf-8") != expected:
        raise ValueError(f"generated output is stale: {path}")


def build(args: argparse.Namespace) -> dict[str, int]:
    symbol_metadata, semantic = load_semantic_functions(args.symbols)
    layout_metadata, objects, structures, fields = load_layouts(args.layouts)
    if symbol_metadata["rom_revision"] != layout_metadata["rom_revision"]:
        raise ValueError("symbol and layout databases describe different ROM revisions")
    cfg = load_cfg(args.cfg_dir)
    reconciled, mismatches = reconcile_functions(cfg, semantic)
    if args.apply_cfg and mismatches:
        apply_cfg_names(reconciled)
        cfg = load_cfg(args.cfg_dir)
        reconciled, mismatches = reconcile_functions(cfg, semantic)
    if mismatches:
        details = "\n  ".join(mismatches)
        raise ValueError(f"CFG names differ from semantic database; run with --apply-cfg:\n  {details}")
    python_output = render_python(symbol_metadata, semantic, objects, structures, fields)
    docs_output = render_docs(symbol_metadata, cfg, semantic, objects, structures, fields)
    json_output = render_json(symbol_metadata, cfg, semantic, objects, structures, fields, args.root)
    if args.check:
        _check(args.python_out, python_output)
        _check(args.docs_out, docs_output)
    else:
        _write(args.python_out, python_output)
        _write(args.docs_out, docs_output)
        _write(args.json_out, json_output)
    return {"cfg_functions": len(cfg), "semantic_functions": len(semantic), "objects": len(objects), "fields": len(fields)}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--cfg-dir", type=Path)
    parser.add_argument("--symbols", type=Path)
    parser.add_argument("--layouts", type=Path)
    parser.add_argument("--python-out", type=Path)
    parser.add_argument("--docs-out", type=Path)
    parser.add_argument("--json-out", type=Path)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--apply-cfg", action="store_true", help="rename matching CFG entries before generating")
    action.add_argument("--check", action="store_true", help="validate CFG and require tracked outputs to be current")
    args = parser.parse_args(argv)
    args.root = args.root.resolve()
    args.cfg_dir = (args.cfg_dir or args.root / "recomp").resolve()
    args.symbols = (args.symbols or args.root / "recomp" / "symbols.toml").resolve()
    args.layouts = (args.layouts or args.root / "recomp" / "layouts.toml").resolve()
    args.python_out = (args.python_out or args.root / "scripts" / "dkc2_symbols_generated.py").resolve()
    args.docs_out = (args.docs_out or args.root / "docs" / "SYMBOL_DATABASE.md").resolve()
    args.json_out = (args.json_out or args.root / ".cache" / "dkc2-symbols.json").resolve()
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        result = build(parse_args(argv))
    except (OSError, ValueError) as error:
        print(f"symbol database error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
