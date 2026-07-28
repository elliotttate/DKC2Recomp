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

/*
 * Return the subset of currently enabled BG1/BG2 layers whose tilemaps have
 * 64 columns. BG3 is deliberately excluded because DKC2 also uses it for HUD
 * and staging data that is not safe to expose outside the native viewport.
 */
uint8_t Dkc2VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);
bool Dkc2VideoPpuCanExtend(uint8_t bg_mode,
                           const uint8_t bg_xsc[4],
                           uint8_t main_layers,
                           uint8_t sub_layers);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc2VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/*
 * Decode one exact 8x8 BG tile from DKC2's decompressed level representation.
 * The game stores a column-major map of 32x32 metatiles and a second table
 * containing each metatile's sixteen 8x8 tilemap entries. Both buffers share
 * one 64 KiB CPU bank during gameplay. No ROM-derived bytes are retained by
 * this source-owned helper.
 */
bool Dkc2VideoDecodeLevelTile(const uint8_t *bank_data,
                              size_t bank_size,
                              uint16_t level_map_base,
                              uint16_t metatile_base,
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

#endif
