#include "dkc2_animal_crate_cheat.h"

#include <string.h>

enum {
  kSubpixelXOffset = 0x04u,
  kSubpixelYOffset = 0x08u,
  kSubpixelDefault = 0x8000u,
  kCrateAnimationOffset = 0x36u,
};

static uint16_t ReadLe16(const uint8_t *wram, size_t address) {
  return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1u] << 8));
}

static void WriteLe16(uint8_t *wram, size_t address, uint16_t value) {
  wram[address] = (uint8_t)(value & 0xFFu);
  wram[address + 1u] = (uint8_t)(value >> 8);
}

static bool IsSpriteSlot(uint16_t slot, uint16_t first_slot) {
  if (slot < first_slot || slot >= kDkc2PauseCrateSpriteTableEnd)
    return false;
  return ((slot - kDkc2PauseCrateSpriteTableStart) %
          kDkc2PauseCrateSpriteStride) == 0u;
}

static bool HasFreeSlot(const uint8_t *wram) {
  for (uint16_t slot = kDkc2PauseCrateFirstFreeSlot;
       slot < kDkc2PauseCrateSpriteTableEnd;
       slot = (uint16_t)(slot + kDkc2PauseCrateSpriteStride)) {
    if (ReadLe16(wram, slot + kDkc2PauseCrateSpriteTypeOffset) == 0u)
      return true;
  }
  return false;
}

bool Dkc2PauseCrateSpawnEnabledFromEnv(const char *env_value, bool fallback) {
  if (!env_value)
    return fallback;
  return strcmp(env_value, "0") != 0;
}

const char *Dkc2PauseCrateName(Dkc2PauseCrateKind kind) {
  switch (kind) {
    case kDkc2PauseCrateRambi: return "Rambi";
    case kDkc2PauseCrateRattly: return "Rattly";
    case kDkc2PauseCrateSquawks: return "Squawks";
    case kDkc2PauseCrateSquitter: return "Squitter";
    case kDkc2PauseCrateEnguarde: return "Enguarde";
    default: return NULL;
  }
}

uint16_t Dkc2PauseCrateScriptPtr(Dkc2PauseCrateKind kind) {
  switch (kind) {
    case kDkc2PauseCrateRambi: return kDkc2PauseCrateScriptRambi;
    case kDkc2PauseCrateRattly: return kDkc2PauseCrateScriptRattly;
    case kDkc2PauseCrateSquawks: return kDkc2PauseCrateScriptSquawks;
    case kDkc2PauseCrateSquitter: return kDkc2PauseCrateScriptSquitter;
    case kDkc2PauseCrateEnguarde: return kDkc2PauseCrateScriptEnguarde;
    default: return 0u;
  }
}

uint16_t Dkc2PauseCrateAnimalId(Dkc2PauseCrateKind kind) {
  switch (kind) {
    case kDkc2PauseCrateSquitter: return 0x02ACu;
    case kDkc2PauseCrateRattly: return 0x02ADu;
    case kDkc2PauseCrateSquawks: return 0x02AEu;
    case kDkc2PauseCrateRambi: return 0x02AFu;
    case kDkc2PauseCrateEnguarde: return 0x02B0u;
    default: return 0u;
  }
}

uint8_t Dkc2SquawksDismountCarry(uint8_t cartridge_carry, bool enabled) {
  return enabled ? 1u : (uint8_t)(cartridge_carry != 0u);
}

bool Dkc2IsPauseCrateMenuActive(const uint8_t *wram, size_t wram_size,
                                bool enabled) {
  if (!enabled || !wram || wram_size < kDkc2PauseCrateMinWramSize)
    return false;
  if ((ReadLe16(wram, kDkc2PauseCrateLevelFlagsAddr) &
       kDkc2PauseCratePausedFlagBit) == 0u)
    return false;
  const uint16_t kong = ReadLe16(wram, kDkc2PauseCrateActiveKongAddr);
  return IsSpriteSlot(kong, kDkc2PauseCrateSpriteTableStart) &&
         ReadLe16(wram, kong + kDkc2PauseCrateSpriteTypeOffset) != 0u;
}

static uint16_t PressedOutsidePauseControls(const uint8_t *wram) {
  const uint16_t pressed = ReadLe16(wram, kDkc2PauseCrateActivePressedAddr);
  if ((pressed & (kDkc2PauseCrateInputStartBit |
                  kDkc2PauseCrateInputSelectBit)) != 0u)
    return 0u;
  return pressed;
}

bool Dkc2UpdatePauseCrateSelection(const uint8_t *wram, size_t wram_size,
                                   bool enabled,
                                   Dkc2PauseCrateKind *selected) {
  if (!selected || !Dkc2IsPauseCrateMenuActive(wram, wram_size, enabled))
    return false;
  int index = (int)*selected;
  if (index < 0 || index >= (int)kDkc2PauseCrateCount)
    index = 0;
  const uint16_t pressed = PressedOutsidePauseControls(wram);
  const bool left = (pressed & kDkc2PauseCrateInputLeftBit) != 0u;
  const bool right = (pressed & kDkc2PauseCrateInputRightBit) != 0u;
  if (left == right)
    return false;
  const int count = (int)kDkc2PauseCrateCount;
  index = right ? (index + 1) % count : (index + count - 1) % count;
  *selected = (Dkc2PauseCrateKind)index;
  return true;
}

bool Dkc2DetectPauseCrateSpawn(const uint8_t *wram, size_t wram_size,
                               bool enabled, Dkc2PauseCrateKind selected,
                               Dkc2PauseCrateRequest *out) {
  if (out)
    memset(out, 0, sizeof *out);
  if (!Dkc2IsPauseCrateMenuActive(wram, wram_size, enabled))
    return false;
  const uint16_t script = Dkc2PauseCrateScriptPtr(selected);
  if (script == 0u)
    return false;
  if ((PressedOutsidePauseControls(wram) & kDkc2PauseCrateInputBBit) == 0u)
    return false;
  if (!HasFreeSlot(wram))
    return false;
  if (out) {
    const uint16_t kong = ReadLe16(wram, kDkc2PauseCrateActiveKongAddr);
    out->kind = selected;
    out->script_ptr = script;
    out->spawn_x = ReadLe16(wram, kong + kDkc2PauseCrateSpriteXOffset);
    out->spawn_y = ReadLe16(wram, kong + kDkc2PauseCrateSpriteYOffset);
  }
  return true;
}

bool Dkc2PlacePauseCrate(uint8_t *wram, size_t wram_size, uint16_t slot,
                         const Dkc2PauseCrateRequest *request) {
  if (!wram || !request || wram_size < kDkc2PauseCrateMinWramSize)
    return false;
  const uint16_t animal = Dkc2PauseCrateAnimalId(request->kind);
  if (animal == 0u || !IsSpriteSlot(slot, kDkc2PauseCrateFirstFreeSlot))
    return false;
  if (ReadLe16(wram, slot + kDkc2PauseCrateSpriteTypeOffset) !=
          kDkc2PauseCrateType ||
      ReadLe16(wram, slot + kCrateAnimationOffset) != animal)
    return false;
  WriteLe16(wram, slot + kSubpixelXOffset, kSubpixelDefault);
  WriteLe16(wram, slot + kDkc2PauseCrateSpriteXOffset, request->spawn_x);
  WriteLe16(wram, slot + kSubpixelYOffset, kSubpixelDefault);
  WriteLe16(wram, slot + kDkc2PauseCrateSpriteYOffset, request->spawn_y);
  return true;
}
