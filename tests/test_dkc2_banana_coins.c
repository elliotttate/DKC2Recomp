#include "dkc2_banana_coins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc2_banana_coins.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

enum { kWramSize = 0x20000 };

static uint8_t s_wram[kWramSize];

static void Write16(size_t address, uint16_t value) {
  s_wram[address] = (uint8_t)value;
  s_wram[address + 1u] = (uint8_t)(value >> 8);
}

static uint16_t Read16(size_t address) {
  return (uint16_t)(s_wram[address] | (s_wram[address + 1u] << 8));
}

/* A synthetic save buffer: `seed` stands in for the file's contents. */
static void FillBuffer(uint8_t seed) {
  for (size_t i = 0; i < kDkc2BananaCoinSaveBufferSize; i++)
    s_wram[kDkc2BananaCoinWramSaveBuffer + i] = (uint8_t)(seed + i * 7u);
}

static void Save(Dkc2BananaCoinKeeper *keeper, uint8_t seed, uint16_t coins) {
  FillBuffer(seed);
  Write16(kDkc2BananaCoinWramCoins, coins);
  (void)Dkc2BananaCoinKeeperNoteSave(keeper, s_wram, kWramSize);
}

/* Loads the file `seed` the way the cartridge does: the new-file defaults
 * zero the coins, then the loader unpacks the buffer. */
static bool Load(const Dkc2BananaCoinKeeper *keeper, uint8_t seed,
                 bool enabled) {
  FillBuffer(seed);
  Write16(kDkc2BananaCoinWramCoins, 0);
  return Dkc2BananaCoinKeeperRestore(keeper, s_wram, kWramSize, enabled);
}

static void TestSaveAndRestore(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  CHECK(!Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 0);

  Save(&keeper, 1, 42);
  CHECK(keeper.dirty);
  CHECK(keeper.count == 1);
  CHECK(Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 42);

  /* Turned off, the loaded file keeps the cartridge's zero. */
  CHECK(!Load(&keeper, 1, false));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 0);

  /* A different file (another slot, or an erased one) matches nothing. */
  CHECK(!Load(&keeper, 2, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 0);

  /* A copied file has the same bytes, so it finds its source's coins. */
  CHECK(Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 42);
}

static void TestResaveUpdatesTheRecord(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  Save(&keeper, 1, 10);
  keeper.dirty = false;
  /* Saving the same coins again changes nothing. */
  Save(&keeper, 1, 10);
  CHECK(!keeper.dirty);
  /* Saving an unchanged record with different coins updates it. */
  Save(&keeper, 1, 7);
  CHECK(keeper.dirty);
  CHECK(keeper.count == 1);
  CHECK(Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 7);

  /* A newer save of the file is a new record; the older one stays for a
   * quick save that restores the older battery save. */
  Save(&keeper, 3, 25);
  CHECK(keeper.count == 2);
  CHECK(keeper.records[0].coins == 25);
  CHECK(Load(&keeper, 3, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 25);
  CHECK(Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 7);

  /* Saving an older record again moves it to the front. */
  Save(&keeper, 1, 8);
  CHECK(keeper.count == 2);
  CHECK(keeper.records[0].coins == 8);
  CHECK(keeper.records[1].coins == 25);
}

static void TestOldestRecordIsDropped(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  for (unsigned i = 0; i <= kDkc2BananaCoinRecordCount; i++)
    Save(&keeper, (uint8_t)(10 + i), (uint16_t)i);
  CHECK(keeper.count == kDkc2BananaCoinRecordCount);
  CHECK(!Load(&keeper, 10, true));
  CHECK(Load(&keeper, 11, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 1);
  CHECK(Load(&keeper, (uint8_t)(10 + kDkc2BananaCoinRecordCount), true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == kDkc2BananaCoinRecordCount);
}

static void TestContestIsLeftAlone(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  Write16(kDkc2BananaCoinWramPlayMode, kDkc2BananaCoinPlayModeContest);
  Save(&keeper, 1, 30);
  CHECK(keeper.count == 0);
  Write16(kDkc2BananaCoinWramPlayMode, 1);
  Save(&keeper, 1, 30);
  Write16(kDkc2BananaCoinWramPlayMode, kDkc2BananaCoinPlayModeContest);
  CHECK(!Load(&keeper, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 0);
  Write16(kDkc2BananaCoinWramPlayMode, 0);
}

static void TestRejectsSmallWram(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  uint32_t key = 0;
  const size_t small = kDkc2BananaCoinWramSaveBuffer +
                       kDkc2BananaCoinSaveBufferSize - 1u;
  CHECK(!Dkc2BananaCoinSaveKey(s_wram, small, &key));
  CHECK(!Dkc2BananaCoinKeeperNoteSave(&keeper, s_wram, small));
  CHECK(!Dkc2BananaCoinKeeperRestore(&keeper, s_wram, small, true));
  CHECK(!Dkc2BananaCoinKeeperNoteSave(NULL, s_wram, kWramSize));
}

static void Reseal(uint8_t *data) {
  uint32_t hash = 2166136261u;
  const size_t end = kDkc2BananaCoinFileSize - 4;
  for (size_t i = 0; i < end; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  for (unsigned i = 0; i < 4; i++) data[end + i] = (uint8_t)(hash >> (8 * i));
}

static void TestSerializeRoundTrip(void) {
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  Save(&keeper, 1, 12);
  Save(&keeper, 2, 99);
  uint8_t data[kDkc2BananaCoinFileSize];
  CHECK(Dkc2BananaCoinKeeperSerialize(&keeper, data, sizeof data - 1) == 0);
  CHECK(Dkc2BananaCoinKeeperSerialize(&keeper, data, sizeof data) ==
        sizeof data);
  CHECK(memcmp(data, "DKC2COIN", 8) == 0);

  Dkc2BananaCoinKeeper loaded;
  Dkc2BananaCoinKeeperInit(&loaded);
  loaded.dirty = true;
  CHECK(Dkc2BananaCoinKeeperDeserialize(&loaded, data, sizeof data));
  CHECK(!loaded.dirty);
  CHECK(loaded.count == 2);
  CHECK(Load(&loaded, 2, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 99);
  CHECK(Load(&loaded, 1, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 12);

  /* Damaged, resized, wrong-version, oversized or repeated-key files are
   * refused and leave the keeper as it was. */
  uint8_t bad[kDkc2BananaCoinFileSize];
  memcpy(bad, data, sizeof bad);
  bad[20] ^= 1u;
  CHECK(!Dkc2BananaCoinKeeperDeserialize(&loaded, bad, sizeof bad));
  CHECK(!Dkc2BananaCoinKeeperDeserialize(&loaded, data, sizeof data - 1));
  memcpy(bad, data, sizeof bad);
  bad[8] = 2;
  Reseal(bad);
  CHECK(!Dkc2BananaCoinKeeperDeserialize(&loaded, bad, sizeof bad));
  memcpy(bad, data, sizeof bad);
  bad[12] = kDkc2BananaCoinRecordCount + 1;
  Reseal(bad);
  CHECK(!Dkc2BananaCoinKeeperDeserialize(&loaded, bad, sizeof bad));
  memcpy(bad, data, sizeof bad);
  memcpy(bad + 24, bad + 16, 4);
  Reseal(bad);
  CHECK(!Dkc2BananaCoinKeeperDeserialize(&loaded, bad, sizeof bad));
  CHECK(loaded.count == 2);
  CHECK(Load(&loaded, 2, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 99);
}

static void TestFileRoundTrip(void) {
  const char *path = "test_dkc2_banana_coins.dat";
  remove(path);
  Dkc2BananaCoinKeeper keeper;
  Dkc2BananaCoinKeeperInit(&keeper);
  CHECK(Dkc2BananaCoinKeeperLoadFile(&keeper, path) ==
        kDkc2BananaCoinLoadMissing);
  Save(&keeper, 5, 64);
  CHECK(Dkc2BananaCoinKeeperSaveFile(&keeper, path));
  Dkc2BananaCoinKeeper loaded;
  Dkc2BananaCoinKeeperInit(&loaded);
  CHECK(Dkc2BananaCoinKeeperLoadFile(&loaded, path) == kDkc2BananaCoinLoadOk);
  CHECK(Load(&loaded, 5, true));
  CHECK(Read16(kDkc2BananaCoinWramCoins) == 64);

  FILE *stream = fopen(path, "wb");
  CHECK(stream != NULL);
  CHECK(fwrite("junk", 1, 4, stream) == 4);
  CHECK(fclose(stream) == 0);
  CHECK(Dkc2BananaCoinKeeperLoadFile(&loaded, path) ==
        kDkc2BananaCoinLoadInvalid);
  CHECK(loaded.count == 1);
  remove(path);

  /* A missing file in a usable directory can be created; one under a
   * missing directory cannot, and the probe leaves nothing behind. */
  CHECK(Dkc2BananaCoinKeeperCanCreate(path));
  stream = fopen(path, "rb");
  CHECK(stream == NULL);
  if (stream) fclose(stream);
  stream = fopen("test_dkc2_banana_coins.dat.tmp", "rb");
  CHECK(stream == NULL);
  if (stream) fclose(stream);
  const char *orphan = "test_dkc2_banana_missing_dir/coins.dat";
  CHECK(Dkc2BananaCoinKeeperLoadFile(&loaded, orphan) ==
        kDkc2BananaCoinLoadMissing);
  CHECK(!Dkc2BananaCoinKeeperCanCreate(orphan));
  CHECK(!Dkc2BananaCoinKeeperCanCreate(""));
}

static void TestEnvironment(void) {
  CHECK(Dkc2KeepBananaCoinsFromEnv(NULL, true));
  CHECK(!Dkc2KeepBananaCoinsFromEnv(NULL, false));
  CHECK(!Dkc2KeepBananaCoinsFromEnv("0", true));
  CHECK(Dkc2KeepBananaCoinsFromEnv("1", false));
}

int main(void) {
  TestSaveAndRestore();
  TestResaveUpdatesTheRecord();
  TestOldestRecordIsDropped();
  TestContestIsLeftAlone();
  TestRejectsSmallWram();
  TestSerializeRoundTrip();
  TestFileRoundTrip();
  TestEnvironment();
  puts("test_dkc2_banana_coins: ok");
  return 0;
}
