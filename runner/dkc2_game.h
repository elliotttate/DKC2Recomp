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
  uint32_t margin_decoded;
} Dkc2RiggingStats;
void Dkc2GetRiggingStats(Dkc2RiggingStats *out);
/* Scanline bands read from the cartridge's HDMA tables for the last
 * rendered frame (host-only diagnostics). */
int Dkc2GetHdmaBandCount(void);
