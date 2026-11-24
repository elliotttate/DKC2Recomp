#include "dkc2_intro_skip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_intro_skip.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

static uint16_t Read16(const uint8_t *wram, size_t addr) {
  return (uint16_t)(wram[addr] | ((unsigned)wram[addr + 1u] << 8));
}

static void Write16(uint8_t *wram, size_t addr, uint16_t value) {
  wram[addr] = (uint8_t)(value & 0xFFu);
  wram[addr + 1u] = (uint8_t)(value >> 8);
}

/* A scene at full brightness with no fade pending. */
static void ResetScene(uint8_t *wram, size_t size, uint16_t continuation,
                       uint16_t frame) {
  memset(wram, 0, size);
  Write16(wram, kDkc2IntroContinuationAddr, continuation);
  Write16(wram, kDkc2IntroFrameAddr, frame);
  wram[kDkc2IntroBrightnessAddr] = kDkc2IntroFullBrightness;
}

int main(void) {
  CHECK(Dkc2IntroSkipEnabledFromEnv(NULL));
  CHECK(Dkc2IntroSkipEnabledFromEnv(""));
  CHECK(Dkc2IntroSkipEnabledFromEnv("1"));
  CHECK(!Dkc2IntroSkipEnabledFromEnv("0"));

  static uint8_t wram[0x1000];
  const uint16_t start = kDkc2SnesInputStartBit;

  /* Null and undersized buffers fail safely. */
  CHECK(!Dkc2ApplyIntroStartSkip(NULL, sizeof wram, start, true));
  ResetScene(wram, sizeof wram, kDkc2IntroContinuationLogos, 0x0080u);
  CHECK(!Dkc2ApplyIntroStartSkip(wram, kDkc2IntroSeenFlagAddr + 1u, start,
                                 true));

  /* Logo scene: nothing without Start, with other buttons, or disabled. */
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, 0u, true));
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram,
                                 (uint16_t)((1u << 0) | (1u << 8)), true));
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, false));
  CHECK(Read16(wram, kDkc2IntroSeenFlagAddr) == 0u);
  CHECK(wram[kDkc2IntroFadeCommandAddr] == 0u);

  /* Logo scene: ignored below full brightness or while a fade runs. */
  wram[kDkc2IntroBrightnessAddr] = 0x09u;
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  wram[kDkc2IntroBrightnessAddr] = kDkc2IntroFullBrightness;
  wram[kDkc2IntroFadeCommandAddr] = 0x02u;
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  CHECK(Read16(wram, kDkc2IntroSeenFlagAddr) == 0u);
  wram[kDkc2IntroFadeCommandAddr] = 0u;

  /* Logo scene: ignored once the cartridge is ending it naturally. */
  Write16(wram, kDkc2IntroFrameAddr, kDkc2IntroLogosNaturalEndFrame);
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  CHECK(Read16(wram, kDkc2IntroSeenFlagAddr) == 0u);

  /* Logo scene + Start: marks the intro seen and starts the fade-out. */
  ResetScene(wram, sizeof wram, kDkc2IntroContinuationLogos, 0x0080u);
  CHECK(Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  CHECK(Read16(wram, kDkc2IntroSeenFlagAddr) == kDkc2IntroSeenValue);
  CHECK(wram[kDkc2IntroFadeCommandAddr] == kDkc2IntroFadeOutCommand);
  CHECK(Read16(wram, kDkc2IntroFrameAddr) == 0x0080u);
  /* Held Start does nothing more while the fade runs. */
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));

  /* Copyright scene + Start: advances the frame counter to the fade point. */
  ResetScene(wram, sizeof wram, kDkc2IntroContinuationCopyright, 0x0010u);
  CHECK(Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  CHECK(Read16(wram, kDkc2IntroFrameAddr) ==
        kDkc2IntroCopyrightFadeFrame - 1u);
  CHECK(wram[kDkc2IntroFadeCommandAddr] == 0u);
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));

  /* Copyright scene: ignored during its fade-in. */
  ResetScene(wram, sizeof wram, kDkc2IntroContinuationCopyright, 0x0010u);
  wram[kDkc2IntroFadeCommandAddr] = 0x02u;
  CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
  CHECK(Read16(wram, kDkc2IntroFrameAddr) == 0x0010u);

  /* Boot ($0000), title ($F3A6) and other continuations are untouched. */
  static const uint16_t kOthers[] = {0x0000u, 0xF3A6u, 0x8608u};
  for (size_t i = 0; i < sizeof kOthers / sizeof kOthers[0]; i++) {
    ResetScene(wram, sizeof wram, kOthers[i], 0x0010u);
    CHECK(!Dkc2ApplyIntroStartSkip(wram, sizeof wram, start, true));
    CHECK(Read16(wram, kDkc2IntroSeenFlagAddr) == 0u);
    CHECK(Read16(wram, kDkc2IntroFrameAddr) == 0x0010u);
    CHECK(wram[kDkc2IntroFadeCommandAddr] == 0u);
  }

  return 0;
}
