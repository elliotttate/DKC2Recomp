#include "dkc2_video.h"

#include <stdio.h>
#include <string.h>

static void WriteWord(uint8_t *data, uint16_t address, uint16_t value) {
  data[address] = (uint8_t)value;
  data[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

int main(void) {
  Dkc2VideoSetWidescreen(false);
  if (Dkc2VideoIsWidescreen() ||
      Dkc2VideoWidth() != kDkc2VideoNativeWidth ||
      Dkc2VideoExtra() != 0 ||
      Dkc2VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc2VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoNativeWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: native video geometry\n");
    return 1;
  }

  Dkc2VideoSetWidescreen(true);
  if (Dkc2VideoTerrainReady() ||
      Dkc2VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc2VideoExpandCullSpan(0x140) != 0x140) {
    fprintf(stderr, "FAIL: object bounds widened before terrain was ready\n");
    return 1;
  }
  Dkc2VideoSetTerrainReady(true);
  if (!Dkc2VideoIsWidescreen() ||
      !Dkc2VideoTerrainReady() ||
      Dkc2VideoWidth() != kDkc2VideoWidescreenWidth ||
      Dkc2VideoExtra() != kDkc2VideoWidescreenExtra ||
      Dkc2VideoExpandCullLeft(0x20) != 0x4b ||
      Dkc2VideoExpandCullSpan(0x140) != 0x196 ||
      Dkc2VideoExpandCullLeft(0x30) != 0x5b ||
      Dkc2VideoExpandCullSpan(0x160) != 0x1b6 ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoWidescreenWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: widescreen video geometry\n");
    return 1;
  }

  /* Display aspect = source width * (7/6 PAR) / source height. */
  const int lhs = kDkc2VideoWidescreenWidth * 7 * 9;
  const int rhs = kDkc2VideoHeight * 6 * 16;
  int error = lhs > rhs ? lhs - rhs : rhs - lhs;
  if (error > 7 * 9) {
    fprintf(stderr,
            "FAIL: widescreen geometry is not within one pixel of 16:9\n");
    return 1;
  }

  {
    const uint8_t bounded[4] = {0x70, 0x78, 0x74, 0x00};
    const uint8_t streamable[4] = {0x71, 0x5c, 0x79, 0x00};
    const uint8_t bg2_streamable[4] = {0x70, 0x5d, 0x79, 0x00};
    if (Dkc2VideoPpuCanExtend(1, bounded, 0x07, 0x10) ||
        !Dkc2VideoPpuCanExtend(1, streamable, 0x17, 0x10) ||
        Dkc2VideoPpuCanExtend(1, streamable, 0x02, 0x00) ||
        Dkc2VideoPpuCanExtend(7, streamable, 0x17, 0x10) ||
        Dkc2VideoPpuWideLayerMask(1, streamable, 0x17, 0x10) != 0x01 ||
        Dkc2VideoPpuWideLayerMask(1, bg2_streamable, 0x06, 0x00) != 0x02) {
      fprintf(stderr, "FAIL: PPU widescreen capability classification\n");
      return 1;
    }
  }

  if (Dkc2VideoUnwrapPpuScroll(0x0010, 0x03f8) != 0x0410 ||
      Dkc2VideoUnwrapPpuScroll(0x03f0, 0x0408) != 0x03f0 ||
      Dkc2VideoUnwrapPpuScroll(0x0123, 0x0520) != 0x0523 ||
      Dkc2VideoUnwrapPpuScroll(0x0123, 0x0120) != 0x0123) {
    fprintf(stderr, "FAIL: PPU scroll phase unwrapping\n");
    return 1;
  }

  {
    uint8_t bank[0x10000];
    uint16_t tile = 0;
    memset(bank, 0, sizeof bank);

    /* World tile (5,10) -> metatile (1,2), sub-tile (1,2). */
    WriteWord(bank, 0x1024, 0x0003);
    WriteWord(bank, 0x2072, 0x1234);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000, 5, 10, &tile) ||
        tile != 0x1234) {
      fprintf(stderr, "FAIL: normal level metatile decode\n");
      return 1;
    }

    /* Horizontal flip selects sub-x 2 and applies the tilemap flip bit. */
    WriteWord(bank, 0x1024, 0x4003);
    WriteWord(bank, 0x2074, 0x0234);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000, 5, 10, &tile) ||
        tile != 0x4234) {
      fprintf(stderr, "FAIL: horizontally flipped metatile decode\n");
      return 1;
    }

    /* Both flips select sub-tile (2,1) and preserve both output flips. */
    WriteWord(bank, 0x1024, 0xc003);
    WriteWord(bank, 0x206c, 0x0567);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000, 5, 10, &tile) ||
        tile != 0xc567) {
      fprintf(stderr, "FAIL: doubly flipped metatile decode\n");
      return 1;
    }

    if (Dkc2VideoDecodeLevelTile(
            NULL, sizeof bank, 0x1000, 0x2000, 5, 10, &tile) ||
        Dkc2VideoDecodeLevelTile(
            bank, sizeof bank - 1u, 0x1000, 0x2000, 5, 10, &tile) ||
        Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000, 0x2000, 10, &tile)) {
      fprintf(stderr, "FAIL: invalid level source was accepted\n");
      return 1;
    }
  }

  {
    uint16_t vram[0x8000];
    uint16_t tile = 0xffff;
    const uint16_t base = 0x2000;
    const uint16_t transparent_tile = 0x0123;
    for (size_t word = 0; word < sizeof vram / sizeof vram[0]; word++)
      vram[word] = 0xffff;
    for (unsigned word = 0; word < 16u; word++)
      vram[(base + transparent_tile * 16u + word) & 0x7fffu] = 0;

    if (!Dkc2VideoFindTransparent4bppTile(
            vram, sizeof vram / sizeof vram[0], base, &tile) ||
        tile != transparent_tile) {
      fprintf(stderr, "FAIL: transparent 4bpp tile lookup\n");
      return 1;
    }
    if (Dkc2VideoFindTransparent4bppTile(
            NULL, sizeof vram / sizeof vram[0], base, &tile) ||
        Dkc2VideoFindTransparent4bppTile(vram, 16, base, &tile)) {
      fprintf(stderr, "FAIL: invalid transparent tile source was accepted\n");
      return 1;
    }
  }

  Dkc2VideoSetWidescreen(false);
  if (Dkc2VideoTerrainReady()) {
    fprintf(stderr, "FAIL: native mode retained widescreen terrain state\n");
    return 1;
  }

  puts("DKC2 video geometry tests passed");
  return 0;
}
