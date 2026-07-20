# Roadmap

## Milestone 0 — Foundation

- [x] Verify the exact USA v1.0 ROM without modifying it.
- [x] Detect a 512-byte copier header.
- [x] Parse the HiROM header and interrupt vectors.
- [x] Implement and test DKC2's 4 MiB HiROM mapping.
- [x] Keep ROMs and generated game content outside the source tree.

## Milestone 1 — Reference and decoder

- [x] Track E/M/X/C state for unambiguous decoding.
- [x] Decode every W65C816 opcode.
- [x] Analyze reset, native NMI, and native IRQ entry points.
- [x] Import external symbols without copying research source.
- [x] Follow direct control flow and export a typed graph.
- [x] Privately rebuild the supported revision byte for byte.
- [ ] Record deterministic emulator traces for boot and one idle title frame.

## Milestone 2 — Native boot

- [x] Define the complete instruction-state CPU register file and semantics.
- [x] Add an interpreter for all direct and unresolved control flow.
- [x] Pass the full comparable 5,080,000-case CPU state corpus.
- [x] Implement WRAM, ROM, SRAM, I/O callbacks, and main open bus.
- [x] Implement A-bus-to-B-bus general DMA and the VRAM clear path.
- [x] Execute the real reset path to the SPC700 upload handshake.
- [x] Integrate a compatibly licensed SPC700 and DSP core.
- [x] Execute the IPL handshake and verify synthetic CPU/APU uploads in ARAM.
- [x] Run DKC2 through its APU upload path to the `$4211` timing boundary.
- [x] Add an opt-in master-cycle scheduler and timed APU continuation.
- [ ] Compare the post-upload ARAM/DSP state with an accurate emulator.
- [x] Add HDMA, NMI/IRQ scheduling, H/V counters, and controller registers.
- [x] Add Mode-7 and CPU multiplication/division result registers.
- [x] Add the WRAM data/address ports and deterministic memory fingerprints.
- [x] Run a 20-million-instruction neutral-input probe without a hardware
      barrier.
- [x] Add a headless Mode-0/1/3/5 background, sprite, and color-math renderer
      with deterministic frame hashes and private image export.
- [x] Implement Mode-7 BG1/EXTBG rendering and exactly pixel-match a private
      Rareware-logo frame against Snes9x 1.63.
- [x] Compare VRAM, CGRAM, and OAM against a private Snes9x save state beside
      the matched logo frame; all three memories match byte for byte.
- [ ] Capture beam-aligned display registers for the matched logo frame and
      repeat the full comparison at the title screen.
- [x] Emit native C for imported and structurally proven control flow (98.79%
      of current exact CPU-mode variants in the conservative release profile).
- [x] Retain 42 unproven variants as authoritative LLE without CFG or HLE
      band-aids, then repeat the 12,000-frame attract gate.
- [x] Stand up the DKC2-owned repository with `snesrecomp` as a pinned
      submodule and reproducible private HiROM generation.
- [x] Complete a 12,000-frame neutral-input headless soak without a runtime
      freeze or interpreter abandonment.
- [x] Resolve the native foreground/sprite tile artifacts against an accurate
      reference capture.
- [x] Prove two complete attract cycles with deterministic video/audio event
      checkpoints rather than frame-count liveness alone.
- [x] Compare one complete native attract-cycle PCM stream with Snes9x for
      clipping, discontinuities, level, duration, and long silence regions.
- [x] Resolve the approximately 54-frame first-cycle timing lag: the
      interpreter was clearing program-bank bit 7 and charging FastROM code as
      its SlowROM mirror. All three loading windows now align within one frame.
- [ ] Localize the remaining six-frame-early first-cycle offset (three frames
      before the first title event and one additional frame per later demo).
- [x] Perform manual watch/listen and gameplay passes through a real host audio
      device, including death/restart and cross-game regression checks.
- [x] Add a Windows desktop window, keyboard input, XInput controller input,
      exact-rate frame pacing, and queued native-rate stereo output.
- [x] Make the desktop target double-clickable without a console window and
      prompt for the external private ROM when no path is supplied.
- [x] Remove the host clear-then-draw flicker with atomic off-screen GDI
      composition and reference/recording evidence.
- [x] Add fixed 3x keyboard/controller fast-forward and bounded in-memory
      rewind with a real hidden state-restore integration test.
- [x] Complete the documented manual keyboard/controller/watch/listen pass and
      resolve every observed release-blocking artifact before 0.0.1 sign-off.

Exit criterion: reach and render the title screen with correct audio and input.

## Milestone 3 — First level

- [x] Implement file selection and 2 KiB SRAM persistence, including an
      automatic previous-save backup and test isolation.
- [ ] Make Pirate Panic playable from entrance to goal.
- [ ] Compare frame, memory, input, and audio checkpoints to the reference.

## Milestone 4 — Complete game

- [ ] Cover all levels, bonuses, bosses, maps, two-player modes, endings, and
      102% completion.
- [ ] Add deterministic regression recordings and save-state fixtures.

## Milestone 5 — Maintainable port

- [ ] Replace generated functions with readable, tested C by subsystem.
- [ ] Isolate rendering, sound, level loading, objects, players, collisions,
      cameras, menus, maps, and saves.
- [ ] Add optional modern enhancements only after the faithful baseline passes.
