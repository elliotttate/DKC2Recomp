#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "dkc2_game.h"
#include "diagnostics.h"
#include "desktop_filter.h"
#include "desktop_fps.h"
#include "desktop_input.h"
#include "desktop_launcher.h"
#include "desktop_paths.h"
#include "desktop_present_sdl.h"
#include "desktop_rewind.h"
#include "verified_rom.h"

#include "common_rtl.h"
#include "host_report.h"
#include "launcher.h"
#include "snes/snes.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define DKC2_MKDIR(path) _mkdir(path)
#else
#define DKC2_MKDIR(path) mkdir(path, 0755)
#endif

#ifndef DKC2_RELEASE_VERSION
#define DKC2_RELEASE_VERSION "dev"
#endif

enum {
  kFrameWidth = 256,
  kFrameHeight = 224,
  kBytesPerPixel = 4,
  kAudioRate = 32040,
  kAudioChannels = 2,
  kMaximumFrameAudio = 534,
  kHostSpeedMultiplier = 3,
  kRewindSnapshotInterval = 3,
  kRewindSnapshotCapacity = 300,
  kMaximumControllers = 2,
  kPathCapacity = 4096,
};

static const double kVideoRate = 60.098811862;

typedef struct SdlHost {
  Dkc2SdlPresenter presenter;
  Dkc2DesktopColorFilter color_filter;
  SDL_AudioDeviceID audio_device;
  SDL_GameController *controllers[kMaximumControllers];
  uint8_t pixels[kFrameWidth * kFrameHeight * kBytesPerPixel];
  uint8_t filtered_pixels[kFrameWidth * kFrameHeight * kBytesPerPixel];
  int16_t scaled_audio[kMaximumFrameAudio * kAudioChannels];
  int player_source[kDkc2DesktopPlayerCount];
  int player_deadzone[kDkc2DesktopPlayerCount];
  int audio_volume;
  bool audio_available;
  bool running;
  bool hidden;
} SdlHost;

typedef struct SdlControls {
  uint32_t controller;
  uint32_t host_actions;
} SdlControls;

typedef enum SdlSpeedMode {
  kSdlSpeedNormal,
  kSdlSpeedRewind,
  kSdlSpeedFastForward,
} SdlSpeedMode;

static bool EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static bool EnsureSaveDirectory(void) {
  if (DKC2_MKDIR("saves") == 0) return true;
  return errno == EEXIST;
}

static void ShowError(const char *message) {
  fprintf(stderr, "%s\n", message ? message : "Unknown error");
  if (SDL_WasInit(SDL_INIT_VIDEO))
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                   "Unable to start DKC2",
                                   message ? message : "Unknown error", NULL);
}

static bool WriteFramePpm(const char *path, const uint8_t *pixels) {
  FILE *stream = fopen(path, "wb");
  if (!stream) return false;
  bool ok = fprintf(stream, "P6\n%d %d\n255\n", kFrameWidth,
                    kFrameHeight) > 0;
  for (int y = 0; ok && y < kFrameHeight; y++) {
    const uint8_t *row = pixels + y * kFrameWidth * kBytesPerPixel;
    for (int x = 0; ok && x < kFrameWidth; x++) {
      const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
      ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
    }
  }
  if (fclose(stream) != 0) ok = false;
  return ok;
}

static void CloseControllers(SdlHost *host) {
  for (int i = 0; i < kMaximumControllers; i++) {
    if (host->controllers[i]) SDL_GameControllerClose(host->controllers[i]);
    host->controllers[i] = NULL;
  }
}

static void RefreshControllers(SdlHost *host) {
  CloseControllers(host);
  int opened = 0;
  for (int device = 0;
       device < SDL_NumJoysticks() && opened < kMaximumControllers;
       device++) {
    if (!SDL_IsGameController(device)) continue;
    SDL_GameController *controller = SDL_GameControllerOpen(device);
    if (controller) host->controllers[opened++] = controller;
  }
}

static void PumpEvents(SdlHost *host) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) host->running = false;
    if (event.type == SDL_CONTROLLERDEVICEADDED ||
        event.type == SDL_CONTROLLERDEVICEREMOVED)
      RefreshControllers(host);
  }
}

static uint16_t ReadGamepadButtons(SDL_GameController *controller) {
  uint16_t buttons = 0;
#define MAP_SDL_BUTTON(sdl_button, dkc2_button)                            \
  do {                                                                     \
    if (SDL_GameControllerGetButton(controller, (sdl_button)))             \
      buttons |= (dkc2_button);                                            \
  } while (0)
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP, kDkc2GamepadDpadUp);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN, kDkc2GamepadDpadDown);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT, kDkc2GamepadDpadLeft);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kDkc2GamepadDpadRight);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_START, kDkc2GamepadStart);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_BACK, kDkc2GamepadBack);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
                 kDkc2GamepadLeftShoulder);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                 kDkc2GamepadRightShoulder);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_A, kDkc2GamepadA);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_B, kDkc2GamepadB);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_X, kDkc2GamepadX);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_Y, kDkc2GamepadY);
#undef MAP_SDL_BUTTON
  return buttons;
}

static uint8_t ReadTrigger(SDL_GameController *controller,
                           SDL_GameControllerAxis axis) {
  Sint16 value = SDL_GameControllerGetAxis(controller, axis);
  if (value <= 0) return 0;
  return (uint8_t)(((uint32_t)(uint16_t)value * 255u) / 32767u);
}

static SdlControls ReadControls(SdlHost *host) {
  SdlControls controls = {0, 0};
  SDL_Window *window = (SDL_Window *)host->presenter.window;
  if (!host->hidden && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
    return controls;
  const Uint8 *keys = SDL_GetKeyboardState(NULL);
  if (keys[SDL_SCANCODE_ESCAPE]) host->running = false;
  uint32_t keyboard = 0;
  if (keys[SDL_SCANCODE_Z]) keyboard |= 1u << 0;
  if (keys[SDL_SCANCODE_A]) keyboard |= 1u << 1;
  if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
    keyboard |= 1u << 2;
  if (keys[SDL_SCANCODE_RETURN]) keyboard |= 1u << 3;
  if (keys[SDL_SCANCODE_UP]) keyboard |= 1u << 4;
  if (keys[SDL_SCANCODE_DOWN]) keyboard |= 1u << 5;
  if (keys[SDL_SCANCODE_LEFT]) keyboard |= 1u << 6;
  if (keys[SDL_SCANCODE_RIGHT]) keyboard |= 1u << 7;
  if (keys[SDL_SCANCODE_X]) keyboard |= 1u << 8;
  if (keys[SDL_SCANCODE_S]) keyboard |= 1u << 9;
  if (keys[SDL_SCANCODE_Q]) keyboard |= 1u << 10;
  if (keys[SDL_SCANCODE_W]) keyboard |= 1u << 11;
  if (keys[SDL_SCANCODE_1]) controls.host_actions |= kDkc2HostRewind;
  if (keys[SDL_SCANCODE_2]) controls.host_actions |= kDkc2HostFastForward;
  if (keys[SDL_SCANCODE_F5]) controls.host_actions |= kDkc2HostSaveState;
  if (keys[SDL_SCANCODE_F9]) controls.host_actions |= kDkc2HostLoadState;

  Dkc2GamepadState gamepads[kMaximumControllers];
  size_t gamepad_count = 0;
  for (int i = 0; i < kMaximumControllers; i++) {
    SDL_GameController *controller = host->controllers[i];
    if (!controller || !SDL_GameControllerGetAttached(controller)) continue;
    Dkc2GamepadState *gamepad = &gamepads[gamepad_count++];
    gamepad->buttons = ReadGamepadButtons(controller);
    gamepad->left_x = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 vertical = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_LEFTY);
    gamepad->left_y = vertical == INT16_MIN ? INT16_MAX : (int16_t)-vertical;
    gamepad->left_trigger = ReadTrigger(
        controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    gamepad->right_trigger = ReadTrigger(
        controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  }
  uint32_t gamepad_actions = 0;
  controls.controller = Dkc2RoutePlayerInputs(
      keyboard, gamepads, gamepad_count, host->player_source,
      host->player_deadzone, 30, &gamepad_actions);
  controls.host_actions |= gamepad_actions;
  return controls;
}

static bool InitializeAudio(SdlHost *host) {
  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  SDL_zero(desired);
  SDL_zero(obtained);
  desired.freq = kAudioRate;
  desired.format = AUDIO_S16SYS;
  desired.channels = kAudioChannels;
  desired.samples = 2048;
  host->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
  if (!host->audio_device) return false;
  if (obtained.freq != desired.freq || obtained.format != desired.format ||
      obtained.channels != desired.channels) {
    SDL_CloseAudioDevice(host->audio_device);
    host->audio_device = 0;
    return false;
  }
  SDL_PauseAudioDevice(host->audio_device, 0);
  host->audio_available = true;
  return true;
}

static bool QueueAudio(SdlHost *host, const int16_t *samples, int frames) {
  if (!host->audio_available || frames <= 0) return true;
  size_t sample_count = (size_t)frames * kAudioChannels;
  const int16_t *output = samples;
  if (host->audio_volume != 100) {
    for (size_t i = 0; i < sample_count; i++)
      host->scaled_audio[i] =
          (int16_t)(((int)samples[i] * host->audio_volume) / 100);
    output = host->scaled_audio;
  }
  return SDL_QueueAudio(host->audio_device, output,
                        (Uint32)(sample_count * sizeof output[0])) == 0;
}

static void ResetAudio(SdlHost *host) {
  if (host->audio_device) SDL_ClearQueuedAudio(host->audio_device);
}

static void ShutdownHost(SdlHost *host) {
  CloseControllers(host);
  if (host->audio_device) SDL_CloseAudioDevice(host->audio_device);
  Dkc2SdlPresenterDestroy(&host->presenter);
  Dkc2DesktopColorFilterDestroy(&host->color_filter);
  SDL_Quit();
}

static void PaceFrame(SdlHost *host, uint64_t *deadline,
                      double *deadline_fraction) {
  uint64_t frequency = SDL_GetPerformanceFrequency();
  double ticks = (double)frequency / kVideoRate;
  *deadline_fraction += ticks;
  uint64_t whole_ticks = (uint64_t)*deadline_fraction;
  *deadline_fraction -= (double)whole_ticks;
  *deadline += whole_ticks;
  while (host->running) {
    uint64_t now = SDL_GetPerformanceCounter();
    if (now >= *deadline) {
      if (now - *deadline > frequency / 4) *deadline = now;
      return;
    }
    uint64_t remaining = *deadline - now;
    Uint32 milliseconds = (Uint32)(remaining * 1000 / frequency);
    if (milliseconds > 1) SDL_Delay(milliseconds - 1);
    else SDL_Delay(0);
    PumpEvents(host);
  }
}

static int RunGame(const char *rom_path,
                   const RecompLauncherCSettings *settings) {
  SdlHost host;
  memset(&host, 0, sizeof host);
  host.running = true;
  host.hidden = EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  host.audio_volume = ClampInt(settings->volume, 0, 100);
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    host.player_source[player] = ClampInt(settings->player_src[player], 0, 2);
    host.player_deadzone[player] = ClampInt(settings->deadzone[player], 0, 100);
  }

  unsigned long long test_frame_limit = 0;
  const char *test_frames = getenv("DKC2_DESKTOP_TEST_FRAMES");
  if (test_frames && *test_frames) {
    char *end = NULL;
    test_frame_limit = strtoull(test_frames, &end, 10);
    if (!end || *end != '\0' || test_frame_limit == 0 ||
        test_frame_limit > 1000000) {
      ShowError("DKC2_DESKTOP_TEST_FRAMES must be between 1 and 1000000");
      return 2;
    }
  }
  bool test_rewind_requested = EnvironmentEnabled("DKC2_DESKTOP_TEST_REWIND");
  bool test_fast_forward_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_FASTFORWARD");
  bool sram_enabled = !EnvironmentEnabled("DKC2_DESKTOP_DISABLE_SRAM");
  int screen_filter = ClampInt(settings->screen_kind, 0, 3);
  const char *screen_override = getenv("DKC2_SCREEN");
  if (screen_override && *screen_override &&
      !Dkc2DesktopScreenFilterFromName(screen_override, &screen_filter)) {
    ShowError("DKC2_SCREEN must be raw, crt, composite, or trinitron");
    return 2;
  }

  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc2ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    ShowError(rom_error);
    return 2;
  }
  RtlRegisterGame(Dkc2GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    free(rom);
    ShowError("snesrecomp rejected the verified ROM");
    return 3;
  }
  if (sram_enabled) {
    if (EnsureSaveDirectory()) RtlReadSram();
    else sram_enabled = false;
  }
  if (!Dkc2DesktopColorFilterInit(&host.color_filter, screen_filter)) {
    free(rom);
    ShowError("Unable to initialize the selected screen-color filter");
    return 4;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
               SDL_INIT_TIMER) != 0) {
    free(rom);
    Dkc2DesktopColorFilterDestroy(&host.color_filter);
    ShowError(SDL_GetError());
    return 4;
  }
  char video_error[256] = {0};
  if (!Dkc2SdlPresenterInit(
          &host.presenter, ClampInt(settings->window_scale, 1, 4),
          ClampInt(settings->fullscreen, 0, 2), host.hidden,
          settings->texture_filter != 0, video_error, sizeof video_error)) {
    free(rom);
    ShutdownHost(&host);
    ShowError(video_error);
    return 4;
  }
  RefreshControllers(&host);
  Dkc2BeginDrawing(host.pixels, kFrameWidth * kBytesPerPixel);
  if (settings->enable_audio && !InitializeAudio(&host)) {
    if (test_frame_limit) {
      free(rom);
      ShutdownHost(&host);
      ShowError("SDL audio could not be opened in the desktop test");
      return 6;
    }
    fprintf(stderr, "warning: SDL audio unavailable; continuing silent\n");
  }
  Dkc2DiagnosticsSetPresentation(
      Dkc2SdlPresenterBackend(&host.presenter),
      Dkc2DesktopScreenFilterName(screen_filter), host.audio_available);
  fprintf(stdout, "Video: %s, %s, %s sampling\n",
          Dkc2SdlPresenterBackend(&host.presenter),
          Dkc2DesktopScreenFilterName(screen_filter),
          settings->texture_filter ? "bilinear" : "nearest");
  fprintf(stdout,
          "Controls: arrows=D-pad, Z=B, X=A, A=Y, S=X, Enter=Start, "
          "Shift=Select, Q=L, W=R, 1=Rewind, 2=Fast-forward, "
          "F5=Save slot 0, F9=Load slot 0, Escape=Quit. SDL game "
          "controllers are detected automatically.\n");

  Dkc2DesktopFpsCounter fps_counter;
  uint64_t deadline = SDL_GetPerformanceCounter();
  uint64_t frequency = SDL_GetPerformanceFrequency();
  Dkc2DesktopFpsInit(&fps_counter, deadline);
  double deadline_fraction = 0.0;
  double audio_fraction = 0.0;
  unsigned long long host_frame = 0;
  unsigned rewind_capture_counter = 0;
  int16_t frame_audio[kMaximumFrameAudio * kAudioChannels];
  Dkc2RewindHistory rewind_history;
  memset(&rewind_history, 0, sizeof rewind_history);
  size_t rewind_snapshot_size = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *rewind_scratch = rewind_snapshot_size
      ? (uint8_t *)malloc(rewind_snapshot_size) : NULL;
  bool rewind_available =
      rewind_scratch &&
      Dkc2RewindHistoryInit(&rewind_history, rewind_snapshot_size,
                            kRewindSnapshotCapacity) &&
      RtlSaveSnapshotToMemory(rewind_scratch, rewind_snapshot_size) ==
          rewind_snapshot_size &&
      Dkc2RewindHistoryPush(&rewind_history, rewind_scratch);
  bool test_rewind_completed = false;
  bool test_fast_forward_completed = false;
  bool runtime_failure = false;
  uint32_t previous_state_actions = 0;
  SdlSpeedMode previous_mode = kSdlSpeedNormal;

  while (host.running) {
    PumpEvents(&host);
    Dkc2DiagnosticsHeartbeat(host_frame, Dkc2ResumePc());
    host_report_crash_test_tick();
    SdlControls controls = ReadControls(&host);
    uint32_t state_actions = controls.host_actions &
        (kDkc2HostSaveState | kDkc2HostLoadState);
    uint32_t pressed_state_actions = state_actions & ~previous_state_actions;
    previous_state_actions = state_actions;
    if (pressed_state_actions & kDkc2HostSaveState) {
      bool saved = EnsureSaveDirectory() &&
                   RtlSaveSnapshot(DKC2_STATE_SLOT0_FILE);
      Dkc2SdlPresenterSetTitle(&host.presenter,
          saved ? "DKC2Recomp - Slot 0 saved" : "DKC2Recomp - Save failed");
    }
    if (pressed_state_actions & kDkc2HostLoadState) {
      if (RtlLoadSnapshot(DKC2_STATE_SLOT0_FILE) ||
          RtlLoadSnapshot(DKC2_STATE_SLOT0_LEGACY_FILE)) {
        ResetAudio(&host);
        audio_fraction = 0.0;
        deadline = SDL_GetPerformanceCounter();
        deadline_fraction = 0.0;
        rewind_history.count = 0;
        rewind_history.write_index = 0;
        rewind_capture_counter = 0;
        if (rewind_available &&
            RtlSaveSnapshotToMemory(rewind_scratch, rewind_snapshot_size) ==
                rewind_snapshot_size)
          (void)Dkc2RewindHistoryPush(&rewind_history, rewind_scratch);
        Dkc2DrawPpuFrame();
      }
    }
    if (test_fast_forward_requested && !test_fast_forward_completed &&
        host_frame >= 60)
      controls.host_actions |= kDkc2HostFastForward;
    if (test_rewind_requested && !test_rewind_completed && host_frame >= 120)
      controls.host_actions |= kDkc2HostRewind;

    SdlSpeedMode mode = kSdlSpeedNormal;
    if (controls.host_actions & kDkc2HostRewind) mode = kSdlSpeedRewind;
    else if (controls.host_actions & kDkc2HostFastForward)
      mode = kSdlSpeedFastForward;
    if (mode != previous_mode) {
      ResetAudio(&host);
      audio_fraction = 0.0;
      deadline = SDL_GetPerformanceCounter();
      deadline_fraction = 0.0;
      previous_mode = mode;
    }

    bool frame_ready = false;
    if (mode == kSdlSpeedRewind) {
      if (rewind_available &&
          Dkc2RewindHistoryPop(&rewind_history, rewind_scratch)) {
        if (!RtlLoadSnapshotFromMemory(rewind_scratch, rewind_snapshot_size)) {
          runtime_failure = true;
          break;
        }
        Dkc2DrawPpuFrame();
        frame_ready = true;
        if (test_rewind_requested) test_rewind_completed = true;
      }
    } else {
      int frames_to_run = mode == kSdlSpeedFastForward
          ? kHostSpeedMultiplier : 1;
      unsigned long long iteration_start = host_frame;
      for (int run = 0; run < frames_to_run && host.running; run++) {
        (void)RtlRunFrame(controls.controller);
        if (g_fail || !Dkc2LastLleResult()) {
          fprintf(stderr, "Runtime stopped at frame %llu (resume PC $%06x).\n",
                  host_frame + 1, (unsigned)Dkc2ResumePc());
          Dkc2DiagnosticsFatal("native runtime stopped unexpectedly");
          runtime_failure = true;
          break;
        }
        host_frame++;
        rewind_capture_counter++;
        if (rewind_available &&
            rewind_capture_counter >= kRewindSnapshotInterval) {
          rewind_capture_counter = 0;
          if (RtlSaveSnapshotToMemory(rewind_scratch, rewind_snapshot_size) !=
                  rewind_snapshot_size ||
              !Dkc2RewindHistoryPush(&rewind_history, rewind_scratch))
            rewind_available = false;
        }
        Dkc2DrawPpuFrame();
        frame_ready = true;
        audio_fraction += (double)kAudioRate / kVideoRate;
        int audio_frames = (int)audio_fraction;
        audio_fraction -= audio_frames;
        RtlRenderAudio(frame_audio, audio_frames, kAudioChannels);
        if (mode == kSdlSpeedNormal &&
            !QueueAudio(&host, frame_audio, audio_frames)) {
          fprintf(stderr, "warning: SDL audio queue stopped\n");
          host.audio_available = false;
        }
        if (test_frame_limit && host_frame >= test_frame_limit) {
          host.running = false;
          break;
        }
      }
      if (runtime_failure) break;
      if (test_fast_forward_requested && mode == kSdlSpeedFastForward &&
          host_frame - iteration_start == kHostSpeedMultiplier)
        test_fast_forward_completed = true;
    }

    if (frame_ready) {
      const uint8_t *present_pixels = Dkc2DesktopColorFilterApply(
          &host.color_filter, host.pixels, host.filtered_pixels,
          (size_t)kFrameWidth * kFrameHeight);
      if (!present_pixels ||
          !Dkc2SdlPresenterPresent(&host.presenter, present_pixels,
                                   kFrameWidth, kFrameHeight)) {
        fprintf(stderr, "SDL video presentation failed: %s\n", SDL_GetError());
        Dkc2DiagnosticsFatal("SDL video presentation failed");
        runtime_failure = true;
        break;
      }
    }
    if (mode != kSdlSpeedNormal || !host.audio_available ||
        SDL_GetQueuedAudioSize(host.audio_device) >=
            (Uint32)(kMaximumFrameAudio * kAudioChannels * sizeof(int16_t) * 2))
      PaceFrame(&host, &deadline, &deadline_fraction);
    else {
      deadline = SDL_GetPerformanceCounter();
      deadline_fraction = 0.0;
    }
    unsigned fps = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    if (Dkc2DesktopFpsUpdate(&fps_counter, frame_ready, now, frequency, &fps)) {
      char title[112];
      (void)snprintf(title, sizeof title, "DKC2Recomp v%s (FPS: %u)",
                     DKC2_RELEASE_VERSION, fps);
      Dkc2SdlPresenterSetTitle(&host.presenter, title);
    }
  }

  if (test_rewind_requested && !test_rewind_completed) runtime_failure = true;
  if (test_fast_forward_requested && !test_fast_forward_completed)
    runtime_failure = true;
  bool completed = !runtime_failure && !g_fail && Dkc2LastLleResult();
  const char *frame_output = getenv("DKC2_FRAME_PPM");
  if (frame_output && *frame_output &&
      !WriteFramePpm(frame_output, host.pixels))
    completed = false;
  if (sram_enabled && completed) RtlWriteSram();
  Dkc2DiagnosticsShutdown(completed ? "clean_exit" : "runtime_failure");
  Dkc2RewindHistoryDestroy(&rewind_history);
  free(rewind_scratch);
  free(rom);
  ShutdownHost(&host);
  if (test_frame_limit) {
    fprintf(stdout,
            "result=desktop_completed frames=%llu rewind_restore=%s "
            "fast_forward=%s host=sdl2\n",
            host_frame,
            test_rewind_requested
                ? (test_rewind_completed ? "passed" : "failed")
                : "not_requested",
            test_fast_forward_requested
                ? (test_fast_forward_completed ? "passed" : "failed")
                : "not_requested");
  }
  return completed ? 0 : 5;
}

int main(int argc, char **argv) {
  SDL_SetMainReady();
  bool force_launcher = false;
  int rom_argument = 1;
  if (argc >= 2 && strcmp(argv[1], "--launcher") == 0) {
    force_launcher = true;
    rom_argument++;
  }
  if (argc > rom_argument + 1) {
    fprintf(stderr, "Usage: %s [--launcher] [ROM.smc]\n", argv[0]);
    return 2;
  }
  char rom_path[kPathCapacity] = {0};
  if (argc == rom_argument + 1) {
    if (!snesrecomp_abspath(argv[rom_argument], rom_path, sizeof rom_path))
      (void)snprintf(rom_path, sizeof rom_path, "%s", argv[rom_argument]);
  }
  (void)snesrecomp_anchor_to_exe_dir();
  if (!Dkc2DiagnosticsInit("sdl2", DKC2_RELEASE_VERSION))
    fprintf(stderr, "warning: diagnostics could not be initialized\n");

  RecompLauncherCSettings settings;
  Dkc2LauncherSettingsDefault(&settings);
  Dkc2LauncherSettingsLoad(&settings);
  if (!rom_path[0])
    (void)Dkc2LauncherReadRomCache(rom_path, sizeof rom_path);
  bool suppress_launcher = EnvironmentEnabled("SNESRECOMP_NO_LAUNCHER") ||
                           EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  bool show_launcher = !suppress_launcher &&
      (force_launcher || !settings.skip_launcher || !rom_path[0]);
  if (show_launcher) {
    char selected_rom[kPathCapacity] = {0};
    int action = Dkc2LauncherRun(&settings, rom_path, selected_rom,
                                 sizeof selected_rom, NULL, 0);
    if (action == 1) return 0;
    if (action == 0 && selected_rom[0])
      (void)snprintf(rom_path, sizeof rom_path, "%s", selected_rom);
    (void)Dkc2LauncherSettingsSave(&settings);
  }
  if (!rom_path[0]) {
    fprintf(stderr, "No ROM was selected. Run with --launcher to choose one.\n");
    return 0;
  }
  (void)Dkc2LauncherWriteRomCache(rom_path);
  return RunGame(rom_path, &settings);
}
