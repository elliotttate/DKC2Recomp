# W65C816 CPU conformance

## References and boundary

Instruction behavior was checked against the official
[W65C816S data sheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf)
and commit `db6b10401729d5f20f2181dde5d3d7b037093a4a` of the external
[SingleStepTests 65816 corpus](https://github.com/SingleStepTests/65816).

The corpus is a private development input, not a runtime dependency. Its JSON
files are not copied into this project or its release archives.

## Reproduce the state comparison

Build a temporary shared library containing the CPU implementation:

```sh
cc -std=c11 -O2 -fPIC -shared -Iinclude \
    src/cpu.c src/decode.c src/execute.c \
    -o /tmp/libdkc2cpu.so
```

Then run every comparable vector from an external checkout:

```sh
scripts/run_65816_tests.py \
    --library /tmp/libdkc2cpu.so \
    --corpus /private/65816-tests \
    --limit 10000 \
    --quiet
```

The version 0.3.0 checkpoint reports:

```text
excluded cycle-capped block-move vectors: 44, 54
passed=5080000 failed=0
```

The comparison checks A, X, Y, S, D, PC, DBR, PBR, P, E, and every relevant
memory byte after one interpreted instruction. It covers all 10,000 vectors in
both emulation and native mode for 254 opcodes.

## Why MVP and MVN are separate

The corpus records only the first 100 bus cycles for opcodes `$44` and `$54`,
so many of those expected states stop partway through a block move. The public
CPU API instead treats one call as one complete logical instruction and moves
all `A + 1` bytes. Comparing the two partial-state conventions would create a
false failure. Synthetic tests cover complete block moves in both the CPU and
DMA test programs.

Passing state vectors does not imply cycle accuracy. This interpreter does not
yet expose dummy reads, per-cycle pins, refresh stalls, interrupt sampling
windows, or mid-instruction aborts. Those belong to the future timing layer.

## Edge cases retained as local regressions

The external sweep found and the local tests now preserve these behaviors:

- emulation-mode M/X/S normalization at instruction boundaries;
- legacy page-wrapped stack operations versus multi-byte 65816 stack
  exceptions;
- `PLB`, `PLD`, `JSL`, `RTL`, interrupt, and `RTI` boundary behavior;
- decimal ADC overflow from the decimal-adjusted intermediate result;
- 24-bit direct pointers crossing a direct-page boundary;
- 16-bit operands crossing an A-bus bank boundary; and
- complete logical MVP/MVN transfers with 8- or 16-bit index widths.
