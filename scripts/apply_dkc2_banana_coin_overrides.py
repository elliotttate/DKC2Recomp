#!/usr/bin/env python3
"""Let the host keep Banana Coins with a save file.

The cartridge never saves Banana Coins ($08CA). This adds one call to each
of three routines in the private generated C:

    Dkc2BananaCoinsSavedHook(cpu);   bank_80_A5B1, $80:A61C (PLB)
    Dkc2BananaCoinsSavedHook(cpu);   copy_save_to_sram, $BB:C5D8 (RTS)
    Dkc2BananaCoinsLoadedHook(cpu);  CODE_BBC736, $BB:C846 (RTL)

The save hooks record the live coin count for the save buffer just copied
to SRAM: the Kong Kollege "Save Game" screen copies it in its own loop
($80:A613) before $80:A61C, and copy_save_to_sram is the other copy. The
load hook writes a recorded count back after the loader has unpacked a
file (runner/dkc2_banana_coins.h). Each call goes in just before the
site's anchor line in that block; the interpreter path uses pre-opcode
hooks at the same PCs. Each edit is anchored on the routine's own trace PC
and that line, checked against the routine's save buffer ($56CA) or
Kremkoin ($08CC) access, verified to match exactly once, and is idempotent
across regeneration runs.
"""
import argparse
from pathlib import Path

INCLUDE = '#include "dkc2_banana_coins.h"'

SITES = (
    {
        'name': 'bank_80_A5B1',
        'symbol': 'RecompReturn bank_80_A5B1_M0X0(CpuState *cpu) {',
        'block': '    cpu_trace_block(cpu, 0x80A61C);',
        'before': '    goto L_A61D_M0X0; /* implicit fall-through */',
        'evidence': '0x56ca',
        'call': '    Dkc2BananaCoinsSavedHook(cpu);',
    },
    {
        'name': 'copy_save_to_sram',
        'symbol': 'RecompReturn copy_save_to_sram_M0X0(CpuState *cpu) {',
        'block': '    cpu_trace_block(cpu, 0xBBC5D8);',
        'before': '    { uint16 _ret_s = cpu->S;  /* RTS pop hardware return frame */',
        'evidence': '0x56ca',
        'call': '    Dkc2BananaCoinsSavedHook(cpu);',
    },
    {
        'name': 'CODE_BBC736',
        'symbol': 'RecompReturn CODE_BBC736_M0X0(CpuState *cpu) {',
        'block': '    cpu_trace_block(cpu, 0xBBC846);',
        'before': '    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */',
        'evidence': '(uint16)(0x08cc)',
        'call': '    Dkc2BananaCoinsLoadedHook(cpu);',
    },
)
NEXT_BLOCK = '    cpu_trace_block(cpu, '


def patch_site(text, site):
    """Return text with one site patched; raise ValueError if anchors moved."""
    symbol = site['symbol']
    if text.count(symbol) != 1:
        raise ValueError('expected one %s_M0X0 body' % site['name'])
    body = text.index(symbol)
    end_of_body = text.find('\n}\n', body)
    if end_of_body < 0:
        raise ValueError('%s_M0X0 body is not closed' % site['name'])
    function = text[body:end_of_body]
    if site['evidence'] not in function:
        raise ValueError('%s no longer touches %s' %
                         (site['name'], site['evidence']))
    if function.count(site['block']) != 1:
        raise ValueError('%s return block not found once' % site['name'])
    start = body + function.index(site['block']) + len(site['block'])
    block_end = text.find(NEXT_BLOCK, start)
    if block_end < 0 or block_end > end_of_body:
        block_end = end_of_body
    block = text[start:block_end]
    if block.count(site['before']) != 1:
        raise ValueError('%s block no longer has its anchor line' % site['name'])
    call = site['call']
    if call in function:
        if (function.count(call) != 1 or
                (call + '\n' + site['before']) not in block):
            raise ValueError('%s hook is misplaced' % site['name'])
        return text
    at = start + block.index(site['before'])
    return text[:at] + call + '\n' + text[at:]


def patch_text(text, sites):
    for site in sites:
        text = patch_site(text, site)
    if INCLUDE not in text:
        if text.count('#include "funcs.h"') != 1:
            raise ValueError('generated unit has no single funcs.h include')
        text = text.replace('#include "funcs.h"',
                            '#include "funcs.h"\n' + INCLUDE)
    return text


def apply(directory):
    units = {}
    for site in SITES:
        matches = [p for p in sorted(Path(directory).glob('*.c'))
                   if site['symbol'] in p.read_text(encoding='utf-8')]
        if len(matches) != 1:
            raise ValueError('expected one US v1.0 %s unit' % site['name'])
        units.setdefault(matches[0], []).append(site)
    # Patch every unit before writing any, so a moved anchor leaves the
    # generated sources as they were.
    results = []
    for path, sites in units.items():
        text = path.read_text(encoding='utf-8')
        results.append((path, text, patch_text(text, sites)))
    for path, text, patched in results:
        if patched != text:
            # open(newline='') keeps LF output on Python 3.9, whose
            # Path.write_text has no newline parameter.
            with open(path, 'w', encoding='utf-8', newline='') as handle:
                handle.write(patched)
    return sorted(units)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-dir', type=Path, required=True)
    for unit in apply(parser.parse_args().generated_dir):
        print(unit)
