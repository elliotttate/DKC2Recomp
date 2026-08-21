# Changelog

## Unreleased

- Extended Web Woods' BG3 fog and Gusty Glade's BG3 windblown-leaf layer
  across both 16:9 margins. The level- and tilemap-specific policy repeats the
  completed native `$5C00` scanline after normal priority/color processing;
  other forest BG3 layouts remain clamped pending separate audits.
- Fixed a widescreen-only object-lifecycle regression in Ghostly Grove where
  a treasure chest could disappear before completing its second throw.
  Placement activation remains widened, while explicit live-object and
  deactivation call sites keep native bounds; table-offset bits are no longer
  incorrectly treated as lifecycle context.
- Restored DKC2's shared `$44` scratch word after widened banana rendering so
  presenter-only bounds cannot leak into subsequent cartridge gameplay logic.
- Restored navigation to the pre-boot Assist Tools and Credits pages after the
  expanded launcher enum outgrew the shared model's older Mods-only bound.
  Regression coverage now opens both pages instead of checking labels alone.
- Reject DKC2 save states older than shared format v7 before deserialization.
  Version 13/early Version 14 v6 slots contain an obsolete raw host-CPU
  continuation layout and could otherwise resume at an invalid address. The
  original file is left untouched and the host reports a normal load failure.
- Added a private `DKC2VisibleDebugger.exe` developer target with a persistent
  live ImGui evidence panel, composite/BG1/BG2/BG3/OBJ isolation, exact
  pause/single-step, save/load shortcuts, and current-frame PPM/JSON export.
  Optional margin provenance distinguishes live capture, decompressed-map
  prefill, periodic fold, verified blank, unsafe raw-VRAM fallback, and
  deliberate native-edge repetition. The normal player target is unchanged.
- Created the external Private Version 14 diagnostic handoff from the refreshed
  framework bases. Private packaging now carries `SDL2.dll` when the
  portable host requires it and preserves recording-specific `.start.sav`
  state files alongside `.start.srm` and session metadata.
- Refreshed the source submodules to authoritative `snesrecomp@fe6045c` and
  `recomp-ui@99eba41`. DKC2 retains the supported SDL2 compatibility backend
  until its custom portable presenter is migrated to SDL3.
- Published tested integration revisions `snesrecomp@884abcb` and
  `recomp-ui@7c35690` on the project forks so clean parent checkouts reproduce
  save-state compatibility, debugger provenance, and launcher-page navigation.
- Reconciled current framework APIs: staged SDL2 beside both Windows gameplay
  executables, supplied the launcher console profile, linked the expanded
  interpreter-bridge test dependencies, and adopted valid rewind defaults.
- Made Windows snapshot hashing independent of the optional `Get-FileHash`
  cmdlet by using the .NET SHA-256 implementation.
- Recorded current SMW, Zelda, and DKC1 recomp references, including the
  DKC1 Visible Widescreen Debugger evidence model, without copying source or
  game assets.

- Added an opt-in experimental widescreen path for ordinary wasp-hive rooms.
  The cartridge's sub-mode `$03` normally calls the same `$60`-byte-row square
  scroller already reconstructed for Bramble, while Parrot Chute Panic keeps
  its proven narrow-row exception. Hornet Hole, Rambi Rumble, and King Zing
  remain visually unverified and may still expose layer-specific gaps.
- Fixed Pirate Panic's Rambi charge/downward-camera widescreen corruption.
  At frames 6,509, 6,511, and 6,512 the fine PPU Y value and tile-aligned
  terrain prefill selected opposite 1,024-pixel scroll epochs at the exact
  unwrap tie, causing 1,120 verified-blank margin samples per frame. Terrain
  shadow Y now unwraps the shared 8-pixel tile origin and restores the fine
  phase afterward. The exact replay has no corresponding blank bursts.
- Retained an additional terrain source guard tile at both horizontal margins.
  This independently removes the two-pixel fine-X miss seen at Pirate Panic
  frame 6,404; it was not the cause of the larger owner-visible Rambi defect.
- Changed the route auditor to retain verified-blank bursts of at least 64
  samples as actionable findings even while the camera is moving. Small,
  bounded verified-transparent substitutions remain safe observations.

- Fixed a transient old-4:3-boundary split in widened terrain while DKC2's
  WRAM camera led the horizontal scroll value actually latched by the PPU.
  Terrain shadow capture, margin lookup, and decompressed-map prefill now use
  the same rendered X phase. This principally affects direction changes and
  vertical climbs in Mainbrace Mayhem's attract demo.
- Restored native-boundary seam candidates to the widescreen audit report.
  Proven source ownership alone is not enough to dismiss a seam: it proves
  where pixels came from, not that their presentation phase is correct.

- Fixed attract-demo widescreen terrain rows selecting different 1024-pixel
  scroll epochs inside one viewport. The top rendered row is now unwrapped
  once, every following row advances continuously, and every proven rolling
  layout selects its decompressed source page around `cameraY-$0100`.
  Margin tiles are refreshed from the exact decoded source while newer game
  writes retain priority. A packaged 5,875-frame, five-layer Version 13 audit
  now reports zero actionable findings; two verified-transparent fallbacks
  remain recorded as safe provenance observations.
- Reduced route-auditor false positives. Object lifetime claims now require
  consecutive-frame traces, old-boundary seams must persist and lack a proven
  wide source, and temporal terrain comparisons are skipped when every margin
  cell has an authoritative same-frame source. Safe transparent fallbacks are
  reported separately from actionable defects.

- Fixed the widescreen route auditor's binary PPM reader so a first pixel byte
  equal to ASCII whitespace is retained as image data. Existing completed
  captures can now be reanalyzed without replay, and genuinely missing or
  malformed samples become explicit integrity findings instead of aborting the
  complete report.

- Added a deterministic route-scale widescreen auditor. It replays isolated
  composite/BG/OBJ layers, records per-frame camera/PPU/object state, accounts
  exact shadow/fold/blank/raw-VRAM margin provenance, compares world terrain
  entries as they cross the native boundary, scores old-edge seams, and flags
  placed-object lifetime/render gaps. Reports are private HTML/JSON evidence
  bundles and can be reanalyzed without rerunning the game. A sampled
  three-demo attract audit found zero raw VRAM fallbacks; it retained two
  verified-blank intervals and ranked terrain/seam/object candidates for the
  next fixes.
- Extended all three attract demos as true 16:9 gameplay. Mainbrace Mayhem now
  repeats its authentic BG3 cloud/lighting overlay across the widened BG1
  scene, Rickety Race retains the proven horizontal policy, and Parrot Chute
  Panic gains its disassembly-confirmed 512-pixel/16-metatile row-major BG2
  terrain decoder with cyclic BG1/BG3 hive backdrops. Early/middle/late
  captures render edge-to-edge; a 12,000-frame run completes two cycles with
  zero sequence errors or clipped audio. Final owner motion validation remains
  open. The full configured matrix remains 55/56 with only the pre-existing
  frame-3,309 sprite-reference hash mismatch.
- Fixed collectible bananas disappearing in the left widescreen margin. The
  dedicated banana renderer retained a separate native 15-pixel negative-X
  clip even after its list and formation bounds were widened. That clip now
  expands to 58 pixels only after terrain readiness, exposing the complete
  43-pixel added margin while preserving the native constant in 4:3. Across
  the owner's 4,850-frame `bg-02` marsh replay, left-margin banana OAM samples
  increased from 17 at X=-14..-1 to 63 at X=-43..-1; the 318 right-margin
  samples and their X=256..298 range were unchanged.
- Added a durable semantic symbol database instead of relying on generated
  `func_...`/`CODE_...` names. The source TOML files cover 20 researched
  functions, 19 WRAM objects, and 13 sprite-record fields while automatically
  indexing all 3,314 CFG boundaries. Exact-address CFG synchronization,
  collision/range/overlap validation, generated diagnostic constants, a
  lookup tool, readable reference documentation, and stale-output tests are
  included. Private regeneration retains 3,475 exact AOT variants and the same
  two intentional LLE-only fault variants. Version 13 remains a diagnostic
  checkpoint because owner-observed attract-demo margin glitches are open.
- Added a fail-closed private-symbol promotion tool and synthetic tests. It
  promoted ten residual `CODE_...` CFG functions to context-qualified names,
  regenerated the 3,314-function declaration index and private generated C,
  and intentionally rejected raw-address/data/ambiguous matches.
- Partially corrected horizontal terrain-map page selection at a 256-pixel
  camera-Y rollover. Selecting the source page around camera Y minus `$0100`
  improves native agreement from 277/957 to 924/957 cells; the owner accepted
  route samples 12,000 and 12,300. Samples 12,900, 13,800, and 15,900 remain
  open and are not promoted to image-hash regressions.
- Enabled authentic Pirate Panic ship-rigging BG3 margin rendering only when
  level-effects bit 0, enabled BG3, and the 64-column `$7800` tilemap agree.
  The native 256-pixel region remains pixel-identical in deterministic checks;
  owner motion validation remains pending.
- Fixed recurring Pirate Panic BG1 fragments in the expanded margins. The
  exact decompressed-map cells at the affected frame are transparent, but an
  earlier VRAM capture could remain in their world-keyed shadow history. The
  terrain adapter now clears only verified transparent/void source cells each
  frame and preserves current dynamic game writes. The private `Pirate Panic -
  02` regression at frame 14,400 checks the previously contaminated upper-left
  BG1 margin and passes with zero non-backdrop pixels.
- Added the first proven square-scroller widescreen policy for Bramble
  Scramble. Its game sub-mode `$10` decodes a 48-metatile/`$60`-byte row
  layout instead of borrowing the horizontal or vertical formula. Private
  frame 1,600 matches 954/957 native BG1 cells, renders non-empty terrain in
  both margins, and is retained as a deterministic route regression.
- Replayed the owner's 3,134-frame `bramble-01` recording with paired SRAM.
  It completes with zero input-sequence/runtime errors and records 7,991 OAM
  margin samples across 1,100 frames. Composite checks at frames 1,600, 2,400,
  2,800, and 3,000 show continuous foreground and background art. The route
  ends while level `$002E` is active, so goal-transition acceptance remains
  open.
- Requested one-buffer presentation synchronization from both visible OpenGL
  gameplay hosts. Win32 now loads `wglSwapIntervalEXT` after creating its
  context, SDL reports whether `SDL_GL_SetSwapInterval(1)` succeeded, and both
  record the resulting VSync status in diagnostics. Hidden automation leaves
  VSync off so a driver swap wait cannot stall unattended tests; GDI remains
  compositor-managed. Owner verification is still required before declaring
  the reported tearing resolved.
- Replayed fresh current-code `bg-01` and `bg-02` widescreen sweeps with zero
  input-sequence errors. Layer isolation at `bg-02` frame 2,350 showed that an
  apparent lower-left black block is authored transparent BG2 terrain over
  the legitimate dark BG3 fade, with no west-shadow misses or automatic
  findings, so no replacement tiles were fabricated. The short Bramble
  Scramble sample in `bg-01` remains deliberately pillarboxed pending a
  focused entrance-to-goal recording and screen-family implementation.
- Fixed collectible bananas disappearing from the added right-hand viewport.
  DKC2 uses a dedicated banana-list renderer rather than the common placed-
  object path: its activation and clipping constants now expand only after
  terrain readiness, and its SNES OAM encoder mirrors widened coordinate bit 8
  into the legacy sign-bit input used to produce OAM's ninth X bit. On the
  owner-recorded `bg-02` frame 2,582, the same banana tiles move from the
  erroneous wrapped X=35 to the correct right-margin X=291.
- Extended the widescreen evidence tool with route-wide OAM-margin collection
  and an exact generated-function watch. This proved that the statically
  recompiled banana routine executed with widened bounds before the separate
  OAM high-X packing defect was corrected.
- Added a fail-closed DKC2 screen-profile classifier derived from live WRAM
  and PPU state. Diagnostic bundles now report level type, tileset/layout,
  gameplay/NMI sub-modes, PPU/VRAM setup, the live terrain owner, and whether
  the decompressed map is horizontal-column-major or vertical-row-major.
- Generalized exact terrain decoding for the two proven rolling map layouts.
  Horizontal and vertical gameplay sub-modes use their corresponding map
  address formula; unaudited square/special screens remain centered instead
  of exposing guessed margins. Existing `bg-01` and `bg-02` routes retain
  identical final hashes and complete with zero sequence errors.
- Reduced false-positive diagnostic findings by requiring margin pixels only
  from the classified terrain owner and explicitly repeated BG3. A fresh
  Mudhole Marsh composite/BG2/BG3 capture reports zero findings; the thin
  edge tiles agree with both live decompressed WRAM and cartridge VRAM, so
  they are authored off-screen data rather than stale VRAM.
- Corrected the rolling-terrain shadow's vertical coordinate domain for every
  DKC2 screen whose live `$17B6` destination identifies BG1 or BG2 as the
  terrain owner. Native tilemap captures, VRAM-write history, and exact WRAM
  prefill now share the rendered PPU source phase instead of mixing camera Y
  with rows staged one 256-pixel page above it. The owner-recorded `bg-02`
  route exposed the mismatch in Mudhole Marsh; unusual screen types still fail
  closed and require separate policies.
- Scanned the owner's 5,188-frame `bg-01` route and isolated the first clear
  later-level failure at frames 4,500 and 4,800. The level streamer targets
  BG2 tilemap `$7800`, while the prior source prefill assumed BG1 `$7000`.
  Widescreen terrain reconstruction now selects BG1 or BG2 by matching live
  `$17B6` against the enabled layer tilemap bases. Focused replays fill the
  missing BG2 margins and remove the wrong colored BG1 terrain cells at both
  frames; normal-speed owner validation remains pending.
- Added a screen-specific Mudhole Marsh BG3 policy after owner testing showed
  the terrain repair still left 4:3-colored backdrop bands. The `$6C00` 2bpp
  forest backdrop now repeats its authentic rendered scanline into both
  margins; menus and unaudited BG3 screens remain clamped.
- Diagnosed the owner-observed 1-3 FPS slowdown in Swanky's Bonus Bonanza as a
  native CPU soft hang rather than a process crash or presenter slowdown. The
  Version 11 run exited cleanly, but one call to runtime-selected state
  `$B4:A4CB` exhausted the 2,000,000-instruction interpreter safety cap and
  entered an invalid-dispatch cascade.
- Declared Swanky's runtime-selected game-show states and prize helper as
  explicit AOT entry roots. The shared interpreter call bridge now propagates
  a guest non-local return when a 16-bit `PLA` followed by `RTL` consumes the
  paired JSR frame and an outer JSL frame, instead of incorrectly resuming the
  compiled caller. The focused shared-bridge regression passes 62/62 checks.
- Regenerated all 13 DKC2 banks successfully: 3,325 roots produce 3,475 exact
  AOT variants and the two deliberate original-game fault variants remain LLE.
  The generated dispatch table contains all six exact Swanky entries, and both
  optimized Release and trace builds succeed.
- Added the shared runtime's last 1,024 indirect-dispatch events to DKC2's
  rolling diagnostic report and a focused Swanky validator. Its synthetic test
  passes, it requires native M0X0 dispatch to `$B4:A4CB`, and it correctly
  rejects the original Version 11 cap/corrupt-dispatch artifact. The complete
  suite passes 52/53 tests; the sole failure is the pre-existing frame-3,309
  supplied-ROM sprite-reference mismatch. A fresh owner run remains pending.
- Documented that controller recordings contain forward emulated-frame input,
  not host rewind or save-state load operations. A route that uses either host
  action is useful evidence from the original run but is not an exact
  standalone replay fixture.
- Added fail-loud, cross-platform input recording shared by the Win32 and SDL
  hosts. Recording begins before the frame loop, marks the window title,
  writes byte-stable LF input lines, and surfaces open/write/flush/close
  failures instead of silently losing a test run.
- Added an external private diagnostic-version packager and self-contained
  record, diagnose, and verification helpers. Each recording preserves its
  starting SRAM, and the package keeps the verified ROM, saves, recordings,
  trace captures, and generated evidence outside Git. Existing recordings now
  carry their paired starting SRAM and session metadata into later private
  versions when those files are present. The packager now also carries
  `launcher.cfg` and `keybinds.ini`; its recorder refuses every named-evidence
  collision and requires freshly written performance, tier-2, and last-run
  reports instead of accepting stale rolling files.
- Added the first experimental DKC2 16:9 implementation. It renders a 342x224
  PPU surface (43 extra source pixels per side), presents it with the SNES
  7:6 pixel aspect ratio, and preserves authentic 256x224 output when off.
- Expanded DKC2's common placement/despawn radius and both shared world-sprite
  render bounds for the wider view. A fail-closed, idempotent postprocessor
  reapplies these source-owned adaptations after every private generation.
- Extended horizontally streamable 64-column level tilemaps while centering
  and freshly clearing bounded 32-column menus/rooms, preventing repeated or
  stale VRAM content on the title and Pirate Panic entrance screen.
- Fixed moving Pirate Panic margins that still exposed recycled 64-column
  VRAM pages. Gameplay BG1/BG2 now use world-keyed shadow tilemaps with a
  bounded unknown-cell fallback; the intentionally cyclic BG2 sky/ocean
  repeats from the authentic rendered scanline, and unsafe BG3 remains
  centered.
- Fixed the follow-up BG1 association error found in user footage. DKC2's
  decompressed level map is 256 pixels behind its camera/object coordinates;
  the host now reconstructs exact 8x8 margin tiles from WRAM with that offset,
  preserves the cartridge's vertical column rotation, and stops at the one
  valid guard metatile beyond each level's horizontal camera bound. Pixels
  beyond that guard use a live verified-transparent character instead of
  unrelated WRAM.
- Fixed a late Pirate Panic BG1 strip reproduced by the owner's complete route.
  Vertical reconstruction now derives both its shadow key and source row from
  the PPU phase actually rendered, rather than mixing it with a one-pixel-newer
  WRAM camera value and accidentally wrapping `-1` into `+31` tile rows.
- Added a source-clean NMI-boundary regression and an optional private frame
  6,750 route gate. The latter asserts that the logical upper-left BG1 margin
  is empty while the authentic center remains active.
- Made widened object activation/render bounds fail closed until exact BG1
  terrain reconstruction succeeds for the current frame.
- Added a persistent **Widescreen 16:9** control to the pre-boot launcher and
  a live **Widescreen (16:9, experimental)** control to the in-game Settings
  page. The original 4:3 mode remains the default.
- Added a TCP screenshot helper with stable 342x224 capture, PPU-state reports,
  layer-isolation support, optional route-wide OAM margin reporting, and
  matching historical WRAM/VRAM snapshot export for tile-stream calibration.
- Added a deterministic widescreen diagnosis bundle tool. It repeats the same
  route frame as composite/BG1/BG2/BG3/OBJ captures, decodes DKC2 camera and
  game-sprite WRAM fields, inventories render-consumed OAM, measures real
  non-backdrop margin pixels, preserves WRAM/VRAM and logs privately, and
  classifies the first failing load/activation/render/presentation stage.
- Fixed trace-enabled MSVC builds in the pinned SNESrecomp integration by
  declaring the APU lock hooks before the debug server's audio commands.
- Changed the pre-boot and gameplay window title to
  `DKC2 Recomp Alpha Pre-Release`; the live FPS, CPU telemetry, and Assist
  disclosure remain appended when active.
- Added full in-game control remapping to the pause overlay. Player 1 and
  Player 2 can change all 12 SNES keyboard/controller bindings, and the
  Assist tab can change Rewind, Fast-forward, Save State, and Load State.
  Captures apply live, persist through the shared launcher settings, wait for
  controller release to prevent accidental self-binding, and can be cancelled
  with Escape. Fixed overlay/performance recovery shortcuts remain visible.
- Added real pre-boot keyboard and standard-controller remapping for every
  Player 1/2 SNES action. The same persisted mappings are consumed by both
  Windows and SDL hosts rather than writing an unused bind file.
- Added compact Rewind/Fast-forward controls to each Controller Configure page
  and configurable Rewind, Fast-forward, Save State, and Load State keyboard
  and controller bindings to the Assist Tools page. Assist actions remain
  inert unless the Assist Tools/Cheats gate is enabled.
- Fixed a pre-boot dashboard regression where three right-side navigation
  buttons wrapped to a second row, shortened the content area, and placed an
  ImGui scrollbar beside the box-art card. The shared launcher now lays out
  those buttons in local window coordinates and restores both cursor axes.
- Added a source-only personal-test-version helper that verifies the supported
  ROM hash and copies a normal `Version NN`, ROM, saves, and optional launcher
  settings to an external private folder. Normal repository versions remain
  ROM/save-free and immutable.
- Added an in-game Dear ImGui pause overlay to the Windows OpenGL and portable
  SDL/OpenGL hosts, with Resume, complete launcher-equivalent Settings,
  two-player controller setup, Assist Tools, Credits, and Quit pages.
- Added optional Assist Tools and Credits sections to the shared pre-boot
  recomp-ui launcher.
- Gated rewind, fast-forward, and five file-state slots behind a persistent
  opt-in Assist Tools/Cheats setting. The overlay selects Slots 1–5, stores
  them as `saves/dkc2s0.sav` through `saves/dkc2s4.sav`, and discloses
  `(Assist Tools: On)` in the window title.
- Made volume, screen model, texture filtering, controller source/deadzone,
  and Assist Tools changes apply during a run. Display mode, renderer, window
  scale, audio enable/sample rate, and skip-launcher changes persist for the
  next launch. Restore Defaults resets the complete shared settings value.
- Added a host-neutral overlay action model and hidden private-ROM lifecycle
  coverage that opens, renders, pauses, closes, and resumes the overlay on
  both supported OpenGL hosts. Atomic GDI remains an overlay-free fallback,
  while launcher-enabled Assist shortcuts continue to work there.
- Rebased the DKC2 SNESrecomp integration onto upstream `1d0f2e0`, published
  immutable dated backup branches first, and moved the submodule fetch URL to
  `Nicktendonick/snesrecomp` so the rebased DKC2 commits remain available.
- Restored MSVC builds after the rebase by making generated RAM-routine guard
  ownership explicit and adding the current Cx4 dependency to the synthetic
  interpreter-bridge harness. All three DKC2 hosts build; 44/45 configured
  tests pass, with the former frame-3,309 reference hash intentionally left
  failing pending renewed event-aligned comparison.
- Added deterministic two-player input recording/replay, a synthetic parser
  test, Pirate Panic route telemetry, and a private entrance-to-goal gate that
  rejects clipped audio, interpreter caps, and unresolved dispatches.
- Captured the first private 11,275-frame Pirate Panic route. It reaches the
  normal goal path but exposes an unresolved `$BA:B33F` dispatch at frame
  5,522, so the route remains a documented failing fixture rather than a
  completed correctness milestone.
- Added append-only `Version NN` Windows snapshots. The packager automatically
  chooses the next number, refuses overwrites, records executable hashes and
  source provenance, and excludes private/runtime artifacts.
- Standardized `build-snesrecomp/` as the routine Windows compiler workspace
  and documented every legacy build tree, generated/private output, and
  numbered test-handoff location in `docs/BUILD_HYGIENE.md`.
- Added a confirmed Restore Defaults button to the shared launcher Settings
  footer. DKC2 resets its complete launcher configuration from the same
  authoritative defaults used on first run, without changing the selected ROM,
  SRAM, or save states.
- Added rolling per-launch crash reports, privacy-allowlisted automatic
  diagnostic bundles, Windows minidumps, and clean/fatal/exception integration
  drills for both playable hosts. Mod-aware save isolation remains deferred
  until an actual versioned mod manifest and loader exist.
- Renamed new slot-0 state writes from `saves/dkc20.sav` to
  `saves/dkc2s0.sav` while retaining load-only compatibility with the former
  filename.
- Added a shared SDL2 gameplay host for Windows, Linux, and macOS source builds
  with accelerated video, keyboard/two-controller input, exact-rate queued
  audio, SRAM, states, rewind/fast-forward, filters, and FPS title reporting.
- Added a portable Python private-source generator, a pinned SDL 2.30.9 CMake
  fallback, host-neutral launcher/cache and viewport modules, and synthetic
  portable-tooling/viewport tests.
- Fixed recomp-ui's GCC-only compiler barrier to use the MSVC equivalent when
  building the same Dear ImGui launcher source on Windows.
- Kept Linux/macOS support explicitly provisional until native build,
  controller/audio, visible-attract, and packaging acceptance is completed.
- Added an OpenGL gameplay presenter with automatic atomic-GDI fallback and
  launcher-selectable nearest/bilinear scaling.
- Added opt-in Raw/CRT/Composite/Trinitron screen-color models by adapting the
  pinned PSXRecomp lookup-table implementation; Raw remains the byte-exact
  default and the selected model works through either presenter.
- Added synthetic raw/CRT/filter-metadata coverage plus hidden private-ROM
  OpenGL-required and forced-GDI CRT smoke gates, with complete upstream
  revision and license provenance under `third_party/`.
- Reached structural static 65816 closure. The current generated profile has
  3,325 roots, 3,475 exact AOT variants, and only the two deliberate
  original-game fault variants on LLE.
- Modeled DKC2's WRAM-clearing stack reset as a proven static continuation and
  added a generic `noreturn_jsr` contract for two documented original-game
  crash calls into data while preserving their real interpreter fault path.
- Added Python and Rust parser/analysis/code-generation regressions; 357 shared
  Python tests, 44 Rust tests, and all 32 optimized DKC2 tests pass.
- Rebased continuing development on the public `mstan/DKC2Recomp` 0.0.1
  source baseline and its exact `snesrecomp`/`recomp-ui` submodule pins;
  preserved the former working trees on named backup branches.
- Added an FPS readout to the game-window title and opt-in per-phase
  main-thread telemetry in `performance.log`.
- Enabled speed optimization for Release host builds (`-O3` for GCC/Clang,
  `/O2` for MSVC) and added an optional private Windows icon build input.
- Added synthetic FPS and telemetry tests and repeated the complete 32-test
  suite, including the two-cycle attract gate, on the optimized build.
- Exposed Player 2 in the shared ImGui launcher and routed selected keyboard
  or XInput sources to both native SNES controller ports, with persistent
  source and deadzone settings.
- Restored inherited Windows permissions after private source generation so
  atomic output replacement cannot leave `generated/` inaccessible to the
  interactive user.

## 0.0.1 - 2026-07-19

- Published the first playable Windows preview with the shared Dear ImGui
  `recomp-ui` launcher, North American retail cover art, and strict
  external-ROM verification.
- Shows the native host's Player 1 keyboard/controller slot in the launcher;
  unsupported per-button rebinding remains hidden.
- Added resizable 4:3 presentation, exact-rate stereo audio, persistent SRAM
  with backup rotation, rewind, fast-forward, keyboard controls, and XInput.
- Promoted 3,425 of 3,467 exact DKC2 CPU-mode variants to static C (98.79%);
  the remaining 42 variants use the shared interpreter fallback.
- Fixed direct-page bank selection, indexed address carry, computed-return
  ownership, BRR decoding, and echo FIR semantics in the shared framework.
- Completed a 12,000-frame/two-attract-cycle DKC2 soak with zero sequence
  errors, inspected clean title and in-level frames, and active non-clipping
  audio.
- Fixed the Pirate Panic death/restart black screen by making static MVN/MVP
  transfers preserve 65816 count, index wrapping, data-bank, per-byte timing,
  and legal frame-deadline continuation semantics.
- Added F5/F9 slot-0 save states and deterministic headless state/machine
  capture inputs used to reproduce and close the death regression.
- Completed 12,000-frame attract soaks and manual gameplay checks on the same
  framework revision for DKC2, Super Mario World, A Link to the Past, Mega Man
  X, and Super Metroid.
- Fixed generated monolithic translation units to declare every referenced
  cross-bank exact variant, matching the existing sharded-unit contract.
- Added reproducible Rust-analyzer generation and a release packager that
  refuses ROMs, generated code, saves, screenshots, and audio captures.
