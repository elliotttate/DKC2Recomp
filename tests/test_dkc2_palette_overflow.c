#include "dkc2_palette_overflow.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_palette_overflow.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

static uint8_t g_wram[0x20000];

static void Write16(size_t address, uint16_t value) {
  g_wram[address] = (uint8_t)(value & 0xFFu);
  g_wram[address + 1u] = (uint8_t)(value >> 8);
}

static uint16_t Read16(size_t address) {
  return (uint16_t)(g_wram[address] | ((uint16_t)g_wram[address + 1u] << 8));
}

static uint16_t Record(unsigned slot) {
  return (uint16_t)(kDkc2PaletteSpriteTableAddr +
                    slot * kDkc2PaletteSpriteStride);
}

static void SetSlot(unsigned index, uint16_t id, uint16_t refs) {
  Write16(kDkc2PaletteSlotIdsAddr + 2u * index, id);
  Write16(kDkc2PaletteSlotRefsAddr + 2u * index, refs);
}

static void SetSprite(unsigned slot, uint16_t type, unsigned palette) {
  Write16(Record(slot), type);
  Write16(Record(slot) + kDkc2PaletteAttrOffset,
          (uint16_t)(0x2000u | (palette << 9) | 0x00A0u));
}

/* All eight palettes held: HUD in slot 0 (two HUD references plus the
 * fallback), one sprite each in 1..7. Sprite slot 9 took the fallback. */
static void SetupFull(void) {
  memset(g_wram, 0, sizeof g_wram);
  SetSlot(0, kDkc2PaletteSharedHudId, 3);
  for (unsigned i = 1; i < 8; i++) {
    SetSlot(i, (uint16_t)(0x6400u + i), 1);
    SetSprite(i, (uint16_t)(0x0100u + i), i);
  }
  SetSprite(9, 0x0124u, 0);
  Write16(kDkc2PaletteOverflowIdAddr, 0x6336u);
}

static void TestCallerMapping(void) {
  Dkc2PaletteOverflowCaller caller = {0x84FCu, 0, 0x10B4u, 0x0E40u, 0x0F5Au};
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x10B4u);
  caller.jsr_return = 0x8668u;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x10B4u);
  caller.jsr_return = 0x8C74u;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x0F5Au);
  caller.jsr_return = 0x86FAu; /* four-argument command branches on carry */
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0);
  caller.jsr_return = 0x1234u;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0);

  caller.jsr_return = 0x8A68u;
  caller.jsl_return = 0xB6AB31u;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x10B4u);
  caller.jsl_return = 0x36AB31u; /* same site without the FastROM bit */
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x10B4u);
  caller.jsl_return = 0xBEEA2Du;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x0E40u);
  caller.jsl_return = 0xB5F0EFu; /* HUD request: no record */
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0);
  caller.jsl_return = 0xBEC691u; /* builds an attribute in DP $60 */
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0);
  caller.jsr_return = 0x8A64u;
  caller.jsl_return = 0xBA95A9u;
  CHECK(Dkc2PaletteOverflowSpriteForCaller(&caller) == 0x10B4u);
  CHECK(Dkc2PaletteOverflowSpriteForCaller(NULL) == 0);
}

static void TestRecordBounds(void) {
  CHECK(Dkc2PaletteIsSpriteRecord(0x0D84u));
  CHECK(Dkc2PaletteIsSpriteRecord(0x0DE2u));
  CHECK(Dkc2PaletteIsSpriteRecord(0x1654u));
  CHECK(!Dkc2PaletteIsSpriteRecord(0x16B2u));
  CHECK(!Dkc2PaletteIsSpriteRecord(0x0DE3u));
  CHECK(!Dkc2PaletteIsSpriteRecord(0x0000u));
}

static void TestNote(void) {
  Dkc2PaletteOverflowTracker tracker;
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(!Dkc2PaletteOverflowNote(&tracker, 0x0DE3u, 0x6336u));
  CHECK(!Dkc2PaletteOverflowNote(&tracker, Record(9), 0));
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6337u));
  CHECK(tracker.count == 1 && tracker.entries[0].palette_id == 0x6337u);
  for (unsigned i = 10; i < 10u + kDkc2PaletteOverflowMaxTracked - 1u; i++)
    CHECK(Dkc2PaletteOverflowNote(&tracker, Record(i), 0x6300u));
  CHECK(tracker.count == kDkc2PaletteOverflowMaxTracked);
  CHECK(!Dkc2PaletteOverflowNote(&tracker, Record(20), 0x6300u));
}

static void TestWaitsForFreeSlotThenMoves(void) {
  SetupFull();
  Dkc2PaletteOverflowTracker tracker;
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  Dkc2PaletteOverflowEntry entry;
  /* Still full: no retry, but the entry is confirmed with its type. */
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(tracker.count == 1 && tracker.entries[0].type == 0x0124u);

  /* Sprite 6 releases palette 6. */
  SetSlot(6, 0, 0);
  Write16(Record(6), 0);
  CHECK(Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(entry.record == Record(9) && entry.palette_id == 0x6336u);

  /* The allocator placed it in slot 6: A = slot * 2 in the high byte. */
  CHECK(Dkc2PaletteOverflowAdopt(&tracker, g_wram, sizeof g_wram, &entry,
                                 0x0C00u, false));
  CHECK((Read16(Record(9) + kDkc2PaletteAttrOffset) & 0x0E00u) == 0x0C00u);
  CHECK((Read16(Record(9) + kDkc2PaletteAttrOffset) & 0xF1FFu) == 0x20A0u);
  CHECK(Read16(kDkc2PaletteSlotRefsAddr) == 2u);
  CHECK(tracker.count == 0);
}

static void TestSharesHeldId(void) {
  SetupFull();
  Dkc2PaletteOverflowTracker tracker;
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6403u));
  Dkc2PaletteOverflowEntry entry;
  CHECK(Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  /* A locked slot (negative count) cannot be shared. */
  SetSlot(3, 0x6403u, 0x8000u);
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
}

static void TestDropsStaleEntries(void) {
  SetupFull();
  SetSlot(7, 0, 0);
  Dkc2PaletteOverflowTracker tracker;
  Dkc2PaletteOverflowEntry entry;

  /* Released. */
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  Write16(Record(9), 0);
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(tracker.count == 0);

  /* Given a real palette by the cartridge. */
  SetSprite(9, 0x0124u, 4);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(tracker.count == 0);

  /* Record reused by another type after being confirmed. */
  SetSprite(9, 0x0124u, 0);
  SetSlot(7, 0x6407u, 1);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(tracker.count == 1);
  Write16(Record(9), 0x0200u);
  SetSlot(7, 0, 0);
  CHECK(!Dkc2PaletteOverflowPickRetry(&tracker, g_wram, sizeof g_wram, &entry));
  CHECK(tracker.count == 0);
}

static void TestOverflowAgainKeepsSprite(void) {
  SetupFull();
  Dkc2PaletteOverflowTracker tracker;
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(Dkc2PaletteOverflowNote(&tracker, Record(9), 0x6336u));
  Dkc2PaletteOverflowEntry entry = tracker.entries[0];
  Write16(kDkc2PaletteSlotRefsAddr, 4); /* the retry's own fallback */
  CHECK(!Dkc2PaletteOverflowAdopt(&tracker, g_wram, sizeof g_wram, &entry,
                                  0x0000u, true));
  CHECK(Read16(kDkc2PaletteSlotRefsAddr) == 3u);
  CHECK((Read16(Record(9) + kDkc2PaletteAttrOffset) & 0x0E00u) == 0);
  CHECK(tracker.count == 0);
}

static void TestRecoverSaved(void) {
  Dkc2PaletteOverflowTracker tracker;
  SetupFull();
  SetSlot(6, 0, 0);
  Write16(Record(6), 0);
  Dkc2PaletteOverflowReset(&tracker);
  CHECK(Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));
  CHECK(tracker.count == 1 && tracker.entries[0].record == Record(9) &&
        tracker.entries[0].palette_id == 0x6336u);
  /* Already tracked. */
  CHECK(!Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));

  /* The lost id is held by a slot again: nothing stuck. */
  Dkc2PaletteOverflowReset(&tracker);
  SetSlot(6, 0x6336u, 1);
  CHECK(!Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));
  SetSlot(6, 0, 0);

  /* Ambiguous: two sprites on palette 0. */
  SetSprite(12, 0x0150u, 0);
  CHECK(!Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));
  Write16(Record(12), 0);

  /* Slot 0 does not hold the shared HUD palette. */
  SetSlot(0, 0x6500u, 3);
  CHECK(!Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));
  SetSlot(0, kDkc2PaletteSharedHudId, 3);

  /* No overflow recorded. */
  Write16(kDkc2PaletteOverflowIdAddr, 0);
  CHECK(!Dkc2PaletteOverflowRecoverSaved(&tracker, g_wram, sizeof g_wram));
  CHECK(tracker.count == 0);
}

int main(void) {
  TestCallerMapping();
  TestRecordBounds();
  TestNote();
  TestWaitsForFreeSlotThenMoves();
  TestSharesHeldId();
  TestDropsStaleEntries();
  TestOverflowAgainKeepsSprite();
  TestRecoverSaved();
  puts("dkc2 palette overflow tests passed");
  return 0;
}
