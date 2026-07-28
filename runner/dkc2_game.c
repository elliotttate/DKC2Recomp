#include "dkc2_game.h"
#include "dkc2_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdbool.h>
#include <string.h>

enum {
  kDkc2ResetPc = 0x0083F7,
  kDkc2NmiPc = 0x00F37D,
};

static bool s_cpu_initialized;
static uint32_t s_resume_pc = kDkc2ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;
static bool s_widescreen_shadow_active;
static bool s_widescreen_origin_valid[2];
static uint32_t s_widescreen_world_x[2];
static uint32_t s_widescreen_world_y[2];
static bool s_widescreen_source_valid;
static uint64_t s_widescreen_source_signature;

typedef struct Dkc2HostSnapshot {
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint64_t apu_last_sync_cycles;
  uint64_t apu_last_sync_master;
  int last_lle_result;
  int frame_counter;
  uint8_t cpu_initialized;
  uint8_t last_hdmaen;
  uint8_t memsel;
} Dkc2HostSnapshot;

enum {
  /* NTSC master clocks per non-short host frame. The shared interpreter
   * already accounts each opcode and bus region in this unit. A deadline at
   * this cadence lets VBlank interrupt productive loading/decompression code
   * instead of atomically running hundreds of console frames to the next WAI. */
  kDkc2NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc2RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  if (s_next_frame_master == 0) {
    s_next_frame_master =
        g_cpu.master_cycles + kDkc2NtscFrameMasterClocks;
  }
  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kDkc2NtscFrameMasterClocks;
  interp_bridge_set_master_deadline(s_next_frame_master);

  if (first_frame) {
    cpu_state_init(&g_cpu, g_ram);
    s_cpu_initialized = true;
  }
  if (!first_frame && g_snes->nmiEnabled) {
    /* DKC2's boot/intro NMI is a non-returning frame dispatcher. The handler
     * jumps through the continuation pointer at direct-page $20; that frame
     * routine resets S and ends at its own WAI rather than executing RTI.
     * Run the handler and continuation together to the next quiescent wait.
     * Resuming the pre-NMI WAI afterwards would discard all progress made by
     * the continuation and leave the palette source buffer permanently zero. */
    g_snes->inNmi = true;
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, kDkc2NmiPc);
  } else {
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  }

  interp_bridge_set_master_deadline(0);
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc2NtscFrameMasterClocks;
}

static void Dkc2SaveExtra(SaveLoadInfo *sli) {
  Dkc2HostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.cpu = g_cpu;
  snapshot.cpu.ram = NULL;
  snapshot.resume_pc = s_resume_pc;
  snapshot.next_frame_master = s_next_frame_master;
  snapshot.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  snapshot.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  snapshot.apu_last_sync_cycles = g_apu_last_sync_cycles;
  snapshot.apu_last_sync_master = g_apu_last_sync_master;
  snapshot.last_lle_result = s_last_lle_result;
  snapshot.frame_counter = snes_frame_counter;
  snapshot.cpu_initialized = s_cpu_initialized ? 1u : 0u;
  snapshot.last_hdmaen = g_snesrecomp_last_hdmaen;
  snapshot.memsel = g_memsel;
  sli->func(sli, &snapshot, sizeof snapshot);
}

static void Dkc2LoadExtra(SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  Dkc2HostSnapshot snapshot;
  sli->func(sli, &snapshot, sizeof snapshot);
  g_cpu = snapshot.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = snapshot.resume_pc;
  s_next_frame_master = snapshot.next_frame_master;
  g_main_cpu_cycles_estimate = snapshot.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = snapshot.apu_pace_cycles_estimate;
  g_apu_last_sync_cycles = snapshot.apu_last_sync_cycles;
  g_apu_last_sync_master = snapshot.apu_last_sync_master;
  s_last_lle_result = snapshot.last_lle_result;
  snes_frame_counter = snapshot.frame_counter;
  s_cpu_initialized = snapshot.cpu_initialized != 0;
  g_snesrecomp_last_hdmaen = snapshot.last_hdmaen;
  g_memsel = snapshot.memsel;
}

static void Dkc2OnStateLoaded(uint32_t version) {
  (void)version;
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  WsShadowReset();
  s_widescreen_shadow_active = false;
  memset(s_widescreen_origin_valid, 0, sizeof s_widescreen_origin_valid);
  s_widescreen_source_valid = false;
  Dkc2VideoSetTerrainReady(false);
}

static const RtlGameInfo kDkc2GameInfo = {
  .title = "dkc2",
  .initialize = NULL,
  .run_frame = &Dkc2RunOneFrame,
  .draw_ppu_frame = &Dkc2DrawPpuFrame,
  .save_name_prefix = "dkc2s",
  .state_save_extra = &Dkc2SaveExtra,
  .state_load_extra = &Dkc2LoadExtra,
  .on_state_loaded = &Dkc2OnStateLoaded,
};

const RtlGameInfo *Dkc2GameInfo(void) {
  return &kDkc2GameInfo;
}

void Dkc2BeginDrawing(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

static uint16_t Dkc2ReadWram16(uint16_t address) {
  return (uint16_t)g_ram[address] |
         ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8);
}

static void Dkc2ResetWidescreenShadow(void) {
  if (s_widescreen_shadow_active)
    WsShadowReset();
  s_widescreen_shadow_active = false;
  memset(s_widescreen_origin_valid, 0, sizeof s_widescreen_origin_valid);
  s_widescreen_source_valid = false;
  Dkc2VideoSetTerrainReady(false);
}

static const uint8_t *Dkc2LevelSourceBank(uint8_t *bank_out) {
  const uint8_t bank = g_ram[0x009a];
  if (bank_out)
    *bank_out = bank;
  if (bank == 0x7e)
    return g_ram;
  if (bank == 0x7f)
    return g_ram + 0x10000;
  return NULL;
}

static uint64_t Dkc2LevelSourceSignature(void) {
  const uint64_t bank = g_ram[0x009a];
  const uint64_t map = Dkc2ReadWram16(0x0098);
  const uint64_t metatiles = Dkc2ReadWram16(0x17b4);
  const uint64_t vram = Dkc2ReadWram16(0x17b6);
  return bank | (map << 8) | (metatiles << 24) | (vram << 40);
}

static bool Dkc2PrefillWidescreenLevelBg1(uint8_t layer_mask,
                                          uint32_t camera_x,
                                          uint32_t camera_y) {
  if (!(layer_mask & 0x01u) || PPU_bigTiles(g_ppu, 0))
    return false;

  uint8_t bank = 0;
  const uint8_t *bank_data = Dkc2LevelSourceBank(&bank);
  if (!bank_data)
    return false;

  const uint16_t map_base = Dkc2ReadWram16(0x0098);
  const uint16_t metatile_base = Dkc2ReadWram16(0x17b4);
  const uint16_t maximum_scroll_x = Dkc2ReadWram16(0x0afc);
  uint16_t transparent_tile = 0;
  if (maximum_scroll_x == 0 ||
      !Dkc2VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u, (uint16_t)PPU_bgTileAdr(g_ppu, 0),
          &transparent_tile))
    return false;
  /*
   * The rolling column builder stages one complete 32x32 metatile beyond
   * the native camera limit so fine scrolling never exposes an incomplete
   * edge. That guard column is real level scenery and is safe to reveal.
   * The following metatile belongs to unrelated WRAM.
   */
  const uint32_t source_tile_limit =
      ((uint32_t)maximum_scroll_x + 0x20u + 7u) >> 3;
  const uint32_t extra = (uint32_t)Dkc2VideoExtra();
  const uint32_t first_x =
      camera_x > extra ? camera_x - extra : 0;
  const uint32_t last_x =
      camera_x + (uint32_t)kDkc2VideoNativeWidth - 1u + extra;
  const uint32_t first_tile_x = first_x >> 3;
  const uint32_t last_tile_x = last_x >> 3;
  const uint32_t ppu_scroll_y = g_ppu->vScroll[0] & 0x03ffu;
  const uint32_t fine_y = ppu_scroll_y & 7u;
  const uint32_t visible_tile_rows =
      ((uint32_t)kDkc2VideoHeight + fine_y + 7u) >> 3;
  const uint32_t first_map_row = (ppu_scroll_y >> 3) & 31u;
  const uint32_t first_destination_row = (camera_y >> 3) & 31u;
  const uint32_t first_source_subrow = (camera_y >> 3) & 3u;
  const uint32_t source_base_row =
      ((camera_y - 0x0100u) & 0x01e0u) >> 3;

  size_t decoded = 0;
  size_t expected = 0;
  for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; tile_x++) {
    for (uint32_t row = 0; row < visible_tile_rows; row++) {
      uint16_t entry = 0;
      /*
       * Reproduce the cartridge column builder's vertical rotation rather
       * than assuming camera Y is the VRAM row. $B5:ACC0-$B5:ACCF starts
       * $0100 pixels above the camera; $B5:ADA9-$B5:ADD0 then rotates those
       * 36 source entries into the 32-row rolling tilemap.
       */
      const uint32_t map_row = (first_map_row + row) & 31u;
      const uint32_t row_delta =
          (map_row - first_destination_row) & 31u;
      const uint32_t source_tile_y =
          source_base_row + first_source_subrow + row_delta;
      const uint32_t wrapped_tile_y =
          ((ppu_scroll_y & ~7u) + row * 8u) & 0x03ffu;
      const uint32_t shadow_tile_y =
          Dkc2VideoUnwrapPpuScroll(
              (uint16_t)wrapped_tile_y, camera_y) >> 3;
      expected++;
      /*
       * DKC2's camera/object coordinate system starts one 256-pixel page
       * after the decompressed level map. This is the same relationship made
       * explicit by $B5:ACA8-$B5:ACB7 (source column) and
       * $B5:ADF0-$B5:AE01 (rolling-VRAM destination): while moving right, a
       * source column at X is uploaded to the VRAM column for X+$0100.
       *
       * A matching frame-5499 WRAM/VRAM calibration confirms the mapping:
       * source tile (shadow key - 32) agrees with 1,754/2,048 live BG1 cells
       * (85.6%); the next-best tested offset agrees with only 746/2,048.
       * Remaining differences are expected dynamic/partially staged cells.
       */
      if (tile_x < (0x0100u >> 3)) {
        WsShadowPrefillTile(0, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        continue;
      }
      const uint32_t source_tile_x = tile_x - (0x0100u >> 3);
      /*
       * $0AFC is the camera's maximum horizontal scroll after the cartridge
       * subtracts the 256-pixel native viewport ($B5:E36C-$B5:E373).
       * Adding the streamer's one 32-pixel guard metatile gives the exclusive
       * safe source width. Reading the following metatile crosses into
       * unrelated WRAM; this was the colorful far-right stripe in the
       * frame-9000 capture. Outside that guard, use a verified transparent
       * character so lower layers remain visible without inventing or
       * repeating terrain.
       */
      if (source_tile_x >= source_tile_limit) {
        WsShadowPrefillTile(0, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        continue;
      }
      if (!Dkc2VideoDecodeLevelTile(
              bank_data, 0x10000u, map_base, metatile_base,
              source_tile_x, source_tile_y, &entry))
        continue;
      WsShadowPrefillTile(0, tile_x, shadow_tile_y, entry);
      decoded++;
    }
  }
  (void)bank;
  return expected != 0 && decoded == expected;
}

static bool Dkc2PrepareWidescreenShadow(uint8_t layer_mask) {
  const uint32_t camera_x = Dkc2ReadWram16(0x17BA);
  const uint32_t camera_y = Dkc2ReadWram16(0x17C0);
  const uint64_t source_signature = Dkc2LevelSourceSignature();

  if (!s_widescreen_shadow_active) {
    WsShadowReset();
    memset(s_widescreen_origin_valid, 0, sizeof s_widescreen_origin_valid);
    s_widescreen_shadow_active = true;
  }
  if (!s_widescreen_source_valid ||
      source_signature != s_widescreen_source_signature) {
    WsShadowReset();
    memset(s_widescreen_origin_valid, 0, sizeof s_widescreen_origin_valid);
    s_widescreen_source_signature = source_signature;
    s_widescreen_source_valid = true;
  }

  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if (!(layer_mask & bit)) {
      s_widescreen_origin_valid[layer] = false;
      continue;
    }

    if (layer == 0) {
      /*
       * DKC2's rolling VRAM address is not its world coordinate. BG1 must be
       * keyed by the full WRAM camera while SetScroll supplies the independent
       * PPU buffer phase. Using the PPU phase for both was the Version 08
       * association error visible as partial/floating future columns.
       */
      s_widescreen_world_x[layer] = camera_x;
      s_widescreen_world_y[layer] = camera_y;
    } else {
      const uint32_t anchor_x = s_widescreen_origin_valid[layer]
                                    ? s_widescreen_world_x[layer]
                                    : camera_x;
      const uint32_t anchor_y = s_widescreen_origin_valid[layer]
                                    ? s_widescreen_world_y[layer]
                                    : camera_y;
      s_widescreen_world_x[layer] =
          Dkc2VideoUnwrapPpuScroll(g_ppu->hScroll[layer], anchor_x);
      s_widescreen_world_y[layer] =
          Dkc2VideoUnwrapPpuScroll(g_ppu->vScroll[layer], anchor_y);
    }
    s_widescreen_origin_valid[layer] = true;

    WsShadowSetWorld(layer, s_widescreen_world_x[layer],
                    s_widescreen_world_y[layer]);
    WsShadowSetScroll(layer, g_ppu->hScroll[layer], g_ppu->vScroll[layer]);
    WsShadowSetWestKeep(layer, 8);
    WsShadowSetEastKeep(layer, 8);
    /*
     * An unknown world cell must never fall through to a stale rolling VRAM
     * page. Exact viewport/history captures replace this bounded fallback as
     * soon as DKC2 displays or uploads the corresponding tile.
     */
    uint16_t blank_entry = 0;
    if (!PPU_bigTiles(g_ppu, layer))
      Dkc2VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, layer), &blank_entry);
    WsShadowSetBlankTile(layer, blank_entry);
    if (layer == 1)
      WsShadowSetPeriodicFold(layer);
  }

  WsShadowFrame(g_ppu);
  return Dkc2PrefillWidescreenLevelBg1(
      layer_mask, camera_x, camera_y);
}

void Dkc2DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};

  /*
   * Widescreen is a host-only PPU policy. Reapply it for every frame because
   * reset/state restore deliberately does not serialize presentation
   * geometry.
   */
  uint8_t wide_layer_mask =
      Dkc2VideoIsWidescreen()
          ? Dkc2VideoPpuWideLayerMask(g_ppu->bgmode, g_ppu->bgXsc,
                                      g_ppu->screenEnabled[0],
                                      g_ppu->screenEnabled[1])
          : 0;
  bool extend_world = wide_layer_mask != 0;
  if (extend_world) {
    PpuSetExtraSpace(g_ppu, (uint8_t)Dkc2VideoExtra());
    PpuSetWidescreenLayerMask(g_ppu, wide_layer_mask);
    /*
     * DKC2's BG2 parallax backdrop is a deliberately wrapping 32-column
     * tilemap on these Mode 1 gameplay screens. Repeat its already-rendered
     * native scanline into the margins instead of exposing arbitrary BG2
     * VRAM or leaving the fixed-color backdrop visible. BG1 carries the
     * collision-bearing level art and remains world-keyed below.
     */
    uint8_t enabled_layers =
        (uint8_t)((g_ppu->screenEnabled[0] |
                   g_ppu->screenEnabled[1]) & 0x03u);
    uint8_t repeat_layers =
        (uint8_t)(enabled_layers & (uint8_t)~wide_layer_mask & 0x02u);
    PpuSetWidescreenLayerRepeat(g_ppu, repeat_layers);
    Dkc2VideoSetTerrainReady(
        Dkc2PrepareWidescreenShadow(wide_layer_mask));
  } else if (Dkc2VideoIsWidescreen()) {
    Dkc2ResetWidescreenShadow();
    /*
     * Clear the whole host row before centering a bounded 256-column screen.
     * PpuSetExtraSpaceCentered intentionally draws no margin pixels, so this
     * prevents the preceding wide gameplay frame from surviving there.
     */
    size_t row_bytes = (size_t)Dkc2VideoWidth() * kDkc2VideoBytesPerPixel;
    for (int y = 0; y < kDkc2VideoHeight; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
             0, row_bytes);
    PpuSetExtraSpaceCentered(g_ppu, (uint8_t)Dkc2VideoExtra());
  } else {
    Dkc2ResetWidescreenShadow();
    PpuSetExtraSpace(g_ppu, 0);
  }

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  for (int line = 0; line <= 224; line++) {
    ppu_runLine(g_ppu, line);
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }

  /* The static-recomp host advances one complete game frame and one complete
   * render pass as separate operations. Model the VBlank boundary after the
   * visible lines so the PPU reloads its internal OAM port from OAMADD before
   * the next frame's NMI performs DKC2's 544-byte OAM DMA. Without this call,
   * the DMA source is correct but the destination begins at the stale address
   * left by the preceding transfer and the sprite table rotates every frame. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
}

uint32_t Dkc2ResumePc(void) {
  return s_resume_pc;
}

int Dkc2LastLleResult(void) {
  return s_last_lle_result;
}

/* Required neutral hooks declared by generated funcs.h. */
void RunOneFrameOfGame_Internal(void) {
  Dkc2RunOneFrame();
}

void ResetSpritesFunc(int first) {
  (void)first;
}
