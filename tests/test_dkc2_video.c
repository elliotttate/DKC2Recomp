#include "dkc2_hdma.h"
#include "dkc2_video.h"
#include "snes/ws_shadow.h"

#include <stdio.h>
#include <string.h>

static void WriteWord(uint8_t *data, uint16_t address, uint16_t value) {
  data[address] = (uint8_t)value;
  data[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

/* Synthetic guest memory for the HDMA dry run: one 64 KiB WRAM bank. */
static uint8_t s_fake_wram[0x10000];

static const uint8_t *FakeHdmaPointer(void *context, uint32_t address) {
  (void)context;
  if ((address >> 16) != 0x7eu)
    return NULL;
  return s_fake_wram + (address & 0xffffu);
}

static bool FakeHdmaReadable(void *context, const uint8_t *pointer,
                             size_t length) {
  (void)context;
  if (!pointer || pointer < s_fake_wram)
    return false;
  const size_t offset = (size_t)(pointer - s_fake_wram);
  return offset <= sizeof s_fake_wram && length <= sizeof s_fake_wram - offset;
}

static Dkc2VideoMetatileFill GridClassifier(void *context,
                                            uint32_t metatile_x,
                                            uint32_t metatile_y) {
  const char *const *grid = (const char *const *)context;
  if (metatile_y >= 4 || metatile_x >= 8)
    return kDkc2VideoMetatileUnknown;
  switch (grid[metatile_y][metatile_x]) {
    case '.': return kDkc2VideoMetatileEmpty;
    case '#': return kDkc2VideoMetatileFull;
    case '/': return kDkc2VideoMetatilePartial;
    default: return kDkc2VideoMetatileUnknown;
  }
}

static bool CheckMargins(uint16_t camera_x, uint16_t maximum_scroll_x,
                         int expected_bias, int expected_left,
                         int expected_right) {
  int bias = 99, left = 99, right = 99;
  Dkc2VideoPresentationMargins(camera_x, maximum_scroll_x,
                               &bias, &left, &right);
  if (bias != expected_bias || left != expected_left ||
      right != expected_right) {
    fprintf(stderr,
            "FAIL: presentation margins for camera %u max %u: "
            "got bias %d left %d right %d, expected %d %d %d\n",
            camera_x, maximum_scroll_x, bias, left, right,
            expected_bias, expected_left, expected_right);
    return false;
  }
  return true;
}

int main(void) {
  Dkc2VideoSetWidescreen(false);
  if (Dkc2VideoIsWidescreen() ||
      Dkc2VideoGetAspect() != kDkc2VideoAspectNative ||
      Dkc2VideoWidth() != kDkc2VideoNativeWidth ||
      Dkc2VideoExtra() != 0 ||
      Dkc2VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc2VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc2VideoPromoteOamXHigh(0x0120) != 0x0120 ||
      !CheckMargins(0x0100, 0x0800, 0, 0, 0) ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoNativeWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: native video geometry\n");
    return 1;
  }

  Dkc2VideoAspect parsed_aspect = kDkc2VideoAspectNative;
  if (!Dkc2VideoAspectFromName("16:10", &parsed_aspect) ||
      parsed_aspect != kDkc2VideoAspect16x10 ||
      strcmp(Dkc2VideoAspectName(parsed_aspect), "16:10") != 0 ||
      Dkc2VideoAspectFromName("wide", &parsed_aspect)) {
    fprintf(stderr, "FAIL: aspect vocabulary\n");
    return 1;
  }

  Dkc2VideoSetAspect(kDkc2VideoAspect16x10);
  if (!Dkc2VideoIsWidescreen() ||
      Dkc2VideoGetAspect() != kDkc2VideoAspect16x10 ||
      Dkc2VideoWidth() != 308 ||
      Dkc2VideoExtra() != kDkc2Video16x10Extra) {
    fprintf(stderr, "FAIL: 16:10 video geometry\n");
    return 1;
  }
  {
    Dkc2VideoEdgePolicy policy = kDkc2VideoEdgePolicyCount;
    if (Dkc2VideoGetEdgePolicy() != kDkc2VideoEdgeGlide ||
        !Dkc2VideoEdgePolicyFromName("bars", &policy) ||
        policy != kDkc2VideoEdgeBars ||
        !Dkc2VideoEdgePolicyFromName("shift", &policy) ||
        policy != kDkc2VideoEdgeShift ||
        !Dkc2VideoEdgePolicyFromName("glide", &policy) ||
        policy != kDkc2VideoEdgeGlide ||
        strcmp(Dkc2VideoEdgePolicyName(kDkc2VideoEdgeGlide), "glide") != 0 ||
        !Dkc2VideoEdgePolicyFromName("reflect", &policy) ||
        policy != kDkc2VideoEdgeReflect ||
        Dkc2VideoEdgePolicyFromName("wide", &policy) ||
        strcmp(Dkc2VideoEdgePolicyName(kDkc2VideoEdgeBars), "bars") != 0 ||
        strcmp(Dkc2VideoEdgePolicyName(kDkc2VideoEdgeShift), "shift") != 0) {
      fprintf(stderr, "FAIL: edge policy vocabulary\n");
      return 1;
    }
  }
  /* reflect: the view stays locked to the camera and both margins remain
   * visible everywhere; the terrain decoder mirrors columns at a wall. */
  /* reflect: the view is locked to the camera with full margins. */
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeReflect);
  if (!CheckMargins(0x0100, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 0, 26, 26) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26)) {
    fprintf(stderr, "FAIL: 16:10 reflect margins\n");
    return 1;
  }
  {
    uint32_t source = 99;
    bool mirror = true;
    if (Dkc2VideoResolveEdgeTile(40, 0x0100, &source, &mirror) != 0 ||
        source != 8 || mirror ||
        Dkc2VideoResolveEdgeTile(31, 0x0100, &source, &mirror) != 1 ||
        source != 0 || !mirror ||
        Dkc2VideoResolveEdgeTile(26, 0x0100, &source, &mirror) != 1 ||
        source != 5 || !mirror ||
        Dkc2VideoResolveEdgeTile(0, 0x0100, &source, &mirror) != 1 ||
        source != 31 || !mirror ||
        Dkc2VideoResolveEdgeTile(64, 0x0100, &source, &mirror) != 1 ||
        source != 31 || !mirror ||
        Dkc2VideoResolveEdgeTile(70, 0x0100, &source, &mirror) != 1 ||
        source != 25 || !mirror ||
        Dkc2VideoResolveEdgeTile(100, 0x0100, &source, &mirror) != -1 ||
        Dkc2VideoResolveEdgeTile(63, 0x0100, &source, &mirror) != 0 ||
        source != 31 || mirror ||
        Dkc2VideoResolveEdgeTile(31, 0x0100, NULL, &mirror) != -1 ||
        !Dkc2VideoMarginLeavesAuthoredExtent(0x0100, 0x0800) ||
        !Dkc2VideoMarginLeavesAuthoredExtent(0x0110, 0x0800) ||
        Dkc2VideoMarginLeavesAuthoredExtent(0x011a, 0x0800) ||
        Dkc2VideoMarginLeavesAuthoredExtent(0x0300, 0x0800) ||
        !Dkc2VideoMarginLeavesAuthoredExtent(0x07f0, 0x0800) ||
        Dkc2VideoMarginLeavesAuthoredExtent(0x0300, 0x00ff)) {
      fprintf(stderr, "FAIL: reflected edge tiles\n");
      return 1;
    }
  }
  {
    /* Structural wall continuation over a synthetic metatile map. */
    static const char *const grid[4] = {
        "..##..##",   /* row 0: empty target, thick wall two cells east */
        "..##..##",   /* row 1: corroborates row 0 */
        "../#..##",   /* row 2: a partial cell between target and wall */
        "..#/..##",   /* row 3: full cell but thin, no corroborating row */
    };
    uint32_t source = 99;
    if (!Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                           1, 3, 0, &source) ||
        source != 2 ||
        !Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                           0, 3, 1, &source) ||
        source != 2 ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 3, 2, &source) ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 3, 3, &source) ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          2, 3, 0, &source) ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          4, 3, 0, &source) ||
        !Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                           5, 3, 0, &source) ||
        source != 3 ||
        /* east: a target beside the wall itself still resolves to it. */
        !Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                           4, 3, 0, &source) ||
        source != 3 ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                          5, 6, 0, &source) ||
        Dkc2VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 1, 0, &source) ||
        Dkc2VideoFindStructuralWallSource(NULL, (void *)grid, false,
                                          1, 3, 0, &source)) {
      fprintf(stderr, "FAIL: structural wall source\n");
      return 1;
    }
    static uint16_t vram[0x8000];
    memset(vram, 0, sizeof vram);
    vram[0x1000 + 16 * 2 + 5] = 0x0100;  /* character 2 has one opaque row */
    if (!Dkc2VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x0001) ||
        Dkc2VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x0002) ||
        Dkc2VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x4002) ||
        !Dkc2VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0xc003) ||
        Dkc2VideoCharacterIsTransparent(vram, 0x100u, 0x1000, 0x0001)) {
      fprintf(stderr, "FAIL: character transparency\n");
      return 1;
    }
  }
  /* bars: the view stays locked to the camera and the visible margin
   * shrinks to the authored extent. */
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeBars);
  if (!CheckMargins(0x0100, 0x0800, 0, 0, 26) ||
      !CheckMargins(0x0110, 0x0800, 0, 16, 26) ||
      !CheckMargins(0x0300, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x07f8, 0x0800, 0, 26, 8) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      Dkc2VideoMarginLeavesAuthoredExtent(0x0100, 0x0800)) {
    fprintf(stderr, "FAIL: 16:10 bars margins\n");
    return 1;
  }
  {
    uint32_t source = 99;
    bool mirror = true;
    if (Dkc2VideoResolveEdgeTile(31, 0x0100, &source, &mirror) != -1 ||
        mirror ||
        Dkc2VideoResolveEdgeTile(64, 0x0100, &source, &mirror) != -1 ||
        Dkc2VideoResolveEdgeTile(40, 0x0100, &source, &mirror) != 0 ||
        source != 8) {
      fprintf(stderr, "FAIL: bars edge tiles\n");
      return 1;
    }
  }
  /* shift: presentation geometry at the fixed level endpoints. A room that
   * can absorb both margins biases the viewport inward and shows full
   * margins; a narrower room is centered and each visible margin is clamped
   * to the authored extent; an unknown bound keeps the symmetric margin; a
   * camera outside the authored range is never shifted by more than one
   * margin. */
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeShift);
  if (!CheckMargins(0x0100, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0110, 0x0800, 10, 26, 26) ||
      !CheckMargins(0x0200, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, -26, 26, 26) ||
      !CheckMargins(0x07f0, 0x0800, -10, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 16, 16, 16) ||
      !CheckMargins(0x0120, 0x0120, -16, 16, 16) ||
      !CheckMargins(0x0100, 0x0100, 0, 0, 0) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      !CheckMargins(0x0900, 0x0800, -26, 26, 0) ||
      !CheckMargins(0x0080, 0x0800, 26, 0, 26)) {
    fprintf(stderr, "FAIL: 16:10 presentation margins\n");
    return 1;
  }
  /* glide: the same pins as shift, but the inward shift is released one
   * pixel per eight pixels of camera travel from each wall, so the view is
   * centered 208 pixels in at 16:10 and the frame never leaves the level. */
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeGlide);
  if (!CheckMargins(0x0100, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0107, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0108, 0x0800, 25, 26, 26) ||
      !CheckMargins(0x0100 + 104, 0x0800, 13, 26, 26) ||
      !CheckMargins(0x0100 + 207, 0x0800, 1, 26, 26) ||
      !CheckMargins(0x0100 + 208, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0400, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800 - 208, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800 - 8, 0x0800, -25, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, -26, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 16, 16, 16) ||
      !CheckMargins(0x0120, 0x0120, -16, 16, 16) ||
      !CheckMargins(0x0100, 0x0100, 0, 0, 0) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      !CheckMargins(0x0900, 0x0800, -26, 26, 0) ||
      !CheckMargins(0x0080, 0x0800, 26, 0, 26)) {
    fprintf(stderr, "FAIL: 16:10 glide margins\n");
    return 1;
  }
  {
    const int lhs = Dkc2VideoWidth() * 7 * 10;
    const int rhs = kDkc2VideoHeight * 6 * 16;
    const int error = lhs > rhs ? lhs - rhs : rhs - lhs;
    if (error > 7 * 10) {
      fprintf(stderr,
              "FAIL: 16:10 geometry is not within one source pixel\n");
      return 1;
    }
  }

  Dkc2VideoSetWidescreen(true);
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeShift);
  if (Dkc2VideoTerrainReady() ||
      Dkc2VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc2VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc2VideoPromoteOamXHigh(0x0120) != 0x0120) {
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
      !CheckMargins(0x0100, 0x0800, 43, 43, 43) ||
      !CheckMargins(0x0800, 0x0800, -43, 43, 43) ||
      !CheckMargins(0x0100, 0x0140, 32, 32, 32) ||
      Dkc2VideoPixelCount() !=
          (size_t)kDkc2VideoWidescreenWidth * kDkc2VideoHeight) {
    fprintf(stderr, "FAIL: widescreen video geometry\n");
    return 1;
  }
  Dkc2VideoSetEdgePolicy(kDkc2VideoEdgeReflect);

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
    const uint8_t rattle_battle[4] = {0x71, 0x5c, 0x79, 0x00};
    const uint8_t topsail_trouble[4] = {0x79, 0x70, 0x6c, 0x00};
    const uint8_t mainbrace[4] = {0x79, 0x70, 0x6c, 0x00};
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
    /* A 64-column allocation whose second page overlaps another enabled
     * background is bounded content, not a physical width: Mudhole Marsh's
     * BG3 $6D ($6C00-$73FF) collides with BG1 $71 ($7000). */
    {
      const uint8_t mudhole[4] = {0x71, 0x79, 0x6d, 0x00};
      const uint8_t tall_bg1[4] = {0x73, 0x79, 0x7c, 0x00};
      if (!Dkc2VideoTilemapPagesCollide(mudhole, 2, 0x07) ||
          Dkc2VideoTilemapPagesCollide(mudhole, 2, 0x02) ||
          Dkc2VideoTilemapPagesCollide(mudhole, 0, 0x07) ||
          Dkc2VideoTilemapPagesCollide(mudhole, 1, 0x07) ||
          Dkc2VideoTilemapPagesCollide(rattle_battle, 2, 0x07) ||
          Dkc2VideoTilemapPagesCollide(streamable, 0, 0x07) ||
          !Dkc2VideoTilemapPagesCollide(tall_bg1, 0, 0x07) ||
          Dkc2VideoTilemapPagesCollide(tall_bg1, 0, 0x03) ||
          Dkc2VideoTilemapPagesCollide(NULL, 2, 0x07) ||
          Dkc2VideoPhysicalWideLayerMask(1, mudhole, 0x17, 0x00) != 0x03 ||
          Dkc2VideoPpuWideLayerMask(1, mudhole, 0x17, 0x00) != 0x03 ||
          Dkc2VideoRepeatLayerMask(1, 0x17, 0x00, 0x03) != 0x04) {
        fprintf(stderr, "FAIL: colliding tilemap pages are bounded\n");
        return 1;
      }
    }
    /* Capability floor for physical tilemap width. Rattle Battle uses the
     * standard ship-deck PPU shape: streamed BG1, bounded/repeated BG2, and
     * a real 64-column BG3. */
    if (Dkc2VideoPhysicalWideLayerMask(
            1, rattle_battle, 0x17, 0x10) != 0x05 ||
        Dkc2VideoPpuWideLayerMask(
            1, rattle_battle, 0x17, 0x10) != 0x01 ||
        Dkc2VideoPhysicalWideLayerMask(
            1, topsail_trouble, 0x17, 0x13) != 0x01 ||
        Dkc2VideoPhysicalWideLayerMask(
            1, mainbrace, 0x04, 0x13) != 0x01 ||
        Dkc2VideoPhysicalWideLayerMask(
            1, dual_streamable, 0x17, 0x00) != 0x03 ||
        Dkc2VideoPhysicalWideLayerMask(
            1, bounded, 0x07, 0x10) != 0x00 ||
        Dkc2VideoPhysicalWideLayerMask(
            7, rattle_battle, 0x17, 0x10) != 0x00 ||
        Dkc2VideoPhysicalWideLayerMask(
            1, NULL, 0x17, 0x10) != 0x00) {
      fprintf(stderr, "FAIL: physical-width layer capability floor\n");
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
        Dkc2VideoLevelLayoutForScene(0x02, 0x0015) !=
            kDkc2VideoLevelLayoutShipHold ||
        Dkc2VideoLevelLayoutForScene(0xffff, 0xffff) !=
            kDkc2VideoLevelLayoutUnknown) {
      fprintf(stderr, "FAIL: DKC2 level map layout classification\n");
      return 1;
    }
    /* Every enabled bounded background repeats; no level identity is
     * consulted. The wide mask (BG1/BG2 terrain plus any physical BG3)
     * excludes rolling layers from this mask. */
    if (Dkc2VideoRepeatLayerMask(1, 0x17, 0x00, 0x03) != 0x04 ||
        Dkc2VideoRepeatLayerMask(1, 0x13, 0x00, 0x01) != 0x02 ||
        Dkc2VideoRepeatLayerMask(1, 0x04, 0x13, 0x01) != 0x06 ||
        Dkc2VideoRepeatLayerMask(1, 0x17, 0x13, 0x01) != 0x06 ||
        Dkc2VideoRepeatLayerMask(1, 0x01, 0x16, 0x02) != 0x05 ||
        Dkc2VideoRepeatLayerMask(1, 0x17, 0x10, 0x05) != 0x02 ||
        Dkc2VideoRepeatLayerMask(1, 0x17, 0x00, 0x07) != 0x00 ||
        Dkc2VideoRepeatLayerMask(1, 0x10, 0x00, 0x03) != 0x00 ||
        Dkc2VideoRepeatLayerMask(7, 0x17, 0x00, 0x03) != 0x00) {
      fprintf(stderr, "FAIL: bounded-layer repeat policy\n");
      return 1;
    }

    /* Per-band terrain-phase classification. The vectors are the preserved
     * lava-stage HDMA states: BG2 owns the stream at the frame anchor, and
     * inside the upper band BG1 takes BG2's phase while BG2 moves to a
     * distant effect phase. Later in the same room the ordinary BG1 anchor
     * comes within 19/37 pixels of BG2 and must still be an effect plane;
     * a six-pixel reversal lead is terrain, seven is not. */
    if (!Dkc2VideoScrollAtTerrainPhase(414, 487, 414, 487) ||
        !Dkc2VideoScrollAtTerrainPhase(408, 487, 414, 487) ||
        !Dkc2VideoScrollAtTerrainPhase(420, 491, 414, 487) ||
        Dkc2VideoScrollAtTerrainPhase(407, 487, 414, 487) ||
        Dkc2VideoScrollAtTerrainPhase(414, 492, 414, 487) ||
        Dkc2VideoScrollAtTerrainPhase(829, 448, 414, 487) ||
        Dkc2VideoScrollAtTerrainPhase(207, 859, 414, 487) ||
        !Dkc2VideoScrollAtTerrainPhase(18, 495, 18, 495) ||
        Dkc2VideoScrollAtTerrainPhase(37, 458, 18, 495) ||
        Dkc2VideoScrollAtTerrainPhase(521, 863, 18, 495) ||
        !Dkc2VideoScrollAtTerrainPhase(833, 431, 833, 431) ||
        Dkc2VideoScrollAtTerrainPhase(416, 831, 833, 431) ||
        !Dkc2VideoScrollAtTerrainPhase(0x0002, 0x03fe, 0x03fe, 0x0001) ||
        Dkc2VideoScrollPhaseDistance(0x0002, 0x03fe) != 4 ||
        Dkc2VideoScrollPhaseDistance(0x0200, 0x0000) != 0x0200) {
      fprintf(stderr, "FAIL: terrain-phase band classification\n");
      return 1;
    }
  }

  {
    /* HDMA dry run against the preserved lava balloon-band geometry:
     * channel 0 moves BG2HOFS to 521 for lines 1-165 then restores 18;
     * channel 1 switches TM/TS to $13/$00 for lines 1-122, then $13/$04;
     * channel 2 is an indirect single-line BG1VOFS write; channel 3 points
     * outside readable memory and must terminate harmlessly. */
    memset(s_fake_wram, 0, sizeof s_fake_wram);
    /* A line count above 127 would set the repeat bit, so the cartridge
     * writes 165 lines as two entries. */
    uint8_t *table0 = s_fake_wram + 0x2000;
    table0[0] = 127; table0[1] = 0x09; table0[2] = 0x02;
    table0[3] = 38;  table0[4] = 0x09; table0[5] = 0x02;
    table0[6] = 59;  table0[7] = 0x12; table0[8] = 0x00;
    table0[9] = 0;
    uint8_t *table1 = s_fake_wram + 0x2100;
    table1[0] = 122; table1[1] = 0x13; table1[2] = 0x00;
    table1[3] = 59;  table1[4] = 0x13; table1[5] = 0x04;
    table1[6] = 0;
    uint8_t *table2 = s_fake_wram + 0x2200;
    table2[0] = 0x81; table2[1] = 0x00; table2[2] = 0x23;
    table2[3] = 0;
    s_fake_wram[0x2300] = 0x1f;
    s_fake_wram[0x2301] = 0x01;

    Dkc2HdmaChannelConfig channels[8];
    memset(channels, 0, sizeof channels);
    channels[0].active = true;
    channels[0].b_address = 0x0f;
    channels[0].mode = 2;
    channels[0].table_address = 0x7e2000;
    channels[1].active = true;
    channels[1].b_address = 0x2c;
    channels[1].mode = 1;
    channels[1].table_address = 0x7e2100;
    channels[2].active = true;
    channels[2].indirect = true;
    channels[2].indirect_bank = 0x7e;
    channels[2].b_address = 0x0e;
    channels[2].mode = 2;
    channels[2].table_address = 0x7e2200;
    channels[3].active = true;
    channels[3].b_address = 0x11;
    channels[3].mode = 2;
    channels[3].table_address = 0x7effff;
    channels[4].active = true;
    channels[4].b_address = 0x13;
    channels[4].mode = 2;
    channels[4].table_address = 0xc02000;

    Dkc2HdmaFrameState start;
    memset(&start, 0, sizeof start);
    start.h_scroll[0] = 643; start.h_scroll[1] = 18; start.h_scroll[2] = 834;
    start.v_scroll[0] = 442; start.v_scroll[1] = 495; start.v_scroll[2] = 175;
    start.main_layers = 0x13;
    start.sub_layers = 0x04;
    const Dkc2HdmaMemory memory = {FakeHdmaPointer, FakeHdmaReadable, NULL};
    static Dkc2HdmaBands bands;
    Dkc2HdmaScanBands(channels, &start, &memory, &bands);
    const Dkc2HdmaBand *first = Dkc2HdmaBandForLine(&bands, 1);
    const Dkc2HdmaBand *middle = Dkc2HdmaBandForLine(&bands, 150);
    const Dkc2HdmaBand *last = Dkc2HdmaBandForLine(&bands, 224);
    if (bands.count != 3 || !first || !middle || !last ||
        first->first_line != 1 || first->last_line != 122 ||
        middle->first_line != 123 || middle->last_line != 165 ||
        last->first_line != 166 || last->last_line != 224 ||
        first->h_scroll[1] != 521 || middle->h_scroll[1] != 521 ||
        last->h_scroll[1] != 18 ||
        first->main_layers != 0x13 || first->sub_layers != 0x00 ||
        middle->sub_layers != 0x04 || last->sub_layers != 0x04 ||
        first->v_scroll[0] != 0x011f || last->v_scroll[0] != 0x011f ||
        first->h_scroll[0] != 643 || first->h_scroll[2] != 834 ||
        Dkc2HdmaBandForLine(&bands, 0) != first ||
        Dkc2HdmaBandForLine(&bands, 122) != first ||
        Dkc2HdmaBandForLine(&bands, 123) != middle ||
        Dkc2HdmaBandForLine(&bands, 166) != last) {
      fprintf(stderr, "FAIL: HDMA band dry run (%d bands)\n", bands.count);
      return 1;
    }
    /* Without any active channel the whole frame is one band. */
    memset(channels, 0, sizeof channels);
    Dkc2HdmaScanBands(channels, &start, &memory, &bands);
    if (bands.count != 1 || bands.band[0].first_line != 1 ||
        bands.band[0].last_line != 224 ||
        bands.band[0].h_scroll[1] != 18 ||
        Dkc2HdmaBandForLine(&bands, 100) != &bands.band[0]) {
      fprintf(stderr, "FAIL: HDMA dry run without channels\n");
      return 1;
    }
    Dkc2HdmaScanBands(channels, NULL, &memory, &bands);
    if (bands.count != 0 || Dkc2HdmaBandForLine(&bands, 1) != NULL) {
      fprintf(stderr, "FAIL: HDMA dry run rejects a missing frame state\n");
      return 1;
    }
    /* Tilemap base and size writes (BGnSC) split bands as well: a lava
     * stage swaps the BG1 and BG2 maps at the lava line. */
    uint8_t *table3 = s_fake_wram + 0x2400;
    table3[0] = 100; table3[1] = 0x79; table3[2] = 0x67;
    table3[3] = 1;   table3[4] = 0x67; table3[5] = 0x79;
    table3[6] = 0;
    memset(channels, 0, sizeof channels);
    channels[0].active = true;
    channels[0].b_address = 0x07;
    channels[0].mode = 1;
    channels[0].table_address = 0x7e2400;
    start.bg_sc[0] = 0x67; start.bg_sc[1] = 0x79; start.bg_sc[2] = 0x74;
    Dkc2HdmaScanBands(channels, &start, &memory, &bands);
    if (bands.count != 2 || bands.band[0].first_line != 1 ||
        bands.band[0].last_line != 100 || bands.band[1].first_line != 101 ||
        bands.band[0].bg_sc[0] != 0x79 || bands.band[0].bg_sc[1] != 0x67 ||
        bands.band[1].bg_sc[0] != 0x67 || bands.band[1].bg_sc[1] != 0x79 ||
        bands.band[0].bg_sc[2] != 0x74 || bands.band[1].bg_sc[2] != 0x74) {
      fprintf(stderr, "FAIL: HDMA dry run tilemap base bands (%d bands)\n",
              bands.count);
      return 1;
    }
  }

  {
    uint8_t pages[4];
    if (Dkc2VideoTilemapPages(0x67, pages) != 4 || pages[0] != 25 ||
        pages[1] != 26 || pages[2] != 27 || pages[3] != 28 ||
        Dkc2VideoTilemapPages(0x79, pages) != 2 || pages[0] != 30 ||
        pages[1] != 31 ||
        Dkc2VideoTilemapPages(0x74, pages) != 1 || pages[0] != 29 ||
        Dkc2VideoTilemapPages(0x0a, pages) != 2 || pages[0] != 2 ||
        pages[1] != 3 || Dkc2VideoTilemapPages(0x67, NULL) != 0) {
      fprintf(stderr, "FAIL: tilemap page enumeration\n");
      return 1;
    }
  }

  {
    uint32_t mirrored = 0;
    /* West: the native edge is source tile 24; the tile beside it mirrors
     * the edge tile, the next one the tile behind it. */
    if (!Dkc2VideoMirrorSourceTileAcrossEdge(23, 24, false, &mirrored) ||
        mirrored != 24 ||
        !Dkc2VideoMirrorSourceTileAcrossEdge(20, 24, false, &mirrored) ||
        mirrored != 27 ||
        Dkc2VideoMirrorSourceTileAcrossEdge(24, 24, false, &mirrored) ||
        Dkc2VideoMirrorSourceTileAcrossEdge(30, 24, false, &mirrored)) {
      fprintf(stderr, "FAIL: west held-wall mirror\n");
      return 1;
    }
    /* East: the native edge is source tile 55. */
    if (!Dkc2VideoMirrorSourceTileAcrossEdge(56, 55, true, &mirrored) ||
        mirrored != 55 ||
        !Dkc2VideoMirrorSourceTileAcrossEdge(59, 55, true, &mirrored) ||
        mirrored != 52 ||
        Dkc2VideoMirrorSourceTileAcrossEdge(55, 55, true, &mirrored) ||
        Dkc2VideoMirrorSourceTileAcrossEdge(50, 55, true, &mirrored) ||
        Dkc2VideoMirrorSourceTileAcrossEdge(56, 55, true, NULL)) {
      fprintf(stderr, "FAIL: east held-wall mirror\n");
      return 1;
    }
  }

  {
    /* Wrap authoring of a 64-column plane. */
    static uint16_t vram[0x8000];
    memset(vram, 0, sizeof vram);
    /* A 96-pixel cabin wall (period 12 columns) on a 64x32 map at $7800. */
    for (unsigned row = 0; row < 12; row++)
      for (unsigned column = 0; column < 64; column++)
        vram[0x7800 + (column >= 32 ? 0x400 : 0) + (row << 5) +
             (column & 31)] = (uint16_t)(0x100 + column % 12);
    if (Dkc2VideoTilemapWrapsAuthored(vram, 0x8000, 0x79)) {
      fprintf(stderr, "FAIL: a 96-pixel wall was taken for a wrapping plane\n");
      return 1;
    }
    /* A sparse rock plane on a 64x64 map at $6400: scattered entries in
     * rows 16-30 reaching both map edges, then far spikes repeating every
     * 32 columns in rows 40-63. */
    for (unsigned row = 16; row <= 30; row++)
      for (unsigned column = 0; column < 64; column++)
        if ((column * 7 + row * 3) % 5 == 0 || column == row - 16 ||
            column == 63 - (row - 16))
          vram[0x6400 + (column >= 32 ? 0x400 : 0) +
               (row >= 32 ? 0x800 : 0) + ((row & 31) << 5) +
               (column & 31)] = (uint16_t)(0x200 + column + row);
    for (unsigned row = 40; row < 64; row++)
      for (unsigned column = 0; column < 64; column++)
        vram[0x6400 + (column >= 32 ? 0x400 : 0) + 0x800 +
             ((row & 31) << 5) + (column & 31)] =
            (uint16_t)(0x300 + column % 32 + row);
    if (!Dkc2VideoTilemapWrapsAuthored(vram, 0x8000, 0x67)) {
      fprintf(stderr, "FAIL: the rock plane was not taken for a wrapping plane\n");
      return 1;
    }
    /* The same plane with its last nine columns blank in every row is a
     * backdrop narrower than its allocation. */
    for (unsigned row = 0; row < 64; row++)
      for (unsigned column = 55; column < 64; column++)
        vram[0x6400 + 0x400 + (row >= 32 ? 0x800 : 0) + ((row & 31) << 5) +
             (column & 31)] = 0;
    if (Dkc2VideoTilemapWrapsAuthored(vram, 0x8000, 0x67)) {
      fprintf(stderr, "FAIL: a blank edge strip was taken for a wrapping plane\n");
      return 1;
    }
    /* Empty, 32-column, and missing maps are never planes. */
    if (Dkc2VideoTilemapWrapsAuthored(vram, 0x8000, 0x45) ||
        Dkc2VideoTilemapWrapsAuthored(vram, 0x8000, 0x74) ||
        Dkc2VideoTilemapWrapsAuthored(vram, 0x4000, 0x67) ||
        Dkc2VideoTilemapWrapsAuthored(NULL, 0x8000, 0x67)) {
      fprintf(stderr, "FAIL: an empty or bounded map was taken for a plane\n");
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
  {
    /* At Topsail Trouble's lower camera limit the rendered 10-bit phase
     * unwraps to world tile 512. The complete 224-pixel viewport reaches
     * tile 540, so the shared world-keyed shadow must retain that range. */
    const uint32_t topsail_top =
        Dkc2VideoLevelSourceTileY(0x0007, 0x0f08, 0);
    const uint32_t topsail_bottom =
        Dkc2VideoLevelSourceTileY(0x0007, 0x0f08, 28);
    if (topsail_top != 512 || topsail_bottom != 540 ||
        topsail_bottom >= kWsShadowYTiles) {
      fprintf(stderr, "FAIL: terrain shadow cannot retain Topsail bottom\n");
      return 1;
    }
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

    /* Ship-hold maps store 80 metatiles per $a0-byte row. */
    WriteWord(bank, 0x1142, 0x0003);
    WriteWord(bank, 0x2072, 0x6789);
    if (!Dkc2VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc2VideoLevelLayoutShipHold, 5, 10, &tile) ||
        tile != 0x6789) {
      fprintf(stderr, "FAIL: ship-hold level metatile decode\n");
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

  {
    /* Ship-deck rigging decode: a synthetic bank with two metatile
     * definitions and a map that uses each flip flag once. */
    static uint8_t bank[0x10000];
    memset(bank, 0, sizeof bank);
    /* Definition 0 holds sixteen distinct tiles in row-major order.
     * Definition 1 is authored with its horizontal flip bit already set. */
    for (unsigned row = 0; row < 4; row++)
      for (unsigned column = 0; column < 4; column++)
        WriteWord(bank,
                  (uint16_t)(kDkc2VideoRiggingMetatileOffset + row * 8u +
                             column * 2u),
                  (uint16_t)(0x0100u + row * 4u + column));
    for (unsigned word = 0; word < 16; word++)
      WriteWord(bank,
                (uint16_t)(kDkc2VideoRiggingMetatileOffset + 32u + word * 2u),
                0x4200u);
    /* Map column 0, rows 0-3: definition 0 plain, mirrored horizontally,
     * mirrored vertically, and both. Map column 39 (the last), row 15:
     * definition 1 mirrored horizontally. */
    const uint16_t map = kDkc2VideoRiggingMapOffset;
    WriteWord(bank, (uint16_t)(map + 0u), 0x0000u);
    WriteWord(bank, (uint16_t)(map + 2u), 0x4000u);
    WriteWord(bank, (uint16_t)(map + 4u), 0x8000u);
    WriteWord(bank, (uint16_t)(map + 6u), 0xc000u);
    WriteWord(bank, (uint16_t)(map + 39u * 32u + 15u * 2u), 0x4001u);
    uint16_t tile = 0;
    /* Pixel (8, 16) is tile column 1, row 2 of the metatile. */
    if (!Dkc2VideoDecodeRiggingTile(bank, sizeof bank, 8, 16, &tile) ||
        tile != 0x0109u ||
        !Dkc2VideoDecodeRiggingTile(bank, sizeof bank, 8, 32 + 16, &tile) ||
        tile != 0x410au ||
        !Dkc2VideoDecodeRiggingTile(bank, sizeof bank, 8, 64 + 16, &tile) ||
        tile != 0x8105u ||
        !Dkc2VideoDecodeRiggingTile(bank, sizeof bank, 8, 96 + 16, &tile) ||
        tile != 0xc106u) {
      fprintf(stderr, "FAIL: rigging metatile decode\n");
      return 1;
    }
    /* Map X wraps at 1280 and map Y at 512, and a flag toggles a tile's
     * own flip bit rather than setting it. */
    if (!Dkc2VideoDecodeRiggingTile(bank, sizeof bank,
                                    1280u * 2u + 1248u + 8u,
                                    512u + 15u * 32u + 8u, &tile) ||
        tile != 0x0200u) {
      fprintf(stderr, "FAIL: rigging map wrap or flip toggle\n");
      return 1;
    }
    if (Dkc2VideoDecodeRiggingTile(NULL, sizeof bank, 0, 0, &tile) ||
        Dkc2VideoDecodeRiggingTile(bank, 0x8000, 0, 0, &tile)) {
      fprintf(stderr, "FAIL: invalid rigging source was accepted\n");
      return 1;
    }
  }

  {
    /* The rigging's 8-bit vertical scroll is rebuilt in 256-pixel epochs
     * from the camera: below and above map Y 256, with the PPU trailing the
     * camera by a pixel, and at a 256 boundary. */
    if (Dkc2VideoRiggingShadowY(103, 360) != 103 ||
        Dkc2VideoRiggingShadowY(0, 514) != 256 ||
        Dkc2VideoRiggingShadowY(0, 513) != 256 ||
        Dkc2VideoRiggingShadowY(11, 525) != 267 ||
        Dkc2VideoRiggingShadowY(255, 513) != 255 ||
        Dkc2VideoRiggingShadowY(1, 770) != 513) {
      fprintf(stderr, "FAIL: rigging vertical epoch\n");
      return 1;
    }
  }

  {
    /* Row-upload high-byte shift: exact cells match; a cell whose low byte
     * is exact and whose high byte is the previous cell's decoded high byte
     * matches; the first cell of a page accepts any high byte; a low-byte
     * difference never matches. */
    if (!Dkc2VideoRiggingCellMatches(0x6141u, 0x6141u, 0x413eu, false) ||
        !Dkc2VideoRiggingCellMatches(0x6141u, 0x4141u, 0x413eu, false) ||
        Dkc2VideoRiggingCellMatches(0x6141u, 0x4141u, 0x613eu, false) ||
        !Dkc2VideoRiggingCellMatches(0x6141u, 0x0141u, 0x613eu, true) ||
        Dkc2VideoRiggingCellMatches(0x6141u, 0x6142u, 0x6141u, true) ||
        Dkc2VideoRiggingCellMatches(0x413eu, 0x4141u, 0x413eu, false)) {
      fprintf(stderr, "FAIL: rigging row-upload cell rule\n");
      return 1;
    }
  }

  {
    uint16_t vram[0x8000];
    uint16_t tile = 0xffff;
    const uint16_t base = 0x3000;
    const uint16_t transparent_tile = 0x013e;
    for (size_t word = 0; word < sizeof vram / sizeof vram[0]; word++)
      vram[word] = 0xffff;
    for (unsigned word = 0; word < 8u; word++)
      vram[(base + transparent_tile * 8u + word) & 0x7fffu] = 0;
    if (!Dkc2VideoFindTransparent2bppTile(
            vram, sizeof vram / sizeof vram[0], base, &tile) ||
        tile != transparent_tile) {
      fprintf(stderr, "FAIL: transparent 2bpp tile lookup\n");
      return 1;
    }
    if (Dkc2VideoFindTransparent2bppTile(
            NULL, sizeof vram / sizeof vram[0], base, &tile) ||
        Dkc2VideoFindTransparent2bppTile(vram, 8, base, &tile)) {
      fprintf(stderr, "FAIL: invalid transparent 2bpp source was accepted\n");
      return 1;
    }
  }

  {
    /* Lava geyser steam decode: a synthetic upper half of bank $80 with the
     * long-pointer table and one distinct block per animation frame. */
    static uint8_t bank[0x8000];
    memset(bank, 0, sizeof bank);
    const uint16_t table = (uint16_t)(kDkc2VideoGeyserTableAddress & 0x7fffu);
    for (unsigned frame = 0; frame < 8u; frame++) {
      const bool tall = frame >= 4u;
      const unsigned rows = tall ? 18u : 10u;
      const uint16_t block = (uint16_t)(0x1000u + frame * 0x100u);
      bank[table + frame * 4u] = (uint8_t)((block + 0x8000u) & 0xffu);
      bank[table + frame * 4u + 1u] = (uint8_t)((block + 0x8000u) >> 8);
      bank[table + frame * 4u + 2u] = 0x80;
      for (unsigned column = 0; column < 3u; column++)
        for (unsigned row = 0; row < rows; row++)
          WriteWord(bank,
                    (uint16_t)(block + (column * rows + row) * 2u),
                    (uint16_t)(0x0100u * (frame + 1u) + column * 0x20u +
                               row));
    }
    uint16_t entry = 0;
    if (Dkc2VideoGeyserRows(true) != 18u || Dkc2VideoGeyserRows(false) != 10u ||
        Dkc2VideoGeyserFirstMapRow(true) != 6u ||
        Dkc2VideoGeyserFirstMapRow(false) != 14u) {
      fprintf(stderr, "FAIL: geyser block geometry\n");
      return 1;
    }
    uint32_t tile_x = 0;
    if (!Dkc2VideoGeyserFirstTileX(0x0321u, &tile_x) || tile_x != 99u ||
        !Dkc2VideoGeyserFirstTileX(800u, &tile_x) || tile_x != 99u ||
        !Dkc2VideoGeyserFirstTileX(8u, &tile_x) || tile_x != 0u ||
        Dkc2VideoGeyserFirstTileX(7u, &tile_x) ||
        Dkc2VideoGeyserFirstTileX(0u, &tile_x) ||
        Dkc2VideoGeyserFirstTileX(800u, NULL)) {
      fprintf(stderr, "FAIL: geyser leftmost tile column\n");
      return 1;
    }
    if (!Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 0, 0, &entry) ||
        entry != 0x0100u ||
        !Dkc2VideoGeyserEntry(bank, sizeof bank, 2, false, 2, 9, &entry) ||
        entry != 0x0349u ||
        !Dkc2VideoGeyserEntry(bank, sizeof bank, 0, true, 1, 17, &entry) ||
        entry != 0x0531u ||
        !Dkc2VideoGeyserEntry(bank, sizeof bank, 3, true, 2, 0, &entry) ||
        entry != 0x0840u) {
      fprintf(stderr, "FAIL: geyser block entry decode\n");
      return 1;
    }
    if (Dkc2VideoGeyserEntry(bank, sizeof bank, 4, false, 0, 0, &entry) ||
        Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 3, 0, &entry) ||
        Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 0, 10, &entry) ||
        Dkc2VideoGeyserEntry(bank, sizeof bank, 0, true, 0, 18, &entry) ||
        Dkc2VideoGeyserEntry(NULL, sizeof bank, 0, false, 0, 0, &entry) ||
        Dkc2VideoGeyserEntry(bank, 0x4000u, 0, false, 0, 0, &entry) ||
        Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 0, 0, NULL)) {
      fprintf(stderr, "FAIL: invalid geyser entry request was accepted\n");
      return 1;
    }
    /* A block pointer outside bank $80's ROM half is refused. */
    bank[table + 2u] = 0x81;
    if (Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 0, 0, &entry)) {
      fprintf(stderr, "FAIL: foreign geyser block pointer was accepted\n");
      return 1;
    }
    bank[table + 2u] = 0x80;
    bank[table + 1u] = 0x10;
    if (Dkc2VideoGeyserEntry(bank, sizeof bank, 0, false, 0, 0, &entry)) {
      fprintf(stderr, "FAIL: low-half geyser block pointer was accepted\n");
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
