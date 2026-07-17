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

### Import address annotations without assembling

The independently maintained
[Yoshifanatic1 DKC2 disassembly](https://github.com/Yoshifanatic1/Donkey-Kong-Country-2-Disassembly)
also annotates many labels with absolute SNES addresses. The source-only
importer can create a private WLA-style overlay directly from a downloaded ZIP
or extracted checkout:

```powershell
python scripts\import_dkc2_symbols.py `
    "C:\private\Donkey-Kong-Country-2-Disassembly.zip" `
    private\dkc2-yoshifanatic-v1.sym

.\build\Release\dkc2_analyze.exe "C:\private\dkc2.smc" 100000 `
    --follow-calls `
    --symbols private\dkc2-yoshifanatic-v1.sym
```

The importer reads only `.asm` text and emits label/address metadata; it does
not read `incbin` payloads or copy assembly into this repository. Local labels
are qualified with their enclosing global label to avoid collisions. The
generated `.sym` remains ignored under `private/`.

The user-suggested checkout was inspected privately at commit
`bc1138ffa5568b4f74deba1a5ff6af8e562c06a1` on 2026-07-16. Its repository
contains GPL-3.0 license text. No source or ROM-derived data was copied into
this project; the importer produced a private 180-label overlay, and the
annotated control flow was used to name the audio loader, NMI continuation,
and object-list sort involved in the frame-3,048 diagnosis.

Keep the cloned reference, `.sfc`, `.sym`, ROM, and any extracted data outside
the project or under ignored private directories. Only original tools, tests,
documentation, and metadata such as hashes belong in a distributable archive.

## Compare a private attract-cycle audio capture

The native and libretro reference tools accept `DKC2_AUDIO_PCM`. It writes
headerless signed 16-bit little-endian stereo at the SNES DSP's 32,040 Hz rate.
Keep both outputs under ignored `private/` storage:

```powershell
$env:DKC2_AUDIO_PCM="private\native-attract.pcm"
.\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe `
    "C:\private\dkc2.smc" 6000

$env:DKC2_AUDIO_PCM="private\reference-attract.pcm"
.\build-snesrecomp\Release\dkc2_libretro_capture.exe `
    "C:\private\snes9x_libretro.dll" "C:\private\dkc2.smc" 6000 `
    "private\reference-attract.ppm"

python scripts\compare_audio_pcm.py `
    private\native-attract.pcm private\reference-attract.pcm
```

The comparison is deliberately not byte-exact. It rejects clipping, large
level/peak/discontinuity differences, a duration mismatch, and added or moved
long silence regions. Snes9x can emit a small initial audio-buffer surplus, and
independent S-DSP integer implementations need not produce identical samples.
The checked 6,000-frame run completed the same three-demo semantic sequence on
both sides and passed with zero clipping and seven corresponding long silences.
This supports, but does not replace, a manual listen through the eventual host
audio device.

## Compare semantic event timing

Set `DKC2_STATE_TRACE=1` on the same 6,000-frame native and Snes9x runs and
redirect standard error to ignored private logs. Then run:

```powershell
python scripts\compare_state_traces.py `
    private\native-state-6000.log private\reference-state-6000.log
```

The comparison requires the same ordered game-mode, demo, level, and
continuation transitions. It also limits every transition to six frames of the
reference and each interpreted level-loading window to one frame of the
reference duration. `active_frame` is reported but excluded from event identity
because attract reset can clear it later within the same sampled video frame.
The current checked result passes with loading durations of `[152, 134, 152]`
native frames versus `[152, 135, 153]` reference frames.

The instruction decoder itself is checked against the official
[W65C816S data sheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf).
