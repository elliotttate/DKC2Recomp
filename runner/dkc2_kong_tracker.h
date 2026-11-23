#ifndef DKC2_KONG_TRACKER_H
#define DKC2_KONG_TRACKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-side record of which levels have had all four K-O-N-G letters
 * collected, per save file, for DKC2 USA v1.0.
 *
 * The cartridge never saves KONG letters: they live only in WRAM while a
 * level runs. This tracker keeps its own sidecar file next to save.srm and
 * never writes the battery save, so save.srm stays byte-identical to what
 * the cartridge writes and remains portable to other emulators.
 *
 * Cartridge facts (docs/HARDWARE_NOTES.md, "KONG letters and the map",
 * records the evidence for each):
 *
 * - $0902 bits 0-3: K, O, N, G collected in the running level. The letter
 *   pickup ($BE:BADD) sets its bit with TSB and awards the life when
 *   ($0902 + 1) has a zero low nibble. A star barrel copies the byte to
 *   $08B2 ($B3:E4DD) and a respawn restores it ($BB:93AC); leaving a level
 *   clears it ($BB:BDE3, $BB:BE51, $BB:BF20). Bit 4 is a different flag
 *   ($B3:C454) and is ignored.
 * - $08A8: the level entered from the map ($B5:D45C copies the node's level
 *   from $D3). It is the number the cleared-level routine marks
 *   ($BB:8158) and stays put in sub-areas and bonus rooms, which change
 *   $D3.
 * - $0611: the active file slot, 0-2, which indexes the SRAM table at
 *   $80:ABA8 / $BB:C5DA. Files start at SRAM $008, $2B0, and $558 and are
 *   $2A8 bytes long.
 * - $060D: 2 selects a file's second data set for the player in $060F
 *   bit 0 ($BB:C60A); the tracker leaves that mode alone.
 * - $0024: the main loop continuation; $8819 while a level runs and $87D9
 *   on the world maps and the Crocodile Isle map.
 * - Maps: $06B1 is the world (0 for Crocodile Isle) and $06AB the global
 *   node under the Kongs. The node table at $B4:CF03 holds three bytes per
 *   node, a kind and a bank $B4 record pointer ($B4:B4B7); kind $0E is a
 *   level stop, $10 a boss. A record's word at +7 points to its name
 *   (length byte first) and its word at +9 is the level the node enters
 *   ($B4:8AE1). The world descriptor is the word at $B4:C216 + 2 * world;
 *   its word at +$0A counts the node words that start at +$0E. From the
 *   isle, node n opens world n + 1 ($B5:D2CC); Krazy Kremland's map is
 *   worlds 4 and 9, joined at nodes $44, $4E, $4F, and $52 ($B4:AF14).
 * - The map's name text is pure #FFFF00 with a black outline on native rows
 *   201-214 of a world map and rows 12-25 of the Crocodile Isle map.
 */
enum {
  kDkc2KongSlotCount = 3,
  kDkc2KongLevelBitmapBytes = 32,
  kDkc2KongWorldCount = 14,
  kDkc2KongIsleWorlds = 7,

  kDkc2KongWramMainLoop = 0x0024,
  kDkc2KongWramPlayMode = 0x060d,
  kDkc2KongWramFileSlot = 0x0611,
  kDkc2KongWramMapNode = 0x06ab,
  kDkc2KongWramMapWorld = 0x06b1,
  kDkc2KongWramEnteredLevel = 0x08a8,
  kDkc2KongWramLetters = 0x0902,
  kDkc2KongAllLetters = 0x0f,
  kDkc2KongPlayModeContest = 2,

  kDkc2KongMainLoopLevel = 0x8819,
  kDkc2KongMainLoopMap = 0x87d9,

  kDkc2KongRomNodeTable = 0xb4cf03,
  kDkc2KongRomWorldDescriptors = 0xb4c216,
  kDkc2KongNodeEntrySize = 3,
  kDkc2KongNodeCount = 0x78,
  kDkc2KongNodeKindLevel = 0x0e,
  kDkc2KongRecordName = 7,
  kDkc2KongRecordLevel = 9,
  kDkc2KongWorldNodeCount = 0x0a,
  kDkc2KongWorldNodes = 0x0e,
  kDkc2KongMaxWorldNodes = 32,
  kDkc2KongKremlandWorld = 4,
  kDkc2KongKremlandUpperWorld = 9,

  kDkc2KongSramSlotSize = 0x2a8,
  kDkc2KongSramChecksumEnd = 0x2a2,

  kDkc2KongFileSize = 112,
};

extern const uint16_t kDkc2KongSramSlotOffsets[kDkc2KongSlotCount];

typedef struct Dkc2KongTracker {
  uint8_t levels[kDkc2KongSlotCount][kDkc2KongLevelBitmapBytes];
  uint8_t sram_shadow[kDkc2KongSlotCount][kDkc2KongSramSlotSize];
  bool sram_shadow_valid;
  /* Set whenever levels[] changes; the owner clears it after persisting. */
  bool dirty;
} Dkc2KongTracker;

typedef enum Dkc2KongBadge {
  kDkc2KongBadgeNone = 0,
  kDkc2KongBadgeLevel = 1,
  kDkc2KongBadgeWorld = 2,
} Dkc2KongBadge;

void Dkc2KongTrackerInit(Dkc2KongTracker *tracker);
bool Dkc2KongTrackerHasLevel(const Dkc2KongTracker *tracker, unsigned slot,
                             unsigned level);
/* Level 0 is never recorded. Returns true when the flag was newly set. */
bool Dkc2KongTrackerSetLevel(Dkc2KongTracker *tracker, unsigned slot,
                             unsigned level);
void Dkc2KongTrackerClearSlot(Dkc2KongTracker *tracker, unsigned slot);

/* The sidecar format: "DKC2KONG", a little-endian version (1), three
 * 32-byte level bitmaps, and an FNV-1a checksum of everything before it. */
size_t Dkc2KongTrackerSerialize(const Dkc2KongTracker *tracker, uint8_t *out,
                                size_t capacity);
bool Dkc2KongTrackerDeserialize(Dkc2KongTracker *tracker, const uint8_t *data,
                                size_t size);
typedef enum Dkc2KongLoadResult {
  kDkc2KongLoadOk = 0,
  kDkc2KongLoadMissing = 1,
  kDkc2KongLoadInvalid = 2,
} Dkc2KongLoadResult;
Dkc2KongLoadResult Dkc2KongTrackerLoadFile(Dkc2KongTracker *tracker,
                                           const char *path);
/* Writes path.tmp and replaces path with it in one step, so a failure keeps
 * the previous file. */
bool Dkc2KongTrackerSaveFile(const Dkc2KongTracker *tracker, const char *path);

/* Records the entered level ($08A8) for the active slot once all four
 * letters are held while a level runs. Returns true when the flag was newly
 * set. */
bool Dkc2KongTrackerObserveFrame(Dkc2KongTracker *tracker,
                                 const uint8_t *wram, size_t wram_size);
/* The cartridge's own file check ($BB:C526): the word at +4, bit 0 ignored,
 * is $52, $152, or $252, and the words at +0 and +2 are the 16-bit sum and
 * XOR of the words from +6 up to +$2A2. The file select's erase increments
 * the word at +0 ($80:AB4E). */
bool Dkc2KongSramSlotValid(const uint8_t *slot);
/* Follows the file select's copy and erase: a slot whose SRAM changed and
 * no longer passes the file check loses its flags, and a slot that changed
 * to an exact copy of another valid slot takes that slot's flags (only the
 * levels every candidate source shares when the source is ambiguous). The
 * first call only adopts the SRAM as the baseline and changes no flag: a
 * file played only through quick saves has no valid SRAM file but owns its
 * flags. Returns true when any flag changed. */
bool Dkc2KongTrackerSyncSram(Dkc2KongTracker *tracker, const uint8_t *sram,
                             size_t sram_size);
/* Quick saves and rewind restore the battery RAM with the rest of the
 * machine. That is not the file select at work, so after a state load the
 * SRAM is taken as the new baseline and no flag changes: an older state
 * without a file must not erase the badges recorded for it. Does nothing
 * before the first sync. */
void Dkc2KongTrackerRebaseSram(Dkc2KongTracker *tracker, const uint8_t *sram,
                               size_t sram_size);

/* The level a map node enters when it is a named level stop, else 0. */
unsigned Dkc2KongNodeLevel(const uint8_t *rom, size_t rom_size,
                           unsigned node);
/* Writes the distinct level numbers of an isle world (1-7, with Krazy
 * Kremland's second map for world 4) into out and returns how many there
 * were (0 when the ROM tables are out of range). */
size_t Dkc2KongWorldLevels(const uint8_t *rom, size_t rom_size,
                           unsigned world, uint8_t *out, size_t capacity);
bool Dkc2KongTrackerWorldComplete(const Dkc2KongTracker *tracker,
                                  unsigned slot, const uint8_t *rom,
                                  size_t rom_size, unsigned world);
/* What the map node under the Kongs earns. */
Dkc2KongBadge Dkc2KongTrackerMapBadge(const Dkc2KongTracker *tracker,
                                      const uint8_t *wram, size_t wram_size,
                                      const uint8_t *rom, size_t rom_size);

/* The node index can change before the departing name is erased. A badge is
 * shown only once the map has stayed on one node for kDkc2KongSettleFrames
 * game frames. */
enum { kDkc2KongSettleFrames = 16 };
typedef struct Dkc2KongMapSettle {
  uint32_t key;
  uint32_t frames;
} Dkc2KongMapSettle;
/* Call once per game frame; returns true while the map node is settled. */
bool Dkc2KongMapSettleUpdate(Dkc2KongMapSettle *settle, const uint8_t *wram,
                             size_t wram_size);

/* Draws "KONG" beside the map's name text in a 32-bit BGRX frame of any
 * width: the bottom name rows on a world map, the top rows on the isle map
 * (isle_map true). Returns false, drawing nothing, when no name text is on
 * screen. */
bool Dkc2KongDrawBadge(uint8_t *pixels, size_t pitch, int width, int height,
                       bool isle_map);

bool Dkc2KongBadgesEnabledFromEnv(const char *env_value);

#ifdef __cplusplus
}
#endif

#endif
