#include "dkc2_glimmer_flash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_glimmer_flash.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

enum { kWramSize = 0x20000, kSlot = 0x0D50 };

static uint8_t s_wram[kWramSize];

static void Write16(size_t addr, uint16_t value) {
  s_wram[addr] = (uint8_t)(value & 0xFFu);
  s_wram[addr + 1u] = (uint8_t)(value >> 8);
}

static uint8_t *Buffer(size_t index) {
  return s_wram + kDkc2GlimmerLightTableAddr +
         index * kDkc2GlimmerLightBufferStride;
}

/* The table $80:DF8A writes: one entry with the beam table, then $0000. */
static void WriteFlash(size_t index) {
  uint8_t *table = Buffer(index);
  table[0] = 1u;
  table[1] = (uint8_t)(kDkc2GlimmerLightBeamPointer & 0xFFu);
  table[2] = (uint8_t)(kDkc2GlimmerLightBeamPointer >> 8);
  table[3] = 0u;
  table[4] = 0u;
}

/* A beam table shaped like the routine's normal output. */
static const uint8_t kBeam[] = {
    0x7F, 0x35, 0xE1, 0x21, 0x35, 0xE1, 0xB0, 0xC4, 0xE1,
    0xB0, 0xC6, 0xE1, 0x01, 0x35, 0xE1, 0x00};

static void Reset(uint16_t animation) {
  memset(s_wram, 0xA5, sizeof s_wram);
  Write16(kDkc2GlimmerSlotAddr, kSlot);
  Write16((size_t)kSlot + kDkc2GlimmerAnimationOffset, animation);
  memcpy(Buffer(0), kBeam, sizeof kBeam);
  WriteFlash(1);
}

static void CheckShadowFallback(size_t index) {
  const uint8_t *table = Buffer(index);
  CHECK(table[0] == 1u);
  CHECK((table[1] | (table[2] << 8)) == kDkc2GlimmerLightShadowPointer);
  CHECK(table[3] == 0u);
}

int main(void) {
  CHECK(!Dkc2GlimmerFlashReductionFromEnv(NULL, false));
  CHECK(Dkc2GlimmerFlashReductionFromEnv(NULL, true));
  CHECK(Dkc2GlimmerFlashReductionFromEnv("1", false));
  CHECK(!Dkc2GlimmerFlashReductionFromEnv("0", true));

  /* Null and undersized buffers fail safely. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  CHECK(!Dkc2SuppressGlimmerTurnFlash(NULL, kWramSize));
  CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kDkc2GlimmerLightTableAddr));

  /* The flash buffer takes the other buffer's beam, terminator included. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  CHECK(Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  CHECK(memcmp(Buffer(1), kBeam, sizeof kBeam) == 0);
  CHECK(memcmp(Buffer(0), kBeam, sizeof kBeam) == 0);
  /* Nothing past the copied table is touched. */
  CHECK(Buffer(1)[sizeof kBeam] == 0xA5u);
  /* Once replaced there is nothing more to do. */
  CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));

  /* Buffer 0 can hold the flash as well. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  memcpy(Buffer(1), kBeam, sizeof kBeam);
  WriteFlash(0);
  CHECK(Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  CHECK(memcmp(Buffer(0), kBeam, sizeof kBeam) == 0);

  /* Both buffers flashing: both fall back to the outside-the-beam row. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  WriteFlash(0);
  CHECK(Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  CheckShadowFallback(0);
  CheckShadowFallback(1);

  /* An unterminated or empty other buffer is not copied. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  memset(Buffer(0), 0x40, kDkc2GlimmerLightTableMaxBytes);
  CHECK(Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  CheckShadowFallback(1);
  Reset(kDkc2GlimmerFacingCameraAnimation);
  Buffer(0)[0] = 0u;
  CHECK(Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  CheckShadowFallback(1);

  /* Other animations keep the cartridge's table, as does no Glimmer. */
  static const uint16_t kOthers[] = {0x1C14u, 0x1C1Au, 0x1C1Eu, 0x1BCCu};
  for (size_t i = 0; i < sizeof kOthers / sizeof kOthers[0]; i++) {
    Reset(kOthers[i]);
    CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
    CHECK(Buffer(1)[0] == 1u);
    CHECK((Buffer(1)[1] | (Buffer(1)[2] << 8)) ==
          kDkc2GlimmerLightBeamPointer);
  }
  Reset(kDkc2GlimmerFacingCameraAnimation);
  Write16(kDkc2GlimmerSlotAddr, 0u);
  CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));

  /* Hold: a normal light frame records its window register only. */
  {
    Dkc2GlimmerFlashHold hold = {0};
    const Dkc2GlimmerLightChannel light = {
        true, true, kDkc2GlimmerLightWindowRight1,
        0x7E0000u | kDkc2GlimmerLightTableAddr};
    Dkc2GlimmerLightChannel channel = light;
    uint8_t sub = kDkc2GlimmerLightSubscreen;
    Reset(0x1C18u);
    CHECK(!Dkc2HoldGlimmerFlashFrame(NULL, s_wram, kWramSize, &channel, &sub));
    CHECK(!Dkc2HoldGlimmerFlashFrame(&hold, s_wram, kWramSize, &channel,
                                     &sub));
    CHECK(hold.window_register == kDkc2GlimmerLightWindowRight1);
    CHECK(sub == kDkc2GlimmerLightSubscreen);

    /* The flash frame: TS restored, previous frame's register kept. */
    channel.b_address = kDkc2GlimmerLightWindowLeft1;
    channel.table_address += kDkc2GlimmerLightBufferStride;
    sub = 0u;
    CHECK(Dkc2HoldGlimmerFlashFrame(&hold, s_wram, kWramSize, &channel,
                                    &sub));
    CHECK(sub == kDkc2GlimmerLightSubscreen);
    CHECK(channel.b_address == kDkc2GlimmerLightWindowRight1);
    CHECK(hold.window_register == kDkc2GlimmerLightWindowRight1);

    /* Without a recorded register only TS is restored. */
    hold.window_register = 0u;
    channel.b_address = kDkc2GlimmerLightWindowLeft1;
    sub = 0u;
    CHECK(Dkc2HoldGlimmerFlashFrame(&hold, s_wram, kWramSize, &channel,
                                    &sub));
    CHECK(sub == kDkc2GlimmerLightSubscreen);
    CHECK(channel.b_address == kDkc2GlimmerLightWindowLeft1);

    /* Anything but Glimmer's light channel is left alone. */
    const Dkc2GlimmerLightChannel others[] = {
        {false, true, kDkc2GlimmerLightWindowLeft1, light.table_address},
        {true, false, kDkc2GlimmerLightWindowLeft1, light.table_address},
        {true, true, 0x0Du, light.table_address},
        {true, true, kDkc2GlimmerLightWindowLeft1, 0x7E9000u},
        {true, true, kDkc2GlimmerLightWindowLeft1, 0x0080F2u},
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
      hold.window_register = kDkc2GlimmerLightWindowRight1;
      channel = others[i];
      sub = 0u;
      CHECK(!Dkc2HoldGlimmerFlashFrame(&hold, s_wram, kWramSize, &channel,
                                       &sub));
      CHECK(sub == 0u);
      CHECK(channel.b_address == others[i].b_address);
    }

    /* No Glimmer: nothing changes and the recorded register is dropped. */
    Write16(kDkc2GlimmerSlotAddr, 0u);
    hold.window_register = kDkc2GlimmerLightWindowRight1;
    channel = light;
    sub = 0u;
    CHECK(!Dkc2HoldGlimmerFlashFrame(&hold, s_wram, kWramSize, &channel,
                                     &sub));
    CHECK(sub == 0u);
    CHECK(hold.window_register == 0u);
  }

  /* A buffer that is not exactly the flash table is left alone. */
  Reset(kDkc2GlimmerFacingCameraAnimation);
  Buffer(1)[3] = 0x10u;
  CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));
  Reset(kDkc2GlimmerFacingCameraAnimation);
  Buffer(1)[1] = (uint8_t)(kDkc2GlimmerLightShadowPointer & 0xFFu);
  CHECK(!Dkc2SuppressGlimmerTurnFlash(s_wram, kWramSize));

  return 0;
}
