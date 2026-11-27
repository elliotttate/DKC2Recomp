#!/usr/bin/env python3
"""Let the host see a sprite palette request fall back to palette 0.

When all eight OBJ palettes are taken, request_palette_direct ($BB:8A6F)
reports error 2 and continues at $BB:8A8D, which stores the lost id in $05F7
and hands the caller palette 0. This adds one call at the start of that block
in the private generated C:

    Dkc2SpritePaletteOverflowHook(cpu);

The hook (runner/dkc2_game.c) only reads guest state: the caller's return
address, DP $64/$68 and Y identify the sprite, $05A7 the palette id. The
host retries the request through the cartridge's allocator once a palette
frees (runner/dkc2_palette_overflow.h). The interpreter path uses a
pre-opcode hook at the same PC. The edit is anchored on the routine's own
trace PC, checked against the block's $05A7 read and $05F7 write, verified to
match exactly once, and is idempotent across regeneration runs.
"""
import argparse
from pathlib import Path

INCLUDE = '#include "dkc2_palette_overflow.h"'
SYMBOL = 'RecompReturn request_palette_direct_M0X0(CpuState *cpu) {'
ANCHOR = '    cpu_trace_block(cpu, 0xBB8A8D);'
NEXT_BLOCK = '    cpu_trace_block(cpu, 0xBB8A'
CALL = '    Dkc2SpritePaletteOverflowHook(cpu);'
READ_ID = 'cpu_read16(cpu, cpu->DB, (uint16)(0x05a7))'
WRITE_LOST = 'cpu_write16(cpu, cpu->DB, (uint16)(0x05f7)'


def patch_text(text):
    """Return the patched unit text; raise ValueError if anchors moved."""
    if text.count(SYMBOL) != 1:
        raise ValueError('expected one request_palette_direct_M0X0 body')
    body = text.index(SYMBOL)
    end_of_body = text.find('\n}\n', body)
    if end_of_body < 0:
        raise ValueError('request_palette_direct_M0X0 body is not closed')
    function = text[body:end_of_body]
    if function.count(ANCHOR) != 1:
        raise ValueError('palette overflow block ($BB:8A8D) not found once')
    start = body + function.index(ANCHOR) + len(ANCHOR)
    block_end = text.find(NEXT_BLOCK, start)
    if block_end < 0 or block_end > end_of_body:
        block_end = end_of_body
    block = text[start:block_end]
    if READ_ID not in block or WRITE_LOST not in block:
        raise ValueError('$BB:8A8D no longer copies $05A7 to $05F7')
    if CALL in function:
        if function.count(CALL) != 1 or not block.startswith('\n' + CALL):
            raise ValueError('palette overflow hook is misplaced')
        patched = text
    else:
        patched = text[:start] + '\n' + CALL + text[start:]
    if INCLUDE not in patched:
        if patched.count('#include "funcs.h"') != 1:
            raise ValueError('generated unit has no single funcs.h include')
        patched = patched.replace('#include "funcs.h"',
                                  '#include "funcs.h"\n' + INCLUDE)
    return patched


def apply(directory):
    matches = [p for p in sorted(Path(directory).glob('*.c'))
               if SYMBOL in p.read_text(encoding='utf-8')]
    if len(matches) != 1:
        raise ValueError('expected one US v1.0 request_palette_direct unit')
    path = matches[0]
    text = path.read_text(encoding='utf-8')
    patched = patch_text(text)
    if patched != text:
        # open(newline='') keeps LF output on Python 3.9, whose
        # Path.write_text has no newline parameter.
        with open(path, 'w', encoding='utf-8', newline='') as handle:
            handle.write(patched)
    return path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-dir', type=Path, required=True)
    print(apply(parser.parse_args().generated_dir))
