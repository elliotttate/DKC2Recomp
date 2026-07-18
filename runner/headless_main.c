#include "dkc2_game.h"
#include "verified_rom.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "sha256.h"
#include "snes/ppu.h"
#include "snes/apu.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void PrintHash(FILE *stream, const uint8_t hash[32]) {
  for (int i = 0; i < 32; i++) fprintf(stream, "%02x", hash[i]);
}

static uint16_t ReadWram16(size_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static void StoreLe16(uint8_t **cursor, uint16_t value) {
  *(*cursor)++ = (uint8_t)value;
  *(*cursor)++ = (uint8_t)(value >> 8);
}

static void StoreLe32(uint8_t **cursor, uint32_t value) {
  StoreLe16(cursor, (uint16_t)value);
  StoreLe16(cursor, (uint16_t)(value >> 16));
}

static int WriteFramePpm(const char *path, const uint8_t *pixels,
                         size_t width, size_t height, size_t pitch) {
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fprintf(stream, "P6\n%zu %zu\n255\n", width, height) > 0;
  for (size_t y = 0; ok && y < height; y++) {
    const uint8_t *row = pixels + y * pitch;
    for (size_t x = 0; ok && x < width; x++) {
      const uint8_t rgb[3] = { row[x * 4 + 2], row[x * 4 + 1],
                               row[x * 4] };
      ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
    }
  }
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

static unsigned long long s_trace_pc_hits;
static uint32_t s_trace_path[32];
static size_t s_trace_path_count;
static size_t s_trace_path_index;

static void TracePc(CpuState *cpu, uint32_t pc24) {
  s_trace_pc_hits++;
  if (s_trace_pc_hits <= 16 ||
      (s_trace_pc_hits & (s_trace_pc_hits - 1)) == 0) {
    fprintf(stderr,
            "dkc2_trace_pc hit=%llu frame=%d pc=$%06x a=$%04x x=$%04x "
            "y=$%04x s=$%04x db=$%02x p=$%02x continuation=$%04x "
            "dispatcher=$%04x intro_state=$%04x dp42=$%04x dp44=$%04x "
            "dp46=$%04x dp48=$%04x "
            "links30/60/70/80/90/a0/b0/d0=$%04x/$%04x/$%04x/$%04x/"
            "$%04x/$%04x/$%04x/$%04x\n",
            s_trace_pc_hits, snes_frame_counter, (unsigned)pc24, cpu->A,
            cpu->X, cpu->Y, cpu->S, cpu->DB, cpu->P,
            (unsigned)(g_ram[0x20] | ((unsigned)g_ram[0x21] << 8)),
            (unsigned)(g_ram[0x24] | ((unsigned)g_ram[0x25] << 8)),
            (unsigned)(g_ram[0x2a] | ((unsigned)g_ram[0x2b] << 8)),
            (unsigned)(g_ram[0x42] | ((unsigned)g_ram[0x43] << 8)),
            (unsigned)(g_ram[0x44] | ((unsigned)g_ram[0x45] << 8)),
            (unsigned)(g_ram[0x46] | ((unsigned)g_ram[0x47] << 8)),
            (unsigned)(g_ram[0x48] | ((unsigned)g_ram[0x49] << 8)),
            (unsigned)(g_ram[0x1a670] | ((unsigned)g_ram[0x1a671] << 8)),
            (unsigned)(g_ram[0x1a6a0] | ((unsigned)g_ram[0x1a6a1] << 8)),
            (unsigned)(g_ram[0x1a6b0] | ((unsigned)g_ram[0x1a6b1] << 8)),
            (unsigned)(g_ram[0x1a6c0] | ((unsigned)g_ram[0x1a6c1] << 8)),
            (unsigned)(g_ram[0x1a6d0] | ((unsigned)g_ram[0x1a6d1] << 8)),
            (unsigned)(g_ram[0x1a6e0] | ((unsigned)g_ram[0x1a6e1] << 8)),
            (unsigned)(g_ram[0x1a6f0] | ((unsigned)g_ram[0x1a6f1] << 8)),
            (unsigned)(g_ram[0x1a710] | ((unsigned)g_ram[0x1a711] << 8)));
    fflush(stderr);
  }
  if (s_trace_path_index + 1 < s_trace_path_count) {
    s_trace_path_index++;
    s_trace_pc_hits = 0;
    interp_bridge_set_pre_opcode_hook(s_trace_path[s_trace_path_index],
                                      TracePc);
    fprintf(stderr, "dkc2_trace_path next=$%06x\n",
            (unsigned)s_trace_path[s_trace_path_index]);
    fflush(stderr);
  }
}

static int ArmTracePath(const char *text) {
  const char *cursor = text;
  while (*cursor) {
    if (s_trace_path_count == sizeof s_trace_path / sizeof s_trace_path[0])
      return 0;
    char *end = NULL;
    unsigned long pc = strtoul(cursor, &end, 16);
    if (end == cursor || pc > 0xfffffful || (*end != ',' && *end != '\0'))
      return 0;
    s_trace_path[s_trace_path_count++] = (uint32_t)pc;
    if (*end == '\0') break;
    cursor = end + 1;
  }
  if (!s_trace_path_count) return 0;
  interp_bridge_set_pre_opcode_hook(s_trace_path[0], TracePc);
  fprintf(stderr, "dkc2_trace_path armed=%zu first=$%06x\n",
          s_trace_path_count, (unsigned)s_trace_path[0]);
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: dkc2_snesrecomp_headless <rom.sfc> [frames]\n");
    return 2;
  }
  long frame_limit = argc == 3 ? strtol(argv[2], NULL, 10) : 600;
  if (frame_limit < 1 || frame_limit > 1000000) {
    fprintf(stderr, "frames must be between 1 and 1000000\n");
    return 2;
  }

  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc2ReadVerifiedRom(argv[1], &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    fprintf(stderr, "%s: %s\n", rom_error, argv[1]);
    return 2;
  }

  RtlRegisterGame(Dkc2GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fprintf(stderr, "snesrecomp rejected the verified ROM\n");
    free(rom);
    return 4;
  }

  const char *trace_path_text = getenv("DKC2_TRACE_PATH");
  const char *trace_pc_text = getenv("DKC2_TRACE_PC");
  if (trace_path_text && *trace_path_text) {
    if (!ArmTracePath(trace_path_text)) {
      fprintf(stderr,
              "DKC2_TRACE_PATH must be a comma-separated list of 24-bit "
              "hexadecimal addresses\n");
      free(rom);
      return 2;
    }
  } else if (trace_pc_text && *trace_pc_text) {
    char *end = NULL;
    unsigned long trace_pc = strtoul(trace_pc_text, &end, 16);
    if (!end || *end != '\0' || trace_pc > 0xfffffful) {
      fprintf(stderr, "DKC2_TRACE_PC must be a 24-bit hexadecimal address\n");
      free(rom);
      return 2;
    }
    interp_bridge_set_pre_opcode_hook((uint32_t)trace_pc, TracePc);
    fprintf(stderr, "dkc2_trace_pc armed=$%06lx\n", trace_pc);
  }

  enum { kWidth = 256, kHeight = 224, kBytesPerPixel = 4 };
  static uint8_t pixels[kWidth * kHeight * kBytesPerPixel];
  Dkc2BeginDrawing(pixels, kWidth * kBytesPerPixel);

  enum { kMaximumAudioFramesPerVideoFrame = 534 };
  int16_t audio[kMaximumAudioFramesPerVideoFrame * 2];
  /* Snes9x reports 60.098811862 Hz and the SNES DSP produces 32,040 stereo
   * frames per second. Carry the fraction between host frames instead of
   * consuming 534 frames unconditionally (which runs audio about 0.16% fast). */
  const double audio_frames_per_video_frame = 32040.0 / 60.098811862;
  double audio_frame_accumulator = 0.0;
  unsigned long long audio_rendered_frames = 0;
  uint64_t audio_fnv1a = UINT64_C(14695981039346656037);
  FILE *audio_pcm = NULL;
  const char *audio_pcm_path = getenv("DKC2_AUDIO_PCM");
  if (audio_pcm_path && *audio_pcm_path) {
    audio_pcm = fopen(audio_pcm_path, "wb");
    if (!audio_pcm) {
      fprintf(stderr, "unable to open private audio output: %s\n",
              audio_pcm_path);
      free(rom);
      return 10;
    }
  }
  unsigned long video_active_frames = 0;
  unsigned long blank_frames = 0;
  unsigned long consecutive_blank_frames = 0;
  unsigned long max_consecutive_blank_frames = 0;
  unsigned long audio_active_frames = 0;
  unsigned long audio_silent_frames = 0;
  unsigned long long audio_nonzero_samples = 0;
  unsigned long long audio_clipped_samples = 0;
  unsigned long long audio_zero_frames = 0;
  unsigned long long consecutive_audio_zero_frames = 0;
  unsigned long long max_consecutive_audio_zero_frames = 0;
  unsigned audio_peak = 0;
  unsigned audio_max_delta = 0;
  int previous_audio_samples[2] = { 0, 0 };
  int audio_sample_history_initialized = 0;
  int state_trace = 0;
  const char *state_trace_text = getenv("DKC2_STATE_TRACE");
  if (state_trace_text && *state_trace_text && *state_trace_text != '0')
    state_trace = 1;
  int state_initialized = 0;
  uint16_t previous_game_mode = 0;
  uint16_t previous_demo_status = 0;
  uint16_t previous_demo_sequence = 0;
  uint16_t previous_level = 0;
  uint16_t previous_game_sub_mode = 0;
  unsigned title_entries = 0;
  unsigned demo_starts = 0;
  unsigned demo_ends = 0;
  unsigned attract_cycles = 0;
  unsigned attract_sequence_errors = 0;
  enum { kStateEventSize = 54, kMaxStateEvents = 128 };
  uint8_t state_event_bytes[kStateEventSize * kMaxStateEvents];
  size_t state_event_count = 0;
  /* Input playback (dev, env SNESRECOMP_INPUT_PLAY=<path>): one hex controller
   * mask per line, indexed by emulation frame. Lets a desktop-recorded run
   * (SNESRECOMP_INPUT_REC) be replayed deterministically here so a gameplay-path
   * bug can be delta-debugged. Frames past EOF play neutral (0). */
  static unsigned short *s_input_play = NULL;
  static long s_input_play_n = 0;
  {
    const char *p = getenv("SNESRECOMP_INPUT_PLAY");
    if (p && p[0]) {
      FILE *f = fopen(p, "r");
      if (f) {
        long cap = 65536; s_input_play = malloc(cap * sizeof(unsigned short));
        unsigned v;
        while (fscanf(f, "%x", &v) == 1) {
          if (s_input_play_n >= cap) {
            cap *= 2; s_input_play = realloc(s_input_play, cap * sizeof(unsigned short));
          }
          s_input_play[s_input_play_n++] = (unsigned short)(v & 0xfff);
        }
        fclose(f);
        fprintf(stderr, "input_play: loaded %ld frames from %s\n", s_input_play_n, p);
      }
    }
  }
  for (long frame = 0; frame < frame_limit; frame++) {
    unsigned short _in = (s_input_play && frame < s_input_play_n) ? s_input_play[frame] : 0;
    RtlRunFrame(_in);
    if (g_fail) {
      fprintf(stderr,
              "snesrecomp reported an off-rails runtime failure at host "
              "frame %ld resume=$%06x\n",
              frame, (unsigned)Dkc2ResumePc());
      if (audio_pcm) fclose(audio_pcm);
      free(rom);
      return 6;
    }
    if (!Dkc2LastLleResult()) {
      uint8_t aram_hash[32];
      sha256_compute(g_snes->apu->ram, sizeof g_snes->apu->ram, aram_hash);
      fprintf(stderr,
              "LLE stopped at host frame %ld resume=$%06x x=$%04x "
              "upload=%02x:%02x%02x target=$%02x%02x words=$%02x%02x "
              "transaction=$%02x "
              "apu_in=%02x%02x%02x%02x apu_out=%02x%02x%02x%02x "
              "spc_pc=$%04x spc_a=$%02x spc_x=$%02x spc_y=$%02x "
              "ipl=%d\n",
              frame, (unsigned)Dkc2ResumePc(), g_cpu.X,
              g_ram[0x34], g_ram[0x33], g_ram[0x32],
              g_ram[0x36], g_ram[0x35], g_ram[0x38], g_ram[0x37],
              g_ram[0x00],
              g_snes->apu->inPorts[3], g_snes->apu->inPorts[2],
              g_snes->apu->inPorts[1], g_snes->apu->inPorts[0],
              g_snes->apu->outPorts[3], g_snes->apu->outPorts[2],
              g_snes->apu->outPorts[1], g_snes->apu->outPorts[0],
              g_snes->apu->spc->pc, g_snes->apu->spc->a,
              g_snes->apu->spc->x, g_snes->apu->spc->y,
              g_snes->apu->romReadable ? 1 : 0);
      fprintf(stderr, "aram_sha256=");
      PrintHash(stderr, aram_hash);
      fprintf(stderr, "\n");
      if (audio_pcm) fclose(audio_pcm);
      free(rom);
      return 5;
    }
    Dkc2DrawPpuFrame();

    /* Stable gameplay-state telemetry for attract-mode validation. These
     * addresses are metadata from the independently rebuilt v1.0 map; no ROM
     * or extracted payload is embedded here. Emit only transitions so a long
     * neutral-input run remains small and reproducible. */
    uint16_t game_mode = ReadWram16(0x24);
    uint16_t demo_status = ReadWram16(0x05fb);
    uint16_t demo_sequence = ReadWram16(0x0605);
    uint16_t level = ReadWram16(0x00d3);
    uint16_t game_sub_mode = ReadWram16(0x0096);
    int state_changed = !state_initialized ||
                        game_mode != previous_game_mode ||
                        demo_status != previous_demo_status ||
                        demo_sequence != previous_demo_sequence ||
                        level != previous_level ||
                        game_sub_mode != previous_game_sub_mode;
    if (state_initialized) {
      if (game_mode != previous_game_mode && game_mode == 0xb397)
        title_entries++;
      if (previous_demo_status == 0 && demo_status != 0 &&
          game_mode == 0x87e1) {
        static const uint16_t kExpectedDemoLevels[3] = {
          0x000c, 0x000f, 0x0013
        };
        demo_starts++;
        unsigned expected = (demo_starts - 1) % 3;
        if (demo_sequence != expected + 1 ||
            level != kExpectedDemoLevels[expected])
          attract_sequence_errors++;
      }
      if (previous_demo_status != 0 && demo_status == 0 &&
          previous_game_mode == 0x8819)
        demo_ends++;
      if (previous_demo_sequence == 3 && demo_sequence == 0 &&
          demo_status == 0)
        attract_cycles++;
    }
    if (state_changed && state_event_count < kMaxStateEvents) {
      uint8_t *event = state_event_bytes +
                       state_event_count * kStateEventSize;
      uint8_t *cursor = event;
      uint8_t event_frame_hash[32];
      StoreLe32(&cursor, (uint32_t)(frame + 1));
      StoreLe16(&cursor, game_mode);
      StoreLe16(&cursor, game_sub_mode);
      StoreLe16(&cursor, demo_status);
      StoreLe16(&cursor, demo_sequence);
      StoreLe16(&cursor, ReadWram16(0x05fd));
      StoreLe16(&cursor, ReadWram16(0x05ff));
      StoreLe16(&cursor, level);
      StoreLe16(&cursor, ReadWram16(0x002a));
      StoreLe16(&cursor, ReadWram16(0x0020));
      sha256_compute(pixels, sizeof pixels, event_frame_hash);
      memcpy(cursor, event_frame_hash, sizeof event_frame_hash);
      state_event_count++;
    }
    if (state_trace && state_changed) {
      fprintf(stderr,
              "state_event frame=%ld game_mode=$%04x game_sub_mode=$%04x "
              "demo_status=$%04x demo_sequence=$%04x demo_index=$%04x "
              "demo_timer=$%04x level=$%04x active_frame=$%04x "
              "continuation=$%04x\n",
              frame + 1, game_mode, game_sub_mode, demo_status, demo_sequence,
              ReadWram16(0x05fd), ReadWram16(0x05ff), level,
              ReadWram16(0x002a), ReadWram16(0x0020));
    }
    previous_game_mode = game_mode;
    previous_demo_status = demo_status;
    previous_demo_sequence = demo_sequence;
    previous_level = level;
    previous_game_sub_mode = game_sub_mode;
    state_initialized = 1;

    int frame_active = 0;
    for (size_t i = 0; i < sizeof pixels; i++) {
      if (pixels[i] != 0) {
        frame_active = 1;
        break;
      }
    }
    if (frame_active) {
      video_active_frames++;
      consecutive_blank_frames = 0;
    } else {
      blank_frames++;
      consecutive_blank_frames++;
      if (consecutive_blank_frames > max_consecutive_blank_frames)
        max_consecutive_blank_frames = consecutive_blank_frames;
    }

    audio_frame_accumulator += audio_frames_per_video_frame;
    int audio_frames_this_frame = (int)audio_frame_accumulator;
    audio_frame_accumulator -= audio_frames_this_frame;
    if (audio_frames_this_frame < 0 ||
        audio_frames_this_frame > kMaximumAudioFramesPerVideoFrame) {
      fprintf(stderr, "invalid audio frame request: %d\n",
              audio_frames_this_frame);
      if (audio_pcm) fclose(audio_pcm);
      free(rom);
      return 11;
    }
    size_t audio_samples_this_frame =
        (size_t)audio_frames_this_frame * 2u;
    memset(audio, 0, audio_samples_this_frame * sizeof audio[0]);
    RtlRenderAudio(audio, audio_frames_this_frame, 2);
    audio_rendered_frames += (unsigned)audio_frames_this_frame;
    int audio_active = 0;
    for (size_t i = 0; i < audio_samples_this_frame; i++) {
      int sample = audio[i];
      unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
      unsigned channel = (unsigned)(i & 1u);
      if (magnitude != 0) {
        audio_active = 1;
        audio_nonzero_samples++;
        if (magnitude > audio_peak) audio_peak = magnitude;
      }
      if (magnitude >= 32760u) audio_clipped_samples++;
      if (audio_sample_history_initialized) {
        int delta = sample - previous_audio_samples[channel];
        unsigned delta_magnitude = (unsigned)(delta < 0 ? -delta : delta);
        if (delta_magnitude > audio_max_delta)
          audio_max_delta = delta_magnitude;
      }
      previous_audio_samples[channel] = sample;
      if (channel == 1) audio_sample_history_initialized = 1;
      audio_fnv1a ^= (uint8_t)(sample & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
      audio_fnv1a ^= (uint8_t)(((uint16_t)sample >> 8) & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
    }
    for (int i = 0; i < audio_frames_this_frame; i++) {
      if (audio[i * 2] == 0 && audio[i * 2 + 1] == 0) {
        audio_zero_frames++;
        consecutive_audio_zero_frames++;
        if (consecutive_audio_zero_frames >
            max_consecutive_audio_zero_frames) {
          max_consecutive_audio_zero_frames =
              consecutive_audio_zero_frames;
        }
      } else {
        consecutive_audio_zero_frames = 0;
      }
    }
    if (audio_pcm &&
        fwrite(audio, sizeof audio[0], audio_samples_this_frame, audio_pcm) !=
            audio_samples_this_frame) {
      fprintf(stderr, "unable to write private audio output: %s\n",
              audio_pcm_path);
      fclose(audio_pcm);
      free(rom);
      return 12;
    }
    if (audio_active)
      audio_active_frames++;
    else
      audio_silent_frames++;
    if ((frame + 1) % 60 == 0 || frame + 1 == frame_limit) {
      printf("heartbeat frame=%ld resume=$%06x cpu_pb=$%02x "
             "beam=%u:%u lle=%d\n",
             frame + 1, (unsigned)Dkc2ResumePc(), g_cpu.PB,
             g_snes->vPos, g_snes->hPos, Dkc2LastLleResult());
      fflush(stdout);
    }
  }

  if (audio_pcm && fclose(audio_pcm) != 0) {
    fprintf(stderr, "unable to close private audio output: %s\n",
            audio_pcm_path);
    free(rom);
    return 13;
  }
  audio_pcm = NULL;

  uint8_t frame_hash[32];
  uint8_t wram_hash[32];
  uint8_t vram_hash[32];
  uint8_t cgram_hash[32];
  uint8_t oam_hash[32];
  uint8_t oam_source_hash[32];
  uint8_t state_event_hash[32];
  uint8_t oam_bytes[544];
  unsigned bg_pixels = 0;
  unsigned vram_words = 0;
  unsigned cgram_words = 0;
  for (size_t i = 0; i < sizeof g_ppu->vram / sizeof g_ppu->vram[0]; i++)
    if (g_ppu->vram[i] != 0) vram_words++;
  for (size_t i = 0; i < sizeof g_ppu->cgram / sizeof g_ppu->cgram[0]; i++)
    if (g_ppu->cgram[i] != 0) cgram_words++;
  for (size_t i = 0; i < 256; i++)
    if ((g_ppu->bgBuffers[0].data[i + kPpuExtraLeftRight] & 0xff) != 0)
      bg_pixels++;
  sha256_compute(pixels, sizeof pixels, frame_hash);
  sha256_compute(g_ram, 0x20000, wram_hash);
  sha256_compute((const uint8_t *)g_ppu->vram, sizeof g_ppu->vram, vram_hash);
  sha256_compute((const uint8_t *)g_ppu->cgram, sizeof g_ppu->cgram, cgram_hash);
  memcpy(oam_bytes, g_ppu->oam, sizeof g_ppu->oam);
  memcpy(oam_bytes + sizeof g_ppu->oam, g_ppu->highOam,
         sizeof g_ppu->highOam);
  sha256_compute(oam_bytes, sizeof oam_bytes, oam_hash);
  /* DKC2 builds its complete low/high OAM image at WRAM $0200-$041f and
   * transfers all 544 bytes to $2104 during VBlank. Keeping both hashes in
   * the private integration output distinguishes bad game logic/source data
   * from a stale PPU OAM-port destination. */
  sha256_compute(g_ram + 0x200, sizeof oam_bytes, oam_source_hash);
  sha256_compute(state_event_bytes, state_event_count * kStateEventSize,
                 state_event_hash);
  printf("video_state inidisp=$%02x bgmode=$%02x main=$%02x sub=$%02x "
         "nmi=%d in_nmi=%d frame_counter=%d bg_pixels=%u "
         "vram_words=%u cgram_words=%u brightness31=%u "
         "continuation=$%04x intro_state=$%04x\n",
         g_ppu->inidisp, g_ppu->bgmode, g_ppu->screenEnabled[0],
         g_ppu->screenEnabled[1], g_snes->nmiEnabled ? 1 : 0,
         g_snes->inNmi ? 1 : 0, snes_frame_counter, bg_pixels,
         vram_words, cgram_words, g_ppu->brightnessMult[31],
         (unsigned)(g_ram[0x20] | ((unsigned)g_ram[0x21] << 8)),
         (unsigned)(g_ram[0x2a] | ((unsigned)g_ram[0x2b] << 8)));
  printf("frame_sha256=");
  PrintHash(stdout, frame_hash);
  printf("\nwram_sha256=");
  PrintHash(stdout, wram_hash);
  printf("\nvram_sha256=");
  PrintHash(stdout, vram_hash);
  printf("\ncgram_sha256=");
  PrintHash(stdout, cgram_hash);
  printf("\noam_sha256=");
  PrintHash(stdout, oam_hash);
  printf("\noam_source_sha256=");
  PrintHash(stdout, oam_source_hash);
  printf("\nstate_event_sha256=");
  PrintHash(stdout, state_event_hash);
  printf("\nrun_stats video_active_frames=%lu blank_frames=%lu "
         "max_consecutive_blank_frames=%lu audio_active_frames=%lu "
         "audio_silent_frames=%lu audio_frames=%llu "
         "audio_nonzero_samples=%llu audio_zero_frames=%llu "
         "max_consecutive_audio_zero_frames=%llu "
         "audio_clipped_samples=%llu audio_peak=%u audio_max_delta=%u "
         "audio_fnv1a=%016llx",
         video_active_frames, blank_frames, max_consecutive_blank_frames,
         audio_active_frames, audio_silent_frames, audio_rendered_frames,
         audio_nonzero_samples, audio_zero_frames,
         max_consecutive_audio_zero_frames, audio_clipped_samples, audio_peak,
         audio_max_delta,
         (unsigned long long)audio_fnv1a);
  printf("\nstate_stats events=%zu title_entries=%u demo_starts=%u "
         "demo_ends=%u attract_cycles=%u sequence_errors=%u "
         "demo_status=$%04x demo_sequence=$%04x level=$%04x",
         state_event_count, title_entries, demo_starts, demo_ends,
         attract_cycles, attract_sequence_errors, ReadWram16(0x05fb),
         ReadWram16(0x0605), ReadWram16(0x00d3));
  const char *frame_output = getenv("DKC2_FRAME_PPM");
  if (frame_output && *frame_output) {
    if (!WriteFramePpm(frame_output, pixels, kWidth, kHeight,
                       kWidth * kBytesPerPixel)) {
      fprintf(stderr, "\nunable to write private frame output: %s\n",
              frame_output);
      free(rom);
      return 7;
    }
    printf("\nframe_output=%s", frame_output);
  }
  const char *oam_output = getenv("DKC2_OAM_OUTPUT");
  if (oam_output && *oam_output) {
    FILE *stream = fopen(oam_output, "wb");
    int oam_ok = stream && fwrite(oam_bytes, 1, sizeof oam_bytes, stream) ==
                               sizeof oam_bytes;
    if (stream && fclose(stream) != 0) oam_ok = 0;
    if (!oam_ok) {
      fprintf(stderr, "\nunable to write private OAM output: %s\n",
              oam_output);
      free(rom);
      return 8;
    }
    printf("\noam_output=%s", oam_output);
  }
  const char *wram_output = getenv("DKC2_WRAM_OUTPUT");
  if (wram_output && *wram_output) {
    FILE *stream = fopen(wram_output, "wb");
    int wram_ok = stream && fwrite(g_ram, 1, 0x20000, stream) == 0x20000;
    if (stream && fclose(stream) != 0) wram_ok = 0;
    if (!wram_ok) {
      fprintf(stderr, "\nunable to write private WRAM output: %s\n",
              wram_output);
      free(rom);
      return 9;
    }
    printf("\nwram_output=%s", wram_output);
  }
  const char *vram_output = getenv("DKC2_VRAM_OUTPUT");
  if (vram_output && *vram_output) {
    FILE *stream = fopen(vram_output, "wb");
    size_t vbytes = sizeof g_ppu->vram;
    int vram_ok = stream && fwrite(g_ppu->vram, 1, vbytes, stream) == vbytes;
    if (stream && fclose(stream) != 0) vram_ok = 0;
    if (!vram_ok) {
      fprintf(stderr, "\nunable to write private VRAM output: %s\n",
              vram_output);
      free(rom);
      return 9;
    }
    printf("\nvram_output=%s", vram_output);
  }
  if (audio_pcm_path && *audio_pcm_path) {
    printf("\naudio_output=%s", audio_pcm_path);
  }
  printf("\nresult=completed frames=%ld\n", frame_limit);
  free(rom);
  return 0;
}
