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
      Dkc2VideoExpandPlacementLeft(0x20, 0x00) != 0x20 ||
      Dkc2VideoExpandPlacementSpan(0x140, 0x00) != 0x140 ||
      Dkc2VideoPromoteOamXHigh(0x0120) != 0x0120 ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoNativeWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: native video geometry\n");
    return 1;
  }

  Dkc2VideoSetWidescreen(true);
  if (Dkc2VideoTerrainReady() ||
      Dkc2VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc2VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc2VideoExpandPlacementLeft(0x20, 0x00) != 0x20 ||
      Dkc2VideoExpandPlacementSpan(0x140, 0x00) != 0x140 ||
      Dkc2VideoPromoteOamXHigh(0x0120) != 0x0120) {
    fprintf(stderr, "FAIL: object bounds widened before terrain was ready\n");
    return 1;
  }
  Dkc2VideoSetTerrainReady(true);
  Dkc2VideoSetPlacementRadiusActivation(true);
  if (!Dkc2VideoIsWidescreen() ||
      !Dkc2VideoTerrainReady() ||
      Dkc2VideoWidth() != kDkc2VideoWidescreenWidth ||
      Dkc2VideoExtra() != kDkc2VideoWidescreenExtra ||
      Dkc2VideoExpandCullLeft(0x20) != 0x4b ||
      Dkc2VideoExpandCullSpan(0x140) != 0x196 ||
      Dkc2VideoExpandCullLeft(0x30) != 0x5b ||
      Dkc2VideoExpandCullSpan(0x160) != 0x1b6 ||
      Dkc2VideoExpandPlacementLeft(0x20, 0x00) != 0x4b ||
      Dkc2VideoExpandPlacementSpan(0x140, 0x10) != 0x196 ||
      Dkc2VideoPromoteOamXHigh(0x00ff) != 0x00ff ||
      Dkc2VideoPromoteOamXHigh(0x0100) != 0x8100 ||
      Dkc2VideoPromoteOamXHigh(0x012a) != 0x812a ||
      Dkc2VideoPromoteOamXHigh(0xffff) != 0xffff ||
      !Dkc2VideoTileTouchesWidescreenMargin(93, 752) ||
      Dkc2VideoTileTouchesWidescreenMargin(94, 752) ||
      Dkc2VideoTileTouchesWidescreenMargin(125, 752) ||
      !Dkc2VideoTileTouchesWidescreenMargin(126, 752) ||
      !Dkc2VideoTileTouchesWidescreenMargin(93, 755) ||
      !Dkc2VideoTileTouchesWidescreenMargin(126, 755) ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoWidescreenWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: widescreen video geometry\n");
    return 1;
  }
  Dkc2VideoSetPlacementRadiusActivation(false);
  if (Dkc2VideoExpandPlacementLeft(0x28, 0x00) != 0x28 ||
      Dkc2VideoExpandPlacementSpan(0x150, 0x50) != 0x150) {
    fprintf(stderr, "FAIL: live placement lifecycle radius was widened\n");
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
    const uint8_t dual_streamable[4] = {0x71, 0x79, 0x6c, 0x00};
    const uint8_t mainbrace[4] = {0x79, 0x70, 0x6c, 0x00};
    const uint8_t parrot_chute[4] = {0x6c, 0x79, 0x68, 0x00};
    const uint8_t web_woods[4] = {0x58, 0x69, 0x5c, 0x00};
    const uint8_t wrong_forest_atmosphere[4] = {0x54, 0x69, 0x5c, 0x00};
    if (Dkc2VideoPpuCanExtend(1, bounded, 0x07, 0x10) ||
        !Dkc2VideoPpuCanExtend(1, streamable, 0x17, 0x10) ||
        Dkc2VideoPpuCanExtend(1, streamable, 0x02, 0x00) ||
        Dkc2VideoPpuCanExtend(7, streamable, 0x17, 0x10) ||
        Dkc2VideoPpuWideLayerMask(1, streamable, 0x17, 0x10) != 0x01 ||
        Dkc2VideoPpuWideLayerMask(1, bg2_streamable, 0x06, 0x00) != 0x02 ||
        Dkc2VideoTerrainLayer(0x03, dual_streamable, 0x7000) != 0 ||
        Dkc2VideoTerrainLayer(0x03, dual_streamable, 0x7800) != 1 ||
        Dkc2VideoTerrainLayer(0x01, dual_streamable, 0x7800) != -1 ||
        Dkc2VideoTerrainLayer(0x03, dual_streamable, 0x7bff) != 1 ||
        Dkc2VideoTerrainLayer(0x02, bg2_streamable, 0x5c00) != 1 ||
        Dkc2VideoTerrainLayer(0x03, dual_streamable, 0x6800) != -1 ||
        Dkc2VideoTerrainLayer(0x03, NULL, 0x7000) != -1) {
      fprintf(stderr, "FAIL: PPU widescreen capability classification\n");
      return 1;
    }
    if (Dkc2VideoLevelLayoutForScene(0x0f, 0x0003) !=
            kDkc2VideoLevelLayoutHorizontal ||
        Dkc2VideoLevelLayoutForScene(0x0c, 0x0025) !=
            kDkc2VideoLevelLayoutVertical ||
        Dkc2VideoLevelLayoutForScene(0x10, 0x002e) !=
            kDkc2VideoLevelLayoutSquare ||
        Dkc2VideoLevelLayoutForScene(0x03, 0x0013) !=
            kDkc2VideoLevelLayoutNarrowVertical ||
        Dkc2VideoLevelLayoutForScene(0x03, 0x0002) !=
            kDkc2VideoLevelLayoutSquare ||
        Dkc2VideoLevelLayoutForScene(0x02, 0x0001) !=
            kDkc2VideoLevelLayoutUnknown ||
        Dkc2VideoLevelLayoutForScene(0xffff, 0xffff) !=
            kDkc2VideoLevelLayoutUnknown) {
      fprintf(stderr, "FAIL: DKC2 level map layout classification\n");
      return 1;
    }
    if (Dkc2VideoRepeatLayerMask(
            1, dual_streamable, 0x17, 0x00, 0x03, 0x002c) != 0x04 ||
        Dkc2VideoRepeatLayerMask(
            1, dual_streamable, 0x17, 0x00, 0x03, 0x002e) != 0x00 ||
        Dkc2VideoRepeatLayerMask(
            1, dual_streamable, 0x13, 0x00, 0x01, 0x002c) != 0x02 ||
        Dkc2VideoRepeatLayerMask(
            1, mainbrace, 0x04, 0x13, 0x01, 0x000c) != 0x06 ||
        Dkc2VideoRepeatLayerMask(
            1, parrot_chute, 0x01, 0x16, 0x02, 0x0013) != 0x05 ||
        Dkc2VideoRepeatLayerMask(
            1, parrot_chute, 0x01, 0x16, 0x00, 0x0013) != 0x02 ||
        Dkc2VideoRepeatLayerMask(
            1, web_woods, 0x01, 0x16, 0x02, 0x0017) != 0x05 ||
        Dkc2VideoRepeatLayerMask(
            1, web_woods, 0x01, 0x16, 0x00, 0x0017) != 0x02 ||
        Dkc2VideoRepeatLayerMask(
            1, web_woods, 0x17, 0x00, 0x02, 0x0018) != 0x05 ||
        Dkc2VideoRepeatLayerMask(
            1, web_woods, 0x01, 0x16, 0x02, 0x0019) != 0x00 ||
        Dkc2VideoRepeatLayerMask(
            1, wrong_forest_atmosphere, 0x01, 0x16, 0x02, 0x0017) != 0x00 ||
        Dkc2VideoRepeatLayerMask(
            1, web_woods, 0x00, 0x16, 0x02, 0x0017) != 0x00 ||
        Dkc2VideoRepeatLayerMask(
            7, dual_streamable, 0x17, 0x00, 0x03, 0x002c) != 0x00 ||
        Dkc2VideoRepeatLayerMask(
            1, NULL, 0x17, 0x00, 0x03, 0x002c) != 0x00) {
      fprintf(stderr, "FAIL: PPU widescreen repeat policy\n");
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
  if (Dkc2VideoTerrainShadowY(0x00cb, 0x01cd) != 0x00cb ||
      Dkc2VideoTerrainShadowY(0x002f, 0x0130) != 0x002f ||
      Dkc2VideoTerrainShadowY(0x03f8, 0x04f8) != 0x03f8 ||
      Dkc2VideoTerrainShadowY(0x0004, 0x0204) != 0x0404 ||
      (Dkc2VideoTerrainShadowY(0x0004, 0x0204) >> 3) !=
          Dkc2VideoLevelSourceTileY(0x0004, 0x0204, 0)) {
    fprintf(stderr, "FAIL: terrain shadow Y follows rendered source phase\n");
    return 1;
  }
  if (Dkc2VideoTerrainShadowX(0x02fd, 0x0300) != 0x02fd ||
      Dkc2VideoTerrainShadowX(0x0001, 0x03ff) != 0x0401 ||
      Dkc2VideoTerrainShadowX(0x03ff, 0x0401) != 0x03ff) {
    fprintf(stderr, "FAIL: terrain shadow X follows rendered source phase\n");
    return 1;
  }

  /*
   * At this observed NMI boundary WRAM camera Y is one pixel into the next
   * 8-pixel row while the rendered PPU phase is one pixel behind it. Mixing
   * their integer rows produced (5 - 6) & 31 == 31 and decoded row 37 into
   * source row 5. The rendered phase must remain authoritative.
   */
  if (Dkc2VideoLevelSourceTileY(0x002f, 0x0130, 0) != 5 ||
      Dkc2VideoLevelSourceTileY(0x002f, 0x0130, 1) != 6 ||
      Dkc2VideoLevelSourceTileY(0x0029, 0x012a, 0) != 5 ||
      Dkc2VideoLevelSourceTileY(0x03f8, 0x04f8, 2) != 129 ||
      Dkc2VideoLevelSourceTileY(0x009b, 0x069c, 0) != 275 ||
      Dkc2VideoLevelSourceTileY(0x009b, 0x069c, 1) != 276 ||
      Dkc2VideoLevelSourceTileY(0x003d, 0x0246, 0) != 135 ||
      Dkc2VideoLevelSourceTileY(0x003d, 0x0246, 1) != 136) {
    fprintf(stderr, "FAIL: level source Y follows rendered PPU phase\n");
    return 1;
  }
  if (Dkc2VideoLevelMapTileY(0x00a2, 0x01a3, 0) != 20 ||
      Dkc2VideoLevelMapTileY(0x002e, 0x012f, 0) != 5 ||
      Dkc2VideoLevelMapTileY(0x00ff, 0x0100, 0) != 0x1fff ||
      Dkc2VideoLevelMapTileY(0x00ff, 0x0100, 1) != 0 ||
      Dkc2VideoLevelMapTileY(0x009b, 0x069c, 0) != 179 ||
      Dkc2VideoLevelMapTileY(0x003d, 0x0246, 0) != 39) {
    fprintf(stderr, "FAIL: rolling level-map source page selection\n");
    return 1;
  }
  {
    const uint8_t ship_bg_xsc[4] = {0x71, 0x5c, 0x79, 0x00};
    const uint8_t other_bg_xsc[4] = {0x71, 0x5c, 0x6d, 0x00};
    if (!Dkc2VideoCanWidenShipRigging(
            0x0001, ship_bg_xsc, 0x17, 0x10) ||
        Dkc2VideoCanWidenShipRigging(
            0x0000, ship_bg_xsc, 0x17, 0x10) ||
        Dkc2VideoCanWidenShipRigging(
            0x0001, other_bg_xsc, 0x17, 0x10) ||
        Dkc2VideoCanWidenShipRigging(
            0x0001, ship_bg_xsc, 0x13, 0x10)) {
      fprintf(stderr, "ship rigging BG3 eligibility mismatch\n");
      return 1;
    }
  }

  {
    uint8_t bank[0x10000];
    uint16_t tile = 0;
    memset(bank, 0, sizeof bank);

    /* World tile (5,10) -> metatile (1,2), sub-tile (1,2). */
    WriteWord(bank, 0x1024, 0x0003);
    WriteWord(bank, 0x2072, 0x1234);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0x1234) {
      fprintf(stderr, "FAIL: normal level metatile decode\n");
      return 1;
    }

    /* Horizontal flip selects sub-x 2 and applies the tilemap flip bit. */
    WriteWord(bank, 0x1024, 0x4003);
    WriteWord(bank, 0x2074, 0x0234);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0x4234) {
      fprintf(stderr, "FAIL: horizontally flipped metatile decode\n");
      return 1;
    }

    /* Both flips select sub-tile (2,1) and preserve both output flips. */
    WriteWord(bank, 0x1024, 0xc003);
    WriteWord(bank, 0x206c, 0x0567);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0xc567) {
      fprintf(stderr, "FAIL: doubly flipped metatile decode\n");
      return 1;
    }

    if (Dkc2VideoDecodeLevelTile(
            NULL, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        Dkc2VideoDecodeLevelTile(
            bank, sizeof bank - 1u, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutHorizontal, 0x2000, 10, &tile) ||
        Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutUnknown, 5, 10, &tile)) {
      fprintf(stderr, "FAIL: invalid level source was accepted\n");
      return 1;
    }

    /* Vertical stages store the same metatiles in row-major order. */
    WriteWord(bank, 0x1082, 0x0003);
    WriteWord(bank, 0x2072, 0x3456);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutVertical, 5, 10, &tile) ||
        tile != 0x3456) {
      fprintf(stderr, "FAIL: vertical level metatile decode\n");
      return 1;
    }

    /* Square stages store 48 metatiles per row (0x60 bytes). */
    WriteWord(bank, 0x1182, 0x0003);
    WriteWord(bank, 0x2072, 0x4567);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutSquare, 5, 10, &tile) ||
        tile != 0x4567) {
      fprintf(stderr, "FAIL: square level metatile decode\n");
      return 1;
    }

    /* Parrot Chute Panic stores 16 metatiles per $20-byte row. */
    WriteWord(bank, 0x1042, 0x0003);
    WriteWord(bank, 0x2072, 0x5678);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutNarrowVertical, 5, 10, &tile) ||
        tile != 0x5678) {
      fprintf(stderr, "FAIL: narrow vertical level metatile decode\n");
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
    if (!Dkc2VideoIsTransparentTileEntry(transparent_tile, tile) ||
        !Dkc2VideoIsTransparentTileEntry(0xfc00u | transparent_tile, tile) ||
        Dkc2VideoIsTransparentTileEntry(transparent_tile + 1u, tile)) {
      fprintf(stderr, "FAIL: transparent tile-entry classification\n");
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
