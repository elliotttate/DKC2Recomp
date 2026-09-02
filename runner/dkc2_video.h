#ifndef DKC2_VIDEO_H
#define DKC2_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
  kDkc2VideoWidescreenWidth =
      kDkc2VideoNativeWidth + 2 * kDkc2VideoWidescreenExtra,
  kDkc2VideoBytesPerPixel = 4,
  /*
   * DKC2 can advance a scanline band's alternate terrain phase a few pixels
   * beyond the frame anchor while the camera reverses. Measured maxima are
   * six pixels horizontally and four vertically; larger differences are not
   * the same world plane.
   */
  kDkc2VideoTerrainPhaseLeadX = 6,
  kDkc2VideoTerrainPhaseLeadY = 4,
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

/*
 * What a host margin shows where the level authors nothing: within one
 * margin of a hard level wall, and in rooms narrower than two margins.
 *
 * reflect: the presented view stays locked to the cartridge camera and the
 *          unauthored strip mirrors the nearest authored terrain columns.
 * bars:    the presented view stays locked to the cartridge camera and the
 *          unauthored strip is left black (the visible margin shrinks).
 * shift:   the presented view is moved inward so the margin never leaves the
 *          authored extent; the view therefore stands still for the first
 *          margin's worth of camera motion away from a wall, and every
 *          sprite, HUD included, slides by the same amount.
 */
typedef enum Dkc2VideoEdgePolicy {
  kDkc2VideoEdgeReflect = 0,
  kDkc2VideoEdgeBars,
  kDkc2VideoEdgeShift,
  /* Like shift the wide frame never leaves the authored level, but the
   * inward slide is released one pixel per kDkc2VideoEdgeGlideSpan pixels
   * of camera travel, so the background scrolls at seven eighths of the
   * camera speed until the view is centered instead of standing still.
   * The default. */
  kDkc2VideoEdgeGlide,
  kDkc2VideoEdgePolicyCount,
} Dkc2VideoEdgePolicy;

enum {
  kDkc2VideoEdgeGlideSpan = 8,
};

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

void Dkc2VideoSetEdgePolicy(Dkc2VideoEdgePolicy policy);
Dkc2VideoEdgePolicy Dkc2VideoGetEdgePolicy(void);
bool Dkc2VideoEdgePolicyFromName(const char *name,
                                 Dkc2VideoEdgePolicy *policy);
const char *Dkc2VideoEdgePolicyName(Dkc2VideoEdgePolicy policy);

/*
 * Host presentation geometry near DKC2's fixed level endpoints under the
 * active edge policy. Every known layout authors terrain from world
 * X=`$0100` through maximum_scroll_x+256. With `shift`, the presented
 * viewport is moved inward (bias) while the room can absorb the margin and
 * centered with clamped margins when it cannot. With `bars`, the bias is
 * zero and each visible margin is clamped to the authored extent. With
 * `reflect`, the bias is zero and both margins stay fully visible; the
 * terrain decoder mirrors authored columns into the unauthored strip. The
 * cartridge camera, collision, exits, streaming, and WRAM stay stock; a
 * nonzero bias shifts BG scroll and OBJ placement together. An unknown
 * bound (maximum below the origin) keeps the full symmetric margin.
 */
void Dkc2VideoPresentationMargins(uint16_t camera_x,
                                  uint16_t maximum_scroll_x,
                                  int *bias,
                                  int *left_margin,
                                  int *right_margin);

/*
 * Resolve a world tile column to a decompressed level-map source column.
 * Inside the authored extent the source is the world column minus the
 * `$0100` origin (returns 0). Outside it, the `reflect` policy mirrors the
 * nearest authored columns across the boundary and requests the horizontal
 * flip (returns 1); any other policy, or a mirror that would leave the
 * authored range, yields no source (returns -1, verified transparent).
 */
int Dkc2VideoResolveEdgeTile(uint32_t world_tile_x,
                             uint16_t maximum_scroll_x,
                             uint32_t *source_tile_x,
                             bool *mirror_horizontally);

/* True when a host margin would extend past the authored extent on either
 * side under the active edge policy without being clamped, which is where a
 * physical 64-column BG3 could expose unauthored ring columns. */
bool Dkc2VideoMarginLeavesAuthoredExtent(uint16_t camera_x,
                                         uint16_t maximum_scroll_x);

/* True when any part of an 8x8 world tile lies outside the authentic
 * 256-pixel viewport and can therefore be sampled by a widened margin. */
bool Dkc2VideoTileTouchesWidescreenMargin(uint32_t world_tile_x,
                                          uint32_t camera_x);

/*
 * DKC2 stores left margins and total horizontal spans separately. Keeping
 * these calculations here makes the generated-code adaptations switch back
 * to the exact cartridge values whenever widescreen is disabled.
 */
uint16_t Dkc2VideoExpandCullLeft(uint16_t native_margin);
uint16_t Dkc2VideoExpandCullSpan(uint16_t native_span);
uint16_t Dkc2VideoPromoteOamXHigh(uint16_t screen_x);

/*
 * True when a background's 64-column tilemap allocation is not physically
 * its own: its second 32-column page overlaps another enabled background's
 * tilemap. DKC2's Mudhole Marsh BG3 advertises 64 columns at $6C00 while
 * BG1's terrain map occupies $7000, so its "adjacent columns" are terrain
 * rows. Such a layer is bounded content and repeats its rendered line.
 */
bool Dkc2VideoTilemapPagesCollide(const uint8_t bg_xsc[4],
                                  unsigned layer,
                                  uint8_t enabled_layers);

/*
 * Return the subset of currently enabled BG1/BG2 terrain candidates whose
 * tilemaps have 64 physically distinct columns. BG3 is handled by the
 * separate physical-width capability after the exact terrain source has
 * passed its readiness gate.
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

/*
 * Select the layers whose authentic rendered scanline is repeated into the
 * margins: every enabled Mode-1 background that is not in the wide (64-column)
 * render mask. A 32-column tilemap wraps at 256 pixels on hardware, so a
 * period-256 repeat of the rendered line is exactly what a wider PPU would
 * draw from that map; HDMA phase, windows, and color math are already in the
 * rendered line. Rolling 64-column layers never repeat through this mask;
 * they are world-keyed or band-classified by the adapter.
 */
uint8_t Dkc2VideoRepeatLayerMask(uint8_t bg_mode,
                                 uint8_t main_layers,
                                 uint8_t sub_layers,
                                 uint8_t wide_layer_mask);

/* Shortest distance between two 10-bit SNES scroll phases. */
uint16_t Dkc2VideoScrollPhaseDistance(uint16_t a, uint16_t b);

/*
 * True when a layer's scroll for one scanline band is the terrain phase:
 * within the measured lead tolerance of the scroll that the live terrain
 * owner rendered at the frame anchor. A 64-column layer at the terrain phase
 * displays the streamed world map and is served from the world-keyed store;
 * any other phase is a bounded effect plane and repeats its rendered line.
 * This is structural: it has no level, mode, or screen-composition
 * signature, and either physical layer may take either role in any band.
 */
bool Dkc2VideoScrollAtTerrainPhase(uint16_t h_scroll,
                                   uint16_t v_scroll,
                                   uint16_t terrain_h_scroll,
                                   uint16_t terrain_v_scroll);

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

/* A 4bpp character whose sixteen VRAM words are all zero draws nothing. */
bool Dkc2VideoCharacterIsTransparent(const uint16_t *vram,
                                     size_t word_count,
                                     uint16_t character_base,
                                     uint16_t tile_entry);

/*
 * Locate a fully transparent 2bpp character (mode 1 BG3) in live SNES VRAM.
 * The returned tilemap entry has palette, priority, and flip bits clear.
 */
bool Dkc2VideoFindTransparent2bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

/*
 * Ship-deck rigging decode. The Gangplank Galleon deck levels draw their
 * foreground rigging on a 64-column BG3 that the cartridge streams one
 * 8-pixel column at a time ($B5:A950 builds a column, $B5:AAE6 a row) from a
 * 1280x512-pixel metatile map in ROM bank $F5. The map is column-major: 40
 * columns of sixteen 16-bit entries, each a 32-byte metatile definition
 * index (four rows of four tilemap words) whose bits 14-15 mirror the
 * definition horizontally or vertically and toggle the tile's own flip bits.
 * Map X wraps at 1280 pixels and map Y at 512, exactly as the cartridge's
 * own address arithmetic does. No ROM-derived bytes are retained here.
 */
enum {
  kDkc2VideoRiggingBank = 0xf5,
  kDkc2VideoRiggingMapOffset = 0x26a7,
  kDkc2VideoRiggingMetatileOffset = 0x2087,
  kDkc2VideoRiggingMapWidth = 0x500,
  kDkc2VideoRiggingMapHeight = 0x200
};
bool Dkc2VideoDecodeRiggingTile(const uint8_t *bank_data,
                                size_t bank_size,
                                uint32_t map_x,
                                uint32_t map_y,
                                uint16_t *tile_entry);

/*
 * Structural wall continuation for host margins. A level map can hold
 * wholly transparent 32x32 metatiles beside a shaft or wall that the
 * cartridge camera can never show, because the player, not a camera bound,
 * stops there. A margin that reaches such cells shows the backdrop through
 * a hole the console never has. When the empty target metatile has a fully
 * populated metatile as the first non-empty cell toward the native edge on
 * its row, that metatile is backed by another full one toward the native
 * center (a wall, not a one-cell mast or crate), and an adjacent row repeats
 * the empty/full relationship, the wall is continued from that source. Any partial metatile in between is an
 * authored opening and fails closed. The classifier answers for metatile
 * coordinates in the decoded level map's tile space (tile / 4).
 */
typedef enum Dkc2VideoMetatileFill {
  kDkc2VideoMetatileUnknown = 0,
  kDkc2VideoMetatileEmpty,
  kDkc2VideoMetatilePartial,
  kDkc2VideoMetatileFull,
} Dkc2VideoMetatileFill;

typedef Dkc2VideoMetatileFill (*Dkc2VideoMetatileClassifier)(
    void *context, uint32_t metatile_x, uint32_t metatile_y);

bool Dkc2VideoFindStructuralWallSource(Dkc2VideoMetatileClassifier classify,
                                       void *context,
                                       bool east_side,
                                       uint32_t target_metatile_x,
                                       uint32_t edge_metatile_x,
                                       uint32_t metatile_y,
                                       uint32_t *source_metatile_x);

/* A tilemap entry carries palette, priority, and flip bits in addition to its
 * 10-bit character index. This recognizes a transparent character without
 * discarding those presentation bits. */
bool Dkc2VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile);

#ifdef __cplusplus
}
#endif

#endif
