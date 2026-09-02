#include "dkc2_video.h"

#include <string.h>

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static Dkc2VideoAspect s_aspect;
static Dkc2VideoEdgePolicy s_edge_policy = kDkc2VideoEdgeGlide;

void Dkc2VideoSetEdgePolicy(Dkc2VideoEdgePolicy policy) {
  if (policy < kDkc2VideoEdgeReflect || policy >= kDkc2VideoEdgePolicyCount)
    policy = kDkc2VideoEdgeGlide;
  s_edge_policy = policy;
}

Dkc2VideoEdgePolicy Dkc2VideoGetEdgePolicy(void) {
  return s_edge_policy;
}

bool Dkc2VideoEdgePolicyFromName(const char *name,
                                 Dkc2VideoEdgePolicy *policy) {
  if (!name || !policy)
    return false;
  if (strcmp(name, "reflect") == 0 || strcmp(name, "mirror") == 0 ||
      strcmp(name, "0") == 0) {
    *policy = kDkc2VideoEdgeReflect;
    return true;
  }
  if (strcmp(name, "bars") == 0 || strcmp(name, "clamp") == 0 ||
      strcmp(name, "1") == 0) {
    *policy = kDkc2VideoEdgeBars;
    return true;
  }
  if (strcmp(name, "shift") == 0 || strcmp(name, "bias") == 0 ||
      strcmp(name, "2") == 0) {
    *policy = kDkc2VideoEdgeShift;
    return true;
  }
  if (strcmp(name, "glide") == 0 || strcmp(name, "3") == 0) {
    *policy = kDkc2VideoEdgeGlide;
    return true;
  }
  return false;
}

const char *Dkc2VideoEdgePolicyName(Dkc2VideoEdgePolicy policy) {
  switch (policy) {
    case kDkc2VideoEdgeBars:
      return "bars";
    case kDkc2VideoEdgeShift:
      return "shift";
    case kDkc2VideoEdgeGlide:
      return "glide";
    case kDkc2VideoEdgeReflect:
    default:
      return "reflect";
  }
}

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

static int Dkc2VideoClampInt(int value, int lower, int upper) {
  if (value < lower)
    return lower;
  if (value > upper)
    return upper;
  return value;
}

void Dkc2VideoPresentationMargins(uint16_t camera_x,
                                  uint16_t maximum_scroll_x,
                                  int *bias,
                                  int *left_margin,
                                  int *right_margin) {
  const int extra = Dkc2VideoExtra();
  int result_bias = 0;
  int result_left = extra;
  int result_right = extra;
  const int lower = 0x0100;
  const int upper = maximum_scroll_x;
  if (extra > 0 && upper >= lower) {
    if (s_edge_policy == kDkc2VideoEdgeShift ||
        s_edge_policy == kDkc2VideoEdgeGlide) {
      const int range = upper - lower;
      int reach = range / 2;
      if (reach > extra)
        reach = extra;
      /* The pins: the presented center never leaves [lower+reach,
       * upper-reach], so the wide frame stays inside the level. */
      const int pin_low = lower + reach - (int)camera_x;
      const int pin_high = upper - reach - (int)camera_x;
      int wanted = 0;
      if (s_edge_policy == kDkc2VideoEdgeGlide) {
        /* One margin of inward shift at each wall, released one pixel per
         * kDkc2VideoEdgeGlideSpan pixels of camera travel away from it. */
        const int span = extra * kDkc2VideoEdgeGlideSpan;
        const int west_travel = (int)camera_x - lower;
        const int east_travel = upper - (int)camera_x;
        if (west_travel >= 0 && west_travel < span)
          wanted += extra - west_travel / kDkc2VideoEdgeGlideSpan;
        if (east_travel >= 0 && east_travel < span)
          wanted -= extra - east_travel / kDkc2VideoEdgeGlideSpan;
      }
      result_bias = Dkc2VideoClampInt(
          Dkc2VideoClampInt(wanted, pin_low, pin_high), -extra, extra);
      const int presented = (int)camera_x + result_bias;
      result_left = Dkc2VideoClampInt(presented - lower, 0, extra);
      result_right = Dkc2VideoClampInt(upper - presented, 0, extra);
    } else if (s_edge_policy == kDkc2VideoEdgeBars) {
      result_left = Dkc2VideoClampInt((int)camera_x - lower, 0, extra);
      result_right = Dkc2VideoClampInt(upper - (int)camera_x, 0, extra);
    }
  }
  if (bias)
    *bias = result_bias;
  if (left_margin)
    *left_margin = result_left;
  if (right_margin)
    *right_margin = result_right;
}

bool Dkc2VideoMarginLeavesAuthoredExtent(uint16_t camera_x,
                                         uint16_t maximum_scroll_x) {
  const int extra = Dkc2VideoExtra();
  const int lower = 0x0100;
  const int upper = maximum_scroll_x;
  if (extra <= 0 || upper < lower || s_edge_policy != kDkc2VideoEdgeReflect)
    return false;
  return (int)camera_x - lower < extra || upper - (int)camera_x < extra;
}

int Dkc2VideoResolveEdgeTile(uint32_t world_tile_x,
                             uint16_t maximum_scroll_x,
                             uint32_t *source_tile_x,
                             bool *mirror_horizontally) {
  const uint32_t origin_tile = 0x0100u >> 3;
  /* First world tile column at or beyond the authored extent. */
  const uint32_t extent_tile =
      ((uint32_t)maximum_scroll_x + kDkc2VideoNativeWidth) >> 3;
  if (mirror_horizontally)
    *mirror_horizontally = false;
  if (!source_tile_x)
    return -1;
  if (world_tile_x >= origin_tile && world_tile_x < extent_tile) {
    *source_tile_x = world_tile_x - origin_tile;
    return 0;
  }
  if (s_edge_policy != kDkc2VideoEdgeReflect || extent_tile <= origin_tile)
    return -1;
  uint32_t mirrored;
  if (world_tile_x < origin_tile) {
    /* Reflect about the boundary between world tiles 31 and 32:
     * 31 -> 32, 30 -> 33, and so on. */
    mirrored = 2u * origin_tile - 1u - world_tile_x;
  } else {
    /* Reflect about the boundary just before the first tile beyond the
     * extent: extent -> extent-1, extent+1 -> extent-2, and so on. */
    if (world_tile_x >= 2u * extent_tile)
      return -1;
    mirrored = 2u * extent_tile - 1u - world_tile_x;
  }
  if (mirrored < origin_tile || mirrored >= extent_tile)
    return -1;
  *source_tile_x = mirrored - origin_tile;
  if (mirror_horizontally)
    *mirror_horizontally = true;
  return 1;
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

uint16_t Dkc2VideoPromoteOamXHigh(uint16_t screen_x) {
  /* DKC2's banana renderer derives OAM's ninth X bit from bit 15 because
   * native play only needs that bit for negative coordinates. In the
   * widened right margin, $0100-$012a must therefore mirror bit 8 into the
   * sign position before the original packing sequence performs XBA/ASL. */
  if (Dkc2VideoTerrainReady() && (screen_x & 0x0100u))
    return (uint16_t)(screen_x | 0x8000u);
  return screen_x;
}

bool Dkc2VideoTilemapPagesCollide(const uint8_t bg_xsc[4],
                                  unsigned layer,
                                  uint8_t enabled_layers) {
  if (!bg_xsc || layer >= 4 || !(bg_xsc[layer] & 1u))
    return false;
  /* Tilemap pages are $400-word aligned, so two pages overlap exactly when
   * their addresses are equal. The 64-column extension pages are the odd
   * pages of the allocation (1, and 3 for a 64x64 map). If one of them is
   * another enabled background's base page, that background owns it and
   * this layer's "adjacent columns" are not its own. */
  const uint16_t base = (uint16_t)((bg_xsc[layer] & 0xfcu) << 8);
  const unsigned extension_pages = (bg_xsc[layer] & 2u) ? 2u : 1u;
  for (unsigned extension = 0; extension < extension_pages; extension++) {
    const uint16_t page = (uint16_t)(
        (base + (2u * extension + 1u) * 0x400u) & 0x7fffu);
    for (unsigned other = 0; other < 4; other++) {
      if (other == layer || !(enabled_layers & (1u << other)))
        continue;
      const uint16_t other_base = (uint16_t)((bg_xsc[other] & 0xfcu) << 8);
      if (page == other_base)
        return true;
    }
  }
  return false;
}

uint8_t Dkc2VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers) {
  /* DKC2's audited rolling level tilemaps use Mode 1. Mode 7 and other
   * special screens need explicit reconstruction rather than stale BGxSC
   * state accidentally opting them into the generic path. */
  if (!bg_xsc || (bg_mode & 7u) != 1u) return 0;

  uint8_t enabled = (uint8_t)((main_layers | sub_layers) & 0x0f);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 2; layer++) {
    uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u) &&
        !Dkc2VideoTilemapPagesCollide(bg_xsc, layer, enabled))
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
    if ((enabled & bit) && (bg_xsc[layer] & 1u) &&
        !Dkc2VideoTilemapPagesCollide(bg_xsc, layer, enabled))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

uint8_t Dkc2VideoRepeatLayerMask(uint8_t bg_mode,
                                 uint8_t main_layers,
                                 uint8_t sub_layers,
                                 uint8_t wide_layer_mask) {
  if ((bg_mode & 7u) != 1u)
    return 0;
  const uint8_t enabled =
      (uint8_t)((main_layers | sub_layers) & 0x07u);
  return (uint8_t)(enabled & (uint8_t)~wide_layer_mask);
}

uint16_t Dkc2VideoScrollPhaseDistance(uint16_t a, uint16_t b) {
  uint16_t distance = (uint16_t)((a - b) & 0x03ffu);
  if (distance > 0x0200u)
    distance = (uint16_t)(0x0400u - distance);
  return distance;
}

bool Dkc2VideoScrollAtTerrainPhase(uint16_t h_scroll,
                                   uint16_t v_scroll,
                                   uint16_t terrain_h_scroll,
                                   uint16_t terrain_v_scroll) {
  return Dkc2VideoScrollPhaseDistance(h_scroll, terrain_h_scroll) <=
             kDkc2VideoTerrainPhaseLeadX &&
         Dkc2VideoScrollPhaseDistance(v_scroll, terrain_v_scroll) <=
             kDkc2VideoTerrainPhaseLeadY;
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

bool Dkc2VideoCharacterIsTransparent(const uint16_t *vram,
                                     size_t word_count,
                                     uint16_t character_base,
                                     uint16_t tile_entry) {
  if (!vram || word_count < 0x8000u)
    return false;
  const uint16_t address =
      (uint16_t)(character_base + (uint16_t)((tile_entry & 0x03ffu) * 16u));
  for (unsigned word = 0; word < 16u; word++) {
    if (vram[(address + word) & 0x7fffu] != 0)
      return false;
  }
  return true;
}

bool Dkc2VideoFindTransparent2bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry) {
  if (!vram || word_count < 0x8000u || !tile_entry)
    return false;

  for (uint16_t tile = 0; tile < 0x0400u; tile++) {
    const uint16_t address =
        (uint16_t)(character_base + (uint16_t)(tile * 8u));
    bool transparent = true;
    for (unsigned word = 0; word < 8u; word++) {
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

bool Dkc2VideoDecodeRiggingTile(const uint8_t *bank_data,
                                size_t bank_size,
                                uint32_t map_x,
                                uint32_t map_y,
                                uint16_t *tile_entry) {
  if (!bank_data || bank_size < 0x10000u || !tile_entry)
    return false;
  const uint32_t x = map_x % (uint32_t)kDkc2VideoRiggingMapWidth;
  const uint32_t y = map_y & (uint32_t)(kDkc2VideoRiggingMapHeight - 1);
  /* $B5:A96A-$B5:A97F: entry = map + (x & $0FE0) + ((y & $01E0) >> 4). */
  const uint16_t map_address =
      (uint16_t)(kDkc2VideoRiggingMapOffset + (x & 0x0fe0u) +
                 ((y & 0x01e0u) >> 4));
  const uint16_t entry = Dkc2VideoReadBankWord(bank_data, map_address);
  unsigned column = (x >> 3) & 3u;
  unsigned row = (y >> 3) & 3u;
  if (entry & 0x4000u)
    column = 3u - column;
  if (entry & 0x8000u)
    row = 3u - row;
  /*
   * $B5:A9B1-$B5:AA5E: definition = table + (entry << 5) in 16-bit
   * arithmetic (the flag bits leave the accumulator), the tile at
   * row * 8 + column * 2, and each flag toggles the tile's matching flip
   * bit with EOR, so a definition authored with a flipped tile unflips it.
   */
  const uint16_t definition =
      (uint16_t)(kDkc2VideoRiggingMetatileOffset + (uint16_t)(entry << 5) +
                 row * 8u + column * 2u);
  *tile_entry = (uint16_t)(Dkc2VideoReadBankWord(bank_data, definition) ^
                           (entry & 0xc000u));
  return true;
}

bool Dkc2VideoRiggingCellMatches(uint16_t decoded, uint16_t ring,
                                 uint16_t previous_decoded,
                                 bool first_in_page) {
  if (decoded == ring)
    return true;
  if ((decoded & 0x00ffu) != (ring & 0x00ffu))
    return false;
  if (first_in_page)
    return true;
  return (ring & 0xff00u) == (previous_decoded & 0xff00u);
}

uint32_t Dkc2VideoRiggingShadowY(uint16_t ppu_scroll_y, uint32_t camera_y) {
  const int64_t anchor = (int64_t)camera_y - 0x101;
  int64_t candidate = (anchor & ~(int64_t)0xff) | (int64_t)(ppu_scroll_y & 0xffu);
  if (candidate < anchor - 0x80)
    candidate += 0x100;
  else if (candidate > anchor + 0x80)
    candidate -= 0x100;
  if (candidate < 0)
    candidate = 0;
  return (uint32_t)candidate;
}

static bool Dkc2VideoWallRelation(Dkc2VideoMetatileClassifier classify,
                                  void *context, uint32_t target_x,
                                  uint32_t source_x, uint32_t metatile_y) {
  return classify(context, target_x, metatile_y) == kDkc2VideoMetatileEmpty &&
         classify(context, source_x, metatile_y) == kDkc2VideoMetatileFull;
}

bool Dkc2VideoFindStructuralWallSource(Dkc2VideoMetatileClassifier classify,
                                       void *context,
                                       bool east_side,
                                       uint32_t target_metatile_x,
                                       uint32_t edge_metatile_x,
                                       uint32_t metatile_y,
                                       uint32_t *source_metatile_x) {
  if (!classify || !source_metatile_x)
    return false;
  if (east_side ? target_metatile_x <= edge_metatile_x
                : target_metatile_x >= edge_metatile_x)
    return false;
  if (classify(context, target_metatile_x, metatile_y) !=
      kDkc2VideoMetatileEmpty)
    return false;
  /* Walk from the target toward the native edge; the first cell that is not
   * empty decides. A partial cell is an authored opening. */
  uint32_t candidate = target_metatile_x;
  bool found = false;
  for (;;) {
    if (east_side) {
      if (candidate == 0 || candidate - 1 < edge_metatile_x)
        break;
      candidate--;
    } else {
      if (candidate + 1 > edge_metatile_x)
        break;
      candidate++;
    }
    const Dkc2VideoMetatileFill fill =
        classify(context, candidate, metatile_y);
    if (fill == kDkc2VideoMetatileEmpty)
      continue;
    found = fill == kDkc2VideoMetatileFull;
    break;
  }
  if (!found)
    return false;
  /* A wall is at least two metatiles thick: the cell behind the source,
   * toward the native center, must be full as well. A one-cell mast, crate,
   * or post standing in open sky is not a wall to continue. */
  {
    const uint32_t behind = east_side ? candidate - 1u : candidate + 1u;
    if ((east_side && candidate == 0) ||
        classify(context, behind, metatile_y) != kDkc2VideoMetatileFull)
      return false;
  }
  /* The same empty-target/full-source relationship on an adjacent row
   * distinguishes a continuing wall from an isolated block or decoration. */
  const bool above =
      metatile_y > 0 &&
      Dkc2VideoWallRelation(classify, context, target_metatile_x, candidate,
                            metatile_y - 1u);
  const bool below =
      Dkc2VideoWallRelation(classify, context, target_metatile_x, candidate,
                            metatile_y + 1u);
  if (!above && !below)
    return false;
  *source_metatile_x = candidate;
  return true;
}

bool Dkc2VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile) {
  return (tile_entry & 0x03ffu) == (transparent_tile & 0x03ffu);
}
