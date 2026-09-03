#ifndef DKC2_VIDEO_H
#define DKC2_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dkc2_hdma.h"

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
  /* How far from the camera the scroll covering most of a frame's lines
   * may lie and still be taken as the terrain phase when neither the
   * frame-start register nor any band is at the camera phase (see
   * Dkc2VideoSelectTerrainPhase). A parallax effect layer sits hundreds
   * of pixels off and never qualifies. */
  kDkc2VideoTerrainPhaseFollowX = 32,
  kDkc2VideoTerrainPhaseFollowY = 32,
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
/* The presentation margins with the level's left camera bound supplied:
 * the glide treats `minimum_scroll_x` as the west wall. A level whose
 * authored world begins where the player is held (Screech's Sprint starts
 * on a plank platform at world 608 with nothing west of it) has no
 * minimum-scroll word, so the host derives the bound from the level map
 * (Dkc2VideoHoldWest). Dkc2VideoPresentationMargins is the same with the
 * map's first page, $0100, as the bound. */
void Dkc2VideoPresentationMarginsBounded(uint16_t camera_x,
                                         uint16_t minimum_scroll_x,
                                         uint16_t maximum_scroll_x,
                                         int *bias,
                                         int *left_margin,
                                         int *right_margin);

/* The margins the bounds allow for a bias the host has already chosen
 * (the bias moves at most one pixel per frame toward the glide's target,
 * so a bound that appears or vanishes never snaps the picture). */
void Dkc2VideoMarginsForBias(uint16_t camera_x, uint16_t minimum_scroll_x,
                             uint16_t maximum_scroll_x, int bias,
                             int *left_margin, int *right_margin);


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
/* The presentation bias the host rendered last (positive: the presented
 * window sits right of the cartridge camera). The camera-relative cull
 * helpers below widen asymmetrically by it. */
void Dkc2VideoSetPresentationBias(int bias);
int Dkc2VideoPresentationBias(void);

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
/*
 * VRAM word pages (1024 words each) that a background tilemap occupies for
 * one BGnSC value: one for 32x32, two for 64x32 or 32x64, four for 64x64,
 * consecutive from the map base. Returns the page count.
 */
unsigned Dkc2VideoTilemapPages(uint8_t bg_sc, uint8_t pages[4]);

/*
 * Whether a 64-column tilemap's content is authored to continue across its
 * own 512-pixel hardware wrap, so a host margin may read the map beyond the
 * native view. A map fails when any row with at least eight entries has a
 * shortest horizontal period that does not divide 64 columns (the ship
 * hold's 96-pixel cabin wall, which the cartridge re-bases to keep its
 * seam off screen) or when a strip of kDkc2VideoPlaneEdgeStrip or more
 * columns at either map edge is blank in every populated row (a backdrop
 * narrower than its allocation). An empty or 32-column map is not a plane.
 */
enum { kDkc2VideoPlaneEdgeStrip = 4 };
/* Rows of a 64-column tilemap that a static plane must not show in a
 * margin: rows populated in at least kDkc2VideoPlaneDenseCells of their
 * 64 cells (a painted strip, not scattered decoration) whose painting
 * stops kDkc2VideoPlaneEdgeStrip or more cells short of either wrap edge.
 * Toxic Tower's BG2 keeps its castle wall across all 64 columns but its
 * cornice strip across 55, and a scanline band whose scroll wraps a
 * margin into the blank nine showed the backdrop through the wall. Bit r
 * of the result is row r (32 or 64 rows by the map size). */
enum { kDkc2VideoPlaneDenseCells = 48 };
uint64_t Dkc2VideoTilemapBrokenRows(const uint16_t *vram, size_t word_count,
                                    uint8_t bg_sc, uint16_t character_base);

bool Dkc2VideoTilemapWrapsAuthored(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base);

/*
 * Whether a 64-column tilemap is an object plane: one 32-column half holds
 * the content and the other half is entirely blank. Haunted Hall draws
 * Kackle as a 32-column block into BG2's left page and positions him with
 * the layer's scroll; the map's hardware wrap beyond the block is blank by
 * design, so a margin may read the map raw (his off-screen part appears,
 * nothing else), while repeating the native line cut him at the edge and
 * copied him into the far margin. Such a map is rewritten as the object
 * animates, so it needs no static gate. Requires at least
 * kDkc2VideoObjectPlaneMinCells entries in the populated half.
 */
enum { kDkc2VideoObjectPlaneMinCells = 64 };
bool Dkc2VideoTilemapIsObjectPlane(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base);

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
/*
 * The terrain layer's rendered scroll phase for the frame. Normally the
 * frame-start BGnHOFS/BGnVOFS pair, but a stage's HDMA can leave another
 * value in the register at frame start and set the camera phase on every
 * rendered line (Slime Climb leaves BG1VOFS at $50 while its HDMA writes the
 * camera row for lines 1-224). The frame-start pair stands when it already
 * lies within the terrain lead of the camera phase; otherwise the band pair
 * within that lead covering the most lines replaces it. Returns true when a
 * band pair was chosen.
 */
bool Dkc2VideoSelectTerrainPhase(const Dkc2HdmaBands *bands,
                                 int layer,
                                 uint16_t frame_h,
                                 uint16_t frame_v,
                                 uint32_t camera_x,
                                 uint32_t camera_y,
                                 uint16_t *phase_h,
                                 uint16_t *phase_v);

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
/*
 * Bytes per metatile row of a row-major level map for a layout: 64 for the
 * vertical shafts (32 metatiles), 192 for the square scroller's audited
 * stage (96 metatiles: the cartridge's column builder $B5:B555 multiplies
 * the row by six), 32 for Parrot Chute Panic, 160 for the ship holds (80
 * metatiles), and 0 for the column-major horizontal layout. A stage can
 * run a different builder than its sub-mode suggests (Bramble $002D uses
 * the 160-byte rows), so the host verifies the stride against the native
 * ring and calibrates it when the default fails.
 */
unsigned Dkc2VideoLevelLayoutRowBytes(Dkc2VideoLevelLayout layout);

/*
 * Decode one tile of a row-major level map with an explicit row stride:
 * metatile (world_x / 32, world_y / 32) at map offset column * 2 + row *
 * row_bytes. The horizontal layout is column-major and uses
 * Dkc2VideoDecodeLevelTile.
 */
/* The id (flip bits stripped) of the metatile at a level-map cell:
 * column-major 16-row pages for the horizontal layout, `row_bytes` strided
 * rows otherwise (0 takes the layout's stride). */
bool Dkc2VideoReadLevelMetatile(const uint8_t *bank_data, size_t bank_size,
                                uint16_t level_map_base,
                                Dkc2VideoLevelLayout layout,
                                unsigned row_bytes,
                                uint32_t metatile_x, uint32_t metatile_y,
                                uint16_t *metatile);

/* The metatile ids the level map in [level_map_base, map_end) most often
 * places beside `metatile` on one side (empty cells, id 0, excluded), most
 * frequent first. Returns how many ids were written, at most `capacity`. */
unsigned Dkc2VideoMetatileNeighbours(const uint8_t *bank_data,
                                     size_t bank_size,
                                     uint16_t level_map_base,
                                     uint16_t map_end,
                                     Dkc2VideoLevelLayout layout,
                                     unsigned row_bytes, uint16_t metatile,
                                     bool east_side, uint16_t *ids,
                                     uint16_t *counts, unsigned capacity);

/* Decode one 8x8 entry of a metatile definition by id (flip bits honoured
 * as the cartridge does), without going through the level map. */
bool Dkc2VideoDecodeMetatileEntry(const uint8_t *bank_data, size_t bank_size,
                                  uint16_t metatile_base,
                                  uint16_t metatile_word, unsigned sub_x,
                                  unsigned sub_y, uint16_t *tile_entry);

bool Dkc2VideoDecodeLevelTileRowMajor(const uint8_t *bank_data,
                                      size_t bank_size,
                                      uint16_t level_map_base,
                                      uint16_t metatile_base,
                                      unsigned row_bytes,
                                      uint32_t world_tile_x,
                                      uint32_t world_tile_y,
                                      uint16_t *tile_entry);

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
 * Whether one rigging ring cell agrees with its decode. The row upload
 * ($B5:AC25) is a word DMA that never sets VMAIN, so it inherits the VRAM
 * increment mode an earlier upload in the same frame left behind; when that
 * mode increments on the low byte, every high byte lands one word late (the
 * cell keeps its own low byte and takes the previous source word's high
 * byte), and the first word of each 32-word page keeps whatever high byte
 * VRAM already held. A cell therefore matches when it is exact, or when its
 * low byte is exact and its high byte is the previous cell's decoded high
 * byte (any high byte for the first cell of a page). Low bytes always have to
 * match, so a wrong map cannot pass.
 */
bool Dkc2VideoRiggingCellMatches(uint16_t decoded, uint16_t ring,
                                 uint16_t previous_decoded,
                                 bool first_in_page);

/*
 * Unwrapped map Y of the rigging layer's frame anchor (the PPU vertical
 * scroll's world value; the top line shows anchor + 1). The cartridge keeps
 * the rigging's vertical scroll to eight bits, (camera Y - $101) & $FF,
 * against a map 512 pixels tall, so the value is rebuilt from the camera in
 * 256-pixel epochs with the PPU's own fine phase, which can trail the
 * camera by a pixel at an NMI boundary. camera_y must be at least $101.
 */
uint32_t Dkc2VideoRiggingShadowY(uint16_t ppu_scroll_y, uint32_t camera_y);

/*
 * Lava geyser steam columns (the hot-air vents of DKC2's lava stages).
 * The cartridge keeps a per-stage list of geyser world X positions in ROM
 * at $B3:D65B (bit 0 set: tall column; entry $8000 ends a list; WRAM $0959
 * holds the stage's first entry offset) and, in NMI sub-mode 18, draws
 * every geyser registered in its four WRAM slots ($095B..$0961: map word
 * offset, bit 15 tall, bit 14 being cleared) as a three-column block of
 * 2bpp map entries into the bounded 32x32 BG3 map ($80:CB71, VMAIN $81,
 * one DMA per column). The column-major entries come from the animation
 * tables that the long-pointer table at $80:D3AD selects: four short
 * frames (three columns by ten rows, map rows 14-23), then four tall
 * frames (three by eighteen, map rows 6-23), frame = (frame counter $2A
 * >> 2) & 3, leftmost map column ((X - 8) >> 3) & 31. Bounded to 256
 * pixels, the map wraps a geyser standing beside the view onto the
 * opposite edge, so host margins decode the same tables instead.
 */
enum {
  kDkc2VideoGeyserListAddress = 0xb3d65b,
  kDkc2VideoGeyserListEnd = 0x8000,
  kDkc2VideoGeyserTableAddress = 0x80d3ad,
  kDkc2VideoGeyserColumns = 3,
  kDkc2VideoGeyserShortRows = 10,
  kDkc2VideoGeyserTallRows = 18,
  kDkc2VideoGeyserLastMapRow = 23,
  kDkc2VideoGeyserFrames = 4
};
/* Rows in a geyser block: eighteen tall, ten short. */
unsigned Dkc2VideoGeyserRows(bool tall);
/* First map row of a geyser block: 6 for a tall column, 14 for a short
 * one; both end on map row 23, the lava surface. */
unsigned Dkc2VideoGeyserFirstMapRow(bool tall);
/* World tile column of a geyser's leftmost block column, (X - 8) >> 3,
 * from a raw list entry (bit 0 is the tall flag). False below X = 8. */
bool Dkc2VideoGeyserFirstTileX(uint16_t list_entry, uint32_t *tile_x);
/* Map entry at (column 0-2, row) of a geyser block for one animation
 * frame. bank_data addresses bank $80 from $8000 (bank_size bytes, at most
 * $8000); the pointer table and every block must stay in that half. */
bool Dkc2VideoGeyserEntry(const uint8_t *bank_data, size_t bank_size,
                          unsigned frame, bool tall, unsigned column,
                          unsigned row, uint16_t *entry);

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

/* A west hold in the level map: the columns beside the cartridge window's
 * first column, up to `reach` of them, are empty for every row in
 * [first_row, last_row]. The player is held at the authored world's edge
 * and the margin would show nothing there. `hold_column` receives the
 * window's first column, whose west edge is the bound. */
bool Dkc2VideoHoldWest(Dkc2VideoMetatileClassifier classify, void *context,
                       uint32_t window_column, unsigned reach,
                       uint32_t first_row, uint32_t last_row,
                       uint32_t *hold_column);

/*
 * Source tile column that mirrors a margin tile across a player-held wall.
 * The wall line is the boundary just outside the cartridge's edge tile
 * (edge_source_tile: the first native source tile on the west, the last on
 * the east), so the tile beside the edge mirrors the edge tile, the next
 * one the tile behind it, and so on; the caller flips the entry
 * horizontally. Returns false when the tile is not beyond that edge.
 */
bool Dkc2VideoMirrorSourceTileAcrossEdge(uint32_t source_tile_x,
                                         uint32_t edge_source_tile,
                                         bool east_side,
                                         uint32_t *mirrored_tile_x);

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
