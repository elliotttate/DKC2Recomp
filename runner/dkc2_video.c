#include "dkc2_video.h"

#include <string.h>

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static Dkc2VideoAspect s_aspect;

void Dkc2VideoSetAspect(Dkc2VideoAspect aspect) {
  if (aspect < kDkc2VideoAspectNative || aspect >= kDkc2VideoAspectCount)
    aspect = kDkc2VideoAspectNative;
  if (s_aspect != aspect)
    s_terrain_ready = false;
  s_aspect = aspect;
  g_ws_active = aspect != kDkc2VideoAspectNative;
  switch (aspect) {
    case kDkc2VideoAspect16x10:
      g_ws_extra = kDkc2Video16x10Extra;
      break;
    case kDkc2VideoAspect16x9:
      g_ws_extra = kDkc2VideoWidescreenExtra;
      break;
    case kDkc2VideoAspectNative:
    default:
      g_ws_extra = 0;
      break;
  }
}

Dkc2VideoAspect Dkc2VideoGetAspect(void) {
  return s_aspect;
}

bool Dkc2VideoAspectFromName(const char *name, Dkc2VideoAspect *aspect) {
  if (!name || !aspect)
    return false;
  if (strcmp(name, "4:3") == 0 || strcmp(name, "native") == 0 ||
      strcmp(name, "0") == 0) {
    *aspect = kDkc2VideoAspectNative;
    return true;
  }
  if (strcmp(name, "16:10") == 0 || strcmp(name, "1") == 0) {
    *aspect = kDkc2VideoAspect16x10;
    return true;
  }
  if (strcmp(name, "16:9") == 0 || strcmp(name, "2") == 0) {
    *aspect = kDkc2VideoAspect16x9;
    return true;
  }
  return false;
}

const char *Dkc2VideoAspectName(Dkc2VideoAspect aspect) {
  switch (aspect) {
    case kDkc2VideoAspect16x10:
      return "16:10";
    case kDkc2VideoAspect16x9:
      return "16:9";
    case kDkc2VideoAspectNative:
    default:
      return "4:3";
  }
}

void Dkc2VideoSetWidescreen(bool enabled) {
  Dkc2VideoSetAspect(enabled ? kDkc2VideoAspect16x9
                             : kDkc2VideoAspectNative);
}

bool Dkc2VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc2VideoSetTerrainReady(bool ready) {
  s_terrain_ready = g_ws_active && ready;
}

bool Dkc2VideoTerrainReady(void) {
  return g_ws_active && s_terrain_ready;
}

int Dkc2VideoWidth(void) {
  return kDkc2VideoNativeWidth + 2 * g_ws_extra;
}

int Dkc2VideoExtra(void) {
  return g_ws_extra;
}

size_t Dkc2VideoPixelCount(void) {
  return (size_t)Dkc2VideoWidth() * kDkc2VideoHeight;
}

bool Dkc2VideoTileTouchesWidescreenMargin(uint32_t world_tile_x,
                                          uint32_t camera_x) {
  const uint64_t left = (uint64_t)world_tile_x << 3;
  const uint64_t native_left = camera_x;
  const uint64_t native_right = native_left + kDkc2VideoNativeWidth;
  return left < native_left || left + 8u > native_right;
}

bool Dkc2VideoResolveWestBoundaryTile(Dkc2VideoLevelLayout layout,
                                      uint32_t world_tile_x,
                                      uint32_t camera_x,
                                      uint32_t *source_tile_x,
                                      bool *mirror_horizontally) {
  const uint32_t terrain_origin_tile = 0x0100u >> 3;
  if (!source_tile_x || !mirror_horizontally ||
      !Dkc2VideoIsWidescreen() ||
      layout == kDkc2VideoLevelLayoutUnknown ||
      world_tile_x >= terrain_origin_tile ||
      !Dkc2VideoTileTouchesWidescreenMargin(world_tile_x, camera_x))
    return false;

  /* Reflect about the boundary between world tiles 31 and 32:
   * 31 -> source 0, 30 -> source 1, and so on. Reversing tile order and
   * toggling each decoded entry's H-flip bit gives a pixel-exact reflection
   * of the first authored terrain instead of a repeated vertical strip. */
  *source_tile_x = terrain_origin_tile - 1u - world_tile_x;
  *mirror_horizontally = true;
  return true;
}

uint16_t Dkc2VideoExpandCullLeft(uint16_t native_margin) {
  return (uint16_t)(native_margin +
                    (Dkc2VideoTerrainReady() ? g_ws_extra : 0));
}

uint16_t Dkc2VideoExpandCullSpan(uint16_t native_span) {
  return (uint16_t)(native_span +
                    (Dkc2VideoTerrainReady() ? 2 * g_ws_extra : 0));
}

uint16_t Dkc2VideoPromoteOamXHigh(uint16_t screen_x) {
  /* DKC2's banana renderer derives OAM's ninth X bit from bit 15 because
   * native play only needs that bit for negative coordinates. In the
   * widened right margin, $0100-$012a must therefore mirror bit 8 into the
   * sign position before the original packing sequence performs XBA/ASL. */
  if (Dkc2VideoTerrainReady() && (screen_x & 0x0100u))
    return (uint16_t)(screen_x | 0x8000u);
  return screen_x;
}

uint8_t Dkc2VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers) {
  /* DKC2's audited rolling level tilemaps use Mode 1. Mode 7 and other
   * special screens need explicit reconstruction rather than stale BGxSC
   * state accidentally opting them into the generic path. */
  if ((bg_mode & 7u) != 1u) return 0;

  uint8_t enabled = (uint8_t)((main_layers | sub_layers) & 0x0f);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 2; layer++) {
    uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

uint8_t Dkc2VideoPhysicalWideLayerMask(uint8_t bg_mode,
                                       const uint8_t bg_xsc[4],
                                       uint8_t main_layers,
                                       uint8_t sub_layers) {
  if (!bg_xsc || (bg_mode & 7u) != 1u)
    return 0;

  const uint8_t enabled =
      (uint8_t)((main_layers | sub_layers) & 0x07u);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 3; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

bool Dkc2VideoCanRepeatShipHoldBackdrop(uint16_t game_sub_mode,
                                        const uint8_t bg_xsc[4],
                                        uint8_t main_layers,
                                        uint8_t sub_layers,
                                        uint8_t wide_layer_mask,
                                        int terrain_layer) {
  if (!bg_xsc || game_sub_mode != 0x02u || terrain_layer != 0)
    return false;
  const uint8_t enabled = (uint8_t)(main_layers | sub_layers);
  const uint8_t bg2_base = (uint8_t)(bg_xsc[1] & 0xfcu);
  return (wide_layer_mask & 0x03u) == 0x03u &&
         (enabled & 0x02u) != 0u &&
         (bg_xsc[0] & 0xfcu) == 0x38u &&
         (bg2_base == 0x70u || bg2_base == 0x78u);
}

uint8_t Dkc2VideoRepeatLayerMask(uint8_t bg_mode,
                                const uint8_t bg_xsc[4],
                                uint8_t main_layers,
                                uint8_t sub_layers,
                                uint8_t wide_layer_mask,
                                uint16_t level_number,
                                uint16_t game_sub_mode) {
  if (!bg_xsc || (bg_mode & 7u) != 1u)
    return 0;

  const uint8_t enabled =
      (uint8_t)((main_layers | sub_layers) & 0x0fu);
  uint8_t repeat =
      (uint8_t)(enabled & (uint8_t)~wide_layer_mask & 0x02u);

  /*
   * Mudhole Marsh ($002c) uses BG3 $6c00 as a cyclic 2bpp forest backdrop.
   * Repeat the fully rendered native scanline rather than reading unseen
   * tilemap columns. Other BG3 uses remain clamped until audited.
   */
  if (level_number == 0x002cu &&
      (enabled & 0x04u) &&
      (bg_xsc[2] & 0xfcu) == 0x6cu)
    repeat = (uint8_t)(repeat | 0x04u);

  /* Topsail Trouble and Mainbrace Mayhem use BG3 $6c00 as bounded cyclic
   * weather overlays above independently widened BG1 terrain. Topsail's
   * layer is rain; Mainbrace uses cloud and lighting. Repeat the rendered
   * scanline so HDMA/per-line phase is preserved without inventing VRAM. */
  if (((level_number == 0x000bu && game_sub_mode == 0x0008u) ||
       level_number == 0x000cu) &&
      (wide_layer_mask & 0x01u) &&
      (enabled & 0x04u) &&
      (bg_xsc[2] & 0xfcu) == 0x6cu)
    repeat = (uint8_t)(repeat | 0x04u);

  /* Parrot Chute Panic's attract route streams terrain through wide BG2.
   * Its bounded BG1 honey drips and BG3 hive wall are cyclic backdrops. */
  if (level_number == 0x0013u && (wide_layer_mask & 0x02u)) {
    if ((enabled & 0x01u) && (bg_xsc[0] & 0xfcu) == 0x6cu)
      repeat = (uint8_t)(repeat | 0x01u);
    if ((enabled & 0x04u) && (bg_xsc[2] & 0xfcu) == 0x68u)
      repeat = (uint8_t)(repeat | 0x04u);
  }

  /* Ship-hold rooms use BG3 $6c00 for the animated water surface. The
   * cartridge supplies it as a 32-column repeating tilemap and varies its
   * horizontal phase through HDMA. Repeat the already-rendered native
   * scanline into both margins so those per-line phases remain intact; raw
   * adjacent VRAM does not contain additional water columns. */
  if (game_sub_mode == 0x02u &&
      (enabled & 0x04u) &&
      (bg_xsc[2] & 0xfcu) == 0x6cu)
    repeat = (uint8_t)(repeat | 0x04u);

  return repeat;
}

bool Dkc2VideoPpuCanExtend(uint8_t bg_mode,
                           const uint8_t bg_xsc[4],
                           uint8_t main_layers,
                           uint8_t sub_layers) {
  return Dkc2VideoPpuWideLayerMask(
             bg_mode, bg_xsc, main_layers, sub_layers) != 0;
}

int Dkc2VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address) {
  if (!bg_xsc)
    return -1;

  const uint16_t stream_base =
      (uint16_t)(stream_vram_word_address & 0xfc00u);
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    const uint16_t map_base =
        (uint16_t)((uint16_t)(bg_xsc[layer] & 0xfcu) << 8);
    if ((wide_layer_mask & bit) && map_base == stream_base)
      return layer;
  }
  return -1;
}

Dkc2VideoLevelLayout Dkc2VideoLevelLayoutForScene(
    uint16_t game_sub_mode, uint16_t level_number) {
  /* wasp_hive_game_sub_mode normally calls square_level_scroll_handler
   * ($B5:B54A). Its level-variant nibble selects the alternate $B5:B317
   * path for Parrot Chute Panic ($0013), whose map has 16 metatiles per
   * $20-byte row. Keep that exception narrow; expose the ordinary hive
   * rooms through the same 48-metatile/$60-byte square layout already used
   * by the shared cartridge handler. Visual route acceptance is still
   * required for Hornet Hole, Rambi Rumble, and the King Zing arena. */
  if (game_sub_mode == 0x03u) {
    if (level_number == 0x0013u)
      return kDkc2VideoLevelLayoutNarrowVertical;
    return kDkc2VideoLevelLayoutSquare;
  }

  switch (game_sub_mode) {
    case 0x02:
      /* ship_hold_game_sub_mode ($80:D486) calls the square scroll family,
       * while ship-hold source maps use the tileset's proven 80-metatile
       * row stride. Lockjaw's Locker's exact state matches all 957 visible
       * BG1 cells with this decoder; treating its 64-column VRAM ring as a
       * static map exposes stale pages at both widescreen edges. */
      return kDkc2VideoLevelLayoutShipHold;
    case 0x01:
    case 0x06:
    case 0x07:
    case 0x09:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x12:
    case 0x15:
    case 0x18:
    case 0x1a:
    case 0x1f:
      return kDkc2VideoLevelLayoutHorizontal;
    case 0x08:
    case 0x0c:
    case 0x16:
    case 0x1e:
      return kDkc2VideoLevelLayoutVertical;
    case 0x10:
      return kDkc2VideoLevelLayoutSquare;
    default:
      return kDkc2VideoLevelLayoutUnknown;
  }
}

uint32_t Dkc2VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor) {
  const uint32_t period = 0x400u;
  const uint32_t half_period = period / 2u;
  uint32_t candidate = (anchor & ~(period - 1u)) |
                       ((uint32_t)ppu_scroll & (period - 1u));
  if (candidate + half_period < anchor)
    candidate += period;
  else if (candidate > anchor + half_period && candidate >= period)
    candidate -= period;
  return candidate;
}

uint32_t Dkc2VideoTerrainShadowY(uint16_t ppu_scroll_y, uint32_t camera_y) {
  /* Keep the shadow origin in the same epoch as the tile-row decoder.
   * Dkc2VideoLevelSourceTileY aligns the PPU value to an 8-pixel row before
   * unwrapping it. Unwrapping the fine value independently can choose the
   * opposite 1024-pixel epoch at the exact +/-512 tie. Pirate Panic reaches
   * that boundary at camera Y=$0204 / PPU Y=$0004 after Rambi's charge: the
   * prefill was keyed at tile row 128 while margin lookup started at row 0,
   * producing a one-frame verified-blank strip. Unwrap the common tile
   * origin once, then restore the rendered fine phase. */
  const uint16_t tile_origin = (uint16_t)(ppu_scroll_y & 0x03f8u);
  return Dkc2VideoUnwrapPpuScroll(tile_origin, camera_y) +
         (uint32_t)(ppu_scroll_y & 7u);
}

uint32_t Dkc2VideoTerrainShadowX(uint16_t ppu_scroll_x, uint32_t camera_x) {
  return Dkc2VideoUnwrapPpuScroll(ppu_scroll_x, camera_x);
}

uint32_t Dkc2VideoLevelSourceTileY(uint16_t ppu_scroll_y,
                                   uint32_t camera_y,
                                   uint32_t viewport_tile_row) {
  /* Unwrap the top rendered tile once, then walk the viewport in a single
   * continuous world domain. Unwrapping each row independently can choose
   * opposite 1024-pixel epochs around the +/-512 midpoint: Mainbrace at
   * camera Y=$069c / PPU Y=$009b mapped row 0 to world tile 275 but row 1
   * backward to 148. The same discontinuity cut Parrot Chute Panic's BG2
   * margins during rapid vertical motion. */
  const uint32_t top = Dkc2VideoUnwrapPpuScroll(
      (uint16_t)(ppu_scroll_y & 0x03f8u), camera_y);
  return (top >> 3) + viewport_tile_row;
}

uint32_t Dkc2VideoLevelMapTileY(uint16_t ppu_scroll_y,
                                uint32_t camera_y,
                                uint32_t viewport_tile_row) {
  const int64_t source_anchor = (int64_t)camera_y - 0x0100;
  int64_t source_y = (int64_t)(ppu_scroll_y & 0x00f8u);
  while (source_y + 0x80 < source_anchor)
    source_y += 0x100;
  while (source_y > source_anchor + 0x80)
    source_y -= 0x100;
  source_y += (int64_t)viewport_tile_row * 8;
  /* The horizontal map address is periodic in its low 16-bit world Y. Keep
   * a negative page representable without passing an out-of-range uint32_t
   * to Dkc2VideoDecodeLevelTile. */
  return (uint32_t)(source_y / 8) & 0x1fffu;
}

static uint16_t Dkc2VideoReadBankWord(const uint8_t *bank_data,
                                      uint16_t address) {
  uint16_t next = (uint16_t)(address + 1u);
  return (uint16_t)bank_data[address] |
         ((uint16_t)bank_data[next] << 8);
}

bool Dkc2VideoDecodeLevelTile(const uint8_t *bank_data,
                              size_t bank_size,
                              uint16_t level_map_base,
                              uint16_t metatile_base,
                              Dkc2VideoLevelLayout layout,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry) {
  if (!bank_data || bank_size < 0x10000u || !tile_entry)
    return false;
  if (world_tile_x > 0x1fffu || world_tile_y > 0x1fffu)
    return false;

  const uint16_t world_x = (uint16_t)(world_tile_x << 3);
  const uint16_t world_y = (uint16_t)(world_tile_y << 3);
  uint16_t map_offset = 0;
  if (layout == kDkc2VideoLevelLayoutHorizontal) {
    map_offset = (uint16_t)((world_x & 0xffe0u) +
                            ((world_y & 0x01e0u) >> 4));
  } else if (layout == kDkc2VideoLevelLayoutVertical) {
    map_offset = (uint16_t)(((world_x & 0xffe0u) >> 4) +
                            ((world_y & 0xffe0u) << 1));
  } else if (layout == kDkc2VideoLevelLayoutSquare) {
    /* Bramble's square scroller stores 48 metatiles per 0x60-byte row. */
    map_offset = (uint16_t)(((world_x & 0xffe0u) >> 4) +
                            (world_y & 0xffe0u) * 6u);
  } else if (layout == kDkc2VideoLevelLayoutNarrowVertical) {
    /* Parrot Chute Panic stores 16 metatiles per $20-byte row. */
    map_offset = (uint16_t)(((world_x & 0xffe0u) >> 4) +
                            (world_y & 0xffe0u));
  } else if (layout == kDkc2VideoLevelLayoutShipHold) {
    /* Ship-hold maps are 80 metatiles / $a0 bytes per row. */
    map_offset = (uint16_t)(((world_x & 0xffe0u) >> 4) +
                            (world_y & 0xffe0u) * 5u);
  } else {
    return false;
  }
  const uint16_t metatile = Dkc2VideoReadBankWord(
      bank_data, (uint16_t)(level_map_base + map_offset));

  unsigned sub_x = (unsigned)world_tile_x & 3u;
  unsigned sub_y = (unsigned)world_tile_y & 3u;
  const uint16_t flips = (uint16_t)(metatile & 0xc000u);
  if (flips & 0x4000u)
    sub_x = 3u - sub_x;
  if (flips & 0x8000u)
    sub_y = 3u - sub_y;

  /*
   * Match the cartridge's five ASLs followed immediately by ADC. The final
   * ASL carry (original metatile bit 11) participates in the address.
   */
  const uint16_t scaled =
      (uint16_t)((uint16_t)(metatile << 5) +
                 ((metatile & 0x0800u) ? 1u : 0u));
  const uint16_t definition_offset =
      (uint16_t)(scaled + (uint16_t)(sub_y * 8u + sub_x * 2u));
  const uint16_t source = Dkc2VideoReadBankWord(
      bank_data, (uint16_t)(metatile_base + definition_offset));
  *tile_entry = (uint16_t)(source ^ flips);
  return true;
}

bool Dkc2VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry) {
  if (!vram || word_count < 0x8000u || !tile_entry)
    return false;

  for (uint16_t tile = 0; tile < 0x0400u; tile++) {
    const uint16_t address =
        (uint16_t)(character_base + (uint16_t)(tile * 16u));
    bool transparent = true;
    for (unsigned word = 0; word < 16u; word++) {
      if (vram[(address + word) & 0x7fffu] != 0) {
        transparent = false;
        break;
      }
    }
    if (transparent) {
      *tile_entry = tile;
      return true;
    }
  }
  return false;
}

bool Dkc2VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile) {
  return (tile_entry & 0x03ffu) == (transparent_tile & 0x03ffu);
}
