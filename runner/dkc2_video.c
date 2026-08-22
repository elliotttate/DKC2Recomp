#include "dkc2_video.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;

void Dkc2VideoSetWidescreen(bool enabled) {
  if (g_ws_active != enabled)
    s_terrain_ready = false;
  g_ws_active = enabled;
  g_ws_extra = enabled ? kDkc2VideoWidescreenExtra : 0;
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

uint16_t Dkc2VideoExpandCullLeft(uint16_t native_margin) {
  return (uint16_t)(native_margin +
                    (Dkc2VideoTerrainReady() ? g_ws_extra : 0));
}

uint16_t Dkc2VideoExpandCullSpan(uint16_t native_span) {
  return (uint16_t)(native_span +
                    (Dkc2VideoTerrainReady() ? 2 * g_ws_extra : 0));
}

static bool g_placement_radius_activation;

void Dkc2VideoSetPlacementRadiusActivation(bool activation) {
  g_placement_radius_activation = activation;
}

uint16_t Dkc2VideoExpandPlacementLeft(uint16_t native_margin,
                                      uint16_t radius_table_index) {
  (void)radius_table_index;
  return g_placement_radius_activation
               ? Dkc2VideoExpandCullLeft(native_margin)
               : native_margin;
}

uint16_t Dkc2VideoExpandPlacementSpan(uint16_t native_span,
                                      uint16_t radius_table_index) {
  (void)radius_table_index;
  return g_placement_radius_activation
               ? Dkc2VideoExpandCullSpan(native_span)
               : native_span;
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

bool Dkc2VideoCanWidenShipRigging(uint16_t level_effects,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers) {
  if (!bg_xsc || (level_effects & 0x0001u) == 0u)
    return false;
  const uint8_t enabled = (uint8_t)(main_layers | sub_layers);
  return (enabled & 0x04u) != 0u && bg_xsc[2] == 0x79u;
}

uint8_t Dkc2VideoRepeatLayerMask(uint8_t bg_mode,
                                const uint8_t bg_xsc[4],
                                uint8_t main_layers,
                                uint8_t sub_layers,
                                uint8_t wide_layer_mask,
                                uint16_t level_number) {
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

  /* Web Woods ($0017) and Gusty Glade ($0018) use streamed BG2 terrain plus
   * bounded BG1 $5800 atmosphere (fog or windblown leaves) over a bounded
   * BG3 $5c00 forest backdrop. Repeat both completed native scanlines so the
   * composed effect covers the 16:9 margins without sampling tilemap columns
   * the game never populated. */
  if ((level_number == 0x0017u || level_number == 0x0018u) &&
      (wide_layer_mask & 0x02u) &&
      (enabled & 0x05u) == 0x05u &&
      (bg_xsc[0] & 0xfcu) == 0x58u &&
      (bg_xsc[2] & 0xfcu) == 0x5cu)
    repeat = (uint8_t)(repeat | 0x05u);

  /* Mainbrace Mayhem's attract route uses BG3 $6c00 as the cyclic cloud and
   * lighting overlay above its independently widened BG1 terrain. */
  if (level_number == 0x000cu &&
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
