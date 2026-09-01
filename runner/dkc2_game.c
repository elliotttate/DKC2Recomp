#include "dkc2_game.h"
#include "dkc2_hdma.h"
#include "dkc2_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
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
static uint32_t s_widescreen_world_x[2];
static uint32_t s_widescreen_world_y[2];
static bool s_widescreen_source_valid;
static uint64_t s_widescreen_source_signature;
static Dkc2TerrainPrefillStats s_terrain_prefill_stats;

/* Per-frame scanline geometry read from the cartridge's own HDMA tables and
 * the presentation policy chosen for each (wide layer, band). A band is
 * either served from the world-keyed terrain store (the layer displays the
 * streamed level map at the terrain phase) or repeats its rendered native
 * scanline (a bounded effect or backdrop plane). */
enum {
  kDkc2BandPolicyRepeat = 0,
  kDkc2BandPolicyWorld = 1,
};
static Dkc2HdmaBands s_frame_bands;
static uint8_t s_band_policy[2][kDkc2HdmaMaxBands];

void Dkc2GetTerrainPrefillStats(Dkc2TerrainPrefillStats *out) {
  if (out)
    *out = s_terrain_prefill_stats;
}

int Dkc2GetHdmaBandCount(void) {
  return s_frame_bands.count;
}

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

static void Dkc2RecordTerrainPrefillTile(int layer,
                                          uint32_t world_tile_x,
                                          uint32_t world_tile_y,
                                          uint16_t expected_entry,
                                          bool margin) {
  uint16_t actual = 0;
  if (!WsShadowLookupWorldTile(
          layer, world_tile_x, world_tile_y, &actual))
    return;
  s_terrain_prefill_stats.present++;
  if (margin)
    s_terrain_prefill_stats.margin_present++;
  if (actual != expected_entry)
    return;
  s_terrain_prefill_stats.matching++;
  if (margin)
    s_terrain_prefill_stats.margin_matching++;
}

static bool Dkc2PrefillWidescreenLevelTerrain(uint8_t layer_mask,
                                              int terrain_layer,
                                              Dkc2VideoLevelLayout layout,
                                              uint32_t rendered_x,
                                              uint32_t camera_y) {
  memset(&s_terrain_prefill_stats, 0, sizeof s_terrain_prefill_stats);
  if (terrain_layer < 0 || terrain_layer >= 2 ||
      layout == kDkc2VideoLevelLayoutUnknown ||
      !(layer_mask & (uint8_t)(1u << terrain_layer)) ||
      PPU_bigTiles(g_ppu, terrain_layer))
    return false;

  uint8_t bank = 0;
  const uint8_t *bank_data = Dkc2LevelSourceBank(&bank);
  if (!bank_data)
    return false;

  const uint16_t map_base = Dkc2ReadWram16(0x0098);
  const uint16_t metatile_base = Dkc2ReadWram16(0x17b4);
  const uint16_t maximum_scroll_x = Dkc2ReadWram16(0x0afc);
  const uint16_t maximum_scroll_y = Dkc2ReadWram16(0x0afe);
  uint16_t transparent_tile = 0;
  if (maximum_scroll_x == 0 ||
      !Dkc2VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer),
          &transparent_tile))
    return false;
  const uint32_t extra = (uint32_t)Dkc2VideoExtra();
  /*
   * The rolling column builder stages one complete 32x32 metatile beyond
   * the native camera limit so fine scrolling never exposes an incomplete
   * edge. The following metatile belongs to unrelated WRAM in every case.
   * The presentation clamp keeps the visible margins inside the authored
   * camera extent, so this limit only protects the fine-scroll guard tiles.
   */
  const uint32_t source_tile_limit =
      ((uint32_t)maximum_scroll_x + 0x20u + 7u) >> 3;
  const uint32_t source_tile_limit_y =
      ((uint32_t)maximum_scroll_y + 7u) >> 3;
  /* Keep one decoded tile beyond both host margins. A fine-scroll phase can
   * make the final one or two pixels address the adjacent tile even though
   * the nominal 342-pixel span still ends inside the previous source cell.
   * Without this guard Pirate Panic briefly fell through to the verified
   * blank tile during Rambi's fast down-right camera move (frame 6404). */
  const uint32_t guard = 8u;
  const uint32_t west_extent = extra + guard;
  const uint32_t first_x =
      rendered_x > west_extent ? rendered_x - west_extent : 0;
  const uint32_t last_x =
      rendered_x + (uint32_t)kDkc2VideoNativeWidth - 1u + extra + guard;
  const uint32_t first_tile_x = first_x >> 3;
  const uint32_t last_tile_x = last_x >> 3;
  const uint32_t ppu_scroll_y =
      g_ppu->vScroll[terrain_layer] & 0x03ffu;
  const uint32_t fine_y = ppu_scroll_y & 7u;
  const int visible_tile_rows =
      (int)(((uint32_t)kDkc2VideoHeight + fine_y + 7u) >> 3);
  /*
   * Reproduce the cartridge column builder's vertical rotation rather
   * than assuming WRAM camera Y is the already-latched PPU row.
   * $B5:ACC0-$B5:ACCF starts $0100 pixels above the camera;
   * $B5:ADA9-$B5:ADD0 then rotates those 36 source entries into the
   * 32-row rolling tilemap. The rendered PPU phase can trail the next
   * WRAM camera value by one pixel at an NMI boundary, so both the source
   * row and shadow key must derive from that same PPU phase. Mixing the
   * two phases turns an 8-pixel boundary into a transient +31-row wrap,
   * and PrefillTile then preserves the bad margin entry indefinitely.
   *
   * Decode one guard row above and below the viewport as well: an HDMA
   * band that shares the terrain phase may lead the frame anchor by up to
   * four pixels vertically, and its margin lookups must not fall through.
   */
  const uint32_t top_shadow_row =
      Dkc2VideoLevelSourceTileY((uint16_t)ppu_scroll_y, camera_y, 0);
  const uint32_t top_source_row =
      Dkc2VideoLevelMapTileY((uint16_t)ppu_scroll_y, camera_y, 0);
  size_t decoded = 0;
  size_t expected = 0;
  for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; tile_x++) {
    for (int row = -1; row <= visible_tile_rows; row++) {
      uint16_t entry = 0;
      const bool margin =
          Dkc2VideoTileTouchesWidescreenMargin(tile_x, rendered_x);
      const uint32_t shadow_tile_y =
          (uint32_t)((int64_t)top_shadow_row + row);
      const uint32_t source_tile_y =
          (uint32_t)((int64_t)top_source_row + row) & 0x1fffu;
      expected++;
      if (margin)
        s_terrain_prefill_stats.margin_expected++;
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
       *
       * No authored terrain exists west of that origin or beyond the last
       * camera position plus the viewport. The edge policy decides whether
       * such a column mirrors the nearest authored columns or stays
       * verified transparent.
       */
      uint32_t source_tile_x = 0;
      bool mirror_horizontally = false;
      const int edge = Dkc2VideoResolveEdgeTile(
          tile_x, maximum_scroll_x, &source_tile_x, &mirror_horizontally);
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
      if (edge < 0 || source_tile_x >= source_tile_limit) {
        WsShadowForceTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        Dkc2RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
      if ((layout == kDkc2VideoLevelLayoutVertical ||
           layout == kDkc2VideoLevelLayoutSquare ||
           layout == kDkc2VideoLevelLayoutNarrowVertical) &&
          (maximum_scroll_y == 0 ||
           source_tile_y >= source_tile_limit_y)) {
        WsShadowForceTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        Dkc2RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
      if (!Dkc2VideoDecodeLevelTile(
              bank_data, 0x10000u, map_base, metatile_base,
              layout,
              source_tile_x, source_tile_y, &entry))
        continue;
      if (mirror_horizontally)
        entry ^= 0x4000u;
      /* At the horizontal $xxff->$xx00 vertical page boundary the first
       * visible tile row is supplied by the live rolling map, not by a full
       * decompressed source row. The native row is one pixel high and the
       * retained map can still contain a previous ship section there. Never
       * seed those unobserved side cells from that stale row. */
      if (layout == kDkc2VideoLevelLayoutHorizontal && row <= 0) {
        const uint32_t tile_pixel_x = tile_x << 3;
        if (tile_pixel_x < rendered_x ||
            tile_pixel_x >= rendered_x + kDkc2VideoNativeWidth) {
          WsShadowForceTile(
              terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        }
        decoded++;
        Dkc2RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
      /*
       * An older captured VRAM/DMA-pad tile can survive in a world cell that
       * the verified level map says is transparent. That produced the stray
       * deck fragments in Pirate Panic's upper-left margin. Clear only those
       * verified void cells every frame; non-transparent tiles retain real
       * history so dynamic ship tilemap details are not erased by a static
       * source reconstruction.
       */
      if (Dkc2VideoIsTransparentTileEntry(entry, transparent_tile) ||
          Dkc2VideoTileTouchesWidescreenMargin(tile_x, rendered_x))
        WsShadowForceTile(terrain_layer, tile_x, shadow_tile_y, entry);
      else
        WsShadowPrefillTile(terrain_layer, tile_x, shadow_tile_y, entry);
      decoded++;
      Dkc2RecordTerrainPrefillTile(
          terrain_layer, tile_x, shadow_tile_y, entry, margin);
    }
  }
  s_terrain_prefill_stats.expected = expected;
  s_terrain_prefill_stats.decoded = decoded;
  (void)bank;
  return expected != 0 && decoded == expected;
}

/* Register the terrain owner's world-keyed store (and, when another physical
 * 64-column layer displays the same world map in some HDMA band, that layer
 * as a read-only view of the owner's store), capture the owner's native
 * viewport, and decode the level map into every cell a host margin can
 * sample. Returns whether exact terrain is available for this frame. */
static bool Dkc2PrepareWidescreenShadow(uint8_t layer_mask,
                                        int terrain_layer,
                                        Dkc2VideoLevelLayout layout,
                                        int presentation_bias,
                                        const bool alias_layer[2]) {
  const uint32_t camera_x = Dkc2ReadWram16(0x17BA);
  const uint32_t camera_y = Dkc2ReadWram16(0x17C0);
  const uint64_t source_signature = Dkc2LevelSourceSignature();

  if (!s_widescreen_shadow_active) {
    WsShadowReset();
    s_widescreen_shadow_active = true;
  }
  if (!s_widescreen_source_valid ||
      source_signature != s_widescreen_source_signature) {
    WsShadowReset();
    s_widescreen_source_signature = source_signature;
    s_widescreen_source_valid = true;
  }

  uint32_t owner_world_x = camera_x;
  uint32_t owner_world_y = camera_y;
  uint32_t owner_scroll_x = 0;
  uint32_t owner_scroll_y = 0;
  const bool have_owner =
      terrain_layer >= 0 && terrain_layer < 2 &&
      (layer_mask & (uint8_t)(1u << terrain_layer)) != 0;
  if (have_owner) {
    /*
     * DKC2's rolling VRAM address is not its world coordinate. The layer
     * selected by live stream destination $17B6 uses the full WRAM camera
     * for X, but its vertical column buffer is staged one 256-pixel page
     * above camera Y. Key Y by the rendered PPU source phase so native
     * viewport captures, later VRAM writes, and exact prefills all address
     * the same terrain rows. Use the PPU-latched horizontal phase for both
     * the native viewport and widened margins: the WRAM camera can lead
     * hScroll by 1-3 pixels while DKC2 changes direction, and keying margins
     * from that newer value made the old 4:3 edge visibly split.
     */
    owner_scroll_x = g_ppu->hScroll[terrain_layer];
    owner_scroll_y = g_ppu->vScroll[terrain_layer];
    owner_world_x = Dkc2VideoTerrainShadowX(
        (uint16_t)owner_scroll_x, camera_x);
    owner_world_y = Dkc2VideoTerrainShadowY(
        (uint16_t)owner_scroll_y, camera_y);
    s_widescreen_world_x[terrain_layer] = owner_world_x;
    s_widescreen_world_y[terrain_layer] = owner_world_y;
    WsShadowSetWorld(terrain_layer, owner_world_x, owner_world_y);
    WsShadowSetScroll(terrain_layer, owner_scroll_x, owner_scroll_y);
    WsShadowSetWestKeep(terrain_layer, 8);
    WsShadowSetEastKeep(terrain_layer, 8);
    /* Preserve a live dynamic BG write from this or the immediately prior
     * game frame, but do not allow stale history to defeat the verified
     * decompressed level-map value in the widened terrain margins. */
    WsShadowSetRespectGameWrites(terrain_layer, 1);
    /*
     * An unknown world cell must never fall through to a stale rolling VRAM
     * page. Exact viewport/history captures replace this bounded fallback as
     * soon as DKC2 displays or uploads the corresponding tile.
     */
    uint16_t blank_entry = 0;
    if (!PPU_bigTiles(g_ppu, terrain_layer))
      Dkc2VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer), &blank_entry);
    WsShadowSetBlankTile(terrain_layer, blank_entry);
  }
  for (int layer = 0; layer < 2; layer++) {
    if (layer == terrain_layer)
      continue;
    if (have_owner && alias_layer[layer] &&
        (layer_mask & (uint8_t)(1u << layer))) {
      /* The view shares the owner's keys. The renderer adds this layer's own
       * per-line scroll delta, so a band that leads the frame anchor by a
       * few pixels still resolves the exact world cell. */
      WsShadowSetEntryAlias(layer, terrain_layer,
                            owner_world_x, owner_world_y,
                            owner_scroll_x, owner_scroll_y);
    } else {
      WsShadowClearEntryAlias(layer);
    }
  }

  WsShadowFrame(g_ppu);
  if (!have_owner)
    return false;
  return Dkc2PrefillWidescreenLevelTerrain(
      layer_mask, terrain_layer, layout,
      owner_world_x + presentation_bias, camera_y);
}

/* Guest-address resolution for the HDMA dry run, matching the runner's
 * SimpleHdma table walk: WRAM banks, the low-RAM mirror, and ROM. */
static const uint8_t *Dkc2HdmaPointer(void *context, uint32_t address) {
  (void)context;
  const uint8_t bank = (uint8_t)(address >> 16);
  const uint16_t offset = (uint16_t)address;
  if (bank == 0x7e)
    return g_ram + offset;
  if (bank == 0x7f)
    return g_ram + 0x10000 + offset;
  if ((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && offset < 0x2000)
    return g_ram + offset;
  return RomPtr(address);
}

static bool Dkc2HdmaReadable(void *context, const uint8_t *pointer,
                             size_t length) {
  (void)context;
  if (!pointer)
    return false;
  const uintptr_t address = (uintptr_t)pointer;
  const uintptr_t ram_base = (uintptr_t)g_ram;
  if (address >= ram_base) {
    const size_t offset = (size_t)(address - ram_base);
    if (offset <= sizeof g_ram && length <= sizeof g_ram - offset)
      return true;
  }
  const uint32_t rom_size =
      g_snes && g_snes->cart ? (uint32_t)g_snes->cart->romSize : 0;
  const uintptr_t rom_base = (uintptr_t)g_rom;
  if (g_rom && rom_size != 0 && address >= rom_base) {
    const size_t offset = (size_t)(address - rom_base);
    if (offset <= rom_size && length <= (size_t)rom_size - offset)
      return true;
  }
  return false;
}

static void Dkc2ScanFrameBands(Dkc2HdmaBands *bands) {
  Dkc2HdmaChannelConfig channels[8];
  for (int index = 0; index < 8; index++) {
    const DmaChannel *channel = &g_dma->channel[index];
    channels[index].active =
        (g_snesrecomp_last_hdmaen & (uint8_t)(1u << index)) != 0;
    channels[index].indirect = channel->indirect;
    channels[index].b_address = channel->bAdr;
    channels[index].mode = channel->mode;
    channels[index].indirect_bank = channel->indBank;
    channels[index].table_address =
        (uint32_t)channel->aAdr | ((uint32_t)channel->aBank << 16);
  }
  Dkc2HdmaFrameState start;
  memcpy(start.h_scroll, g_ppu->hScroll, sizeof start.h_scroll);
  memcpy(start.v_scroll, g_ppu->vScroll, sizeof start.v_scroll);
  start.main_layers = g_ppu->screenEnabled[0];
  start.sub_layers = g_ppu->screenEnabled[1];
  start.scroll_prev = g_ppu->scrollPrev;
  start.scroll_prev2 = g_ppu->scrollPrev2;
  const Dkc2HdmaMemory memory = {
      Dkc2HdmaPointer, Dkc2HdmaReadable, NULL};
  Dkc2HdmaScanBands(channels, &start, &memory, bands);
}

/* Decide, for every wide BG1/BG2 layer and every scanline band, whether the
 * layer displays the streamed world map (terrain phase, relative to the
 * scroll the owner rendered at the frame anchor) or a bounded effect plane.
 * Either physical layer may hold either role in any band. */
static void Dkc2ClassifyBands(uint8_t wide_layer_mask,
                              int terrain_layer,
                              const Dkc2HdmaBands *bands,
                              uint8_t policy[2][kDkc2HdmaMaxBands],
                              bool alias_layer[2]) {
  const bool have_owner = terrain_layer >= 0 && terrain_layer < 2;
  const uint16_t terrain_h =
      have_owner ? g_ppu->hScroll[terrain_layer] : 0;
  const uint16_t terrain_v =
      have_owner ? g_ppu->vScroll[terrain_layer] : 0;
  for (int layer = 0; layer < 2; layer++) {
    alias_layer[layer] = false;
    const bool wide = (wide_layer_mask & (uint8_t)(1u << layer)) != 0;
    for (int index = 0; index < bands->count; index++) {
      const Dkc2HdmaBand *band = &bands->band[index];
      const bool world =
          have_owner && wide &&
          Dkc2VideoScrollAtTerrainPhase(
              band->h_scroll[layer], band->v_scroll[layer],
              terrain_h, terrain_v);
      policy[layer][index] =
          world ? kDkc2BandPolicyWorld : kDkc2BandPolicyRepeat;
      if (world && layer != terrain_layer)
        alias_layer[layer] = true;
    }
  }
}

static void Dkc2ApplyBandPolicies(const Dkc2HdmaBand *band,
                                  int band_index,
                                  uint8_t wide_layer_mask) {
  for (unsigned layer = 0; layer < 2; layer++) {
    if (!(wide_layer_mask & (uint8_t)(1u << layer)))
      continue;
    if (band && band_index >= 0 &&
        s_band_policy[layer][band_index] == kDkc2BandPolicyRepeat) {
      PpuSetWidescreenLayerRepeatBand(
          g_ppu, (uint8_t)layer, band->first_line,
          (uint8_t)(band->last_line + 1u));
    } else {
      PpuSetWidescreenLayerRepeatBand(g_ppu, (uint8_t)layer, 0, 0);
    }
  }
}

void Dkc2DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};
  const Dkc2VideoLevelLayout layout =
      Dkc2VideoLevelLayoutForScene(
          Dkc2ReadWram16(0x0529), Dkc2ReadWram16(0x00d3));

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
  if (layout == kDkc2VideoLevelLayoutUnknown)
    wide_layer_mask = 0;
  const bool extend_world = wide_layer_mask != 0;
  int presentation_bias = 0;
  bool band_policies_active = false;
  /* Reset host presentation latches before deriving the current frame. A
   * prior gameplay scene must not leave a physically wide BG3 enabled on a
   * bounded title, menu, or unsupported layout. */
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetWidescreenBg3Widen(g_ppu, 0);
  PpuSetWidescreenPresentationXBias(g_ppu, 0);
  s_frame_bands.count = 0;
  if (extend_world) {
    const int extra = Dkc2VideoExtra();
    PpuSetExtraSpace(g_ppu, (uint8_t)extra);
    const uint16_t camera_x = Dkc2ReadWram16(0x17BA);
    const uint16_t maximum_scroll_x = Dkc2ReadWram16(0x0AFC);
    int bias = 0;
    int left_margin = extra;
    int right_margin = extra;
    Dkc2VideoPresentationMargins(
        camera_x, maximum_scroll_x, &bias, &left_margin, &right_margin);
    const int terrain_layer = Dkc2VideoTerrainLayer(
        wide_layer_mask, g_ppu->bgXsc, Dkc2ReadWram16(0x17B6));
    /* The cartridge has already built this frame's HDMA tables. Read the
     * exact scanline geometry from them before drawing. */
    Dkc2ScanFrameBands(&s_frame_bands);
    bool alias_layer[2] = {false, false};
    Dkc2ClassifyBands(wide_layer_mask, terrain_layer, &s_frame_bands,
                      s_band_policy, alias_layer);
    /* WsShadow owns only BG1/BG2 terrain. Establish exact terrain readiness
     * before allowing any additional physical layer into the final render
     * mask; this keeps 64-column HUD/staging allocations fail-closed. */
    PpuSetWidescreenLayerMask(g_ppu, wide_layer_mask);
    const bool terrain_ready = Dkc2PrepareWidescreenShadow(
        wide_layer_mask, terrain_layer, layout, bias, alias_layer);
    presentation_bias = terrain_ready ? bias : 0;
    PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias);
    if (terrain_ready)
      PpuSetExtraSideSpace(g_ppu, left_margin, right_margin, 0);
    uint8_t physical_wide_mask =
        terrain_ready
            ? Dkc2VideoPhysicalWideLayerMask(
                  g_ppu->bgmode, g_ppu->bgXsc,
                  g_ppu->screenEnabled[0], g_ppu->screenEnabled[1])
            : 0;
    /* A mirrored margin at a level wall has no authored BG3 ring columns to
     * expose. Within one margin of a wall, a physical 64-column BG3 repeats
     * its rendered line like a bounded layer instead of reading the ring. */
    if (Dkc2VideoMarginLeavesAuthoredExtent(camera_x, maximum_scroll_x))
      physical_wide_mask = (uint8_t)(physical_wide_mask & ~0x04u);
    const uint8_t render_layer_mask =
        (uint8_t)(wide_layer_mask | physical_wide_mask);
    PpuSetWidescreenLayerMask(g_ppu, render_layer_mask);
    /* The shared PPU has a separate clamp for BG3. Any enabled physical
     * 64-column BG3 may use authentic adjacent columns after terrain is
     * proven ready. */
    PpuSetWidescreenBg3Widen(
        g_ppu, (physical_wide_mask & 0x04u) != 0 ? 1u : 0u);
    /*
     * Every enabled bounded (32-column) background repeats its rendered
     * native scanline into the margins. That is exactly what a wider PPU
     * would draw from a map that wraps at 256 pixels, including its HDMA
     * phase, windows, and color-math participation. Rolling 64-column
     * layers are handled per scanline band below.
     */
    PpuSetWidescreenLayerRepeat(
        g_ppu, Dkc2VideoRepeatLayerMask(
                   g_ppu->bgmode, g_ppu->screenEnabled[0],
                   g_ppu->screenEnabled[1], render_layer_mask));
    /*
     * A 32-column map wraps at 256 pixels on hardware, so its rendered line
     * repeats at exactly that period and shows whatever seam the authored
     * plane has at its wrap, as the console does once the layer scrolls.
     * Only a bounded backdrop kept in a 64-column allocation (the ship-hold
     * cabin wall) has no hardware wrap to fall back on; those lines
     * continue at the period their own rendered interior proves, and their
     * seven stale fine-scroll endpoints are rebuilt from that period.
     */
    PpuSetWidescreenLayerRepeatAutoPeriod(g_ppu, wide_layer_mask,
                                          wide_layer_mask);
    if (terrain_ready) {
      band_policies_active = true;
    } else {
      /* An unproven rolling layer shows no margin content at all rather
       * than raw recycled VRAM. Bounded layers still repeat. */
      PpuSetWidescreenLayerClamp(g_ppu, wide_layer_mask);
    }
    Dkc2VideoSetTerrainReady(terrain_ready);
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

  const Dkc2HdmaBand *current_band = NULL;
  for (int line = 0; line <= 224; line++) {
    if (band_policies_active) {
      const Dkc2HdmaBand *band = Dkc2HdmaBandForLine(&s_frame_bands, line);
      if (band != current_band) {
        current_band = band;
        Dkc2ApplyBandPolicies(
            band, band ? (int)(band - s_frame_bands.band) : -1,
            wide_layer_mask);
      }
    }
    if (presentation_bias != 0) {
      for (unsigned layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] + presentation_bias);
    }
    ppu_runLine(g_ppu, line);
    if (presentation_bias != 0) {
      for (unsigned layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] - presentation_bias);
    }
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }
  if (band_policies_active) {
    for (unsigned layer = 0; layer < 3; layer++)
      PpuSetWidescreenLayerRepeatBand(g_ppu, (uint8_t)layer, 0, 0);
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
