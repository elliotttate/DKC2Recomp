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
- [x] Add simultaneous two-player co-op for the game's "2 PLAYER TEAM" mode
      through a source-owned generated-code adapter, with the classic
      alternating behavior preserved as a launcher/overlay/env-selectable
      policy that is byte-for-byte stock outside TEAM mode.
- [x] Keep TEAM Player 2 controllable after landing and cover a new TEAM game
      with separate/concurrent input replay and a classic-policy comparison.
- [x] Render the full Escape ImGui menu on the GDI compatibility backend.
- [x] Enable TEAM follower combat and normal colors; verify P2 roll kills,
      stomp ownership/bounce, contact damage and P1 attacks in the first level.
- [x] Match co-op movement limits to 4:3, 16:10 and 16:9 presentation,
      with both Kong roles and level-edge policies covered by private replay.
- [ ] Complete physical two-player and full-game co-op death/respawn coverage.
- [x] Verify both players mounting Rambi, separate on-foot controls, riding,
      dismount/remount and saved ownership from the reported ledges snapshot.
- [x] Include the on-foot player in banana collection while an animal is ridden;
      verify both roles, saved pickups and overlapping hitboxes in Pirate Panic.
- [x] Preserve the survivor's position through death handoff and verify that
      enemy simulation resumes alongside Player 2 control.
- [x] Verify post-kill movement, DK/regular barrel ownership and release height,
      leader-swap collisions, saved loss, DK-barrel revival and repeat damage
      through bounded Pirate Panic controller replays.
- [x] Add an OpenGL presenter with atomic-GDI fallback, nearest/bilinear
      scaling, and opt-in PSXRecomp-derived CRT screen-color models while
      retaining Raw as the verified default.
- [x] Gate the shared pause menu's Reconstruct controls by actual host support;
      preserve saved SDL shader preferences while showing Win32's effective sampler.
- [x] Add an SDL2 gameplay host and Python generation entry point that compile
      from the same source on Windows, Linux, and macOS; verify the complete
      portable host lifecycle on Windows without removing the Win32 host.
- [x] Add rolling crash reports, privacy-allowlisted diagnostic bundles, and
      Windows minidumps, with clean/fatal/exception integration drills.
- [x] Add a Dear ImGui in-game pause overlay to the OpenGL gameplay hosts,
      including all DKC2 launcher settings, two-player controller setup, an
      explicit Assist Tools/Cheats gate, and five save/load slots.
- [x] Center the pause overlay from ImGui's logical display size on high-DPI
      hosts and allow title-bar repositioning after its first placement.
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

- [x] Add native Windows Game/View/Input dropdowns to both desktop hosts,
      live settings/fullscreen, menu Quick State actions and launcher-independent
      keyboard mapping with short-tap and menu-chord handling.

## Milestone 3 — First level

- [x] Add optional Donkey/Kiddy character presentation, private asset import,
      persisted pause-menu choices, and source-only/saved-state verification.
      Mounted presentation includes per-animal attachments, seated/hanging
      poses, movement transitions and compound animal/rider frame selection.
      These slots retain DKC2 abilities; original Donkey/Kiddy mechanics and
      replacement world-map/cutscene/audio assets are outside this feature.

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

### Optional Kong follow-up, September 5

- [x] Freeze normal and mounted replacement animations on SNES Start pause.
- [x] Bind carried barrels to imported hand positions and retain distinct
      overhead/underhand throws, with character-specific release timing.
- [x] Add grounded Down + Y hand slap / body slam with native collision and
      audio responses, preserving original Diddy/Dixie choices.
- [x] Keep legacy packs and saves readable; re-import for new behavior.
- [x] Prevent inherited helicopter flight in either replacement slot and
      cancel a saved/menu-switched glide without breaking original Dixie.
- [x] Animate both tag participants in either slot and facing direction.
- [x] Synchronize Donkey/Kiddy team pickup/carry/throw poses, with native
      release from the matching hands, both slots/facings and pause support.
- [ ] Adapt partner placement for mixed original/replacement teams.
- [x] Replace Kiddy's post-throw hurt-loop fallback with a held seated
      recovery, verified from the user's reported lava-stage snapshot.
- [ ] Complete character-specific movement constants, ordinary collision
      geometry, Kiddy water skipping and DKC3 team/floor-breaking behavior.
- [ ] Port the reference hand-slap bonus-banana spawning if desired.
- [ ] Extend validation beyond the current mounted corpus to live Rattly and
      Enguarde; DKC3 floor-breaking mechanics remain unported.

      screen policies; CRT television and optional character packs now exist; their full-game
      and cross-platform acceptance remains open.


## DKC3 display/controller parity

- [x] Verify Raw/CRT/Composite/Trinitron preset parity and composition with scaling.
- [x] Port Reconstruct to the native Windows OpenGL presenter with all five
      levels, edge strength, softness and smooth shading controls.
- [x] Add 21:9 geometry, menu/launcher selection and both-player movement checks.
- [x] Add per-player stomp rumble, persistent enablement and test pulses.
- [ ] Complete physical-rumble and full-game 21:9 play acceptance.


## Host feature completion (2026-09-13)

- [x] Full CRT presets, masks and sliders on Windows WGL and SDL.
- [x] Donkey/Kiddy menus, external packs and independent TEAM move state.
- [x] MSU-1 folder, enablement, volume, SFX and missing-track fallback.
- [ ] Full-game replacement-character and audio-transition coverage.
- [ ] Exact PCM positions across save/rewind and seamless soundtrack switching.
- [ ] macOS/Linux builds of the combined milestone and physical audio/rumble acceptance.

## Independent TEAM ropes

- [x] Route rope contact to the grabbing Kong without changing camera leader.
- [x] Synthetic mode/ownership/save-reset checks and real-emitter contracts.
- [x] Player 2 Up/Down animation selection and changing displayed frames.
- [x] Reported Topsail Trouble replay: P2 climb, opposing inputs, save/load,
  jump to neighboring rope and unchanged P1 position during P2-only inputs.
- [x] Reported junction: recover an already-stuck save, animate double-rope
  Up/Down, descend to single rope, exit either side and reload mid-transition.
- [x] Hold Right to the outer net rope, reverse and reach the left outer rope
  without looping the turn animation or moving Player 1.
- [ ] Full-game horizontal-rope, animal and other rope-transition coverage.

## Nearby TEAM partner carrying

- [x] Either player can pick up a nearby on-foot partner with SNES A.
- [x] Preserve pickup/carry ownership and controls through save/load.
- [x] Verify drop/throw and both players walking/jumping afterward.
- [x] Verify Diddy/Dixie and imported Donkey/Kiddy with real input replays.
- [ ] Full-game team-throw terrain/enemy and unusual animal-transition coverage.

## TEAM item input isolation

- [x] Filter actor-local carrying decisions by item owner for either player.
- [x] Recognize ground and airborne carrying states after save/load.
- [x] Validate input-only partner run/pick/jump regression and owner release.
- [ ] Full-game throwable, honey-floor and imported-character acceptance.

## TEAM Select catch-up

- [x] Three-second Select hold, independent ownership and one-shot release latch.
- [x] Unit coverage for timing, pause/load resets, simultaneous requests,
  ineligible states, momentum cancellation and held-item translation.
- [x] Input-only replay for both players, pause, save/load and no native tag.
- [x] Rescue a live requester from rope states, including the reported offscreen
  save; verify normal walking/jumping, save/load and stale lost-bit recovery.
- [x] Rescue into/out of water with each Kong's swim physics; verify the reported
  save and enable the independent swimmer's native facing callback.
- [x] Recover a live waiting partner beside grounded Rambi without changing
  rider/animal ownership; replay the reported save and save/load afterward.
- [ ] Physical two-player and full-game terrain/platform-edge acceptance.

## Independent TEAM goals

- [x] Either unmounted player can trigger a native goal landing and own the ending.
- [x] Preserve grounded/ascending rejection and exact native collision checks.
- [x] Replay both players and P2 mid-approach save/load from the reported save.
- [ ] Mounted goals, other goal variants and full-game ending/reward coverage.


### Shared co-op camera and transport-cannon follow-up (2026-10-18)

- [x] Target the midpoint of both playable Kongs on X/Y with native bounds,
  scrolling and fractional motion retained; synthetic overflow/fallback checks.
- [x] Transport-cannon entry belongs to the touching Kong and leaves the
  other player independent, with entry/firing and occupied-save reload checks.
- [x] Recover a snapshot already caught in the old follower recruitment loop.
- [ ] Full-game camera framing, other cannon variants and simultaneous
  occupancy of separate cannons; imported characters and other platforms.


## Windows Bluetooth gamepads (2026-10-18)

- [x] Replace native gameplay's XInput-only path with SDL GameController.
- [x] Recognize the connected SN30 Pro Bluetooth DInput device in the launcher.
- [x] Persist and honor player device GUIDs, hot-plug without moving the other
  player's assignment, and capture bindings from the assigned controller.
- [x] SDL virtual two-controller input/disconnect/reconnect/lifecycle tests.
- [ ] Owner physical Bluetooth button, rumble and latency acceptance; identical
  device GUIDs, other controller models and connection-mode switching.


## Bonus arrival and fixed tail hooks (2026-10-19)

- [x] Restore both live Kongs at the reported bonus entrance without Select.
- [x] Keep a follower's accepted hook grab attached to that player.
- [x] Permit both players on the same fixed hook with independent releases.
- [x] Restart native cooldown for the last hanger departure, including after
  the first player's cooldown expires; replay single presses in both orders.
- [x] Preserve native team-throw barrel entry and restore both players on the
  reported elevated Gangplank landing platform.
- [x] Replay both bonus entrants, both hook grab orders, shared save/load and
  older waiting-state recovery from copied snapshots.
- [ ] Full-game bonus entrances, animal bonus variants, moving/special hooks,
  imported characters and physical two-player acceptance.

## Independent pit survival (2026-10-19)

- [x] Preserve a live teammate when either independent Kong falls into a pit.
- [x] Keep fallen players lost across held input/save/load; retain native
  last-player loss and DK-barrel revival policy.
- [x] Input-only replay of both leaders/fallers and simultaneous pit falls
  from a copied Gangplank snapshot; synthetic survivor rejection tests.
- [ ] Mounted pit handoff, bonus failure variants and full-game acceptance.

## Rattly hanger and offscreen rescue (2026-10-19)

- [x] Let the on-foot partner use the reported hanger without changing Rattly
  rider ownership; native attachment, release and reload acceptance.
- [x] Preserve grounded/airborne on-foot control below a mounted camera using
  native level bounds; retain actual pit loss.
- [x] Three-second Select rescue beside grounded or airborne Rattly, including
  an older offscreen waiting snapshot and stale live-rider loss evidence.
- [ ] Other animal types, full-game hook parameter variants, mounted rider
  deaths and physical two-player acceptance.


## Shared transport and mounted loss (2026-10-20)

- [x] Carry both living players through transport-cannon entry/firing; replay
  either entrant, bonus arrival, independent landing controls and old-loop reload.
- [x] Share Squawks using native hanging/follower animation and save/load.
- [x] Keep Squawks flight controls on the survivor after the native rider hit;
  replay movement/flapping and reject lost-controller input after reload.
- [x] Release Rattly/palette ownership before pit survivor promotion; replay
  native animal despawn, survivor controls and older orphaned-mount recovery.
- [x] Synthetic checks for both mount owners, Rattly/Rambi pit cleanup and
  both Squawks handoff roles; source-only private acceptance script.
- [ ] Full-game barrel variants ($0174/$0178 input acceptance), reverse-role
  Squawks hit replay, other birds, Rambi pit replay and physical co-op play.


## Manual shared barrels (2026-10-21)

- [x] Include $0140 manual barrels in shared native recruitment and flight.
- [x] Either live controller can fire the shared barrel with jump; keep other
  buttons and on-foot input separate, with classic/lost-player guards.
- [x] Both entrants crossed with both shooters: inside reload, paired launch,
  upper-platform arrival and separate controls after landing/reload.
- [x] Synthetic reversed-leader input sharing and rejection coverage.
- [ ] Full-game rotating/moving barrel variants and physical co-op acceptance.


## Shared barrel passenger air control (2026-10-21)

- [x] Initialize independent native shot state for both Kongs, including
  matching launch locks, momentum, gravity and each port's air steering.
- [x] Preserve neutral paired flight, scripted transport, shared entry/firing,
  palette ownership, native terrain handling and separate controller inputs.
- [x] Synthetic both-role lock/momentum/rejection checks; copied-save left/right
  on both ports, opposing air input and detached flight reload acceptance.
- [x] Both leaders and firing controllers: equal upward apex, rightward reach,
  flight reload, native landing and animated leftward movement afterward.
- [ ] Full-game barrel variants and physical two-player air-control acceptance.


## Boss intro partner safety (2026-10-21)

- [x] Preserve native partner wait/follow through scripted $74/$75 ownership.
- [x] Reproduce and prevent Kleever intro lava sinking from a copied selected
  map save; retain the native DK-barrel arrival and scripted movement.
- [x] Held input, intro reload, native ending positions and both players'
  independent movement/jump acceptance after the intro.
- [x] Synthetic both-role scripted-state/wait/follow/release and lost guards.
- [ ] Other boss/cinematic variants, full Kleever fight and physical co-op play.


## Kleever dropping-hook sharing (2026-10-21)

- [x] Recognize the boss's scripted generic hook identity and route accepted
  follower grabs/entry flight to the correct player and anchor.
- [x] Add a native second collision scan after boss occupied camera setup,
  preserving its animation/lifetime and release cooldown.
- [x] Both unmodified grab orders, native camera flags, shared reload, either
  first release and final single-press release after cooldown.
- [x] Both-role synthetic ownership/collider/cooldown guards and strict adapter
  anchor/idempotence plus real-emitter contract checks.
- [ ] Other dynamic hook variants, full boss fight and physical two-player play.


## Shared manual barrel aiming (2026-10-21)

- [x] Either occupant can rotate the reported manual barrel using native rules.
- [x] Per-axis opposing-input cancellation and unchanged matching-input speed.
- [x] Synthetic both-leader guards, separate flight/on-foot controls and classic
  behavior; copied-save both entry orders, either-port aim/fire and reload.
- [x] Optional private DKC2_COOP_BARREL_ROTATION_STATE CMake integration check.
- [ ] Physical two-controller acceptance and other manual barrel variants.


## Forgiving shared stomp and compiled tuning (2026-10-21)

- [x] Accepted enemy bounce can boost a nearby descending teammate with the
  same vertical impulse, retaining independent steering and native enemy logic.
- [x] Both-owner threshold boundaries, rising/grounded/hanging/lost/absent,
  mount/mode guards, one-shot consumption and reset coverage.
- [x] Copied-save both first-stomper orders, slightly staggered descent,
  separate air control, next-platform arrival and mid-bounce reload.
- [x] Compiled Select duration and X/Y stomp thresholds in config/coop_tuning.cfg;
  copied custom build verifies 750 ms and a 16-by-12-pixel window.
- [x] Optional DKC2_COOP_SHARED_STOMP_STATE private integration check.
- [ ] Physical paired controllers, broader enemy/level variants and play-feel tuning.


## Independent enemy knockback and actor poles (2026-10-21)

- [x] Accepted enemy push ownership survives later collision checks and native
  recoil replaces involuntary AI regroup/carry for a controlled partner.
- [x] Copied neutral/each-port/both-port contact, recoil reload and both-port
  intentional pickup regressions.
- [x] Actor-local type-$2D8 candidate, attachment, reaction and native clamp.
- [x] Both grab orders and simultaneous pole use, independent climbing/limits,
  either/last single-press release, first-airborne-frame reload.
- [x] Synthetic clipping boundaries, separate pole identities, lost/classic
  guards and jump latch; strict adapter and real-emitter coverage.
- [x] External DKC2_COOP_ENEMY_PUSH_STATE / DKC2_COOP_POLE_STATE CMake checks.
- [ ] Broader pole families, private separate/disappearing/moving-pole routes,
  and physical paired-controller acceptance.

Actor-pole animation follow-up completed: private frame traces now require
multiple graphics frames during both climb directions for each port in all
three grab orders, plus eventual native idle after stopping.

- [x] Actor-pole midpoint camera: either one or both players climbing,
  both first-grabber orders, both axes, native fractional math and reload.

- [x] Enemy-bounce midpoint camera: either/both bouncing players, both axes and
  fractional Y; reported single/shared bounce and mid-bounce reload regression.

- [x] Pole survivor after either player's pit death: hanging pose/anchor,
  independent climb/jump, lost/midfall reload, and last-player level loss.

- [x] Safe DK-barrel arrival on reported narrow platform: exact living-player
  destination, no entrance drift, mid-entrance/landed reload and own controls.

- [x] Shared-barrel gravity/terminal-speed ownership, old grounded fall-speed
  repair, both leader/work mappings, equal ascent/descent and flight reload.

- [x] Reported Rambi bonus timeout return: native mounted barrel arrival,
  stable rider, independent partner, palette stability and return/landed reload.
- [ ] Other mounted bonus success/exit variants and physical controller play.

- [x] Animal-only sections: dormant inactive Kong, accepted-damage controller
  handoff, native health/healing/final loss and saved turn ownership.
- [x] Synthetic repeated-hit coverage for Rambi/Rattly/Squawks/Enguarde/Squitter
  and either physical owner, plus source/reset/mode guards.
- [ ] Private later-animal section routes and physical paired-controller play.

- [x] Water-entry held-object isolation: both beetle carriers, swimmer/entry
  reload, native carrier water drop and deliberate dry throw; synthetic
  beetle/barrel/crate categories with mode and cleanup behavior preserved.

- [x] Preserve native thorn hurt/death sequences for either simultaneous player,
  target the touching actor and retain survivor controls; copied checkpoint
  reload and last-player loss verified. Other thorn layouts await private playtesting.

- [x] Shared stomp from descending barrel-flight state: either collision owner
  and shooter, equal lift, separate steering, pre-impact and mid-bounce reload.

- [x] Kudgel ground shake: independent grounded eligibility, native stun for
  both Kongs, jumping-player exclusion, normal shared timer and recovery.

- [x] Honey-floor ownership: either toucher, dry teammate movement, both stuck
  independently, own escape/repeated jumps and mid-jump reload.

- [x] Sticky-wall reaction ownership: either toucher, stable hang, own wall
  jump, both sharing a wall and exact mid-jump reload; honey-floor regression.

- [x] Honey movement midpoint camera: all honey states synthetically, reported
  opposite walls, either/both live wall jumps, both axes and snapshot reload.

- [x] Compiled developer.cfg ENABLED gate and configurable F8 normal-level
  completion, native progress/fade, Windows/SDL shortcuts and disabled tests.
- [x] Copied normal-level progress/map exit and actual Windows F8 event check.
- [x] Developer boss completion/map exit and bonus reward/parent return.
- [ ] Verify developer skips through final-boss ending cinematics.

- [x] Throwing a partner into a hanger: release ground/air carrier waits,
  preserve passenger attachment, recover saved waits and retain ordinary throws.

- [x] Select rescue on sticky walls: either player leaves/joins the wall,
  shares a stable anchor, keeps independent jump controls and survives reload.

- [x] Pit loss with honey-floor/sticky-wall survivors: either faller, survivor
  controls, persistent lost state and consistent DK-barrel revival policy.

- [x] Squitter rider pit loss: either rider, both platform edges, palette/mount
  cleanup, survivor controls, mid-fall reload and older affected-save repair.

- [x] Center actual co-op viewport on the player midpoint, preserving native
  bounds/smoothing/streaming; verify Squitter/hanger motion and camera reload.

- [x] Pole grab after revisiting/respawning: expire stale jump anchors, restore
  live active-player loss metadata, preserve dead teammate and own controls.

- [x] Rollercoaster independent jumps: either port, simultaneous/staggered
  jumps, own seats, exact midair reload, either rider hurt, moving survivor
  and control handoff, cart collision and native final-loss/Game Over.

- [x] Coaster stomps: native upward bounce for either rider, configured
  shared bounce, independent movement contexts and safe cart departure.

- [x] Later coaster pit: either player can cross to a new cart while the
  non-jumper stays lost; preserve team life, fixed input ownership and
  mid-gap reload. Both missing retains native final loss and Game Over.

- [x] Lone coaster survivor boards the next cart using native foot contact,
  immediate accepted seating, persistent teammate loss and midair reload.

- [x] Paired next-cart landing tolerance: either empty seat, native offsets,
  independent next jump, staggered arrivals, missed-pit exclusion and reload.

- [x] Detached coaster shared stomp: either owner/partner attachment state,
  native $68 bounce, configured threshold rejection and next-cart landing.

- [x] Early coaster-to-cannon entry: prevent falling-cart reclaim and stale
  global collision context; verify native shot, landing, reload and real miss.

- [x] Scripted coaster exit: rider stays with cart, native end fade reaches
  goal area, normal goal finishes to map, exact mid-exit reload.

- [x] Ceiling-thorn victim ownership: either player takes its own hurt/death,
  survivor controls, exact death reload, dead Select rejection and final loss.

- Shared mode-02 kart: first-boarder Jump moves the native vehicle with
  both players seated; passenger jump-out disabled. Copied room-$0F input
  acceptance and exact mid-jump reload covered. Full kart-race completion
  and every track/hazard arrangement remain unverified.

- Horizontal thrown hook contact and shared camera-framed hooks: native
  overlap, direct attachment, per-occupant release and copied-save checks.
  Every hook arrangement and a full level traversal remain unverified.

- [x] Successive hooks after a sideways throw: recover saved interaction
  masks, share next hook, traverse and share third hook in either order,
  preserve stationary teammate and exact mid-flight reload.

- Cat O' Nine Tails: only actual contacted player joins spin; retain
  independent teammate and camera midpoint. Full level completion and
  simultaneous capture by multiple cats remain unverified.

### Packaging validation follow-up (2026-10-29)
- [x] Resolve cross-edition PowerShell packaging failures; synthetic snapshot and release checks pass, complete CTest 58/58.

- [x] Repair blank-SRAM item replay setup; both player slots pass partner
  Run/Pick, Jump, ground/air snapshot loading and intentional owner release.
