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
- [ ] Emit native C for direct control flow and known calls.
- [ ] Add input, audio output, frame pacing, and a desktop window.

Exit criterion: reach and render the title screen with correct audio and input.

## Milestone 3 — First level

- [ ] Implement file selection and 2 KiB SRAM persistence.
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
