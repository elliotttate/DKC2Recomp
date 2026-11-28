#include "dkc2_banana_coins.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const uint8_t kMagic[8] = {'D', 'K', 'C', '2', 'C', 'O', 'I', 'N'};
enum {
  kFileVersion = 1,
  kRecordsOffset = 16,
  kChecksumOffset = kDkc2BananaCoinFileSize - 4,
};

static uint32_t Fnv1a(const uint8_t *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void Put16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static void Put32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static uint16_t Get16(const uint8_t *in) {
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t Get32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
         ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static bool InContest(const uint8_t *wram) {
  return Get16(wram + kDkc2BananaCoinWramPlayMode) ==
         kDkc2BananaCoinPlayModeContest;
}

void Dkc2BananaCoinKeeperInit(Dkc2BananaCoinKeeper *keeper) {
  if (keeper) memset(keeper, 0, sizeof *keeper);
}

bool Dkc2BananaCoinSaveKey(const uint8_t *wram, size_t wram_size,
                           uint32_t *key) {
  if (!wram || !key ||
      wram_size < (size_t)kDkc2BananaCoinWramSaveBuffer +
                      kDkc2BananaCoinSaveBufferSize)
    return false;
  *key = Fnv1a(wram + kDkc2BananaCoinWramSaveBuffer,
               kDkc2BananaCoinSaveBufferSize);
  return true;
}

bool Dkc2BananaCoinKeeperFind(const Dkc2BananaCoinKeeper *keeper,
                              uint32_t key, uint16_t *coins) {
  if (!keeper) return false;
  for (unsigned i = 0; i < keeper->count; i++) {
    if (keeper->records[i].key == key) {
      if (coins) *coins = keeper->records[i].coins;
      return true;
    }
  }
  return false;
}

bool Dkc2BananaCoinKeeperNoteSave(Dkc2BananaCoinKeeper *keeper,
                                  const uint8_t *wram, size_t wram_size) {
  uint32_t key = 0;
  if (!keeper || !Dkc2BananaCoinSaveKey(wram, wram_size, &key) ||
      InContest(wram))
    return false;
  const Dkc2BananaCoinRecord record = {
      key, Get16(wram + kDkc2BananaCoinWramCoins)};
  /* Remove the key wherever it is (or the oldest record when full), then
   * put the record first. */
  unsigned index = 0;
  while (index < keeper->count && keeper->records[index].key != key) index++;
  if (index == 0 && keeper->count > 0 &&
      keeper->records[0].coins == record.coins)
    return false;
  if (index == keeper->count) {
    if (keeper->count < kDkc2BananaCoinRecordCount)
      keeper->count++;
    index = keeper->count - 1;
  }
  memmove(&keeper->records[1], &keeper->records[0],
          index * sizeof keeper->records[0]);
  keeper->records[0] = record;
  keeper->dirty = true;
  return true;
}

bool Dkc2BananaCoinKeeperRestore(const Dkc2BananaCoinKeeper *keeper,
                                 uint8_t *wram, size_t wram_size,
                                 bool enabled) {
  uint32_t key = 0;
  uint16_t coins = 0;
  if (!enabled || !keeper || !Dkc2BananaCoinSaveKey(wram, wram_size, &key) ||
      InContest(wram) || !Dkc2BananaCoinKeeperFind(keeper, key, &coins))
    return false;
  Put16(wram + kDkc2BananaCoinWramCoins, coins);
  return true;
}

size_t Dkc2BananaCoinKeeperSerialize(const Dkc2BananaCoinKeeper *keeper,
                                     uint8_t *out, size_t capacity) {
  if (!keeper || !out || capacity < kDkc2BananaCoinFileSize ||
      keeper->count > kDkc2BananaCoinRecordCount)
    return 0;
  memset(out, 0, kDkc2BananaCoinFileSize);
  memcpy(out, kMagic, sizeof kMagic);
  Put32(out + 8, kFileVersion);
  Put32(out + 12, keeper->count);
  for (unsigned i = 0; i < keeper->count; i++) {
    uint8_t *record = out + kRecordsOffset + i * 8u;
    Put32(record, keeper->records[i].key);
    Put16(record + 4, keeper->records[i].coins);
  }
  Put32(out + kChecksumOffset, Fnv1a(out, kChecksumOffset));
  return kDkc2BananaCoinFileSize;
}

bool Dkc2BananaCoinKeeperDeserialize(Dkc2BananaCoinKeeper *keeper,
                                     const uint8_t *data, size_t size) {
  if (!keeper || !data || size != kDkc2BananaCoinFileSize) return false;
  if (memcmp(data, kMagic, sizeof kMagic) != 0) return false;
  if (Get32(data + 8) != kFileVersion) return false;
  if (Get32(data + kChecksumOffset) != Fnv1a(data, kChecksumOffset))
    return false;
  const uint32_t count = Get32(data + 12);
  if (count > kDkc2BananaCoinRecordCount) return false;
  Dkc2BananaCoinKeeper loaded;
  memset(&loaded, 0, sizeof loaded);
  for (unsigned i = 0; i < count; i++) {
    const uint8_t *record = data + kRecordsOffset + i * 8u;
    const uint32_t key = Get32(record);
    /* A key appears at most once; a repeat is not a file this wrote. */
    if (Dkc2BananaCoinKeeperFind(&loaded, key, NULL)) return false;
    loaded.records[i].key = key;
    loaded.records[i].coins = Get16(record + 4);
    loaded.count = i + 1;
  }
  *keeper = loaded;
  return true;
}

Dkc2BananaCoinLoadResult Dkc2BananaCoinKeeperLoadFile(
    Dkc2BananaCoinKeeper *keeper, const char *path) {
  if (!keeper || !path) return kDkc2BananaCoinLoadInvalid;
  errno = 0;
  FILE *stream = fopen(path, "rb");
  if (!stream)
    return errno == ENOENT ? kDkc2BananaCoinLoadMissing
                           : kDkc2BananaCoinLoadUnusable;
  uint8_t data[kDkc2BananaCoinFileSize + 1];
  const size_t size = fread(data, 1, sizeof data, stream);
  fclose(stream);
  return Dkc2BananaCoinKeeperDeserialize(keeper, data, size)
             ? kDkc2BananaCoinLoadOk
             : kDkc2BananaCoinLoadInvalid;
}

/* Replace `path` with `temporary` in one step, so a failure leaves the old
 * file in place. POSIX rename() already replaces atomically; Windows
 * rename() refuses an existing target. */
static bool CoinReplaceFile(const char *temporary, const char *path) {
#ifdef _WIN32
  return MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temporary, path) == 0;
#endif
}

bool Dkc2BananaCoinKeeperSaveFile(const Dkc2BananaCoinKeeper *keeper,
                                  const char *path) {
  if (!keeper || !path || !*path) return false;
  uint8_t data[kDkc2BananaCoinFileSize];
  if (Dkc2BananaCoinKeeperSerialize(keeper, data, sizeof data) != sizeof data)
    return false;
  const size_t length = strlen(path);
  char *temporary = (char *)malloc(length + 5);
  if (!temporary) return false;
  memcpy(temporary, path, length);
  memcpy(temporary + length, ".tmp", 5);
  FILE *stream = fopen(temporary, "wb");
  bool ok = stream != NULL;
  if (ok) ok = fwrite(data, 1, sizeof data, stream) == sizeof data;
  if (stream && fclose(stream) != 0) ok = false;
  if (ok) ok = CoinReplaceFile(temporary, path);
  if (!ok) remove(temporary);
  free(temporary);
  return ok;
}

bool Dkc2KeepBananaCoinsFromEnv(const char *env_value, bool fallback) {
  if (!env_value) return fallback;
  return strcmp(env_value, "0") != 0;
}
