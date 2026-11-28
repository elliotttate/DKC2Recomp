#include "dkc2_glimmer_flash.h"

#include <string.h>

static uint16_t ReadLe16(const uint8_t *wram, size_t address) {
  return (uint16_t)(wram[address] | ((unsigned)wram[address + 1u] << 8));
}

static void WriteLe16(uint8_t *wram, size_t address, uint16_t value) {
  wram[address] = (uint8_t)(value & 0xFFu);
  wram[address + 1u] = (uint8_t)(value >> 8);
}

bool Dkc2GlimmerFlashReductionFromEnv(const char *env_value, bool fallback) {
  if (!env_value)
    return fallback;
  return strcmp(env_value, "0") != 0;
}

/* The flash table: one entry covering the screen with the beam table. */
static bool IsFlashTable(const uint8_t *table) {
  return table[0] == 1u &&
         ReadLe16(table, 1u) == kDkc2GlimmerLightBeamPointer &&
         table[3] == 0u;
}

/* Length in bytes including the zero-count terminator, or 0 when the
 * buffer does not hold a complete table. The terminator is copied as one
 * byte; the routine's own 16-bit zero store also clears the next byte,
 * which HDMA never reads. */
static size_t TableLength(const uint8_t *table) {
  for (size_t offset = 0;
       offset < kDkc2GlimmerLightTableMaxBytes; offset += 3u) {
    if (table[offset] == 0u)
      return offset + 1u;
  }
  return 0;
}

static bool IsWindowRegister(uint8_t b_address) {
  return b_address == kDkc2GlimmerLightWindowLeft1 ||
         b_address == kDkc2GlimmerLightWindowRight1;
}

bool Dkc2HoldGlimmerFlashFrame(Dkc2GlimmerFlashHold *hold,
                               const uint8_t *wram, size_t wram_size,
                               Dkc2GlimmerLightChannel *channel,
                               uint8_t *sub_layers) {
  if (!hold || !wram || !channel || !sub_layers ||
      wram_size <= kDkc2GlimmerSlotAddr + 1u)
    return false;
  if (ReadLe16(wram, kDkc2GlimmerSlotAddr) == 0u) {
    hold->window_register = 0u;
    return false;
  }
  const uint32_t first = ((uint32_t)kDkc2GlimmerLightTableBank << 16) |
                         kDkc2GlimmerLightTableAddr;
  if (!channel->active || !channel->indirect ||
      !IsWindowRegister(channel->b_address) ||
      (channel->table_address != first &&
       channel->table_address != first + kDkc2GlimmerLightBufferStride))
    return false;
  if (*sub_layers != 0u) {
    hold->window_register = channel->b_address;
    return false;
  }
  *sub_layers = kDkc2GlimmerLightSubscreen;
  if (IsWindowRegister(hold->window_register))
    channel->b_address = hold->window_register;
  return true;
}

bool Dkc2SuppressGlimmerTurnFlash(uint8_t *wram, size_t wram_size) {
  const size_t buffers_end = kDkc2GlimmerLightTableAddr +
                             2u * kDkc2GlimmerLightBufferStride;
  if (!wram || wram_size < buffers_end)
    return false;
  const uint16_t slot = ReadLe16(wram, kDkc2GlimmerSlotAddr);
  if (slot == 0u)
    return false;
  const size_t animation = (size_t)slot + kDkc2GlimmerAnimationOffset;
  if (animation + 1u >= wram_size ||
      ReadLe16(wram, animation) != kDkc2GlimmerFacingCameraAnimation)
    return false;

  bool changed = false;
  for (size_t buffer = 0; buffer < 2u; buffer++) {
    uint8_t *table = wram + kDkc2GlimmerLightTableAddr +
                     buffer * kDkc2GlimmerLightBufferStride;
    const uint8_t *other = wram + kDkc2GlimmerLightTableAddr +
                           (buffer ^ 1u) * kDkc2GlimmerLightBufferStride;
    if (!IsFlashTable(table))
      continue;
    const size_t length = TableLength(other);
    /* A table with at least one entry, so the light register is written. */
    if (length > 1u && !IsFlashTable(other)) {
      memcpy(table, other, length);
    } else {
      table[0] = 1u;
      WriteLe16(table, 1u, kDkc2GlimmerLightShadowPointer);
      table[3] = 0u;
    }
    changed = true;
  }
  return changed;
}
