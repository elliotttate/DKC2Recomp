# DKC3 desktop feature parity

Comparison source: DKC3Recomp revision
`3a033f19801a2bd3abf784d4b29c4462495d19de` (MIT). Target: DKC2's
Windows desktop and SDL hosts. This inventories host features; DKC3-specific
level rendering and game logic are not interchangeable with DKC2.

| DKC3 option | DKC2 implementation |
| --- | --- |
| Raw, CRT, Composite, Trinitron | Same screen-color LUT implementation; available in View > Screen Model, pause Settings and launcher |
| Nearest, Bilinear | Both native renderers and SDL |
| Reconstruct | SDL and native Windows OpenGL; actual shader capability gates selection |
| Sharp pixels, dither decoding, diagonals, level-2 and level-3 slopes | All five modes available in View > Reconstruction Mode and pause Settings |
| Edge strength, Softness, Smooth shading | All three 0–100% sliders in pause Settings; apply live |
| 4:3, 16:10, 16:9, 21:9 | 256/308/342/446 source columns; launcher, dropdown and pause menu |
| Reflect, Black, Shift, Glide level edges | Existing DKC2 terrain policies retained |
| Window size and fullscreen | Existing launcher/pause controls; native fullscreen dropdown and Alt+Enter retained |
| Stomp feedback and test pulse | Added for both players through XInput and SDL, with assigned-device status and persistent enablement |
| Keyboard/gamepad mapping, deadzones and rebinding | Existing independent two-player input retained |
| Volume, audio enable/rate, skip launcher | Existing settings retained; emulated audio stays at its native rate |
| Pause, quick saves, five slots, rewind, fast-forward, performance log | Existing desktop features retained |
| Restore defaults | Also restores reconstruction tuning, edge policy, co-op policy, rumble and KONG badges |
| Start skips the opening logos (PR 17, `ab380f7`) | Logo and copyright scenes skip to the title through the cartridge's own fade; `DKC2_SKIP_INTRO_START=0` disables |
| KONG letter badges on the map (PR 17) | Per-file sidecar `saves/kong_letters.dat`; level badges on world maps, world badges on the Crocodile Isle map; Settings > Gameplay checkbox |

The CRT options in this DKC3 revision are color-response presets. There are
no separate scanline, curvature or bloom sliders in its desktop filter. The
three reconstruction sliders are separate scaling controls and work with
each of the four screen presets.

`desktop_shaders` exercises 40 paired WGL/SDL settings using synthetic
patterns, including independent tuning changes. The private TEAM replay
checks actual P2 stomp feedback and no feedback on roll/jump-only routes.
The widescreen replay covers both players at both edges in all four aspects.
`supplied_rom_desktop_settings` runs isolated copies of WGL, GDI and SDL twice
each, opens and closes the pause overlay, and checks that custom reconstruction,
CRT, rumble, aspect and input preferences survive both launches. GDI preserves
the requested shader preference while displaying its supported fallback.
Private files and captured game pixels remain outside Git.

Physical controller feel, macOS builds/Metal presentation, other animals,
and full-game ultrawide coverage are not established by these checks.

## Full tube rendering and music follow-up

The newer DKC2 CRT implementation adds a separate CRT television mode with
Living room, Studio monitor, Soft and Custom, six sliders and four masks.
WGL and SDL share the passes; GPU tests cover 40 flat settings plus 13 CRT
variations. Both desktop hosts also expose MSU-1 folder, gain and fallback;
see [MSU-1 audio](MSU1_AUDIO.md). Characters provides Donkey/Kiddy selection
and external packs beyond the original DKC3 comparison.
