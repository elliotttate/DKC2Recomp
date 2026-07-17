#include "dkc2_game.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

#include <stdbool.h>

enum {
  kDkc2ResetPc = 0x0083F7,
  kDkc2NmiPc = 0x00F37D,
};

static bool s_cpu_initialized;
static uint32_t s_resume_pc = kDkc2ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;

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
}

static const RtlGameInfo kDkc2GameInfo = {
  .title = "dkc2",
  .initialize = NULL,
  .run_frame = &Dkc2RunOneFrame,
  .draw_ppu_frame = &Dkc2DrawPpuFrame,
  .save_name_prefix = "dkc2",
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

void Dkc2DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};

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
