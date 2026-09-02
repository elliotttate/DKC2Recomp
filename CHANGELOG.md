# Changelog

## Unreleased

- Fixed Slime Climb's margins showing the wrong terrain: pillars, rafts,
  and their reflections repeating from the native line instead of
  continuing the level. The stage leaves BG1's vertical scroll register at
  a value off the camera at frame start and lets its HDMA write the
  camera row on every rendered line, so the host keyed the terrain and
  classified every band from a phase nothing displays. The terrain phase
  is now taken from the HDMA band at the camera phase covering the most
  lines when the frame-start register is not at that phase.
- Fixed bright blue margins on Barrel Bayou's level intro. The intro is a
  static picture on BG1's own map with no terrain stream, so the wide
  layer is clamped and the PPU filled the margins with the backdrop color,
  which that scene sets to pure blue. While the world is unproven the
  margins are now painted black, as a bounded screen's are.
- Fixed the margin beside a player-held wall opening a cave pocket wider
  than the console ever shows it (the crystal mine shaft at camera 448,
  where Squawks meets the shaft wall and the map beyond is empty). The
  structural wall rule continued the rock only where the edge metatile was
  full; the pocket's boundary rows, whose edge metatiles are partial, now
  mirror the authored terrain across the wall line, as the reflect policy
  does at a level's own wall.
- Fixed the foreground rocks of the lava stages (Red-Hot Ride, Hot-Head
  Hop) stopping at the 4:3 edges. The rocks and the far lava spikes live on
  one static 64-column map that HDMA swaps between BG1 and BG2, and a
  non-terrain band used to repeat its native scanline at 256 pixels, which
  showed the wrong half of a 512-pixel plane beside the view. A band whose
  map is a static, wrap-authored 64-column plane now continues into the
  margins as the map's own hardware wrap; the ship hold's 96-pixel cabin
  wall keeps its proven-period repeat.
- Fixed steam columns appearing over solid rock beside the view in Red-Hot
  Ride, and geysers vanishing from the margins as soon as they left the 4:3
  view. The steam is a bounded 32x32 BG3 the cartridge draws only for
  geysers inside its view; a 256-pixel map wraps a geyser standing just
  outside it onto the opposite edge, which the repeat policy showed as a
  second column. The host now decodes the stage's ROM geyser list and the
  animation tables into the world-keyed BG3 store, verified each frame
  against every block the cartridge has fully drawn, and serves the margins
  plus a 24-pixel inset of each native edge from it.
- Fixed sprites cut off at the right edge near a level's left wall (a
  Zinger beside the rope net at the start of Topsail Trouble lost its right
  half). Under the glide bias the game places objects for the presented
  right margin up to the bias beyond the authentic margin, and the shared
  PPU's nine-bit object X decode treated those as negative. The positive
  range now extends by the presentation bias.
- Fixed the ship hold's water surface stopping at the 4:3 edges. The
  surface line is a bounded BG3 the cartridge enables only inside an HDMA
  band, with the main screen empty at frame start, so the repeat policy
  never saw it and the band drew only the native 256 columns. The policy is
  now derived from the union of every band's screen enables.
- Fixed stale tiles on the mast at the right wall of the vertical ship
  stages (crow's-nest art sitting on the mast after a climb). Under the
  glide bias the columns slid into view were served from the store's
  history, and that history could be a misattributed capture: in a vertical
  stage the cartridge rewrites the ring's other page with the same stale 32
  entries on every row upload, and a one-pixel leftward camera jitter was
  enough to file those writes under the chunk the strip shows. Every
  presented cell outside the cartridge's authentic window now takes the
  decoded map, as the ordinary margins already did.
- Fixed the foreground rigging on the Gangplank Galleon decks disappearing or
  showing a wrong strand at the widescreen edges. The rigging is a 64-column
  BG3 the cartridge streams with no lead, so the ring columns beside the
  native view held a rope from 512 pixels away, and a margin drawn from them
  cut the real rope off at a false apex. The host now decodes the rigging map
  from ROM into a third world-keyed shadow layer, verified against every
  fully uploaded native column each frame before it is trusted; the shared
  PPU's 2bpp renderer gained the same shadow hook its 4bpp layers had. The
  widescreen trace reports the decode as `rigging`.
- Added an experimental **Reconstruct** upscaler for high-density displays,
  selectable beside Nearest and Bilinear in the pause menu's Settings page
  ("Upscaler") and remembered in `launcher.cfg`. It is a single-pass
  OpenGL 2.1 shader evaluated per output pixel: it keeps pixel boundaries
  sharp at any fractional scale (a 342-pixel frame on a 16-inch MacBook Pro
  is about ten times wide), decodes the 2x2 checkerboard and one-pixel line
  dithers SNES artists used for CRT mid-tones into that mid-tone, and
  rebuilds the diagonal edges of the pre-rendered art with an xBR-style
  corner test at 45-degree, 2:1, and 3:1 slopes. The mode combo adds those
  stages one at a time; the strength slider scales the edge blend, the
  softness slider widens every transition from one screen pixel to up to
  three, and the smooth-shading slider turns the flat shading bands of the
  pre-rendered art into gradients where neighboring colors are close, so
  each can be judged alone. Reconstruct defaults to level-2 slopes with
  softness 50 and shading 60. `DKC2_UPSCALER`, `DKC2_RECONSTRUCT_MODE`,
  `DKC2_RECONSTRUCT_STRENGTH`, `DKC2_RECONSTRUCT_SOFTNESS`, and
  `DKC2_RECONSTRUCT_SHADING` override the saved choice for one run, and
  `DKC2_DESKTOP_SCREENSHOT` with `DKC2_DESKTOP_TEST_LOADSTATE` capture the
  presented drawable from a preserved state without a visible window.
- Fixed wrong terrain columns at the biased end of the view. Under a
  presentation bias (`glide` and `shift` near a wall) the PPU's 256-column
  window extends past the cartridge's authentic VRAM window by the bias, and
  those columns were rendered from the rolling ring's stale page: a strip of
  unrelated tiles just inside the right margin in the lava stage and the
  crystal mine. The world-keyed shadow now carries a per-layer native
  viewport inset so those columns are served from history and decoded
  terrain, the repeat-band merge continues a 64-column ring's stale tail
  from the authentic window instead of copying it, and stale fine-scroll
  endpoint repair now happens at the cartridge window's edge rather than
  the screen's, which also removes a few wrong interior pixels at screen
  column 0.
- Continued level walls into margin cells the console can never show. A
  shaft wall that stops the player, not the camera, leaves wholly
  transparent map cells beside it, and a wide margin reaching them showed
  the backdrop through a hole (crystal mine shaft). When such a margin
  metatile column is empty for the whole visible height and has a fully
  populated metatile, itself backed by another, as the first non-empty
  cell toward the native edge, corroborated on an adjacent row, the wall is
  continued from it; partial cells, portholes, doorways, and one-cell masts
  or crates are authored features and stay as they are.
  Decoded tiles are now forced only outside the cartridge window, so the
  columns a bias moves into the margin keep their live ring content.

## 0.0.4 (alpha) - 2026-09-01

- Replaced the per-scene widescreen policies with structural rules derived
  from live PPU geometry. Every enabled bounded background now repeats its
  rendered scanline (a 32-column map wraps at 256 pixels on hardware), a
  64-column allocation whose extension page is another enabled layer's map
  counts as bounded, a bounded backdrop in a 64-column allocation continues
  each line at the horizontal period its own rendered pixels prove while
  32-column maps keep their exact hardware wrap, rolling BG1/BG2 terrain
  layers are
  classified per HDMA scanline band read from the cartridge's own tables, the
  second physical terrain layer reads the owner's world-keyed store through
  an alias view, and the presented viewport is biased and clamped to the
  authored level extent. This removes the Mudhole, Topsail, Mainbrace, Krow's
  Nest, Parrot Chute, ship-hold, split-scroll role-swap, west-reflection, and
  east-mask special cases while reproducing their accepted output. A new
  corpus tool replays every preserved Quick Save at 4:3, 16:9, and 16:10 and
  checks the native center, visible margins, old-boundary seams, raw VRAM
  fallbacks, and runtime failures; the headless widescreen trace now reports
  the presentation bias, visible margins, and HDMA band count. Repeated
  layers now render the 4:3 columns that a presentation bias moves into a
  margin from real VRAM, which removes the last differing pixels at the
  hard-left lava camera bound.
- Made the level-wall presentation a selectable edge policy and changed the
  default. The former inward bias (`shift`) froze the view for the camera's
  first 43 pixels away from a wall, then started scrolling at catch-up speed,
  and slid every sprite including the HUD. The new default `glide` keeps
  that inward view, so nothing past a wall is ever shown, but releases it
  one pixel per eight pixels of camera travel: the picture scrolls at seven
  eighths of the camera speed for eight margins instead of stopping.
  `reflect` keeps the view locked to the game's camera and mirrors the
  nearest authored columns into the unauthored strip, and `bars` leaves that
  strip black. Select from the pause menu's Settings page ("Level edge"),
  `WidescreenEdge=0|1|2|3` in `launcher.cfg`, or
  `DKC2_WIDESCREEN_EDGE=reflect|bars|shift|glide`.
- Fixed the supplied lava-stage Quick Save whose upper and lower scanline
  bands assigned different world roles to BG1/BG2 through HDMA. Widescreen
  now detects that role swap from live PPU geometry, repeats only the cyclic
  effect planes on affected lines, and gives the alternate BG1 terrain band
  its own decoded world shadow plus an exact native-VRAM center capture. The
  split context now follows the same host presentation camera as the rest of
  the frame and restores the ordinary margin cells when its HDMA band ends,
  then continues the restored bounded BG1 lava/effect plane from the authentic
  native scanline while leaving BG2 world-keyed. During a camera reversal,
  the live split's alternate BG1 terrain phase can advance five or six pixels
  beyond the frame's BG2 anchor. The structural detector now accepts that
  measured phase lead while rejecting seven pixels and larger, so BG2/BG3 do
  not disappear from rectangular margin regions during the reversal. Farther
  through the same room, BG1's ordinary frame phase naturally approaches
  BG2's; the detector now keys the role swap to BG1 taking BG2's frame phase
  while BG2 makes the required distant switch, instead of requiring BG1's
  own displacement to exceed an arbitrary threshold. This removes the later
  two-sided terrain/lava cutoffs without broadening the accepted PPU layout.
  A later lava composition keeps that same BG1/BG2 exchange while HDMA
  independently disables BG3 and then moves it to the subscreen. The role
  proof now treats BG3 screen assignment as orthogonal and continues whichever
  bounded effect planes are enabled on each line, removing the two full-height
  black margin blocks at the supplied balloon/barrel Quick Save.
  Fine-scroll chunks that straddle a native edge select the
  cartridge tile for center pixels and the world-shadow tile for margin pixels.
  This removes the remaining motion-dependent lower-band lava slabs, slices,
  and reversal flashes without reusing BG1 rows that the cartridge never
  displays in that role. At horizontal level
  endpoints, the host presentation camera is clamped inward by the active
  margin width, matching DKC1 without changing DKC2's cartridge camera,
  collision, streaming, exits, or WRAM. Unsupported split decodes fail closed.
- Fixed the darker 16:10 extensions in Krow's Nest. That screen adds a
  grayscale BG3 lighting plane to its colored BG2 cloud subscreen; the host
  now repeats the already-rendered BG3 scanline into the margins only when the
  exact level, screen-enable, tilemap, and color-math signature is present.
  The native 256-pixel center and cartridge WRAM/VRAM remain unchanged.
- Fixed missing foreground terrain in both widescreen margins at Topsail
  Trouble's lower camera boundary. The presentation-only world shadow now
  retains the second vertical tile epoch used by rows 512-540 instead of
  silently rejecting every exact prefill beyond row 511. The supplied 16:10
  Quick Save now fills both sides while its native 256-pixel center, WRAM, and
  VRAM remain byte-identical to the 4:3/original-state oracles.
- Fixed Topsail Trouble's disconnected rigging fragment at the terminal right
  camera boundary. Vertical-layout margins now mask complete BG1 tiles beyond
  the authentic terminal viewport instead of exposing the cartridge's hidden
  streamer guard metatile. Horizontal and other layouts retain their existing
  accepted guard behavior; the native center remains pixel-identical to 4:3.

## 0.0.3 (alpha) - 2026-08-31

- Added a native Apple-silicon macOS application bundle with an original Dock
  icon, AppKit Game/View menus, fullscreen, Pixel Sharp/Smooth scaling, live
  4:3/16:10/16:9 selection, SDL2 bundling, Application Support persistence,
  LaunchServices registration, verified ad-hoc signing, and public release
  packaging. Notarization remains open.
- Ported the proven exact-rate Mac pacing model while retaining DKC2's
  60.098811862 Hz game rate: one Mach absolute-deadline authority, a short final
  spin, pacing before presentation, and deadline re-anchoring after a stall.
  Visible macOS OpenGL VSync is disabled by default to avoid a second 60/120 Hz
  gate; `DKC2_KEEP_OPENGL_VSYNC=1` is the diagnostic override.
- Added a centered 308x224 16:10 presentation option (26 source pixels per
  side) alongside native 4:3 and 342x224 16:9.
- Generalized the terrain-ready physical-width policy to every enabled Mode-1
  BG1-BG3 layer. Rattle Battle's authentic 64-column BG3 mast/rigging layer
  now fills the widened margins instead of stopping at the original edges;
  bounded BG3/HUD/staging screens remain fail-closed. Synthetic tests lock the
  Rattle Battle, Mainbrace, bounded, dual-stream, and Mode-7 signatures.
- Fixed Topsail Trouble's rain ending at the old 4:3 edges. Its exact live
  signature (level `$000B`, sub-mode `$0008`, BG1 `$79`, BG2 `$70`, BG3
  `$6C`) now repeats the already-rendered bounded BG3 rain scanline into both
  margins while leaving the physical BG1 terrain and native center unchanged.
- Replaced the Pirate Panic/bonus west-edge room list with a decoded-terrain
  capability rule. At a hard-left boundary, known horizontal, vertical,
  square, and narrow-vertical layouts reflect the first authored terrain tiles
  into only the host-created gutter west of world X=`$0100`; unknown layouts
  fail closed and the authentic 256-pixel center and cartridge state are
  unchanged. Pirate Panic, its ship-deck bonus, and Mainbrace Mayhem were
  accepted in the visible native Mac host.
- Fixed moving Ship Hold rooms such as Lockjaw's Locker by decoding their
  80-metatile/`$A0`-byte source rows into the world-keyed margin shadow instead
  of treating the recycled 64-column VRAM ring as a static map. Its bounded
  BG3 water now repeats the already-rendered scanline across both margins.
  The bounded BG2 `$7000`/`$7800` cabin-wall pages now use their verified
  12-tile/96-pixel screen-space period. This removes the remaining blue seams
  and empty backdrop margins at the old 4:3 edges; the deliberately corrected
  native columns are limited to X=0-6 and X=249-255.
- Fixed stationary Ship Hold margin flicker caused by water HDMA changing the
  live per-scanline BG1 horizontal scroll around a frame-latched world anchor.
  Margin tile lookup and 16x16 pixel phase now use the same signed 10-bit live
  scroll delta. A 600-frame no-input exact replay and a 180-frame moving replay
  both complete without widescreen findings.
- Made Escape return the SDL/Mac game from fullscreen to windowed mode before
  retaining its normal pause-overlay behavior.
- Made the native Mac Game menu's Quick Save State and Quick Load State
  commands always available. The configurable Assist bindings, rewind, and
  fast-forward remain behind the opt-in Assist Tools gate. The macOS build now
  replaces the former `build-macos-native` app location with a symlink to the
  verified canonical app and unregisters noncanonical local bundles from
  LaunchServices so Finder cannot reopen an older executable with the same
  bundle identity.

## 0.0.2 (alpha) - 2026-08-23

This is an alpha. Donkey Kong Country 2 is not finished: expect missing,
wrong, or unstable behaviour outside the paths listed here, and treat the
widescreen path as experimental.

- Moved both shared dependencies to their current upstream tips: `snesrecomp`
  at `fe6045c` (persisted shader graphics settings, opt-in synthetic SRAM for
  mods) and `recomp-ui` at `ad2f3e2` (IPS/IPS32/BPS patch support, shader
  preset picker, fast-forward speed slider, FMV filtering, free-text mod
  option rows, opt-in dashboard identity card).
- Verified the dependency move is behaviourally inert for DKC2: regeneration
  is byte-identical against the checked-in `recomp/` configuration (3,325
  roots, 3,403 exact AOT variants, 74 LLE variants), and the frame-3309
  framebuffer, WRAM, VRAM, CGRAM, and OAM hashes reproduce exactly on both the
  previous and the new engine pin.
- Fixed `.gitmodules`, which pointed both submodules at personal forks rather
  than the `mstan` upstreams the 0.0.1 release notes described. Fresh
  `--recurse-submodules` clones resolved to repositories that do not carry the
  pinned commits.
- Re-pinned the exact-state sprite regression gate, which had been failing
  against a 0.0.1 baseline that no longer reproduced on any tier. The new
  baseline is cross-checked against the interpreter tier: framebuffer, CGRAM,
  and OAM agree byte-for-byte at the same frame, and the former 91-frame
  host-pacing offset between tiers is gone. WRAM and VRAM still diverge
  between tiers at that checkpoint and are tracked as a known issue.
- Full suite is 57/57 with the private ROM gates enabled, including the
  12,000-frame two-complete-attract-cycle deterministic gate (2 cycles, 0
  sequence errors, no audio clipping or discontinuities).

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
