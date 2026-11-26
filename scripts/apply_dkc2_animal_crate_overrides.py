#!/usr/bin/env python3
"""Let the Assist Tools crate option open Squawks's X-button dismount.

dismount_animal_action ($B8:C44A) dismounts the ridden animal on X, but for
Squawks ($0198) it first tests bit 0 of the animal's +$4A ($B8:C461
LDA $4A,x / LSR) and returns when the BCC at $B8:C464 is taken. This adds
one call before that branch in the private generated C:

    cpu->_flag_C = Dkc2SquawksDismountGate(cpu->_flag_C);

Dkc2SquawksDismountGate (runner/dkc2_game.c) returns the cartridge's carry
unchanged unless the option is on, so the routine is otherwise stock. The
interpreter path uses a pre-opcode hook at the same PC. The edit is anchored
on the routine's own trace PCs, verified to match exactly once, and is
idempotent across regeneration runs.
"""
import argparse
from pathlib import Path

INCLUDE = '#include "dkc2_animal_crate_cheat.h"'
SYMBOL = 'RecompReturn dismount_animal_action_M0X0(CpuState *cpu) {'
BLOCK_START = '    cpu_trace_block(cpu, 0xB8C461);'
BLOCK_END = '  L_C466_M0X0:'
BRANCH = ('    if (cpu->_flag_C == 0) { cpu->cycles += 1; '
          'cpu->master_cycles += (g_memsel ? 6 : 8); goto L_C457_M0X0; }')
CALL = '    cpu->_flag_C = Dkc2SquawksDismountGate(cpu->_flag_C);'


def patch_text(text):
    """Return the patched unit text; raise ValueError if anchors moved."""
    if text.count(SYMBOL) != 1:
        raise ValueError('expected one dismount_animal_action_M0X0 body')
    body = text.index(SYMBOL)
    start = text.find(BLOCK_START, body)
    end = text.find(BLOCK_END, start) if start >= 0 else -1
    if start < 0 or end < 0:
        raise ValueError('Squawks dismount block ($B8:C461) not found')
    block = text[start:end]
    if 'cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x004a + cpu->X))' not in block:
        raise ValueError('Squawks dismount block no longer reads $4A,x')
    if block.count(CALL) == 1 and block.count(BRANCH) == 1:
        if block.index(CALL) > block.index(BRANCH):
            raise ValueError('Squawks dismount gate is after its branch')
        patched = text
    elif CALL in block or block.count(BRANCH) != 1:
        raise ValueError('Squawks dismount branch ($B8:C464) changed')
    else:
        block = block.replace(BRANCH, CALL + '\n' + BRANCH)
        patched = text[:start] + block + text[end:]
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
        raise ValueError('expected one US v1.0 dismount_animal_action unit')
    path = matches[0]
    text = path.read_text(encoding='utf-8')
    patched = patch_text(text)
    if patched != text:
        path.write_text(patched, encoding='utf-8', newline='')
    return path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-dir', type=Path, required=True)
    print(apply(parser.parse_args().generated_dir))
