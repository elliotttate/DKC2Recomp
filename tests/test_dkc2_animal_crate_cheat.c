#include "dkc2_animal_crate_cheat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_animal_crate_cheat.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

static uint8_t g_wram[kDkc2PauseCrateMinWramSize];

static void Write16(size_t address, uint16_t value) {
  g_wram[address] = (uint8_t)(value & 0xFFu);
  g_wram[address + 1u] = (uint8_t)(value >> 8);
}

static uint16_t Read16(size_t address) {
  return (uint16_t)(g_wram[address] | ((uint16_t)g_wram[address + 1u] << 8));
}

enum { kKongSlot = 0x0DE2u, kPartnerSlot = 0x0E40u };

/* Paused in a level with both Kongs in their cartridge slots and the active
 * Kong at (x, y). */
static void SetupPausedLevel(uint16_t x, uint16_t y) {
  memset(g_wram, 0, sizeof g_wram);
  Write16(kDkc2PauseCrateLevelFlagsAddr, 0x0200u | kDkc2PauseCratePausedFlagBit);
  Write16(kDkc2PauseCrateActiveKongAddr, kKongSlot);
  Write16(kKongSlot + kDkc2PauseCrateSpriteTypeOffset, 0x00E4u);
  Write16(kPartnerSlot + kDkc2PauseCrateSpriteTypeOffset, 0x00E8u);
  Write16(kKongSlot + kDkc2PauseCrateSpriteXOffset, x);
  Write16(kKongSlot + kDkc2PauseCrateSpriteYOffset, y);
}

static void Press(uint16_t buttons) {
  Write16(kDkc2PauseCrateActivePressedAddr, buttons);
}

static void TestNamesAndScripts(void) {
  static const struct {
    Dkc2PauseCrateKind kind;
    const char *name;
    uint16_t script;
    uint16_t animal;
  } kCrates[] = {
      {kDkc2PauseCrateRambi, "Rambi", 0x9326u, 0x02AFu},
      {kDkc2PauseCrateRattly, "Rattly", 0x9384u, 0x02ADu},
      {kDkc2PauseCrateSquawks, "Squawks", 0x9330u, 0x02AEu},
      {kDkc2PauseCrateSquitter, "Squitter", 0x939Cu, 0x02ACu},
      {kDkc2PauseCrateEnguarde, "Enguarde", 0x9364u, 0x02B0u},
  };
  for (size_t i = 0; i < sizeof kCrates / sizeof kCrates[0]; i++) {
    CHECK(strcmp(Dkc2PauseCrateName(kCrates[i].kind), kCrates[i].name) == 0);
    CHECK(Dkc2PauseCrateScriptPtr(kCrates[i].kind) == kCrates[i].script);
    CHECK(Dkc2PauseCrateAnimalId(kCrates[i].kind) == kCrates[i].animal);
  }
  CHECK(Dkc2PauseCrateName(kDkc2PauseCrateCount) == NULL);
  CHECK(Dkc2PauseCrateScriptPtr(kDkc2PauseCrateCount) == 0u);
  CHECK(Dkc2PauseCrateAnimalId(kDkc2PauseCrateCount) == 0u);
  CHECK(Dkc2PauseCrateName((Dkc2PauseCrateKind)-1) == NULL);
}

static void TestEnvironment(void) {
  CHECK(!Dkc2PauseCrateSpawnEnabledFromEnv(NULL, false));
  CHECK(Dkc2PauseCrateSpawnEnabledFromEnv(NULL, true));
  CHECK(!Dkc2PauseCrateSpawnEnabledFromEnv("0", true));
  CHECK(Dkc2PauseCrateSpawnEnabledFromEnv("1", false));
}

static void TestMenuGate(void) {
  CHECK(!Dkc2IsPauseCrateMenuActive(NULL, sizeof g_wram, true));
  SetupPausedLevel(0x0180u, 0x0120u);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram - 1u, true));
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, false));
  CHECK(Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));

  /* Unpaused. */
  Write16(kDkc2PauseCrateLevelFlagsAddr, 0x0200u);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));

  /* No live Kong record: map screens and transitions. */
  SetupPausedLevel(0x0180u, 0x0120u);
  Write16(kKongSlot + kDkc2PauseCrateSpriteTypeOffset, 0u);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));
  SetupPausedLevel(0x0180u, 0x0120u);
  Write16(kDkc2PauseCrateActiveKongAddr, kKongSlot + 1u);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));
  Write16(kDkc2PauseCrateActiveKongAddr, 0u);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));
  Write16(kDkc2PauseCrateActiveKongAddr, kDkc2PauseCrateSpriteTableEnd);
  CHECK(!Dkc2IsPauseCrateMenuActive(g_wram, sizeof g_wram, true));
}

static void TestSelection(void) {
  SetupPausedLevel(0x0180u, 0x0120u);
  Dkc2PauseCrateKind selected = kDkc2PauseCrateRambi;
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true, NULL));

  Press(kDkc2PauseCrateInputLeftBit);
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, false,
                                       &selected));
  CHECK(selected == kDkc2PauseCrateRambi);
  CHECK(Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                      &selected));
  CHECK(selected == kDkc2PauseCrateEnguarde);

  Press(kDkc2PauseCrateInputRightBit);
  CHECK(Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                      &selected));
  CHECK(selected == kDkc2PauseCrateRambi);
  static const Dkc2PauseCrateKind kOrder[] = {
      kDkc2PauseCrateRattly, kDkc2PauseCrateSquawks,
      kDkc2PauseCrateSquitter, kDkc2PauseCrateEnguarde,
      kDkc2PauseCrateRambi};
  for (size_t i = 0; i < sizeof kOrder / sizeof kOrder[0]; i++) {
    CHECK(Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                        &selected));
    CHECK(selected == kOrder[i]);
  }

  /* Both directions, or a pause control on the same frame, do nothing. */
  Press(kDkc2PauseCrateInputLeftBit | kDkc2PauseCrateInputRightBit);
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                       &selected));
  Press(kDkc2PauseCrateInputStartBit | kDkc2PauseCrateInputRightBit);
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                       &selected));
  Press(kDkc2PauseCrateInputSelectBit | kDkc2PauseCrateInputLeftBit);
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                       &selected));
  CHECK(selected == kDkc2PauseCrateRambi);

  /* Unpaused presses are gameplay input. */
  Write16(kDkc2PauseCrateLevelFlagsAddr, 0x0200u);
  Press(kDkc2PauseCrateInputRightBit);
  CHECK(!Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                       &selected));

  /* An out-of-range selection restarts from Rambi. */
  SetupPausedLevel(0x0180u, 0x0120u);
  selected = kDkc2PauseCrateCount;
  Press(kDkc2PauseCrateInputRightBit);
  CHECK(Dkc2UpdatePauseCrateSelection(g_wram, sizeof g_wram, true,
                                      &selected));
  CHECK(selected == kDkc2PauseCrateRattly);
}

static void TestDetect(void) {
  Dkc2PauseCrateRequest request;
  SetupPausedLevel(0x0180u, 0x0120u);

  Press(kDkc2PauseCrateInputBBit);
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, false,
                                   kDkc2PauseCrateSquawks, &request));
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                   kDkc2PauseCrateCount, &request));
  CHECK(Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                  kDkc2PauseCrateSquawks, &request));
  CHECK(request.kind == kDkc2PauseCrateSquawks);
  CHECK(request.script_ptr == kDkc2PauseCrateScriptSquawks);
  CHECK(request.spawn_x == 0x0180u);
  CHECK(request.spawn_y == 0x0120u);

  /* The partner Kong may be the active one. */
  Write16(kDkc2PauseCrateActiveKongAddr, kPartnerSlot);
  Write16(kPartnerSlot + kDkc2PauseCrateSpriteXOffset, 0x0200u);
  Write16(kPartnerSlot + kDkc2PauseCrateSpriteYOffset, 0x0100u);
  CHECK(Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                  kDkc2PauseCrateRambi, &request));
  CHECK(request.spawn_x == 0x0200u && request.spawn_y == 0x0100u);

  /* Not B, or B with Start/Select (which unpause or exit). */
  Press(0x0F80u);
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                   kDkc2PauseCrateRambi, &request));
  Press(kDkc2PauseCrateInputBBit | kDkc2PauseCrateInputStartBit);
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                   kDkc2PauseCrateRambi, &request));
  Press(kDkc2PauseCrateInputBBit | kDkc2PauseCrateInputSelectBit);
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                   kDkc2PauseCrateRambi, &request));
  CHECK(request.script_ptr == 0u);

  /* Full sprite table: fail before the cartridge allocator would. */
  Press(kDkc2PauseCrateInputBBit);
  for (uint16_t slot = kDkc2PauseCrateFirstFreeSlot;
       slot < kDkc2PauseCrateSpriteTableEnd;
       slot = (uint16_t)(slot + kDkc2PauseCrateSpriteStride))
    Write16(slot + kDkc2PauseCrateSpriteTypeOffset, 0x0100u);
  CHECK(!Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                   kDkc2PauseCrateRambi, &request));
  /* The last slot freeing up is enough. */
  Write16(kDkc2PauseCrateSpriteTableEnd - kDkc2PauseCrateSpriteStride, 0u);
  CHECK(Dkc2DetectPauseCrateSpawn(g_wram, sizeof g_wram, true,
                                  kDkc2PauseCrateRambi, NULL));
}

static void TestPlace(void) {
  enum { kSlot = 0x0F5Au };
  Dkc2PauseCrateRequest request = {kDkc2PauseCrateSquitter,
                                   kDkc2PauseCrateScriptSquitter, 0x0345u,
                                   0x0167u};
  SetupPausedLevel(0x0180u, 0x0120u);
  memset(g_wram + kSlot, 0xAA, kDkc2PauseCrateSpriteStride);
  Write16(kSlot + kDkc2PauseCrateSpriteTypeOffset, kDkc2PauseCrateType);
  Write16(kSlot + 0x36u, 0x02ACu);

  CHECK(!Dkc2PlacePauseCrate(NULL, sizeof g_wram, kSlot, &request));
  CHECK(!Dkc2PlacePauseCrate(g_wram, sizeof g_wram, kSlot, NULL));
  CHECK(!Dkc2PlacePauseCrate(g_wram, sizeof g_wram, kSlot + 2u, &request));
  CHECK(!Dkc2PlacePauseCrate(g_wram, sizeof g_wram, 0x0D84u, &request));

  /* A slot holding something else, or a different crate, is left alone. */
  request.kind = kDkc2PauseCrateRambi;
  CHECK(!Dkc2PlacePauseCrate(g_wram, sizeof g_wram, kSlot, &request));
  request.kind = kDkc2PauseCrateSquitter;
  Write16(kSlot + kDkc2PauseCrateSpriteTypeOffset, 0x0190u);
  CHECK(!Dkc2PlacePauseCrate(g_wram, sizeof g_wram, kSlot, &request));
  CHECK(Read16(kSlot + kDkc2PauseCrateSpriteXOffset) == 0xAAAAu);

  Write16(kSlot + kDkc2PauseCrateSpriteTypeOffset, kDkc2PauseCrateType);
  CHECK(Dkc2PlacePauseCrate(g_wram, sizeof g_wram, kSlot, &request));
  CHECK(Read16(kSlot + 0x04u) == 0x8000u);
  CHECK(Read16(kSlot + kDkc2PauseCrateSpriteXOffset) == 0x0345u);
  CHECK(Read16(kSlot + 0x08u) == 0x8000u);
  CHECK(Read16(kSlot + kDkc2PauseCrateSpriteYOffset) == 0x0167u);
  /* Only the position words are written. */
  CHECK(Read16(kSlot + 0x02u) == 0xAAAAu);
  CHECK(Read16(kSlot + 0x0Cu) == 0xAAAAu);
  CHECK(Read16(kSlot + 0x36u) == 0x02ACu);
}

static void TestSquawksDismountCarry(void) {
  /* Off: the cartridge's own branch ($4A bit 0) is kept exactly. */
  CHECK(Dkc2SquawksDismountCarry(0u, false) == 0u);
  CHECK(Dkc2SquawksDismountCarry(1u, false) == 1u);
  /* On: always take the cartridge's dismount path. */
  CHECK(Dkc2SquawksDismountCarry(0u, true) == 1u);
  CHECK(Dkc2SquawksDismountCarry(1u, true) == 1u);
}

int main(void) {
  TestNamesAndScripts();
  TestSquawksDismountCarry();
  TestEnvironment();
  TestMenuGate();
  TestSelection();
  TestDetect();
  TestPlace();
  puts("dkc2 animal crate cheat tests passed");
  return 0;
}
