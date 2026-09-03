#pragma once

#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"

typedef struct Dkc2TerrainPrefillStats {
  size_t expected;
  size_t decoded;
  size_t present;
  size_t matching;
  size_t margin_expected;
  size_t margin_present;
  size_t margin_matching;
  /* Margin tiles decoded from a continued wall (Dkc2VideoFindStructuralWallSource). */
  size_t structural;
  /* Margin tiles mirrored across a player-held wall whose edge metatile is
   * partial (Dkc2VideoMirrorSourceTileAcrossEdge). */
  size_t mirrored;
  /* Structurally continued tiles whose metatile came from the level map's
   * own adjacency (what the map places beside the wall's metatile) rather
   * than from a copy of the wall's edge column. */
  size_t chained;
  /* The terrain layer's rendered scroll phase used for keys and band
   * classification, and whether an HDMA band supplied it instead of the
   * frame-start register. */
  uint16_t phase_h;
  uint16_t phase_v;
  uint8_t phase_from_band;
  /* Row-major level maps: the bytes per metatile row in use, and the
   * percentage of fully staged native cells the decode reproduced with it
   * this frame (the calibration gate). */
  uint16_t row_bytes;
  uint8_t row_match_percent;
} Dkc2TerrainPrefillStats;

const RtlGameInfo *Dkc2GameInfo(void);
void Dkc2BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc2DrawPpuFrame(void);
uint32_t Dkc2ResumePc(void);
int Dkc2LastLleResult(void);
void Dkc2GetTerrainPrefillStats(Dkc2TerrainPrefillStats *out);
/* Ship-deck rigging (BG3) margin decode accounting for the widescreen
 * trace: whether the cartridge's rigging streamer is active, whether the
 * ROM decode reproduced every fully uploaded native cell this frame, and
 * how many margin cells were decoded. */
typedef struct Dkc2RiggingStats {
  uint8_t configured;
  uint8_t ready;
  uint32_t native_expected;
  uint32_t native_matching;
  uint32_t native_shifted;   /* matched through the row-DMA high-byte shift */
  uint32_t margin_decoded;
} Dkc2RiggingStats;
void Dkc2GetRiggingStats(Dkc2RiggingStats *out);
/* Lava geyser steam (bounded BG3) decode diagnostics for the last rendered
 * frame: whether the stage runs the geyser effect, whether the decode
 * reproduced every geyser column the cartridge had fully drawn, the
 * animation frame the ring shows against the frame-counter prediction,
 * and how many margin/inset cells and listed geysers were decoded. */
typedef struct Dkc2GeyserStats {
  uint8_t configured;
  uint8_t ready;
  uint8_t frame;
  uint8_t frame_predicted;
  uint32_t native_expected;
  uint32_t native_matching;
  uint32_t margin_decoded;
  uint32_t margin_geysers;
} Dkc2GeyserStats;
void Dkc2GetGeyserStats(Dkc2GeyserStats *out);
/* Scanline bands read from the cartridge's HDMA tables for the last
 * rendered frame (host-only diagnostics). */
int Dkc2GetHdmaBandCount(void);
/* Bands of a wide layer presented as a static plane (the map's own wrap)
 * in the last rendered frame (host-only diagnostics). */
int Dkc2GetPlaneBandCount(int layer);
