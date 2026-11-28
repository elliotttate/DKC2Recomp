#ifndef DKC2_INTRO_SKIP_H
#define DKC2_INTRO_SKIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cold-boot opening sequence of DKC2 USA v1.0, established from bank $80
 * disassembly of the supported ROM:
 *
 * - $0020: 16-bit NMI frame continuation (bank $80/$00 code).
 *   - $9378: Rareware logo followed by "Nintendo Presents".
 *   - $B641: copyright screen shown when the logos run to completion.
 *   - $F3A6: title screen ($85EF also stores $B397 to game mode $0024).
 * - $002A: per-scene frame counter, incremented every frame.
 * - $0512: INIDISP brightness shadow; $0513: fade command ($82 = fade out).
 * - $090F: "intro seen" flag. Zero after a cold boot; $80:85EF stores $FFFF
 *   the first time the title is reached.
 *
 * The logo scene already has a Start skip at $80:974E: when $090F is
 * nonzero, the word at $0512 equals $000F (full brightness, no fade), and
 * a newly pressed button matches $D0C0, $80:976B stores $82 to $0513. Once
 * the fade reaches black with $002A below $01A0, $80:9780 jumps to $85EF
 * (title) instead of $B5BA (copyright screen). On cold boot $090F is zero,
 * so the first playthrough of the logos is unskippable.
 *
 * The copyright scene has no button check. It starts its own fade-out when
 * $002A reaches $0070 ($80:B668) and enters the title from $85EF.
 */
enum {
  kDkc2IntroContinuationAddr = 0x0020u,
  kDkc2IntroFrameAddr = 0x002Au,
  kDkc2IntroBrightnessAddr = 0x0512u,
  kDkc2IntroFadeCommandAddr = 0x0513u,
  kDkc2IntroSeenFlagAddr = 0x090Fu,

  kDkc2IntroContinuationLogos = 0x9378u,
  kDkc2IntroContinuationCopyright = 0xB641u,

  kDkc2IntroLogosNaturalEndFrame = 0x01A0u,
  kDkc2IntroCopyrightFadeFrame = 0x0070u,
  kDkc2IntroFullBrightness = 0x0Fu,
  kDkc2IntroFadeOutCommand = 0x82u,
  kDkc2IntroSeenValue = 0xFFFFu,

  /* Packed SNES controller bit for Start in Snes::input1_currentState. */
  kDkc2SnesInputStartBit = (1u << 3),
};

/* Returns true unless env_value is explicitly "0". */
bool Dkc2IntroSkipEnabledFromEnv(const char *env_value);

/* Lets Start skip DKC2's cold-boot logo and copyright scenes through the
 * cartridge's own fade-out and title transition.
 *
 * Logo scene: marks the intro as seen ($090F) and issues the same fade-out
 * the cartridge's own skip path issues at $80:976B, so the scene ends at the
 * title exactly as it does on later boots.
 * Copyright scene: advances $002A so the cartridge's own $0070 check starts
 * its normal fade-out this frame.
 *
 * Call before the frame's NMI dispatch. Returns true if WRAM was changed.
 * Does nothing when disabled, when Start is not held, while a fade is in
 * progress, below full brightness, or outside the two opening scenes. */
bool Dkc2ApplyIntroStartSkip(uint8_t *wram, size_t wram_size,
                             uint16_t input1_state, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
