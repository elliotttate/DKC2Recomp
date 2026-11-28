#ifndef DKC2_BANANA_COINS_H
#define DKC2_BANANA_COINS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gameplay option: keep the spendable Banana Coins with a save file.
 *
 * The cartridge never saves Banana Coins. update_save_buffer ($BB:C5E0)
 * packs Kremkoins, DK coins and lives into the 680-byte record but not
 * $08CA, and loading a file runs the new-file defaults
 * (set_default_new_file_status, $80:8F39, which calls $B4:8000 and zeroes
 * $08CA, $08CC and $08CE) before CODE_BBC736 ($BB:C722) unpacks the
 * record, so every loaded file starts with none (docs/HARDWARE_NOTES.md,
 * "The save record, Klubba's toll, and the Lost World").
 *
 * The keeper is host-only and never writes the battery save, so save.srm
 * stays byte-identical to what the cartridge writes. Cartridge facts it
 * relies on, all for the supported USA v1.0 ROM:
 *
 * - $7E:56CA: the 680-byte save buffer. Two routines copy all of it to
 *   the SRAM file selected by $0611 (table $80:ABA8 / $BB:C5DA). Kong
 *   Kollege's "Save Game" screen, bank_80_A5B1, does it in its own loop at
 *   $80:A613 when $0613 is nonzero and then reaches the PLB at $80:A61C;
 *   copy_save_to_sram ($BB:C5B8) ends with PLB / RTS at $BB:C5D8. The
 *   loader reads the record back from the same buffer and returns with
 *   the RTL at $BB:C846.
 * - $08CA: the Banana Coin count, a 16-bit word.
 * - $060D: 2 is the two-player contest, whose file holds a data set per
 *   player; the keeper leaves that mode alone.
 *
 * A save is keyed by the FNV-1a hash of the whole buffer, the bytes the
 * file select later loads, not by slot. A copied file therefore keeps the
 * coins of its source, an erased one (the erase changes the file's first
 * word) matches nothing, and a quick save that restores an older battery
 * save finds the coins that went with that older file. The most recent
 * kDkc2BananaCoinRecordCount saves are kept. */
enum {
  kDkc2BananaCoinWramCoins = 0x08ca,
  kDkc2BananaCoinWramPlayMode = 0x060d,
  kDkc2BananaCoinPlayModeContest = 2,
  kDkc2BananaCoinWramSaveBuffer = 0x56ca,
  kDkc2BananaCoinSaveBufferSize = 0x2a8,

  kDkc2BananaCoinScreenSavedPc = 0x80a61cu, /* after Kong Kollege's copy */
  kDkc2BananaCoinSavedPc = 0xbbc5d8u,  /* copy_save_to_sram's PLB / RTS */
  kDkc2BananaCoinLoadedPc = 0xbbc846u, /* the file loader's RTL */

  kDkc2BananaCoinRecordCount = 32,
  /* "DKC2COIN", version, count, 32 x (hash, coins, reserved), checksum. */
  kDkc2BananaCoinFileSize = 8 + 4 + 4 + kDkc2BananaCoinRecordCount * 8 + 4,
};

typedef struct Dkc2BananaCoinRecord {
  uint32_t key;
  uint16_t coins;
} Dkc2BananaCoinRecord;

typedef struct Dkc2BananaCoinKeeper {
  /* Most recent first; count entries are used. */
  Dkc2BananaCoinRecord records[kDkc2BananaCoinRecordCount];
  unsigned count;
  /* Set whenever records change; the owner clears it after persisting. */
  bool dirty;
} Dkc2BananaCoinKeeper;

void Dkc2BananaCoinKeeperInit(Dkc2BananaCoinKeeper *keeper);

/* The key of the save buffer in this WRAM image, or false when the image
 * is too small to hold it. */
bool Dkc2BananaCoinSaveKey(const uint8_t *wram, size_t wram_size,
                           uint32_t *key);

/* Call when the buffer has been copied to SRAM: records the
 * live $08CA under the buffer's key, moving it to the front. Does nothing
 * in the two-player contest. Returns true when the records changed. */
bool Dkc2BananaCoinKeeperNoteSave(Dkc2BananaCoinKeeper *keeper,
                                  const uint8_t *wram, size_t wram_size);

/* Call when the loader has unpacked the buffer: when `enabled`, outside
 * the contest, and the buffer matches a recorded save, writes that save's
 * coins to $08CA and returns true. Otherwise leaves the cartridge's zero. */
bool Dkc2BananaCoinKeeperRestore(const Dkc2BananaCoinKeeper *keeper,
                                 uint8_t *wram, size_t wram_size,
                                 bool enabled);

/* The coins recorded under key, if any. */
bool Dkc2BananaCoinKeeperFind(const Dkc2BananaCoinKeeper *keeper,
                              uint32_t key, uint16_t *coins);

size_t Dkc2BananaCoinKeeperSerialize(const Dkc2BananaCoinKeeper *keeper,
                                     uint8_t *out, size_t capacity);
bool Dkc2BananaCoinKeeperDeserialize(Dkc2BananaCoinKeeper *keeper,
                                     const uint8_t *data, size_t size);

typedef enum Dkc2BananaCoinLoadResult {
  kDkc2BananaCoinLoadOk = 0,
  kDkc2BananaCoinLoadMissing = 1,
  kDkc2BananaCoinLoadInvalid = 2,
  /* The file exists but could not be opened (permissions, a directory). */
  kDkc2BananaCoinLoadUnusable = 3,
} Dkc2BananaCoinLoadResult;
Dkc2BananaCoinLoadResult Dkc2BananaCoinKeeperLoadFile(
    Dkc2BananaCoinKeeper *keeper, const char *path);
/* Writes a fresh, exclusively created temporary beside path and replaces
 * path with it in one step, so a failure keeps the previous file
 * (dkc2_sidecar_file.h). */
bool Dkc2BananaCoinKeeperSaveFile(const Dkc2BananaCoinKeeper *keeper,
                                  const char *path);
/* Whether Dkc2BananaCoinKeeperSaveFile could create its temporary now, so
 * a missing sidecar under a missing or read-only directory fails up front
 * instead of at the first save. The probe makes the temporary the same
 * way and removes it. */
bool Dkc2BananaCoinKeeperCanCreate(const char *path);

/* NULL keeps `fallback`; "0" disables; any other value enables. */
bool Dkc2KeepBananaCoinsFromEnv(const char *env_value, bool fallback);

/* Defined in dkc2_game.c with the live keeper. The generated
 * bank_80_A5B1 and copy_save_to_sram call the first at
 * kDkc2BananaCoinScreenSavedPc and kDkc2BananaCoinSavedPc, and CODE_BBC736
 * the second at kDkc2BananaCoinLoadedPc
 * (scripts/apply_dkc2_banana_coin_overrides.py); interpreter hooks at the
 * same PCs make the same calls. */
struct CpuState;
void Dkc2BananaCoinsSavedHook(struct CpuState *cpu);
void Dkc2BananaCoinsLoadedHook(struct CpuState *cpu);

#ifdef __cplusplus
}
#endif

#endif
