#include "dkc2_kong_tracker.h"

#include "dkc2_sidecar_file.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

const uint16_t kDkc2KongSramSlotOffsets[kDkc2KongSlotCount] = {
    0x008, 0x2b0, 0x558};

static const uint8_t kMagic[8] = {'D', 'K', 'C', '2', 'K', 'O', 'N', 'G'};
enum { kFileVersion = 1, kChecksumOffset = 108 };

void Dkc2KongTrackerInit(Dkc2KongTracker *tracker) {
  if (tracker) memset(tracker, 0, sizeof *tracker);
}

bool Dkc2KongTrackerHasLevel(const Dkc2KongTracker *tracker, unsigned slot,
                             unsigned level) {
  if (!tracker || slot >= kDkc2KongSlotCount || level > 0xffu) return false;
  return (tracker->levels[slot][level >> 3] >> (level & 7u)) & 1u;
}

bool Dkc2KongTrackerSetLevel(Dkc2KongTracker *tracker, unsigned slot,
                             unsigned level) {
  if (!tracker || slot >= kDkc2KongSlotCount || level == 0 || level > 0xffu)
    return false;
  if (Dkc2KongTrackerHasLevel(tracker, slot, level)) return false;
  tracker->levels[slot][level >> 3] |= (uint8_t)(1u << (level & 7u));
  tracker->dirty = true;
  return true;
}

static bool SlotHasFlags(const Dkc2KongTracker *tracker, unsigned slot) {
  for (unsigned i = 0; i < kDkc2KongLevelBitmapBytes; i++)
    if (tracker->levels[slot][i]) return true;
  return false;
}

void Dkc2KongTrackerClearSlot(Dkc2KongTracker *tracker, unsigned slot) {
  if (!tracker || slot >= kDkc2KongSlotCount) return;
  if (SlotHasFlags(tracker, slot)) tracker->dirty = true;
  memset(tracker->levels[slot], 0, sizeof tracker->levels[slot]);
}

static uint32_t Fnv1a(const uint8_t *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void Put32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static uint32_t Get32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
         ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

size_t Dkc2KongTrackerSerialize(const Dkc2KongTracker *tracker, uint8_t *out,
                                size_t capacity) {
  if (!tracker || !out || capacity < kDkc2KongFileSize) return 0;
  memcpy(out, kMagic, sizeof kMagic);
  Put32(out + 8, kFileVersion);
  memcpy(out + 12, tracker->levels, sizeof tracker->levels);
  Put32(out + kChecksumOffset, Fnv1a(out, kChecksumOffset));
  return kDkc2KongFileSize;
}

bool Dkc2KongTrackerDeserialize(Dkc2KongTracker *tracker, const uint8_t *data,
                                size_t size) {
  if (!tracker || !data || size != kDkc2KongFileSize) return false;
  if (memcmp(data, kMagic, sizeof kMagic) != 0) return false;
  if (Get32(data + 8) != kFileVersion) return false;
  if (Get32(data + kChecksumOffset) != Fnv1a(data, kChecksumOffset))
    return false;
  memcpy(tracker->levels, data + 12, sizeof tracker->levels);
  /* Level 0 is never recorded. */
  for (unsigned slot = 0; slot < kDkc2KongSlotCount; slot++)
    tracker->levels[slot][0] &= 0xfeu;
  tracker->dirty = false;
  return true;
}

Dkc2KongLoadResult Dkc2KongTrackerLoadFile(Dkc2KongTracker *tracker,
                                           const char *path) {
  if (!tracker || !path) return kDkc2KongLoadInvalid;
  errno = 0;
  FILE *stream = fopen(path, "rb");
  if (!stream)
    return errno == ENOENT ? kDkc2KongLoadMissing : kDkc2KongLoadUnusable;
  uint8_t data[kDkc2KongFileSize + 1];
  size_t size = fread(data, 1, sizeof data, stream);
  fclose(stream);
  return Dkc2KongTrackerDeserialize(tracker, data, size)
             ? kDkc2KongLoadOk
             : kDkc2KongLoadInvalid;
}

bool Dkc2KongTrackerCanCreate(const char *path) {
  return Dkc2SidecarCanCreate(path);
}

bool Dkc2KongTrackerSaveFile(const Dkc2KongTracker *tracker,
                             const char *path) {
  if (!tracker || !path) return false;
  uint8_t data[kDkc2KongFileSize];
  if (Dkc2KongTrackerSerialize(tracker, data, sizeof data) != sizeof data)
    return false;
  return Dkc2SidecarWriteFile(path, data, sizeof data);
}

static uint16_t Read16(const uint8_t *memory, size_t address) {
  return (uint16_t)(memory[address] | ((uint16_t)memory[address + 1u] << 8));
}

static bool WramUsable(const uint8_t *wram, size_t wram_size) {
  return wram && wram_size >= 0x2000u;
}

/* The contest mode keeps two players' progress in one file; leave it be. */
static bool SinglePlayFile(const uint8_t *wram, unsigned *slot) {
  if (Read16(wram, kDkc2KongWramPlayMode) == kDkc2KongPlayModeContest)
    return false;
  *slot = Read16(wram, kDkc2KongWramFileSlot);
  return *slot < kDkc2KongSlotCount;
}

bool Dkc2KongTrackerObserveFrame(Dkc2KongTracker *tracker,
                                 const uint8_t *wram, size_t wram_size) {
  if (!tracker || !WramUsable(wram, wram_size)) return false;
  if (Read16(wram, kDkc2KongWramMainLoop) != kDkc2KongMainLoopLevel)
    return false;
  if ((wram[kDkc2KongWramLetters] & kDkc2KongAllLetters) !=
      kDkc2KongAllLetters)
    return false;
  unsigned slot;
  if (!SinglePlayFile(wram, &slot)) return false;
  return Dkc2KongTrackerSetLevel(tracker, slot,
                                 Read16(wram, kDkc2KongWramEnteredLevel));
}

bool Dkc2KongSramSlotValid(const uint8_t *slot) {
  if (!slot) return false;
  const uint16_t marker = (uint16_t)(Read16(slot, 4) & 0xfffeu);
  if (marker != 0x0052u && marker != 0x0152u && marker != 0x0252u)
    return false;
  uint16_t sum = 0, parity = 0;
  for (unsigned offset = 6; offset < kDkc2KongSramChecksumEnd; offset += 2) {
    sum = (uint16_t)(sum + Read16(slot, offset));
    parity ^= Read16(slot, offset);
  }
  return Read16(slot, 0) == sum && Read16(slot, 2) == parity;
}

static bool SlotBlank(const uint8_t *slot) {
  return !Dkc2KongSramSlotValid(slot);
}

bool Dkc2KongTrackerSyncSram(Dkc2KongTracker *tracker, const uint8_t *sram,
                             size_t sram_size) {
  if (!tracker || !sram ||
      sram_size < (size_t)kDkc2KongSramSlotOffsets[kDkc2KongSlotCount - 1] +
                      kDkc2KongSramSlotSize)
    return false;
  const uint8_t *slots[kDkc2KongSlotCount];
  for (unsigned i = 0; i < kDkc2KongSlotCount; i++)
    slots[i] = sram + kDkc2KongSramSlotOffsets[i];

  const bool was_dirty = tracker->dirty;
  tracker->dirty = false;
  /* The first call only takes the SRAM as the baseline. A slot without a
   * valid file may still be a file played only from quick saves, never
   * saved at Kong Kollege, so its flags are not stale. */
  if (tracker->sram_shadow_valid) {
    uint8_t before[kDkc2KongSlotCount][kDkc2KongLevelBitmapBytes];
    memcpy(before, tracker->levels, sizeof before);
    for (unsigned j = 0; j < kDkc2KongSlotCount; j++) {
      if (memcmp(slots[j], tracker->sram_shadow[j], kDkc2KongSramSlotSize) ==
          0)
        continue;
      if (SlotBlank(slots[j])) {
        Dkc2KongTrackerClearSlot(tracker, j);
        continue;
      }
      /* A slot that now matches another slot's image was copied from it.
       * Copy from the flags as they were before this sync, so a copy onto a
       * slot that also changed this frame cannot chain. When several slots
       * hold the same image (a slot copied twice, or played without saving
       * after a copy), the source is ambiguous: keep only the levels every
       * candidate agrees on, so no candidate's extra badges are carried
       * onto the wrong file. A candidate that also changed this sync is
       * usually another destination of the same copy, and its pre-sync
       * flags belong to the file it replaced, so unchanged candidates are
       * preferred; changed ones are used only when no unchanged slot holds
       * the image (a slot saved and copied within one sync), and then only
       * if they held a file before this sync. */
      uint8_t copied[kDkc2KongLevelBitmapBytes];
      bool any_source = false;
      for (int pass = 0; pass < 2 && !any_source; pass++) {
        for (unsigned i = 0; i < kDkc2KongSlotCount; i++) {
          if (i == j || SlotBlank(slots[i]) ||
              memcmp(slots[i], slots[j], kDkc2KongSramSlotSize) != 0)
            continue;
          const bool changed = memcmp(slots[i], tracker->sram_shadow[i],
                                      kDkc2KongSramSlotSize) != 0;
          if (changed != (pass == 1)) continue;
          /* A slot that was blank before this sync cannot be the source. */
          if (changed && SlotBlank(tracker->sram_shadow[i])) continue;
          if (!any_source) {
            memcpy(copied, before[i], sizeof copied);
            any_source = true;
          } else {
            for (unsigned b = 0; b < kDkc2KongLevelBitmapBytes; b++)
              copied[b] &= before[i][b];
          }
        }
      }
      if (any_source &&
          memcmp(tracker->levels[j], copied, sizeof copied) != 0) {
        memcpy(tracker->levels[j], copied, sizeof copied);
        tracker->dirty = true;
      }
    }
  }
  for (unsigned i = 0; i < kDkc2KongSlotCount; i++)
    memcpy(tracker->sram_shadow[i], slots[i], kDkc2KongSramSlotSize);
  tracker->sram_shadow_valid = true;
  const bool changed = tracker->dirty;
  tracker->dirty = was_dirty || changed;
  return changed;
}

void Dkc2KongTrackerRebaseSram(Dkc2KongTracker *tracker, const uint8_t *sram,
                               size_t sram_size) {
  if (!tracker || !tracker->sram_shadow_valid || !sram ||
      sram_size < (size_t)kDkc2KongSramSlotOffsets[kDkc2KongSlotCount - 1] +
                      kDkc2KongSramSlotSize)
    return;
  for (unsigned i = 0; i < kDkc2KongSlotCount; i++)
    memcpy(tracker->sram_shadow[i], sram + kDkc2KongSramSlotOffsets[i],
           kDkc2KongSramSlotSize);
}

static bool RomByte(const uint8_t *rom, size_t rom_size, uint32_t pc24,
                    uint8_t *out) {
  const uint8_t bank = (uint8_t)(pc24 >> 16);
  const uint16_t address = (uint16_t)pc24;
  /* HiROM: banks $C0-$FF and the $8000 half of $80-$BF map the same 64 KiB
   * pages. */
  if (bank < 0xc0u && address < 0x8000u) return false;
  const size_t offset = ((size_t)(bank & 0x3fu) << 16) | address;
  if (!rom || offset >= rom_size) return false;
  *out = rom[offset];
  return true;
}

static bool RomWord(const uint8_t *rom, size_t rom_size, uint32_t pc24,
                    uint16_t *out) {
  uint8_t lo, hi;
  if (!RomByte(rom, rom_size, pc24, &lo) ||
      !RomByte(rom, rom_size, (pc24 & 0xff0000u) | (uint16_t)(pc24 + 1u), &hi))
    return false;
  *out = (uint16_t)(lo | (hi << 8));
  return true;
}

static uint32_t BankB4(uint16_t address) {
  return (kDkc2KongRomNodeTable & 0xff0000u) | address;
}

unsigned Dkc2KongNodeLevel(const uint8_t *rom, size_t rom_size,
                           unsigned node) {
  if (node >= kDkc2KongNodeCount) return 0;
  const uint32_t entry =
      (uint32_t)kDkc2KongRomNodeTable + node * kDkc2KongNodeEntrySize;
  uint8_t kind, name_length;
  uint16_t record, name, level;
  if (!RomByte(rom, rom_size, entry, &kind) ||
      kind != kDkc2KongNodeKindLevel ||
      !RomWord(rom, rom_size, entry + 1u, &record) ||
      !RomWord(rom, rom_size,
               BankB4((uint16_t)(record + kDkc2KongRecordName)), &name) ||
      !RomByte(rom, rom_size, BankB4(name), &name_length) ||
      !RomWord(rom, rom_size,
               BankB4((uint16_t)(record + kDkc2KongRecordLevel)), &level))
    return 0;
  /* Waypoints on a path are kind $0E too, with a one-character name and a
   * level word that is not theirs to enter. */
  if (name_length <= 1 || level == 0 || level > 0xffu) return 0;
  return level;
}

/* Appends the level stops of one world's node list to found[]. */
static bool CollectWorldLevels(const uint8_t *rom, size_t rom_size,
                               unsigned world, uint8_t *found,
                               size_t *count) {
  uint16_t descriptor, nodes;
  if (!RomWord(rom, rom_size,
               (uint32_t)kDkc2KongRomWorldDescriptors + world * 2u,
               &descriptor) ||
      !RomWord(rom, rom_size,
               BankB4((uint16_t)(descriptor + kDkc2KongWorldNodeCount)),
               &nodes) ||
      nodes > kDkc2KongMaxWorldNodes)
    return false;
  for (unsigned i = 0; i < nodes; i++) {
    uint16_t node;
    if (!RomWord(rom, rom_size,
                 BankB4((uint16_t)(descriptor + kDkc2KongWorldNodes + i * 2u)),
                 &node))
      return false;
    const unsigned level = Dkc2KongNodeLevel(rom, rom_size, node);
    if (level == 0) continue;
    bool seen = false;
    for (size_t k = 0; k < *count; k++) seen = seen || found[k] == level;
    if (!seen && *count < 2u * kDkc2KongMaxWorldNodes)
      found[(*count)++] = (uint8_t)level;
  }
  return true;
}

size_t Dkc2KongWorldLevels(const uint8_t *rom, size_t rom_size,
                           unsigned world, uint8_t *out, size_t capacity) {
  if (world == 0 || world > kDkc2KongIsleWorlds) return 0;
  /* Uniqueness is tracked here, not in `out`, which may be shorter than the
   * list or absent. */
  uint8_t found[2 * kDkc2KongMaxWorldNodes];
  size_t count = 0;
  if (!CollectWorldLevels(rom, rom_size, world, found, &count)) return 0;
  if (world == kDkc2KongKremlandWorld &&
      !CollectWorldLevels(rom, rom_size, kDkc2KongKremlandUpperWorld, found,
                          &count))
    return 0;
  for (size_t i = 0; out && i < count && i < capacity; i++) out[i] = found[i];
  return count;
}

bool Dkc2KongTrackerWorldComplete(const Dkc2KongTracker *tracker,
                                  unsigned slot, const uint8_t *rom,
                                  size_t rom_size, unsigned world) {
  uint8_t levels[2 * kDkc2KongMaxWorldNodes];
  const size_t count =
      Dkc2KongWorldLevels(rom, rom_size, world, levels, sizeof levels);
  if (count == 0 || count > sizeof levels) return false;
  for (size_t i = 0; i < count; i++)
    if (!Dkc2KongTrackerHasLevel(tracker, slot, levels[i])) return false;
  return true;
}

Dkc2KongBadge Dkc2KongTrackerMapBadge(const Dkc2KongTracker *tracker,
                                      const uint8_t *wram, size_t wram_size,
                                      const uint8_t *rom, size_t rom_size) {
  if (!tracker || !WramUsable(wram, wram_size)) return kDkc2KongBadgeNone;
  if (Read16(wram, kDkc2KongWramMainLoop) != kDkc2KongMainLoopMap)
    return kDkc2KongBadgeNone;
  unsigned slot;
  if (!SinglePlayFile(wram, &slot)) return kDkc2KongBadgeNone;
  const unsigned world = Read16(wram, kDkc2KongWramMapWorld);
  const unsigned node = Read16(wram, kDkc2KongWramMapNode);
  if (world > kDkc2KongWorldCount || node >= kDkc2KongNodeCount)
    return kDkc2KongBadgeNone;
  if (world == 0) {
    /* Isle node n opens world n + 1. */
    if (node >= kDkc2KongIsleWorlds) return kDkc2KongBadgeNone;
    return Dkc2KongTrackerWorldComplete(tracker, slot, rom, rom_size,
                                        node + 1u)
               ? kDkc2KongBadgeWorld
               : kDkc2KongBadgeNone;
  }
  const unsigned level = Dkc2KongNodeLevel(rom, rom_size, node);
  return level != 0 && Dkc2KongTrackerHasLevel(tracker, slot, level)
             ? kDkc2KongBadgeLevel
             : kDkc2KongBadgeNone;
}

bool Dkc2KongMapSettleUpdate(Dkc2KongMapSettle *settle, const uint8_t *wram,
                             size_t wram_size) {
  if (!settle) return false;
  if (!WramUsable(wram, wram_size) ||
      Read16(wram, kDkc2KongWramMainLoop) != kDkc2KongMainLoopMap) {
    settle->key = 0;
    settle->frames = 0;
    return false;
  }
  const uint32_t key = 0x80000000u |
                       ((uint32_t)wram[kDkc2KongWramMapWorld] << 16) |
                       ((uint32_t)wram[kDkc2KongWramMapNode] << 8) |
                       wram[kDkc2KongWramFileSlot];
  if (key != settle->key) {
    settle->key = key;
    settle->frames = 0;
  }
  if (settle->frames < kDkc2KongSettleFrames) settle->frames++;
  return settle->frames >= kDkc2KongSettleFrames;
}

/* 7x7 glyphs, doubled vertically to the 14 rows of the map's name font. */
static const char *const kGlyphs[4][7] = {
    {"##...##", "##..##.", "##.##..", "####...", "##.##..", "##..##.",
     "##...##"},
    {".#####.", "##...##", "##...##", "##...##", "##...##", "##...##",
     ".#####."},
    {"##...##", "###..##", "####.##", "##.####", "##..###", "##...##",
     "##...##"},
    {".#####.", "##...##", "##.....", "##..###", "##...##", "##...##",
     ".#####."},
};

enum {
  kWorldNameTop = 201,
  kIsleNameTop = 12,
  kNameRows = 14,
  kNameMinimumPixels = 24,
  kGlyphWidth = 7,
  kGlyphAdvance = 8,
  kBadgeWidth = 4 * kGlyphAdvance - 1,
  kBadgeHeight = 14,
  kBadgeGap = 10,
};

static bool IsNameYellow(const uint8_t *pixel) {
  return pixel[0] == 0x00 && pixel[1] == 0xff && pixel[2] == 0xff;
}

static bool BadgeInk(int x, int y) {
  if (x < 0 || y < 0 || x >= kBadgeWidth || y >= kBadgeHeight) return false;
  const int glyph = x / kGlyphAdvance;
  const int column = x % kGlyphAdvance;
  if (column >= kGlyphWidth) return false;
  return kGlyphs[glyph][y / 2][column] == '#';
}

static void PutPixel(uint8_t *pixels, size_t pitch, int width, int height,
                     int x, int y, uint8_t b, uint8_t g, uint8_t r) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  uint8_t *pixel = pixels + (size_t)y * pitch + (size_t)x * 4u;
  pixel[0] = b;
  pixel[1] = g;
  pixel[2] = r;
}

bool Dkc2KongDrawBadge(uint8_t *pixels, size_t pitch, int width, int height,
                       bool isle_map) {
  const int name_top = isle_map ? kIsleNameTop : kWorldNameTop;
  const int name_bottom = name_top + kNameRows - 1;
  if (!pixels || width <= 0 || height <= name_bottom ||
      pitch < (size_t)width * 4u)
    return false;
  int left = width, right = -1, count = 0;
  for (int y = name_top; y <= name_bottom; y++) {
    const uint8_t *row = pixels + (size_t)y * pitch;
    for (int x = 0; x < width; x++) {
      if (!IsNameYellow(row + (size_t)x * 4u)) continue;
      if (x < left) left = x;
      if (x > right) right = x;
      count++;
    }
  }
  if (count < kNameMinimumPixels) return false;

  int origin_x = right + kBadgeGap;
  int origin_y = name_top;
  if (origin_x + kBadgeWidth + 1 >= width) {
    /* No room beside the name: centre the badge above a bottom name, or
     * below a top one. */
    origin_x = (left + right) / 2 - kBadgeWidth / 2;
    origin_y = isle_map ? name_bottom + 4 : name_top - kBadgeHeight - 3;
  }
  for (int y = -1; y <= kBadgeHeight; y++)
    for (int x = -1; x <= kBadgeWidth; x++) {
      if (BadgeInk(x, y)) continue;
      bool edge = false;
      for (int dy = -1; dy <= 1 && !edge; dy++)
        for (int dx = -1; dx <= 1 && !edge; dx++)
          edge = BadgeInk(x + dx, y + dy);
      if (edge)
        PutPixel(pixels, pitch, width, height, origin_x + x, origin_y + y,
                 0x00, 0x00, 0x00);
    }
  for (int y = 0; y < kBadgeHeight; y++)
    for (int x = 0; x < kBadgeWidth; x++)
      if (BadgeInk(x, y))
        PutPixel(pixels, pitch, width, height, origin_x + x, origin_y + y,
                 0x00, 0xff, 0xff);
  return true;
}

bool Dkc2KongBadgesEnabledFromEnv(const char *env_value) {
  return !(env_value && strcmp(env_value, "0") == 0);
}
