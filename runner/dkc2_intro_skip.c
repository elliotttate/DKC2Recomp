#include "dkc2_intro_skip.h"

#include <string.h>

static uint16_t ReadLe16(const uint8_t *wram, size_t address) {
  return (uint16_t)(wram[address] | ((unsigned)wram[address + 1u] << 8));
}

static void WriteLe16(uint8_t *wram, size_t address, uint16_t value) {
  wram[address] = (uint8_t)(value & 0xFFu);
  wram[address + 1u] = (uint8_t)(value >> 8);
}

bool Dkc2IntroSkipEnabledFromEnv(const char *env_value) {
  if (!env_value)
    return true;
  return strcmp(env_value, "0") != 0;
}

bool Dkc2ApplyIntroStartSkip(uint8_t *wram, size_t wram_size,
                             uint16_t input1_state, bool enabled) {
  if (!enabled || !wram || wram_size <= kDkc2IntroSeenFlagAddr + 1u)
    return false;
  if ((input1_state & kDkc2SnesInputStartBit) == 0u)
    return false;
  /* Same gate as the cartridge's own check at $80:9753: the word at $0512
   * must be $000F, meaning full brightness and no fade command pending. */
  if (wram[kDkc2IntroBrightnessAddr] != kDkc2IntroFullBrightness ||
      wram[kDkc2IntroFadeCommandAddr] != 0u)
    return false;

  const uint16_t continuation = ReadLe16(wram, kDkc2IntroContinuationAddr);
  const uint16_t frame = ReadLe16(wram, kDkc2IntroFrameAddr);

  if (continuation == kDkc2IntroContinuationLogos) {
    /* At or past $01A0 the cartridge is already ending the scene. */
    if (frame >= kDkc2IntroLogosNaturalEndFrame)
      return false;
    WriteLe16(wram, kDkc2IntroSeenFlagAddr, kDkc2IntroSeenValue);
    wram[kDkc2IntroFadeCommandAddr] = kDkc2IntroFadeOutCommand;
    return true;
  }

  if (continuation == kDkc2IntroContinuationCopyright) {
    /* $80:B660 increments $002A before comparing it with $0070. */
    const uint16_t target = (uint16_t)(kDkc2IntroCopyrightFadeFrame - 1u);
    if (frame >= target)
      return false;
    WriteLe16(wram, kDkc2IntroFrameAddr, target);
    return true;
  }

  return false;
}
