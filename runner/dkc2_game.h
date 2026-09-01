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
} Dkc2TerrainPrefillStats;

const RtlGameInfo *Dkc2GameInfo(void);
void Dkc2BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc2DrawPpuFrame(void);
uint32_t Dkc2ResumePc(void);
int Dkc2LastLleResult(void);
void Dkc2GetTerrainPrefillStats(Dkc2TerrainPrefillStats *out);
/* Scanline bands read from the cartridge's HDMA tables for the last
 * rendered frame (host-only diagnostics). */
int Dkc2GetHdmaBandCount(void);
