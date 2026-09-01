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
- [x] Promote unambiguous context-qualified symbols into the active
      SNESRecomp CFG and reject raw-address, data-label, collision, and
      ambiguous-name cases with synthetic tests.
- [x] Add a revision-addressed semantic symbol and WRAM-layout database that
      drives CFG naming, diagnostic constants, lookup output, generated
      documentation, and stale-output validation.
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
- [x] Reach structural static closure: the current 13-bank profile emits 3,475
      exact AOT variants from 3,325 roots and retains only the two deliberate
      original-game fault variants on LLE.
- [x] Preserve the shared interpreter as a safety tier and as the exact
      exceptional destination for two documented dormant original-game crash
      calls into non-code bytes; do not invent return behavior for either bug.
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
- [x] Rebase continuing work on the public DKC2Recomp repository and exact
      current submodule pins while preserving the former working trees.
- [x] Add an FPS title readout, opt-in per-phase host telemetry, Release speed
      optimization, and optional private executable-icon packaging.
- [x] Expose Player 2 in the shared ImGui launcher and route independently
      selected keyboard/XInput sources into both SNES controller ports.
- [x] Add an OpenGL presenter with atomic-GDI fallback, nearest/bilinear
      scaling, and opt-in PSXRecomp-derived CRT screen-color models while
      retaining Raw as the verified default.
- [x] Add an SDL2 gameplay host and Python generation entry point that compile
      from the same source on Windows, Linux, and macOS; verify the complete
      portable host lifecycle on Windows without removing the Win32 host.
- [x] Add rolling crash reports, privacy-allowlisted diagnostic bundles, and
      Windows minidumps, with clean/fatal/exception integration drills.
- [x] Add a Dear ImGui in-game pause overlay to the OpenGL gameplay hosts,
      including all DKC2 launcher settings, two-player controller setup, an
      explicit Assist Tools/Cheats gate, and five save/load slots.
- [x] Add optional Assist Tools and Credits pages to the shared pre-boot
      recomp-ui launcher through additive, host-supplied capability fields.
- [x] Add persistent, runtime-consumed keyboard and standard-controller
      remapping for both players plus configurable Assist shortcuts in the
      Controller and Assist Tools pre-boot pages.
- [x] Expose those same Player 1/2 and Assist binding editors in the in-game
      Controls tab, with safe capture cancellation and fixed recovery
      shortcuts.
- [x] Repair the expanded pre-boot dashboard layout and add an external,
      hash-verified personal test-bundle workflow without weakening the
      repository ROM/save boundary.
- [ ] Add mod-aware save isolation after the versioned mod manifest and loader
      exist. Do not invent mod identities or migrate current saves early.
- [ ] Pass the native Linux acceptance matrix and publish a source-clean Linux
      package.
- [x] Create and locally validate the native Apple-silicon application bundle,
      AppKit menu/icon, bundled SDL2, Application Support persistence, and
      ad-hoc signing verification.
- [x] Keep native Mac Quick Save/Load available independently of the optional
      Assist bindings and prevent the legacy local build path from reopening a
      stale bundle after a successful canonical build.
- [ ] Developer-ID sign, notarize, archive, and publish the native macOS build;
      complete broad controller coverage before making a public release claim.
- [ ] Move installed-build saves/configuration to each remaining platform's
      user-data directory while preserving an explicit portable-folder mode.
      macOS now uses `~/Library/Application Support/Flat2VR/DKC2Recomp`.

Exit criterion: reach and render the title screen with correct audio and input.

## Milestone 3 — First level

- [x] Implement file selection and 2 KiB SRAM persistence, including an
      automatic previous-save backup and test isolation.
- [x] Add a deterministic private input-recording/replay harness and a Pirate
      Panic route gate that can assert level entry, completion-flag changes,
      exit transition, active video, and unclipped audio.
- [ ] Make Pirate Panic playable from entrance to goal.
- [ ] Compare frame, memory, input, and audio checkpoints to the reference.
- [x] Add an experimental 342x224 16:9 PPU/presenter path, common
      spawn/despawn and sprite-render boundary adaptations, bounded-screen
      pillarboxing, and pre-boot/in-game toggles.
- [x] Add centered 308x224 16:10 presentation plus native Mac menu control,
      while preserving 256x224 4:3 and 342x224 16:9.
- [x] Request and report one-buffer VSync for visible Windows OpenGL hosts;
      use a single exact-rate Mach pacing authority on visible macOS instead.
      Hidden automation remains unsynchronized and GDI compositor-managed.
- [x] Visually inspect Pirate Panic's composite and isolated BG1/BG2/BG3/OBJ
      output through deterministic TCP screenshots and scan its route for OBJ
      samples in both extended margins.
- [x] Replace Pirate Panic's raw rolling-VRAM margin reads with world-keyed
      BG1/BG2 history, a bounded unknown-cell fallback, and a cyclic BG2
      parallax policy after moving footage disproved the initial static check.
- [x] Reconstruct unseen Pirate Panic BG1 columns from its decompressed WRAM
      metatile map, including the `$0100` camera/source offset, vertical
      column rotation, transparent level-boundary fill, and fail-closed object
      widening.
- [x] Generalize hard-left decoded-terrain presentation across known map
      layouts by reflecting only the first authored columns into the host-only
      gutter west of world X=`$0100`. Pirate Panic, bonus `$006F`, and vertical
      Mainbrace Mayhem have visible native-Mac acceptance; unknown layouts
      still fail closed.
- [x] Reclassify ship-hold sub-mode `$02` as rolling 80-metatile-row terrain,
      reconstruct both margins from that source map, repeat its bounded BG3
      water and the verified 96-pixel BG2 `$7000`/`$7800` cabin-wall pattern,
      and make
      Escape leave fullscreen before opening the SDL/Mac overlay. The BG2
      correction changes only the clipped seven-column bands at native X=0-6
      and X=249-255.
      Lockjaw's Locker has exact-state moving-layer evidence; other ship-hold
      entrances still need fresh-entry coverage.
- [x] Promote every enabled physical 64-column Mode-1 layer, including BG3,
      only after the exact BG1/BG2 terrain source is ready. The Rattle Battle
      `$0005` / sub-mode `$0006` ship-deck signature now widens BG1+BG3 and
      repeats bounded BG2; representative bounded, Mainbrace, dual-stream,
      and Mode-7 signatures form a synthetic fail-closed capability floor.
- [x] Preserve and replay the owner's exact Topsail Trouble state. Level
      `$000B` / sub-mode `$0008` keeps physical BG1 terrain and repeats bounded
      BG3 `$6C00` rain after rendering. Both 16:10 margins contain rain and the
      native center matches the pre-fix plane pixel-for-pixel.
- [x] Accept the owner's later lower-camera Quick Save at `(636,3848)`.
      Topsail's decoded terrain reaches tile rows 512-540, so the host-only
      world shadow now retains 1,024 vertical rows. Both physical BG1 margins
      fill with exact terrain, the native center matches 4:3 pixel-for-pixel,
      and WRAM/VRAM remain unchanged.
- [x] Accept the owner's terminal-right Topsail Quick Save at `(768,3301)`.
      The vertical-layout policy now masks only complete tiles beyond the
      authentic terminal viewport, removing the hidden guard-metatile rigging
      fragment from the right margin with zero native-center differences.
- [x] Accept the owner's Krow's Nest Quick Save at `(256,288)`. The exact
      `$0009` color-math signature now repeats bounded BG3 together with BG2,
      removing the darker extension bands while preserving a pixel-identical
      4:3 center and unchanged WRAM/VRAM.
- [ ] Record a clean Topsail Trouble fresh-entry route and validate vertical
      traversal at normal speed; the exact-state branch proves the reported
      location but is not whole-stage closure.
- [ ] Record a clean Rattle Battle fresh-entry route and inspect motion at
      normal speed in composite and isolated BG1/BG2/BG3 output. The owner's
      screenshot identifies the defect, but a static image and synthetic
      signature test are not whole-stage acceptance.
- [ ] Validate every Pirate Panic screen transition manually at normal speed,
      including enemy behavior at both edges, before enabling widescreen by
      default.
- [ ] Audit and implement explicit policies for every level archetype, bonus,
      boss, map, Mode-7 screen, vertical room, and special foreground effect.
- [x] Add automatic temporal route checks for raw VRAM fallback, verified
      blank fallback, exact world-terrain identity changes, old-edge seams,
      and placed-object lifetime/render gaps, with private HTML/JSON evidence.
- [ ] Add reference-emulator-aligned widescreen route checkpoints. Automatic
      candidates and source provenance do not certify artistic intent,
      priority, animation, or every screen type without an oracle.
- [x] Reconstruct the supplied lava-stage HDMA role swap structurally rather
      than by level ID: isolate cyclic BG2/BG3 line continuation, re-key and
      decode the alternate BG1 terrain band, preserve its exact native VRAM
      viewport, and fail closed when that second terrain role is unavailable.
- [x] Stabilize the restored lower band after the lava-stage role swap. Once
      the swap is proven, continue bounded BG1 from its authentic native
      scanline below the band and keep BG2 world-keyed, eliminating the
      direction-dependent lava column in both the later supplied Quick Save
      and the earlier hard-left same-level anchor.
- [x] Keep the structural lava role proof across DKC2's camera-reversal phase
      lead. The live alternate terrain phase may run five or six pixels ahead
      of the frame anchor; the bounded detector retains all layers at both
      margins while rejecting larger unproven differences. Straddling
      fine-scroll tiles retain cartridge pixels in the exact native center
      while using world-shadow pixels only beyond its edges.
- [x] Keep the same BG1/BG2 role proof when a later lava composition changes
      BG3 independently between disabled and subscreen states. Repeat only
      the bounded planes enabled on each line and retain the supplied
      balloon/barrel Quick Save as a two-margin temporal regression.
- [ ] Add a clean pre-entry lava-stage route and traverse the complete room at
      normal speed; the supplied Quick Save and deterministic exact-state
      replays do not certify every transition or later camera state.
- [x] Replace the per-scene widescreen policies with structural rules:
      every bounded background repeats its rendered line, colliding 64-column
      allocations are bounded, repeated lines continue at their own proven
      pixel period, rolling BG1/BG2 layers are classified per HDMA band read
      from the cartridge's tables, the second physical terrain layer reads
      the owner's world store through an alias view, and the presented
      viewport is biased and clamped to the authored level extent. The
      fourteen preserved Quick Saves and the three attract demos reproduce
      every accepted result with an exact native center.
- [x] Add `scripts/check_widescreen_state_corpus.py` and the optional
      `DKC2_STATE_CORPUS` CTest so a general rule is validated against every
      preserved state at once instead of one Quick Save at a time.
- [x] Render the biased 4:3 columns of a repeated layer from real VRAM so
      the presented native viewport never depends on a repeat approximation.
      The hard-left lava Quick Save's 46 differing pixels per frame, present
      before the structural rules, are gone; every preserved state has an
      exact interior center at 16:9 and 16:10.
- [x] Explain the blank right BG1 margin of the level `$000F` sub-mode
      `$0009` Quick Save and the blank 16:10 left BG1 margin of the
      terminal-right Topsail state. Both are authored emptiness: the decoded
      map is empty there and equally empty inside the native view nearby.
- [x] Replace the endpoint presentation bias as the default. Holding Right
      away from a wall froze the presented view for the camera's first 43
      pixels and then started scrolling at catch-up speed while the HUD slid
      with the bias. `glide`, the default, keeps the inward view but releases
      it over eight margins of travel; `reflect` (view locked to the camera,
      nearest authored columns mirrored into the unauthored strip), `bars`,
      and `shift` are selectable from the pause menu.
- [x] Assess letting the cartridge stream the wide window itself. The
      column and row streamers decode the same `$0098`/`$17B4` map the host
      already decodes, so the pixels could not improve; the change would
      move work into guest VRAM and save states. Not implemented; the
      measured streamer geometry is recorded in the hardware notes.
- [x] Diagnose and eliminate the known attract-demo margin defects: repeat
      Mainbrace Mayhem's BG3 cloud/lighting layer, retain Rickety Race's proven
      horizontal policy, and reconstruct Parrot Chute Panic's 16-metatile
      row-major BG2 terrain plus cyclic hive backdrops. Early/middle/late
      captures and two deterministic cycles pass. The follow-up 5,875-frame
      five-layer audit established source provenance, but its original
      zero-actionable conclusion was retracted after owner motion footage
      exposed a transient Mainbrace rendered-X phase split. That phase path is
      corrected; two safe transparent fallbacks remain as telemetry.
- [ ] Complete the owner's final normal-speed watch of all three attract demos;
      automated completion and agent-reviewed still frames do not replace
      validation on the owner's display.
- [x] Add a deterministic widescreen evidence-bundle tool that isolates PPU
      layers and correlates camera/game-sprite WRAM, render-consumed OAM,
      VRAM, margin pixels, and runtime-integrity events.
- [x] Assemble and smoke-test an external private Version 10 diagnostic kit
      with fail-loud input recording, paired starting SRAM, packaged trace
      capture tools, and append-only evidence directories.
- [x] Classify and correct the owner-recorded late Pirate Panic BG1 strip:
      phase-align vertical reconstruction to the rendered PPU row and retain
      frame 6,750 as a semantic private regression.
- [x] Classify the later `Pirate Panic - 02` transparent-margin artifacts as
      stale shadow history rather than bad source-map decoding. Clear only
      verified transparent/void terrain cells, retain dynamic game writes, and
      gate private frame 14,400 with a zero-pixel upper-left BG1 assertion.
- [ ] Finish the horizontal source-page/dynamic-layer rollover exposed by
      `Lv01-02` / `Pirate Panic - 02`. The owner accepted frames 12,000 and
      12,300 after source-page correction; 12,900, 13,800, and 15,900 remain
      open, and no known-bad image is retained as a passing regression.
- [x] Coarse-scan the 5,188-frame `bg-01` route and isolate frames 4,500/4,800:
      the live level stream targets BG2 `$7800`, but source prefill is
      hard-coded to BG1 `$7000`.
- [x] Select the decompressed terrain destination from live `$17B6`, prefill
      the matching BG layer, and retain `bg-01` frames 4,500/4,800 as private
      deterministic regressions.
- [ ] Complete the owner's normal-speed `bg-01` validation and audit the
      remaining sparse secondary BG1-margin classifier finding separately
      from the corrected BG2 terrain source.
- [x] Remove Mudhole Marsh's flat 4:3 BG3 bands with a level- and tilemap-
      specific repeat of the authentic rendered `$6C00` forest scanline.
- [x] Reconcile standard rolling-terrain capture, VRAM-write history, lookup,
      and prefill to one PPU source-Y domain for either live BG1/BG2 owner;
      retain `bg-02` as the private vertical-motion regression.
- [x] Capture the 4,850-frame `bg-02` swamp route from preserved starting SRAM
      and replay it end to end with widescreen enabled after the source-Y
      correction; it exits cleanly with zero sequence errors.
- [x] Classify live DKC2 gameplay sub-modes into horizontal, vertical, and
      unaudited square/special screen families; decode horizontal column-major
      and vertical row-major terrain with separate proven address formulas.
- [x] Add the live screen profile to private diagnostic bundles and scope
      automatic margin findings to the terrain owner/repeated layers. A fresh
      swamp frame-2,600 bundle has zero findings and distinguishes authored
      off-screen tiles from stale VRAM.
- [x] Trace the dedicated banana-list renderer, widen its fail-closed traversal
      and clipping boundaries, correct its right-margin OAM ninth-X-bit
      packing, and extend its independent negative-X tile clip through the
      whole left margin. Retain private `bg-02` frame 2,582 and the complete
      4,850-frame route as evidence.
- [x] Correct Pirate Panic's Rambi charge/downward-camera tile-epoch split.
      Align shadow lookup with the same tile-origin unwrap as source prefill
      and retain frames 6,509/6,511/6,512 as private exact regressions.
- [ ] Complete normal-speed owner validation of the corrected Rambi route in
      Version 13, including the same charge and downward/left camera movement.
- [ ] Record a focused vertical-stage route to visually validate the new
      row-major terrain path and retain at least one vertical boundary frame.
- [ ] Reconstruct square and special scroll handlers one family at a time;
      they intentionally remain centered until supported by route evidence.
- [x] Reconstruct Bramble sub-mode `$10` as the first square-scroll family
      using its proven `$60`-byte metatile row, and retain private frame 1,600
      as a two-margin BG1 regression.
- [x] Enable an experimental standard wasp-hive terrain policy from the
      cartridge's normal sub-mode `$03` -> `$B5:B54A` square-scroll dispatch,
      while retaining Parrot Chute Panic's narrow-row exception.
- [ ] Record and visually validate Hornet Hole and Rambi Rumble under the
      experimental hive policy; identify any bounded BG2/BG3 foreground or
      backdrop layers before declaring the family supported.
- [ ] Audit King Zing Sting separately for arena bounds, boss behavior,
      sprites, and cyclic hive layers before accepting its widescreen output.
- [ ] Complete normal-speed owner validation of `bg-02` and capture focused
      before/after frames for any remaining vertical or layer-specific defect.
- [ ] Record Bramble Scramble from entrance to goal with paired starting SRAM
      and complete normal-speed owner validation of terrain, bramble
      foreground/background, objects, both margins, death/restart, and bonus
      transitions. The supplied `bramble-01` fixture ends before the goal.
- [x] Diagnose the owner-observed Swanky 1-3 FPS event as a clean-exit CPU soft
      hang: runtime state `$B4:A4CB` exhausted the interpreter step cap and
      entered an invalid-dispatch cascade.
- [x] Declare Swanky's runtime-selected game-show states and prize helper as
      explicit AOT roots, and preserve the handler's M=0 `PLA; RTL` non-local
      return through the generic paired-call bridge.
- [x] Regenerate DKC2 with all six exact Swanky dispatch entries, build
      optimized Release and trace hosts, retain the final 1,024 runtime
      dispatches in diagnostic reports, and add a validator that rejects the
      original cap/corrupt-dispatch artifact. The full suite is 52/53 with only
      the pre-existing frame-3,309 supplied-ROM reference mismatch.
- [x] Assemble and Shousmoke-test external private Version 12 with the repaired
      optimized hosts, carried ROM/saves/settings/bindings/recordings, fresh
      per-run performance/tier-2/dispatch evidence, and packaged validator.
- [ ] Record a fresh focused Swanky run without rewind or save-state loading,
      pass it through the validator with a native `$B4:A4CB` hit and no
      tier-down/corrupt sequence, and complete the game show at normal speed
      under owner control.
- [ ] Extend deterministic recording to encode host rewind and save-state
      actions, or make regression capture explicitly reject those actions.
      Until then, capture focused routes without rewind or state loads.
- [ ] Use paired before/after diagnostic frames to classify each Pirate Panic
      terrain/object pop-in, beginning with BG1 floors and walls, then retain
      every confirmed flow as a synthetic regression.

## Milestone 4 — Complete game

- [ ] Cover all levels, bonuses, bosses, maps, two-player modes, endings, and
      102% completion.
- [ ] Add deterministic regression recordings and save-state fixtures.

## Milestone 5 — Maintainable port

- [ ] Replace generated functions with readable, tested C by subsystem.
- [ ] Isolate rendering, sound, level loading, objects, players, collisions,
      cameras, menus, maps, and saves.
- [ ] Add optional modern enhancements only after the faithful baseline passes.
      The first present-only enhancement (CRT screen-color modelling) is
      isolated behind an opt-in Raw-by-default setting. Experimental
      widescreen is now also opt-in and remains incomplete outside its audited
      screen policies; geometric CRT shaders and asset replacement remain
      future work.
