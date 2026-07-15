# Private reference-validation workflow

This workflow proves that the selected research map describes the exact ROM
revision supported by this project. It is optional for building the public
tools and should be performed only with materials you may lawfully use.

## Confirmed checkpoint

On 2026-07-14, commit
`59a3a8aef88d074f488d25d0d0623fcb37fa3791` of
[H4v0c21/DKC2-disassembly](https://github.com/H4v0c21/DKC2-disassembly)
was assembled for North American revision 0 with Asar 1.91. The resulting
4 MiB image was byte-for-byte identical (`cmp` exit 0) to the supported ROM:

```text
MD5     98458530599b9dff8a7414a7f20b777a
SHA-256 35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633
```

This is strong revision and address-map validation. It does not grant a
license to redistribute either ROM, the disassembly's game data, or generated
outputs. No explicit license file was present at that reference commit, so this
project treats it as a private research reference and copies none of its source.

## Reproduce privately

Install [Asar](https://github.com/RPGHacker/asar) 1.91 and clone the reference
outside this source tree. From the reference repository, run:

```sh
asar -Dversion=0 \
    --symbols=wla \
    --symbols-path=/private/dkc2-v0.sym \
    all.asm /private/dkc2-v0.sfc
```

Then compare the generated file to your verified ROM:

```sh
sha256sum /private/dkc2-v0.sfc /private/dkc2.smc
cmp /private/dkc2-v0.sfc /private/dkc2.smc
```

Both SHA-256 values must equal the baseline above, and `cmp` must report no
difference. Delete the generated `.sfc` when it is no longer needed.

The WLA map can be supplied at analysis time:

```sh
./build/dkc2_analyze /private/dkc2.smc 100000 \
    --follow-calls \
    --symbols /private/dkc2-v0.sym \
    --dot generated/reset.dot
```

Keep the cloned reference, `.sfc`, `.sym`, ROM, and any extracted data outside
the project or under ignored private directories. Only original tools, tests,
documentation, and metadata such as hashes belong in a distributable archive.

The instruction decoder itself is checked against the official
[W65C816S data sheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf).
