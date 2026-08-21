#pragma once

#include <stddef.h>
#include <stdbool.h>
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

typedef struct Dkc2WidescreenDebugState {
  uint32_t resume_pc;
  uint16_t mode_0529;
  uint16_t scene_00d3;
  uint16_t level_effects_052b;
  uint16_t camera_x;
  uint16_t camera_y;
  uint8_t ppu_mode;
  uint8_t visible_layers;
  uint8_t wide_layers;
  uint8_t debug_layer_mask;
  uint64_t west_hit[4];
  uint64_t west_miss[4];
  uint64_t east_hit[4];
  uint64_t east_miss[4];
  uint64_t raw_fallback[4];
} Dkc2WidescreenDebugState;

const RtlGameInfo *Dkc2GameInfo(void);
void Dkc2BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc2DrawPpuFrame(void);
uint32_t Dkc2ResumePc(void);
int Dkc2LastLleResult(void);
void Dkc2GetTerrainPrefillStats(Dkc2TerrainPrefillStats *out);
void Dkc2DebugSetLayerMask(uint8_t mask);
void Dkc2DebugSetProvenanceEnabled(bool enabled);
bool Dkc2DebugProvenanceEnabled(void);
void Dkc2GetWidescreenDebugState(Dkc2WidescreenDebugState *out);
