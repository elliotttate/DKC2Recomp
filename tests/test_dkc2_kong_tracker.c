#include "dkc2_kong_tracker.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_kong_tracker.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

enum { kWramSize = 0x20000, kRomSize = 0x400000, kWidth = 256, kHeight = 224 };

static uint8_t s_wram[kWramSize];
static uint8_t *s_rom;
static uint8_t s_sram[0x800];
static uint8_t s_frame[kHeight][kWidth * 4];

static void Write16(uint8_t *memory, size_t address, uint16_t value) {
  memory[address] = (uint8_t)value;
  memory[address + 1u] = (uint8_t)(value >> 8);
}

/* Recomputes the sidecar's FNV-1a checksum over its first 108 bytes. */
static void Reseal(uint8_t *data) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 108; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  data[108] = (uint8_t)hash;
  data[109] = (uint8_t)(hash >> 8);
  data[110] = (uint8_t)(hash >> 16);
  data[111] = (uint8_t)(hash >> 24);
}

static size_t HiRomOffset(uint32_t pc24) {
  return ((size_t)((pc24 >> 16) & 0x3fu) << 16) | (uint16_t)pc24;
}

/* Synthetic node records in bank $B4: a kind in the global table, a record
 * whose word at +7 points to a length-prefixed name and whose word at +9 is
 * the level. */
static uint16_t s_next_record = 0xd000;

static void AddNode(unsigned node, uint8_t kind, uint16_t level,
                    const char *name) {
  const uint16_t record = s_next_record;
  const uint16_t name_at = (uint16_t)(record + 0x0f);
  s_next_record = (uint16_t)(record + 0x30);
  const size_t entry =
      HiRomOffset(kDkc2KongRomNodeTable) + node * kDkc2KongNodeEntrySize;
  s_rom[entry] = kind;
  Write16(s_rom, entry + 1u, record);
  Write16(s_rom, HiRomOffset(0xb40000u | record) + kDkc2KongRecordName,
          name_at);
  Write16(s_rom, HiRomOffset(0xb40000u | record) + kDkc2KongRecordLevel,
          level);
  const size_t length = strlen(name);
  s_rom[HiRomOffset(0xb40000u | name_at)] = (uint8_t)length;
  memcpy(s_rom + HiRomOffset(0xb40000u | name_at) + 1u, name, length);
}

static void AddWorld(unsigned world, const uint16_t *nodes, unsigned count) {
  const uint16_t descriptor = (uint16_t)(0xc500 + world * 0x40u);
  Write16(s_rom, HiRomOffset(kDkc2KongRomWorldDescriptors) + world * 2u,
          descriptor);
  const size_t base = HiRomOffset(0xb40000u | descriptor);
  Write16(s_rom, base + kDkc2KongWorldNodeCount, (uint16_t)count);
  for (unsigned i = 0; i < count; i++)
    Write16(s_rom, base + kDkc2KongWorldNodes + i * 2u, nodes[i]);
}

/* Isle nodes 0-3; world 1 with two levels, a boss, a shop, and a waypoint
 * carrying another world's level word; world 4 split across worlds 4 and
 * 9 like Krazy Kremland. */
static void BuildRom(void) {
  memset(s_rom, 0, kRomSize);
  s_next_record = 0xd000;
  AddNode(0x00, 0x0e, 0, "WORLD ONE");
  AddNode(0x03, 0x0e, 0, "WORLD FOUR");
  AddNode(0x41, 0x0e, 0x03, "LEVEL A");
  AddNode(0x42, 0x0e, 0x0c, "LEVEL B");
  AddNode(0x0e, 0x10, 0x09, "BOSS");
  AddNode(0x0d, 0x06, 0x00, "SHOP");
  AddNode(0x27, 0x0e, 0x05, "A");
  AddNode(0x45, 0x0e, 0x2c, "LOWER");
  AddNode(0x49, 0x0e, 0x0e, "UPPER");
  static const uint16_t isle[] = {0x00, 0x01, 0x02, 0x03};
  static const uint16_t world1[] = {0x41, 0x42, 0x0e, 0x0d, 0x27, 0x41};
  static const uint16_t world4[] = {0x45};
  static const uint16_t world9[] = {0x49, 0x27};
  AddWorld(0, isle, 4);
  AddWorld(1, world1, 6);
  AddWorld(4, world4, 1);
  AddWorld(9, world9, 2);
}

/* Rewrites a file's marker and checksums the way the cartridge's save
 * does. */
static void SealSlot(uint8_t *data, uint16_t marker) {
  Write16(data, 4, marker);
  uint16_t sum = 0, parity = 0;
  for (unsigned offset = 6; offset < kDkc2KongSramChecksumEnd; offset += 2) {
    const uint16_t word = (uint16_t)(data[offset] | (data[offset + 1u] << 8));
    sum = (uint16_t)(sum + word);
    parity ^= word;
  }
  Write16(data, 0, sum);
  Write16(data, 2, parity);
}

static void SetSlotData(unsigned slot, uint8_t seed) {
  uint8_t *data = s_sram + kDkc2KongSramSlotOffsets[slot];
  for (unsigned i = 0; i < kDkc2KongSramSlotSize; i++)
    data[i] = (uint8_t)(seed + i * 7u);
  SealSlot(data, 0x0052);
}

static void TestSlotValidity(void) {
  uint8_t slot[kDkc2KongSramSlotSize];
  memset(slot, 0, sizeof slot);
  CHECK(!Dkc2KongSramSlotValid(slot));
  CHECK(!Dkc2KongSramSlotValid(NULL));
  SealSlot(slot, 0x0052);
  CHECK(Dkc2KongSramSlotValid(slot)); /* an all-zero body sums to 0 */
  CHECK(slot[0] == 0 && slot[1] == 0);
  for (unsigned i = 6; i < sizeof slot; i++) slot[i] = (uint8_t)(i * 13u);
  SealSlot(slot, 0x0253); /* bit 0 of the marker is ignored */
  CHECK(Dkc2KongSramSlotValid(slot));
  slot[100] ^= 1u;
  CHECK(!Dkc2KongSramSlotValid(slot)); /* sum and XOR disagree */
  slot[100] ^= 1u;
  slot[kDkc2KongSramChecksumEnd + 1] ^= 0x5a; /* outside the checksum */
  CHECK(Dkc2KongSramSlotValid(slot));
  SealSlot(slot, 0x0352);
  CHECK(!Dkc2KongSramSlotValid(slot)); /* unknown marker */
  SealSlot(slot, 0x0152);
  Write16(slot, 0, (uint16_t)((slot[0] | (slot[1] << 8)) + 1));
  CHECK(!Dkc2KongSramSlotValid(slot)); /* the file select's erase */
}

static void TestFlagsAndFormat(void) {
  Dkc2KongTracker tracker;
  Dkc2KongTrackerInit(&tracker);
  CHECK(!Dkc2KongTrackerSetLevel(&tracker, 0, 0)); /* no level */
  CHECK(!Dkc2KongTrackerSetLevel(&tracker, 0, 0x100));
  CHECK(!Dkc2KongTrackerSetLevel(&tracker, 3, 0x03)); /* bad slot */
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 1, 0x03));
  CHECK(!Dkc2KongTrackerSetLevel(&tracker, 1, 0x03));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 2, 0x9a));
  CHECK(tracker.dirty);
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 1, 0x03));
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));

  uint8_t data[kDkc2KongFileSize];
  CHECK(Dkc2KongTrackerSerialize(&tracker, data, sizeof data - 1) == 0);
  CHECK(Dkc2KongTrackerSerialize(&tracker, data, sizeof data) ==
        kDkc2KongFileSize);
  CHECK(memcmp(data, "DKC2KONG", 8) == 0);

  Dkc2KongTracker copy;
  Dkc2KongTrackerInit(&copy);
  CHECK(Dkc2KongTrackerDeserialize(&copy, data, sizeof data));
  CHECK(Dkc2KongTrackerHasLevel(&copy, 1, 0x03));
  CHECK(Dkc2KongTrackerHasLevel(&copy, 2, 0x9a));
  CHECK(!copy.dirty);

  data[20] ^= 1u;
  CHECK(!Dkc2KongTrackerDeserialize(&copy, data, sizeof data));
  data[20] ^= 1u;
  /* A future version with a valid checksum is rejected for its version. */
  data[8] = 2;
  Reseal(data);
  CHECK(!Dkc2KongTrackerDeserialize(&copy, data, sizeof data));
  /* A valid image is rejected only for its size. */
  data[8] = 1;
  Reseal(data);
  CHECK(Dkc2KongTrackerDeserialize(&copy, data, sizeof data));
  CHECK(!Dkc2KongTrackerDeserialize(&copy, data, sizeof data - 1));
  /* A stored level 0 is dropped. */
  data[12] |= 1u;
  Reseal(data);
  CHECK(Dkc2KongTrackerDeserialize(&copy, data, sizeof data));
  CHECK(!Dkc2KongTrackerHasLevel(&copy, 0, 0));
}

static void TestFiles(void) {
  const char *path = "test_dkc2_kong_tracker.dat";
  remove(path);
  Dkc2KongTracker tracker;
  Dkc2KongTrackerInit(&tracker);
  CHECK(Dkc2KongTrackerLoadFile(&tracker, path) == kDkc2KongLoadMissing);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x0b));
  CHECK(Dkc2KongTrackerSaveFile(&tracker, path));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x15));
  CHECK(Dkc2KongTrackerSaveFile(&tracker, path)); /* replaces the file */

  Dkc2KongTracker loaded;
  Dkc2KongTrackerInit(&loaded);
  CHECK(Dkc2KongTrackerLoadFile(&loaded, path) == kDkc2KongLoadOk);
  CHECK(Dkc2KongTrackerHasLevel(&loaded, 0, 0x0b));
  CHECK(Dkc2KongTrackerHasLevel(&loaded, 0, 0x15));

  FILE *stream = fopen(path, "wb");
  CHECK(stream != NULL);
  fputs("not a tracker", stream);
  fclose(stream);
  CHECK(Dkc2KongTrackerLoadFile(&loaded, path) == kDkc2KongLoadInvalid);
  remove(path);

  /* A missing file in a usable directory can be created, and the probe
   * leaves no sidecar behind (dkc2_sidecar_file.h; test_dkc2_sidecar_file
   * covers names that are already taken). */
  CHECK(Dkc2KongTrackerCanCreate(path));
  stream = fopen(path, "rb");
  CHECK(stream == NULL);
  if (stream) fclose(stream);

  /* A parent that is a regular file, made here (removed first, then
   * created exclusively so the setup never follows a planted symlink), can
   * never hold the file, whatever an earlier run left behind. */
  const char *not_a_dir = "test_dkc2_kong_not_a_dir";
  remove(not_a_dir);
  stream = fopen(not_a_dir, "wbx");
  CHECK(stream != NULL);
  if (stream) fclose(stream);
  CHECK(!Dkc2KongTrackerCanCreate("test_dkc2_kong_not_a_dir/kong.dat"));
  remove(not_a_dir);
  CHECK(!Dkc2KongTrackerCanCreate(""));
}

static void TestObserve(void) {
  Dkc2KongTracker tracker;
  Dkc2KongTrackerInit(&tracker);
  memset(s_wram, 0, sizeof s_wram);
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopLevel);
  Write16(s_wram, kDkc2KongWramEnteredLevel, 0x0b);
  Write16(s_wram, kDkc2KongWramFileSlot, 1);

  s_wram[kDkc2KongWramLetters] = 0x07; /* K, O, N */
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  s_wram[kDkc2KongWramLetters] = 0x1f; /* plus G and an unrelated bit */
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopMap);
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopLevel);
  CHECK(Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 1, 0x0b));
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));

  /* A sub-area or bonus room changes $D3, not the entered level. */
  Write16(s_wram, 0x00d3, 0x9f);
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 1, 0x9f));

  /* Contest mode, unknown slots, and level 0 are ignored. */
  Write16(s_wram, kDkc2KongWramEnteredLevel, 0x15);
  Write16(s_wram, kDkc2KongWramPlayMode, kDkc2KongPlayModeContest);
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc2KongWramPlayMode, 1); /* TEAM */
  Write16(s_wram, kDkc2KongWramFileSlot, 3);
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc2KongWramFileSlot, 2);
  Write16(s_wram, kDkc2KongWramEnteredLevel, 0);
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc2KongWramEnteredLevel, 0x15);
  CHECK(Dkc2KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 2, 0x15));
  CHECK(!Dkc2KongTrackerObserveFrame(&tracker, s_wram, 0x100));
}

static void TestSramSync(void) {
  Dkc2KongTracker tracker;
  Dkc2KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 1);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x03));
  /* Slot 1 has no SRAM file: it is played only through quick saves. */
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 1, 0x0c));
  tracker.dirty = false;

  /* Adopting the SRAM at startup changes no flag, blank slots included. */
  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(!tracker.dirty);
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 1, 0x0c));
  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(!tracker.dirty);

  /* An ordinary save changes a slot without touching its flags. */
  s_sram[kDkc2KongSramSlotOffsets[0] + 40] ^= 0x55;
  SealSlot(s_sram + kDkc2KongSramSlotOffsets[0], 0x0052);
  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));

  /* A new game in an empty slot starts with no flags. */
  SetSlotData(2, 9);
  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));

  /* Copy slot 0 onto slot 1. */
  memcpy(s_sram + kDkc2KongSramSlotOffsets[1],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  CHECK(Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 1, 0x03));
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 1, 0x0c)); /* replaced */
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));

  /* Erase slot 0 the way the file select does (its first word incremented,
   * the rest left behind) and fill slot 1 with a pattern. */
  uint8_t *slot0 = s_sram + kDkc2KongSramSlotOffsets[0];
  Write16(slot0, 0, (uint16_t)((slot0[0] | (slot0[1] << 8)) + 1u));
  memset(s_sram + kDkc2KongSramSlotOffsets[1], 0x55, kDkc2KongSramSlotSize);
  tracker.dirty = false;
  CHECK(Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(tracker.dirty);
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 1, 0x03));

  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, 0x7ff));

  /* A quick save from before slot 2 held a file restores blank SRAM there:
   * after the rebase the flags stay, and the next sync sees no change. */
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 2, 0x15));
  memset(s_sram + kDkc2KongSramSlotOffsets[2], 0, kDkc2KongSramSlotSize);
  Dkc2KongTrackerRebaseSram(&tracker, s_sram, sizeof s_sram);
  CHECK(!Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 2, 0x15));
  /* Before the first sync a rebase does nothing; startup then adopts the
   * SRAM, still keeping the flags of the slot without a file (the reported
   * case: Pirate Panic recorded on a file never saved in game, then the
   * app restarted). */
  Dkc2KongTracker fresh;
  Dkc2KongTrackerInit(&fresh);
  CHECK(Dkc2KongTrackerSetLevel(&fresh, 2, 0x15));
  fresh.dirty = false;
  Dkc2KongTrackerRebaseSram(&fresh, s_sram, sizeof s_sram);
  CHECK(!fresh.sram_shadow_valid);
  CHECK(!Dkc2KongTrackerSyncSram(&fresh, s_sram, sizeof s_sram));
  CHECK(fresh.sram_shadow_valid && !fresh.dirty);
  CHECK(Dkc2KongTrackerHasLevel(&fresh, 2, 0x15));

  /* Two slots hold the same image with different flags (played without
   * saving after a copy); a copy onto the third keeps only the levels both
   * agree on. */
  Dkc2KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  memcpy(s_sram + kDkc2KongSramSlotOffsets[1],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x03));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x0c));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 1, 0x03));
  (void)Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram);
  memcpy(s_sram + kDkc2KongSramSlotOffsets[2],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  CHECK(Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 2, 0x03));
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 2, 0x0c));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 0, 0x0c));

  /* One slot copied onto both others within one sync: each copy takes the
   * unchanged source's flags, not the intersection with the other
   * destination's old flags. */
  Dkc2KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  SetSlotData(1, 5);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x03));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x0c));
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 1, 0x15));
  (void)Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram);
  memcpy(s_sram + kDkc2KongSramSlotOffsets[1],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  memcpy(s_sram + kDkc2KongSramSlotOffsets[2],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  CHECK(Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  for (unsigned slot = 1; slot < 3; slot++) {
    CHECK(Dkc2KongTrackerHasLevel(&tracker, slot, 0x03));
    CHECK(Dkc2KongTrackerHasLevel(&tracker, slot, 0x0c));
    CHECK(!Dkc2KongTrackerHasLevel(&tracker, slot, 0x15));
  }

  /* A slot saved and copied within one sync is still the source: no
   * unchanged slot holds the image, so the changed one is used. */
  Dkc2KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x03));
  (void)Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram);
  s_sram[kDkc2KongSramSlotOffsets[0] + 40] ^= 0x55;
  SealSlot(s_sram + kDkc2KongSramSlotOffsets[0], 0x0052);
  memcpy(s_sram + kDkc2KongSramSlotOffsets[1],
         s_sram + kDkc2KongSramSlotOffsets[0], kDkc2KongSramSlotSize);
  CHECK(Dkc2KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 1, 0x03));
  CHECK(Dkc2KongTrackerHasLevel(&tracker, 0, 0x03));
}

static Dkc2KongBadge Badge(const Dkc2KongTracker *tracker) {
  return Dkc2KongTrackerMapBadge(tracker, s_wram, sizeof s_wram, s_rom,
                                 kRomSize);
}

static void TestWorldsAndMapBadge(void) {
  BuildRom();
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, 0x41) == 0x03);
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, 0x0e) == 0); /* boss */
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, 0x0d) == 0); /* shop */
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, 0x27) == 0); /* waypoint */
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, 0x00) == 0); /* isle */
  CHECK(Dkc2KongNodeLevel(s_rom, kRomSize, kDkc2KongNodeCount) == 0);

  uint8_t levels[8];
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 0, levels, 8) == 0);
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 8, levels, 8) == 0);
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 1, levels, 8) == 2);
  CHECK(levels[0] == 0x03 && levels[1] == 0x0c);
  /* Krazy Kremland: world 4 and its second map, world 9. */
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 4, levels, 8) == 2);
  CHECK(levels[0] == 0x2c && levels[1] == 0x0e);
  CHECK(Dkc2KongWorldLevels(s_rom, 0x1000, 1, levels, 8) == 0);
  /* A short or absent output still counts every level, writing only what
   * fits. */
  memset(levels, 0xee, sizeof levels);
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 1, levels, 1) == 2);
  CHECK(levels[0] == 0x03 && levels[1] == 0xee);
  CHECK(Dkc2KongWorldLevels(s_rom, kRomSize, 1, NULL, 0) == 2);

  Dkc2KongTracker tracker;
  Dkc2KongTrackerInit(&tracker);
  memset(s_wram, 0, sizeof s_wram);
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopMap);
  Write16(s_wram, kDkc2KongWramFileSlot, 0);

  /* World 1, standing on level $03. */
  Write16(s_wram, kDkc2KongWramMapWorld, 1);
  Write16(s_wram, kDkc2KongWramMapNode, 0x41);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x03));
  CHECK(Badge(&tracker) == kDkc2KongBadgeLevel);
  /* The boss node never earns a badge. */
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x09));
  Write16(s_wram, kDkc2KongWramMapNode, 0x0e);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  /* Not while a level runs, in contest mode, nor for another slot. */
  Write16(s_wram, kDkc2KongWramMapNode, 0x41);
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopLevel);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopMap);
  Write16(s_wram, kDkc2KongWramPlayMode, kDkc2KongPlayModeContest);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  Write16(s_wram, kDkc2KongWramPlayMode, 0);
  Write16(s_wram, kDkc2KongWramFileSlot, 1);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  Write16(s_wram, kDkc2KongWramFileSlot, 0);

  /* Isle node 0 opens world 1 and lights once both its levels are done. */
  Write16(s_wram, kDkc2KongWramMapWorld, 0);
  Write16(s_wram, kDkc2KongWramMapNode, 0);
  CHECK(!Dkc2KongTrackerWorldComplete(&tracker, 0, s_rom, kRomSize, 1));
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x0c));
  CHECK(Dkc2KongTrackerWorldComplete(&tracker, 0, s_rom, kRomSize, 1));
  CHECK(Badge(&tracker) == kDkc2KongBadgeWorld);
  /* The waypoint's level word does not count against world 1. */
  CHECK(!Dkc2KongTrackerHasLevel(&tracker, 0, 0x05));

  /* Isle node 3 needs both Krazy Kremland maps. */
  Write16(s_wram, kDkc2KongWramMapNode, 3);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x2c));
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
  CHECK(Dkc2KongTrackerSetLevel(&tracker, 0, 0x0e));
  CHECK(Badge(&tracker) == kDkc2KongBadgeWorld);
  /* Isle nodes past the seventh world are ignored. */
  Write16(s_wram, kDkc2KongWramMapNode, 7);
  CHECK(Badge(&tracker) == kDkc2KongBadgeNone);
}

static void TestMapSettle(void) {
  Dkc2KongMapSettle settle = {0, 0};
  memset(s_wram, 0, sizeof s_wram);
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopMap);
  Write16(s_wram, kDkc2KongWramMapWorld, 1);
  Write16(s_wram, kDkc2KongWramMapNode, 0x41);
  for (int i = 1; i < kDkc2KongSettleFrames; i++)
    CHECK(!Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));

  /* Setting off for another node restarts the wait. */
  Write16(s_wram, kDkc2KongWramMapNode, 0x42);
  CHECK(!Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  for (int i = 2; i < kDkc2KongSettleFrames; i++)
    CHECK(!Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));

  /* Leaving the map resets it. */
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopLevel);
  CHECK(!Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc2KongWramMainLoop, kDkc2KongMainLoopMap);
  CHECK(!Dkc2KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(!Dkc2KongMapSettleUpdate(NULL, s_wram, sizeof s_wram));
}

static void PaintName(int top, int left, int right) {
  memset(s_frame, 0x40, sizeof s_frame);
  for (int y = top; y < top + 14; y++)
    for (int x = left; x <= right; x++) {
      uint8_t *pixel = &s_frame[y][x * 4];
      const bool ink = ((x + y) % 3) != 0;
      pixel[0] = 0x00;
      pixel[1] = ink ? 0xff : 0x00;
      pixel[2] = ink ? 0xff : 0x00;
    }
}

static bool Yellow(int x, int y) {
  const uint8_t *pixel = &s_frame[y][x * 4];
  return pixel[0] == 0x00 && pixel[1] == 0xff && pixel[2] == 0xff;
}

static int YellowIn(int x0, int x1, int y0, int y1) {
  int count = 0;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) count += Yellow(x, y);
  return count;
}

static bool Draw(bool isle) {
  return Dkc2KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, kHeight, isle);
}

static void TestDrawBadge(void) {
  memset(s_frame, 0x40, sizeof s_frame);
  CHECK(!Draw(false));
  CHECK(!Draw(true));
  CHECK(YellowIn(0, kWidth - 1, 0, kHeight - 1) == 0);

  /* Beside a short world-map name. */
  PaintName(201, 90, 160);
  const int name = YellowIn(0, kWidth - 1, 0, kHeight - 1);
  CHECK(!Draw(true)); /* nothing in the isle rows */
  CHECK(Draw(false));
  CHECK(YellowIn(170, 200, 201, 214) > 60);
  CHECK(YellowIn(0, kWidth - 1, 0, 199) == 0);
  CHECK(!Yellow(169, 207) && !Yellow(201, 207)); /* outline */
  CHECK(YellowIn(0, kWidth - 1, 0, kHeight - 1) > name);

  /* Above a world-map name too wide to leave room. */
  PaintName(201, 20, 236);
  CHECK(Draw(false));
  CHECK(YellowIn(0, kWidth - 1, 184, 197) > 60);
  CHECK(YellowIn(237, kWidth - 1, 201, 214) == 0);

  /* Beside an isle name, and below one too wide. */
  PaintName(12, 60, 190);
  CHECK(!Draw(false));
  CHECK(Draw(true));
  CHECK(YellowIn(200, 230, 12, 25) > 60);
  PaintName(12, 20, 236);
  CHECK(Draw(true));
  CHECK(YellowIn(0, kWidth - 1, 29, 42) > 60);
  CHECK(YellowIn(0, kWidth - 1, 0, 11) == 0);

  /* A wide frame keeps the badge on the name's right. */
  static uint8_t wide[kHeight][342 * 4];
  memset(wide, 0x40, sizeof wide);
  for (int y = 201; y <= 214; y++)
    for (int x = 123; x <= 216; x++) {
      wide[y][x * 4] = 0x00;
      wide[y][x * 4 + 1] = 0xff;
      wide[y][x * 4 + 2] = 0xff;
    }
  CHECK(Dkc2KongDrawBadge(&wide[0][0], 342 * 4, 342, kHeight, false));
  CHECK(wide[207][226 * 4 + 1] == 0xff && wide[207][226 * 4 + 2] == 0xff);

  CHECK(!Dkc2KongDrawBadge(NULL, kWidth * 4, kWidth, kHeight, false));
  CHECK(!Dkc2KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, 200, false));
}

int main(void) {
  s_rom = (uint8_t *)calloc(1, kRomSize);
  CHECK(s_rom != NULL);
  CHECK(Dkc2KongBadgesEnabledFromEnv(NULL));
  CHECK(Dkc2KongBadgesEnabledFromEnv("1"));
  CHECK(!Dkc2KongBadgesEnabledFromEnv("0"));
  TestSlotValidity();
  TestFlagsAndFormat();
  TestFiles();
  TestObserve();
  TestSramSync();
  TestWorldsAndMapBadge();
  TestMapSettle();
  TestDrawBadge();
  free(s_rom);
  puts("test_dkc2_kong_tracker: ok");
  return 0;
}
