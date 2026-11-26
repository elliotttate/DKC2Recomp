#include "dkc2_palette_overflow.h"

#include <string.h>

static uint16_t ReadWord(const uint8_t *wram, size_t size, uint32_t addr) {
  if (!wram || addr + 1u >= size)
    return 0;
  return (uint16_t)(wram[addr] | ((uint16_t)wram[addr + 1u] << 8));
}

static void WriteWord(uint8_t *wram, size_t size, uint32_t addr,
                      uint16_t value) {
  if (!wram || addr + 1u >= size)
    return;
  wram[addr] = (uint8_t)value;
  wram[addr + 1u] = (uint8_t)(value >> 8);
}

uint16_t Dkc2PaletteOverflowSpriteForCaller(
    const Dkc2PaletteOverflowCaller *caller) {
  if (!caller)
    return 0;
  switch (caller->jsr_return) {
  case 0x84FCu: /* script command $8D: STA $12 of the record at DP $68 */
  case 0x8668u: /* script palette-table command: same store */
    return caller->dp68;
  case 0x8C74u: /* palette swap helper: TYX, STA $12,x */
    return caller->y;
  case 0x8A64u: /* JSL wrapper $BB:8A61 (table lookup, then $8A6F) */
  case 0x8A68u: /* JSL wrapper $BB:8A65 */
    break;
  default:
    /* $86FA, the four-argument command, branches on the carry and keeps
     * its sprite off the fallback; anything else is not audited. */
    return 0;
  }
  /* The JSL return bank is the caller's program bank; compare without the
   * $80 FastROM mirror bit. */
  const uint32_t site = caller->jsl_return & 0x7FFFFFu;
  switch (site) {
  case 0x36AB31u: /* $B6:AB2D: STA $0012,y with Y = DP $68 */
  case 0x3A956Bu: /* $BA:9567: same */
  case 0x3A95A9u: /* $BA:95A5: same */
    return caller->dp68;
  case 0x3EEA2Du: /* $BE:EA29: STA $12,x with X = DP $64 */
    return caller->dp64;
  default:
    /* $B5:F0EB and $BB:AD2A request the HUD palette for no record;
     * $BE:C68D builds an attribute in DP $60. */
    return 0;
  }
}

bool Dkc2PaletteIsSpriteRecord(uint16_t record) {
  return record >= kDkc2PaletteSpriteTableAddr &&
         record < kDkc2PaletteSpriteTableEnd &&
         (record - kDkc2PaletteSpriteTableAddr) % kDkc2PaletteSpriteStride ==
             0;
}

void Dkc2PaletteOverflowReset(Dkc2PaletteOverflowTracker *tracker) {
  if (tracker)
    memset(tracker, 0, sizeof *tracker);
}

static void RemoveAt(Dkc2PaletteOverflowTracker *tracker, unsigned index) {
  for (unsigned i = index + 1u; i < tracker->count; i++)
    tracker->entries[i - 1u] = tracker->entries[i];
  tracker->count--;
  memset(&tracker->entries[tracker->count], 0,
         sizeof tracker->entries[tracker->count]);
}

static int FindRecord(const Dkc2PaletteOverflowTracker *tracker,
                      uint16_t record) {
  for (unsigned i = 0; i < tracker->count; i++)
    if (tracker->entries[i].record == record)
      return (int)i;
  return -1;
}

bool Dkc2PaletteOverflowNote(Dkc2PaletteOverflowTracker *tracker,
                             uint16_t record, uint16_t palette_id) {
  if (!tracker || !Dkc2PaletteIsSpriteRecord(record) || palette_id == 0)
    return false;
  int index = FindRecord(tracker, record);
  if (index < 0) {
    if (tracker->count >= kDkc2PaletteOverflowMaxTracked)
      return false;
    index = tracker->count++;
  }
  tracker->entries[index].record = record;
  tracker->entries[index].palette_id = palette_id;
  tracker->entries[index].type = 0;
  return true;
}

/* Mirrors the allocator's search: share an unlocked slot with the id, or use
 * the first slot whose id is zero. */
static bool AllocatorWouldPlace(const uint8_t *wram, size_t size,
                                uint16_t palette_id) {
  for (unsigned i = 0; i < kDkc2PaletteSlotCount; i++) {
    const uint16_t id = ReadWord(wram, size, kDkc2PaletteSlotIdsAddr + 2u * i);
    const uint16_t refs =
        ReadWord(wram, size, kDkc2PaletteSlotRefsAddr + 2u * i);
    if (id == 0)
      return true;
    if (id == palette_id && (refs & 0x8000u) == 0)
      return true;
  }
  return false;
}

bool Dkc2PaletteOverflowPickRetry(Dkc2PaletteOverflowTracker *tracker,
                                  const uint8_t *wram, size_t wram_size,
                                  Dkc2PaletteOverflowEntry *out) {
  if (!tracker || !wram || wram_size < 0x2000u)
    return false;
  for (unsigned i = 0; i < tracker->count;) {
    Dkc2PaletteOverflowEntry *entry = &tracker->entries[i];
    const uint16_t type = ReadWord(wram, wram_size, entry->record);
    const uint16_t attr = ReadWord(
        wram, wram_size, (uint32_t)entry->record + kDkc2PaletteAttrOffset);
    const bool stale = type == 0 || (attr & kDkc2PaletteAttrMask) != 0 ||
                       (entry->type != 0 && entry->type != type);
    if (stale) {
      RemoveAt(tracker, i);
      continue;
    }
    entry->type = type;
    i++;
  }
  for (unsigned i = 0; i < tracker->count; i++) {
    if (AllocatorWouldPlace(wram, wram_size, tracker->entries[i].palette_id)) {
      if (out)
        *out = tracker->entries[i];
      return true;
    }
  }
  return false;
}

static void ReturnSlotZeroReference(uint8_t *wram, size_t size) {
  const uint16_t refs = ReadWord(wram, size, kDkc2PaletteSlotRefsAddr);
  if (refs != 0 && (refs & 0x8000u) == 0)
    WriteWord(wram, size, kDkc2PaletteSlotRefsAddr, (uint16_t)(refs - 1u));
}

bool Dkc2PaletteOverflowAdopt(Dkc2PaletteOverflowTracker *tracker,
                              uint8_t *wram, size_t wram_size,
                              const Dkc2PaletteOverflowEntry *entry,
                              uint16_t result_a, bool carry) {
  if (!tracker || !wram || !entry || wram_size < 0x2000u)
    return false;
  const int index = FindRecord(tracker, entry->record);
  if (index >= 0)
    RemoveAt(tracker, (unsigned)index);
  /* Either way one reference on slot 0 is now surplus: the original
   * fallback's when the sprite moves, or the retry's own when it overflowed
   * again (the sprite then keeps the original's). */
  ReturnSlotZeroReference(wram, wram_size);
  if (carry)
    return false;
  const uint32_t attr_addr = (uint32_t)entry->record + kDkc2PaletteAttrOffset;
  const uint16_t attr = ReadWord(wram, wram_size, attr_addr);
  WriteWord(wram, wram_size, attr_addr,
            (uint16_t)((attr & ~kDkc2PaletteAttrMask) |
                       (result_a & kDkc2PaletteAttrMask)));
  return (result_a & kDkc2PaletteAttrMask) != 0;
}

bool Dkc2PaletteOverflowRecoverSaved(Dkc2PaletteOverflowTracker *tracker,
                                     const uint8_t *wram, size_t wram_size) {
  if (!tracker || !wram || wram_size < 0x2000u)
    return false;
  const uint16_t lost = ReadWord(wram, wram_size, kDkc2PaletteOverflowIdAddr);
  if (lost == 0 || lost == kDkc2PaletteSharedHudId)
    return false;
  if (ReadWord(wram, wram_size, kDkc2PaletteSlotIdsAddr) !=
      kDkc2PaletteSharedHudId)
    return false;
  for (unsigned i = 0; i < kDkc2PaletteSlotCount; i++)
    if (ReadWord(wram, wram_size, kDkc2PaletteSlotIdsAddr + 2u * i) == lost)
      return false;
  for (unsigned i = 0; i < tracker->count; i++)
    if (tracker->entries[i].palette_id == lost)
      return false;
  uint16_t candidate = 0;
  unsigned candidates = 0;
  for (uint16_t record = kDkc2PaletteSpriteTableAddr;
       record < kDkc2PaletteSpriteTableEnd;
       record = (uint16_t)(record + kDkc2PaletteSpriteStride)) {
    if (ReadWord(wram, wram_size, record) == 0)
      continue;
    const uint16_t attr =
        ReadWord(wram, wram_size, (uint32_t)record + kDkc2PaletteAttrOffset);
    if ((attr & kDkc2PaletteAttrMask) == 0) {
      candidate = record;
      candidates++;
    }
  }
  if (candidates != 1)
    return false;
  return Dkc2PaletteOverflowNote(tracker, candidate, lost);
}
