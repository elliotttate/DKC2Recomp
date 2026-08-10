#ifndef DKC2_VIDEO_H
#define DKC2_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc2VideoNativeWidth = 256,
  kDkc2VideoHeight = 224,
  /*
   * SNES pixels are presented with a 7:6 pixel aspect ratio. At 224 lines,
   * 342 source columns produce a 1.78125 display aspect, within one source
   * pixel of exact 16:9. The odd ideal width (341 1/3) cannot be centered
   * symmetrically, so use 43 host-rendered columns on both sides.
   */
  kDkc2VideoWidescreenExtra = 43,
  kDkc2VideoWidescreenWidth =
      kDkc2VideoNativeWidth + 2 * kDkc2VideoWidescreenExtra,
  kDkc2VideoBytesPerPixel = 4,
};

typedef enum Dkc2VideoLevelLayout {
  kDkc2VideoLevelLayoutUnknown = 0,
  kDkc2VideoLevelLayoutHorizontal,
  kDkc2VideoLevelLayoutVertical,
  kDkc2VideoLevelLayoutSquare,
} Dkc2VideoLevelLayout;

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc2VideoSetWidescreen(bool enabled);
bool Dkc2VideoIsWidescreen(void);
void Dkc2VideoSetTerrainReady(bool ready);
bool Dkc2VideoTerrainReady(void);
int Dkc2VideoWidth(void);
int Dkc2VideoExtra(void);
size_t Dkc2VideoPixelCount(void);

/*
 * DKC2 stores left margins and total horizontal spans separately. Keeping
 * these calculations here makes the generated-code adaptations switch back
 * to the exact cartridge values whenever widescreen is disabled.
 */
uint16_t Dkc2VideoExpandCullLeft(uint16_t native_margin);
uint16_t Dkc2VideoExpandCullSpan(uint16_t native_span);
uint16_t Dkc2VideoPromoteOamXHigh(uint16_t screen_x);

/*
 * Return the subset of currently enabled BG1/BG2 layers whose tilemaps have
 * 64 columns. BG3 is deliberately excluded because DKC2 also uses it for HUD
 * and staging data that is not safe to expose outside the native viewport.
 */
uint8_t Dkc2VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/* True only for the independently streamed 64-column ship-rigging BG3. */
bool Dkc2VideoCanWidenShipRigging(uint16_t level_effects,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/*
 * Select layers whose authentic 256-pixel scanline may be repeated into the
 * margins. This is deliberately screen-specific for BG3, which is otherwise
 * clamped because DKC2 also uses it for bounded HUD/staging content.
 */
uint8_t Dkc2VideoRepeatLayerMask(uint8_t bg_mode,
                                const uint8_t bg_xsc[4],
                                uint8_t main_layers,
                                uint8_t sub_layers,
                                uint8_t wide_layer_mask,
                                uint16_t level_number);

bool Dkc2VideoPpuCanExtend(uint8_t bg_mode,
                           const uint8_t bg_xsc[4],
                           uint8_t main_layers,
                           uint8_t sub_layers);

/*
 * Identify which enabled wide layer owns DKC2's decompressed level stream.
 * $17B6 is a VRAM word address; BGxSC encodes the matching tilemap base in
 * 0x400-word units. Returns BG1/BG2 as 0/1, or -1 when the live destination
 * is not one of the audited wide layers.
 */
int Dkc2VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address);

/* Classify DKC2's live level main-loop index into a proven map layout. */
Dkc2VideoLevelLayout Dkc2VideoLevelLayoutForGameSubMode(
    uint16_t game_sub_mode);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc2VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/*
 * Select the world-Y domain used by DKC2's rolling terrain shadow. The
 * cartridge stages terrain one 256-pixel page above its camera coordinates,
 * so live tilemap captures and exact prefills must share the rendered PPU
 * phase rather than the raw WRAM camera Y.
 */
uint32_t Dkc2VideoTerrainShadowY(uint16_t ppu_scroll_y, uint32_t camera_y);

/*
 * Resolve a rendered BG1 tile row to DKC2's decompressed level-map row.
 * The PPU scroll is the rendered source phase and may trail the WRAM camera
 * by one pixel at an NMI boundary; using it consistently prevents a transient
 * 31-row wrap from seeding persistent margin history.
 */
uint32_t Dkc2VideoLevelSourceTileY(uint16_t ppu_scroll_y,
                                   uint32_t camera_y,
                                   uint32_t viewport_tile_row);

/*
 * Horizontal stages build their rolling column from the 256-pixel source
 * page immediately above the camera. BG scroll keeps only that page's low
 * eight-bit phase at a page crossing, so select the matching source page
 * around cameraY-$0100 before advancing through every viewport row. This
 * includes row zero: at scroll phase $00ff/camera $0100 its source is the
 * preceding row ($ffff), not row $00ff of the next physical page.
 */
uint32_t Dkc2VideoHorizontalMapTileY(uint16_t ppu_scroll_y,
                                     uint32_t camera_y,
                                     uint32_t viewport_tile_row);

/*
 * Decode one exact 8x8 BG tile from DKC2's decompressed level representation.
 * Horizontal, vertical, and square handlers store 32x32 metatiles in
 * different map orders; layout selects the proven address formula. A second
 * table contains each metatile's sixteen 8x8 tilemap entries. Both buffers
 * share one 64 KiB CPU bank during gameplay. No ROM-derived bytes are retained
 * here.
 */
bool Dkc2VideoDecodeLevelTile(const uint8_t *bank_data,
                              size_t bank_size,
                              uint16_t level_map_base,
                              uint16_t metatile_base,
                              Dkc2VideoLevelLayout layout,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry);

/*
 * Locate a fully transparent 4bpp character in live SNES VRAM. The returned
 * tilemap entry has palette, priority, and flip bits clear.
 */
bool Dkc2VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

/* A tilemap entry carries palette, priority, and flip bits in addition to its
 * 10-bit character index. This recognizes a transparent character without
 * discarding those presentation bits. */
bool Dkc2VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile);

#endif
