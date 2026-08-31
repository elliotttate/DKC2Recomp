#ifndef DKC2_VIDEO_H
#define DKC2_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc2VideoNativeWidth = 256,
  kDkc2VideoHeight = 224,
  /*
   * SNES pixels are presented with a 7:6 pixel aspect ratio. A centered
   * 308x224 source is within one source pixel of 16:10 on that display grid.
   */
  kDkc2Video16x10Extra = 26,
  /*
   * SNES pixels are presented with a 7:6 pixel aspect ratio. At 224 lines,
   * 342 source columns produce a 1.78125 display aspect, within one source
   * pixel of exact 16:9. The odd ideal width (341 1/3) cannot be centered
   * symmetrically, so use 43 host-rendered columns on both sides.
   */
  kDkc2VideoWidescreenExtra = 43,
  /* Ship-hold BG2's cabin-wall tilemap repeats every 12 eight-pixel tiles. */
  kDkc2VideoShipHoldBackdropPeriod = 96,
  /* Its hardware-window endpoint artifact occupies at most seven pixels. */
  kDkc2VideoShipHoldBackdropEdgeRepair = 7,
  kDkc2VideoWidescreenWidth =
      kDkc2VideoNativeWidth + 2 * kDkc2VideoWidescreenExtra,
  kDkc2VideoBytesPerPixel = 4,
};

typedef enum Dkc2VideoAspect {
  kDkc2VideoAspectNative = 0,
  kDkc2VideoAspect16x10,
  kDkc2VideoAspect16x9,
  kDkc2VideoAspectCount,
} Dkc2VideoAspect;

typedef enum Dkc2VideoLevelLayout {
  kDkc2VideoLevelLayoutUnknown = 0,
  kDkc2VideoLevelLayoutHorizontal,
  kDkc2VideoLevelLayoutVertical,
  kDkc2VideoLevelLayoutSquare,
  kDkc2VideoLevelLayoutNarrowVertical,
  /* Ship-hold terrain is row-major with 80 metatiles (160 bytes) per row. */
  kDkc2VideoLevelLayoutShipHold,
} Dkc2VideoLevelLayout;

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc2VideoSetWidescreen(bool enabled);
bool Dkc2VideoIsWidescreen(void);
void Dkc2VideoSetAspect(Dkc2VideoAspect aspect);
Dkc2VideoAspect Dkc2VideoGetAspect(void);
bool Dkc2VideoAspectFromName(const char *name, Dkc2VideoAspect *aspect);
const char *Dkc2VideoAspectName(Dkc2VideoAspect aspect);
void Dkc2VideoSetTerrainReady(bool ready);
bool Dkc2VideoTerrainReady(void);
int Dkc2VideoWidth(void);
int Dkc2VideoExtra(void);
size_t Dkc2VideoPixelCount(void);

/* True when any part of an 8x8 world tile lies outside the authentic
 * 256-pixel viewport and can therefore be sampled by a widened margin. */
bool Dkc2VideoTileTouchesWidescreenMargin(uint32_t world_tile_x,
                                          uint32_t camera_x);

/*
 * DKC2's decoded level terrain begins at world X=$0100 for every known map
 * layout. A centered wide host viewport can therefore ask for a few tiles
 * west of the first authored column at a hard-left camera boundary. When the
 * active terrain layer and layout have already passed the decoder capability
 * gates, reflect the nearest authored terrain tiles into that presentation-
 * only gutter. No cartridge camera, collision, object, or native-center pixel
 * is changed. Unknown layouts still fail closed. Returns the decompressed
 * source tile and whether its horizontal flip bit must be inverted.
 */
bool Dkc2VideoResolveWestBoundaryTile(Dkc2VideoLevelLayout layout,
                                      uint32_t world_tile_x,
                                      uint32_t camera_x,
                                      uint32_t *source_tile_x,
                                      bool *mirror_horizontally);

/*
 * DKC2 stores left margins and total horizontal spans separately. Keeping
 * these calculations here makes the generated-code adaptations switch back
 * to the exact cartridge values whenever widescreen is disabled.
 */
uint16_t Dkc2VideoExpandCullLeft(uint16_t native_margin);
uint16_t Dkc2VideoExpandCullSpan(uint16_t native_span);
uint16_t Dkc2VideoPromoteOamXHigh(uint16_t screen_x);

/*
 * Return the subset of currently enabled BG1/BG2 terrain candidates whose
 * tilemaps have 64 columns. BG3 is handled by the separate physical-width
 * capability after the exact terrain source has passed its readiness gate.
 */
uint8_t Dkc2VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/*
 * Return every enabled Mode-1 background that owns a physical 64-column
 * tilemap. This is a presentation capability, not terrain ownership: BG1/BG2
 * still need the live stream destination and exact source prefill before a
 * scene may widen, while an enabled 64-column BG3 may join the final render
 * mask only after that terrain-ready gate succeeds.
 */
uint8_t Dkc2VideoPhysicalWideLayerMask(uint8_t bg_mode,
                                       const uint8_t bg_xsc[4],
                                       uint8_t main_layers,
                                       uint8_t sub_layers);

/* True only for Ship Hold's bounded BG2 cabin-wall backdrop behind BG1
 * terrain. The room alternates the same wall between the $7000 and $7800
 * tilemap pages; the host may repeat its captured native tile span in
 * margins. */
bool Dkc2VideoCanRepeatShipHoldBackdrop(uint16_t game_sub_mode,
                                        const uint8_t bg_xsc[4],
                                        uint8_t main_layers,
                                        uint8_t sub_layers,
                                        uint8_t wide_layer_mask,
                                        int terrain_layer);

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
                                uint16_t level_number,
                                uint16_t game_sub_mode);

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

/* Classify a live game-loop/level pair into a proven map layout. */
Dkc2VideoLevelLayout Dkc2VideoLevelLayoutForScene(
    uint16_t game_sub_mode, uint16_t level_number);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc2VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/* Resolve terrain X from the scroll value latched by the PPU. The WRAM
 * camera can lead that value by a few pixels at an NMI boundary. */
uint32_t Dkc2VideoTerrainShadowX(uint16_t ppu_scroll_x, uint32_t camera_x);

/*
 * Select the world-Y domain used by DKC2's rolling terrain shadow. The
 * cartridge stages terrain one 256-pixel page above its camera coordinates,
 * so live tilemap captures and exact prefills must share the rendered PPU
 * phase rather than the raw WRAM camera Y.
 */
uint32_t Dkc2VideoTerrainShadowY(uint16_t ppu_scroll_y, uint32_t camera_y);

/*
 * Resolve a rendered terrain tile row to DKC2's decompressed level-map row.
 * The PPU scroll is the rendered source phase and may trail the WRAM camera
 * by one pixel at an NMI boundary. Unwrap the top tile once and advance later
 * rows continuously; independently unwrapping every row can select opposite
 * 1024-pixel epochs near the half-period boundary.
 */
uint32_t Dkc2VideoLevelSourceTileY(uint16_t ppu_scroll_y,
                                   uint32_t camera_y,
                                   uint32_t viewport_tile_row);

/*
 * DKC2's rolling column builders start from the 256-pixel source page above
 * the camera in horizontal, vertical, and narrow-vertical layouts. BG scroll
 * identifies that page by its low eight-bit phase, so select the matching
 * phase nearest cameraY-$0100 before advancing through the viewport. This
 * includes row zero: at scroll phase $00ff/camera $0100 its source is the
 * preceding row ($ffff), not row $00ff of the next physical page.
 */
uint32_t Dkc2VideoLevelMapTileY(uint16_t ppu_scroll_y,
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
