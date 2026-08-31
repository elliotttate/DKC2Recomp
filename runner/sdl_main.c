#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "dkc2_game.h"
#include "dkc2_video.h"
#include "diagnostics.h"
#include "desktop_filter.h"
#include "desktop_fps.h"
#include "desktop_input.h"
#include "desktop_launcher.h"
#include "desktop_overlay.h"
#include "desktop_paths.h"
#include "desktop_present_sdl.h"
#include "desktop_rewind.h"
#include "input_recording.h"
#include "verified_rom.h"

#ifdef __APPLE__
#include "macos_host.h"
#endif

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
  kFrameBufferWidth = kDkc2VideoWidescreenWidth,
  kFrameHeight = kDkc2VideoHeight,
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
  uint8_t pixels[kFrameBufferWidth * kFrameHeight * kBytesPerPixel];
  uint8_t filtered_pixels[
      kFrameBufferWidth * kFrameHeight * kBytesPerPixel];
  int16_t scaled_audio[kMaximumFrameAudio * kAudioChannels];
  int player_source[kDkc2DesktopPlayerCount];
  int player_deadzone[kDkc2DesktopPlayerCount];
  int player_key_bind[kDkc2DesktopPlayerCount]
                     [RECOMP_LAUNCHER_MAX_BINDINGS];
  int player_pad_bind[kDkc2DesktopPlayerCount]
                     [RECOMP_LAUNCHER_MAX_BINDINGS];
  int assist_key_bind[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS];
  int assist_pad_bind[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS];
  int audio_volume;
  Dkc2DesktopOverlay *overlay;
  bool audio_available;
  bool running;
  bool hidden;
  bool menu_chord_previous;
  bool escaped_fullscreen;
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
  const int frame_width = Dkc2VideoWidth();
  FILE *stream = fopen(path, "wb");
  if (!stream) return false;
  bool ok = fprintf(stream, "P6\n%d %d\n255\n", frame_width,
                    kFrameHeight) > 0;
  for (int y = 0; ok && y < kFrameHeight; y++) {
    const uint8_t *row =
        pixels + (size_t)y * frame_width * kBytesPerPixel;
    for (int x = 0; ok && x < frame_width; x++) {
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
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
        event.key.keysym.scancode == SDL_SCANCODE_ESCAPE &&
        Dkc2DesktopEscapeExitsFullscreen(
            Dkc2SdlPresenterIsFullscreen(&host->presenter),
            Dkc2DesktopOverlayIsOpen(host->overlay))) {
      if (Dkc2SdlPresenterSetFullscreen(&host->presenter, false)) {
        host->escaped_fullscreen = true;
        continue;
      }
    }
    bool consumed =
        Dkc2DesktopOverlayProcessSdlEvent(host->overlay, &event);
    if (consumed) continue;
    if (event.type == SDL_QUIT) host->running = false;
    if (event.type == SDL_CONTROLLERDEVICEADDED ||
        event.type == SDL_CONTROLLERDEVICEREMOVED)
      RefreshControllers(host);
  }
}

static uint32_t ReadGamepadButtons(SDL_GameController *controller) {
  uint32_t buttons = 0;
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
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_GUIDE, kDkc2GamepadGuide);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK, kDkc2GamepadLeftStick);
  MAP_SDL_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK, kDkc2GamepadRightStick);
#undef MAP_SDL_BUTTON
  return buttons;
}

static uint8_t ReadTrigger(SDL_GameController *controller,
                           SDL_GameControllerAxis axis) {
  Sint16 value = SDL_GameControllerGetAxis(controller, axis);
  if (value <= 0) return 0;
  return (uint8_t)(((uint32_t)(uint16_t)value * 255u) / 32767u);
}

static bool IsSdlScancodePressed(int scancode, void *context) {
  const Uint8 *keys = (const Uint8 *)context;
  return keys && scancode > SDL_SCANCODE_UNKNOWN &&
         scancode < SDL_NUM_SCANCODES && keys[scancode] != 0;
}

static SdlControls ReadControls(SdlHost *host) {
  SdlControls controls = {0, 0};
  SDL_Window *window = (SDL_Window *)host->presenter.window;
  if (!host->hidden && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
    return controls;
  const Uint8 *keys = SDL_GetKeyboardState(NULL);
  uint32_t keyboard[kDkc2DesktopPlayerCount];
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++)
    keyboard[player] = Dkc2MapKeyboardBindings(
        host->player_key_bind[player], IsSdlScancodePressed, (void *)keys);

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
    gamepad->right_x = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_RIGHTX);
    vertical = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_RIGHTY);
    gamepad->right_y =
        vertical == INT16_MIN ? INT16_MAX : (int16_t)-vertical;
    gamepad->left_trigger = ReadTrigger(
        controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    gamepad->right_trigger = ReadTrigger(
        controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  }
  controls.controller = Dkc2RoutePlayerInputsWithBindings(
      keyboard, gamepads, gamepad_count, host->player_source,
      host->player_deadzone, host->player_pad_bind);
  controls.host_actions = Dkc2MapAssistBindings(
      host->assist_key_bind, host->assist_pad_bind, IsSdlScancodePressed,
      (void *)keys, gamepads, gamepad_count, 30);
  uint32_t menu_buttons = gamepad_count ? gamepads[0].buttons : 0;
  Dkc2DesktopOverlaySetGamepad(
      host->overlay, gamepad_count ? &gamepads[0] : NULL);
  bool menu_chord =
      (menu_buttons & (kDkc2GamepadStart | kDkc2GamepadBack)) ==
      (kDkc2GamepadStart | kDkc2GamepadBack);
  if (menu_chord && !host->menu_chord_previous)
    Dkc2DesktopOverlayToggle(host->overlay);
  host->menu_chord_previous = menu_chord;
  if (Dkc2DesktopOverlayIsOpen(host->overlay)) {
    controls.controller = 0;
    controls.host_actions = 0;
  }
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
  Dkc2DesktopOverlayDestroy(host->overlay);
  host->overlay = NULL;
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
      /* Do not repay a visible host stall with a short catch-up frame. The
       * accepted DKC1 pacer reanchors after a two-millisecond miss; use the
       * same recovery threshold while preserving DKC2's exact 60.0988-Hz
       * fractional cadence. */
      if (now - *deadline > frequency / 500u) {
        *deadline = now;
        *deadline_fraction = 0.0;
      }
      return;
    }
    uint64_t remaining = *deadline - now;
    Uint32 milliseconds = (Uint32)(remaining * 1000 / frequency);
#ifdef __APPLE__
    if (milliseconds > 1)
      Dkc2MacWaitSeconds((double)remaining / (double)frequency);
    else
      SDL_Delay(0);
#else
    if (milliseconds > 1)
      SDL_Delay(milliseconds - 1);
    else
      SDL_Delay(0);
#endif
    PumpEvents(host);
  }
}

#ifdef __APPLE__
static uint32_t ApplyMacCommands(SdlHost *host,
                                 RecompLauncherCSettings *settings) {
  uint32_t commands = Dkc2MacTakeCommands();
  uint32_t host_actions = 0;
  bool settings_changed = false;
  if (commands & kDkc2MacCommandQuit)
    host->running = false;
  if (commands & kDkc2MacCommandToggleOverlay)
    Dkc2DesktopOverlayToggle(host->overlay);
  if (commands & kDkc2MacCommandQuickSave)
    host_actions |= kDkc2HostSaveState;
  if (commands & kDkc2MacCommandQuickLoad)
    host_actions |= kDkc2HostLoadState;
  if (commands & kDkc2MacCommandToggleFullscreen) {
    bool fullscreen = !Dkc2SdlPresenterIsFullscreen(&host->presenter);
    if (Dkc2SdlPresenterSetFullscreen(&host->presenter, fullscreen)) {
      settings->fullscreen = fullscreen ? 1 : 0;
      settings_changed = true;
    }
  }
  if (commands & kDkc2MacCommandFilterNearest) {
    settings->texture_filter = 0;
    settings_changed = true;
  }
  if (commands & kDkc2MacCommandFilterBilinear) {
    settings->texture_filter = 1;
    settings_changed = true;
  }
  if (commands & kDkc2MacCommandAspectNative) {
    settings->aspect_index = kDkc2VideoAspectNative;
    settings_changed = true;
  }
  if (commands & kDkc2MacCommandAspect16x10) {
    settings->aspect_index = kDkc2VideoAspect16x10;
    settings_changed = true;
  }
  if (commands & kDkc2MacCommandAspect16x9) {
    settings->aspect_index = kDkc2VideoAspect16x9;
    settings_changed = true;
  }
  if (settings_changed) {
    settings->widescreen =
        settings->aspect_index != kDkc2VideoAspectNative;
    Dkc2DesktopOverlaySetSettings(host->overlay, settings);
  }
  return host_actions;
}
#endif

static void ApplyOverlaySettings(SdlHost *host,
                                 RecompLauncherCSettings *settings,
                                 int *screen_filter) {
  if (!host || !host->overlay || !settings || !screen_filter) return;
  RecompLauncherCSettings updated;
  Dkc2DesktopOverlayGetSettings(host->overlay, &updated);
  updated.volume = ClampInt(updated.volume, 0, 100);
  updated.texture_filter = updated.texture_filter != 0;
  updated.aspect_index =
      ClampInt(updated.aspect_index, kDkc2VideoAspectNative,
               kDkc2VideoAspectCount - 1);
  updated.widescreen =
      updated.aspect_index != kDkc2VideoAspectNative;
  if (!Dkc2DesktopScreenFilterValid(updated.screen_kind))
    updated.screen_kind = kDkc2ScreenRaw;
  if (updated.screen_kind != *screen_filter) {
    int previous_filter = *screen_filter;
    Dkc2DesktopColorFilterDestroy(&host->color_filter);
    if (Dkc2DesktopColorFilterInit(&host->color_filter,
                                   updated.screen_kind)) {
      *screen_filter = updated.screen_kind;
    } else {
      updated.screen_kind = previous_filter;
      (void)Dkc2DesktopColorFilterInit(&host->color_filter,
                                       previous_filter);
    }
  }
  host->presenter.linear_filter = updated.texture_filter != 0;
  host->audio_volume = updated.volume;
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    host->player_source[player] =
        ClampInt(updated.player_src[player], 0, 2);
    host->player_deadzone[player] =
        ClampInt(updated.deadzone[player], 0, 100);
    updated.player_src[player] = host->player_source[player];
    updated.deadzone[player] = host->player_deadzone[player];
  }
  memcpy(host->player_key_bind, updated.player_key_bind,
         sizeof host->player_key_bind);
  memcpy(host->player_pad_bind, updated.player_pad_bind,
         sizeof host->player_pad_bind);
  memcpy(host->assist_key_bind, updated.assist_key_bind,
         sizeof host->assist_key_bind);
  memcpy(host->assist_pad_bind, updated.assist_pad_bind,
         sizeof host->assist_pad_bind);
  if (Dkc2VideoGetAspect() != (Dkc2VideoAspect)updated.aspect_index) {
    Dkc2VideoSetAspect((Dkc2VideoAspect)updated.aspect_index);
    memset(host->pixels, 0, sizeof host->pixels);
    memset(host->filtered_pixels, 0, sizeof host->filtered_pixels);
    Dkc2BeginDrawing(
        host->pixels, (size_t)Dkc2VideoWidth() * kBytesPerPixel);
  }
  *settings = updated;
}

static int RunGame(const char *rom_path,
                   RecompLauncherCSettings *settings) {
  SdlHost host;
  memset(&host, 0, sizeof host);
  host.running = true;
  host.hidden = EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  host.audio_volume = ClampInt(settings->volume, 0, 100);
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    host.player_source[player] = ClampInt(settings->player_src[player], 0, 2);
    host.player_deadzone[player] = ClampInt(settings->deadzone[player], 0, 100);
  }
  memcpy(host.player_key_bind, settings->player_key_bind,
         sizeof host.player_key_bind);
  memcpy(host.player_pad_bind, settings->player_pad_bind,
         sizeof host.player_pad_bind);
  memcpy(host.assist_key_bind, settings->assist_key_bind,
         sizeof host.assist_key_bind);
  memcpy(host.assist_pad_bind, settings->assist_pad_bind,
         sizeof host.assist_pad_bind);

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
  bool test_overlay_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_OVERLAY");
  bool test_save_load_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_SAVELOAD");
  bool test_save_injected = false;
  bool test_save_completed = false;
  bool test_load_injected = false;
  bool test_load_completed = false;
  bool sram_enabled = !EnvironmentEnabled("DKC2_DESKTOP_DISABLE_SRAM");
  int persisted_aspect =
      ClampInt(settings->aspect_index, kDkc2VideoAspectNative,
               kDkc2VideoAspectCount - 1);
  Dkc2VideoAspect aspect = (Dkc2VideoAspect)persisted_aspect;
  const char *aspect_override = getenv("DKC2_ASPECT");
  bool aspect_override_active = aspect_override && *aspect_override;
  if (aspect_override_active &&
      !Dkc2VideoAspectFromName(aspect_override, &aspect)) {
    ShowError("DKC2_ASPECT must be 4:3, 16:10, or 16:9");
    return 2;
  }
  const char *widescreen_override = getenv("DKC2_WIDESCREEN");
  bool widescreen_override_active = !aspect_override_active &&
      widescreen_override && *widescreen_override;
  if (widescreen_override_active)
    aspect = *widescreen_override != '0'
        ? kDkc2VideoAspect16x9 : kDkc2VideoAspectNative;
  settings->aspect_index = (int)aspect;
  settings->widescreen = aspect != kDkc2VideoAspectNative;
  Dkc2VideoSetAspect(aspect);
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
          settings->texture_filter != 0, Dkc2VideoWidth(), kFrameHeight,
          video_error, sizeof video_error)) {
    free(rom);
    ShutdownHost(&host);
    ShowError(video_error);
    return 4;
  }
  host.overlay = Dkc2DesktopOverlayCreate(settings);
  if (!host.overlay ||
      !Dkc2DesktopOverlayInitSdl(
          host.overlay, host.presenter.window, host.presenter.gl_context)) {
    free(rom);
    ShutdownHost(&host);
    ShowError("Unable to initialize the in-game overlay");
    return 4;
  }
#ifdef __APPLE__
  if (!host.hidden) {
    Dkc2MacInstallMenu();
    Dkc2MacUpdateMenu(
        Dkc2SdlPresenterIsFullscreen(&host.presenter),
        settings->texture_filter != 0, settings->aspect_index);
  }
#endif
  RefreshControllers(&host);
  Dkc2BeginDrawing(
      host.pixels, (size_t)Dkc2VideoWidth() * kBytesPerPixel);
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
  fprintf(stdout, "Video: %s, %s, %s sampling, aspect=%s (%dx%d)\n",
          Dkc2SdlPresenterBackend(&host.presenter),
          Dkc2DesktopScreenFilterName(screen_filter),
          settings->texture_filter ? "bilinear" : "nearest",
          Dkc2VideoAspectName(Dkc2VideoGetAspect()), Dkc2VideoWidth(),
          kFrameHeight);
  fprintf(stdout,
          "Controls: gameplay and Assist bindings are configurable in the "
          "pre-boot launcher. Escape=Exit Fullscreen/Overlay. "
          "SDL game controllers are "
          "detected automatically.\n");

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
  bool test_overlay_completed = false;
  unsigned test_overlay_ticks = 0;
  bool runtime_failure = false;
  uint32_t previous_state_actions = 0;
  SdlSpeedMode previous_mode = kSdlSpeedNormal;
  bool previous_overlay_open = false;
  Dkc2InputRecorder input_recorder = {0};
  char input_recording_error[512] = {0};
  const char *input_recording_path = getenv("SNESRECOMP_INPUT_REC");

  if (input_recording_path && *input_recording_path) {
    if (!Dkc2InputRecorderOpen(
            &input_recorder, input_recording_path,
            input_recording_error, sizeof input_recording_error)) {
      fprintf(stderr, "Input recording failed: %s\n", input_recording_error);
      ShowError(input_recording_error);
      runtime_failure = true;
      host.running = false;
    } else {
      fprintf(stdout, "Input recording enabled: %s\n", input_recording_path);
      Dkc2SdlPresenterSetTitle(
          &host.presenter, DKC2_PRODUCT_TITLE " (Recording Input)");
    }
  }

  while (host.running) {
    PumpEvents(&host);
    if (host.escaped_fullscreen) {
      settings->fullscreen = 0;
      Dkc2DesktopOverlaySetSettings(host.overlay, settings);
      host.escaped_fullscreen = false;
    }
    Dkc2DiagnosticsHeartbeat(host_frame, Dkc2ResumePc());
    host_report_crash_test_tick();
    uint32_t platform_host_actions = 0;
#ifdef __APPLE__
    platform_host_actions = ApplyMacCommands(&host, settings);
    if (test_save_load_requested && !test_save_injected &&
        host_frame >= 30) {
      platform_host_actions |= kDkc2HostSaveState;
      test_save_injected = true;
    }
    if (test_save_load_requested && test_save_completed &&
        !test_load_injected && host_frame >= 60) {
      platform_host_actions |= kDkc2HostLoadState;
      test_load_injected = true;
    }
#endif
    SdlControls controls = ReadControls(&host);
    if (test_overlay_requested && !test_overlay_completed &&
        host_frame >= 30) {
      if (test_overlay_ticks == 0) {
        Dkc2DesktopOverlayToggle(host.overlay);
        test_overlay_ticks = 1;
      } else if (Dkc2DesktopOverlayIsOpen(host.overlay)) {
        test_overlay_ticks++;
        if (test_overlay_ticks >= 30) {
          Dkc2DesktopOverlayToggle(host.overlay);
          test_overlay_completed = true;
        }
      }
    }
    uint32_t overlay_actions =
        Dkc2DesktopOverlayTakeActions(host.overlay);
    if (overlay_actions & kDkc2OverlayActionQuit) {
      host.running = false;
      break;
    }
    if (overlay_actions & kDkc2OverlayActionSaveState)
      controls.host_actions |= kDkc2HostSaveState;
    if (overlay_actions & kDkc2OverlayActionLoadState)
      controls.host_actions |= kDkc2HostLoadState;
    ApplyOverlaySettings(&host, settings, &screen_filter);
    bool overlay_open = Dkc2DesktopOverlayIsOpen(host.overlay);
    if (overlay_open != previous_overlay_open) {
      ResetAudio(&host);
      if (host.audio_device)
        SDL_PauseAudioDevice(host.audio_device, overlay_open ? 1 : 0);
      audio_fraction = 0.0;
      deadline = SDL_GetPerformanceCounter();
      deadline_fraction = 0.0;
      previous_overlay_open = overlay_open;
    }
    bool assist_tools = Dkc2DesktopOverlayAssistTools(host.overlay);
#ifdef __APPLE__
    if (!host.hidden)
      Dkc2MacUpdateMenu(
          Dkc2SdlPresenterIsFullscreen(&host.presenter),
          host.presenter.linear_filter, settings->aspect_index);
#endif
    controls.host_actions = Dkc2ApplyAssistGate(
        controls.host_actions, platform_host_actions, assist_tools);
    uint32_t state_actions = controls.host_actions &
        (kDkc2HostSaveState | kDkc2HostLoadState);
    uint32_t pressed_state_actions = state_actions & ~previous_state_actions;
    previous_state_actions = state_actions;
    if (pressed_state_actions & kDkc2HostSaveState) {
      int slot = Dkc2DesktopOverlaySelectedSlot(host.overlay);
      char path[128];
      RtlSaveSlotPath(slot, path, sizeof path);
      bool saved = EnsureSaveDirectory() && RtlSaveSnapshot(path);
      char status[80];
      (void)snprintf(status, sizeof status,
                     saved ? "Slot %d saved." : "Slot %d save failed.",
                     slot + 1);
      Dkc2DesktopOverlaySetStatus(
          host.overlay, status, saved);
      if (test_save_load_requested && test_save_injected)
        test_save_completed = saved;
      (void)snprintf(status, sizeof status,
                     saved ? DKC2_PRODUCT_TITLE " - Slot %d saved"
                           : DKC2_PRODUCT_TITLE " - Slot %d save failed",
                     slot + 1);
      Dkc2SdlPresenterSetTitle(&host.presenter, status);
    }
    if (pressed_state_actions & kDkc2HostLoadState) {
      int slot = Dkc2DesktopOverlaySelectedSlot(host.overlay);
      char path[128];
      RtlSaveSlotPath(slot, path, sizeof path);
      bool loaded = RtlLoadSnapshot(path);
      if (!loaded && slot == 0)
        loaded = RtlLoadSnapshot(DKC2_STATE_SLOT0_LEGACY_FILE);
      if (loaded) {
        char status[80];
        (void)snprintf(status, sizeof status, "Slot %d loaded.", slot + 1);
        Dkc2DesktopOverlaySetStatus(host.overlay, status, true);
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
        if (test_save_load_requested && test_load_injected)
          test_load_completed = true;
      } else {
        char status[80];
        (void)snprintf(status, sizeof status,
                       "Slot %d could not be loaded.", slot + 1);
        Dkc2DesktopOverlaySetStatus(
            host.overlay, status, false);
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

    bool frame_ready = overlay_open;
    if (overlay_open) {
      mode = kSdlSpeedNormal;
    } else if (mode == kSdlSpeedRewind) {
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
        if (Dkc2InputRecorderIsOpen(&input_recorder) &&
            !Dkc2InputRecorderWrite(
                &input_recorder, controls.controller,
                input_recording_error, sizeof input_recording_error)) {
          fprintf(stderr, "Input recording failed: %s\n",
                  input_recording_error);
          Dkc2DiagnosticsFatal(input_recording_error);
          ShowError(input_recording_error);
          runtime_failure = true;
          host.running = false;
          break;
        }
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

    const bool should_pace =
        overlay_open || mode != kSdlSpeedNormal || !host.audio_available ||
        SDL_GetQueuedAudioSize(host.audio_device) >=
            (Uint32)(kMaximumFrameAudio * kAudioChannels *
                     sizeof(int16_t) * 2);
#ifdef __APPLE__
    /* With OpenGL's second vsync gate disabled, place the presentation itself
     * on the exact DKC2 deadline. Startup may still run a few unpaced frames
     * to establish the existing audio queue; once filled, only this clock
     * controls cadence. */
    if (frame_ready && should_pace &&
        Dkc2SdlPresenterUsesSoftwarePacing(&host.presenter))
      PaceFrame(&host, &deadline, &deadline_fraction);
#endif
    if (frame_ready) {
      const uint8_t *present_pixels = Dkc2DesktopColorFilterApply(
          &host.color_filter, host.pixels, host.filtered_pixels,
          Dkc2VideoPixelCount());
      if (!present_pixels ||
          !Dkc2SdlPresenterPresent(&host.presenter, present_pixels,
                                   Dkc2VideoWidth(), kFrameHeight,
                                   Dkc2DesktopOverlayRenderOpenGl,
                                   host.overlay)) {
        fprintf(stderr, "SDL video presentation failed: %s\n", SDL_GetError());
        Dkc2DiagnosticsFatal("SDL video presentation failed");
        runtime_failure = true;
        break;
      }
    }
    if (should_pace &&
        (!Dkc2SdlPresenterUsesSoftwarePacing(&host.presenter) ||
         !frame_ready))
      PaceFrame(&host, &deadline, &deadline_fraction);
    else if (!should_pace) {
      deadline = SDL_GetPerformanceCounter();
      deadline_fraction = 0.0;
    }
    unsigned fps = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    if (Dkc2DesktopFpsUpdate(&fps_counter, frame_ready, now, frequency, &fps)) {
      char title[112];
      (void)snprintf(title, sizeof title,
                     DKC2_PRODUCT_TITLE " (FPS: %u)", fps);
      if (assist_tools)
        (void)snprintf(title, sizeof title,
                       DKC2_PRODUCT_TITLE
                       " (FPS: %u) (Assist Tools: On)",
                       fps);
      if (Dkc2InputRecorderIsOpen(&input_recorder)) {
        size_t used = strlen(title);
        (void)snprintf(
            title + used, sizeof title - used, " (Recording Input)");
      }
      Dkc2SdlPresenterSetTitle(&host.presenter, title);
    }
  }

  if (test_rewind_requested && !test_rewind_completed) runtime_failure = true;
  if (test_fast_forward_requested && !test_fast_forward_completed)
    runtime_failure = true;
  if (test_overlay_requested && !test_overlay_completed)
    runtime_failure = true;
  if (test_save_load_requested &&
      (!test_save_completed || !test_load_completed))
    runtime_failure = true;
  unsigned long long recorded_frames = input_recorder.frames;
  if (!Dkc2InputRecorderClose(
          &input_recorder, input_recording_error,
          sizeof input_recording_error)) {
    fprintf(stderr, "Input recording failed: %s\n", input_recording_error);
    runtime_failure = true;
  } else if (input_recording_path && *input_recording_path) {
    fprintf(stdout, "Input recording completed: %llu frames at %s\n",
            recorded_frames, input_recording_path);
  }
  bool completed = !runtime_failure && !g_fail && Dkc2LastLleResult();
  const char *frame_output = getenv("DKC2_FRAME_PPM");
  if (frame_output && *frame_output &&
      !WriteFramePpm(frame_output, host.pixels))
    completed = false;
  if (sram_enabled && completed) RtlWriteSram();
  Dkc2DesktopOverlayGetSettings(host.overlay, settings);
  if (aspect_override_active || widescreen_override_active) {
    settings->aspect_index = persisted_aspect;
    settings->widescreen =
        persisted_aspect != kDkc2VideoAspectNative;
  }
  Dkc2DiagnosticsShutdown(completed ? "clean_exit" : "runtime_failure");
  Dkc2RewindHistoryDestroy(&rewind_history);
  free(rewind_scratch);
  free(rom);
  ShutdownHost(&host);
  if (test_frame_limit) {
    fprintf(stdout,
            "result=desktop_completed frames=%llu rewind_restore=%s "
            "fast_forward=%s save_load=%s host=sdl2\n",
            host_frame,
            test_rewind_requested
                ? (test_rewind_completed ? "passed" : "failed")
                : "not_requested",
            test_fast_forward_requested
                ? (test_fast_forward_completed ? "passed" : "failed")
                : "not_requested",
            test_save_load_requested
                ? (test_save_completed && test_load_completed
                       ? "passed" : "failed")
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
#ifdef __APPLE__
  char assets_path[kPathCapacity] = {0};
  char macos_error[256] = {0};
  if (!Dkc2MacPrepareRuntimeDirectory(
          assets_path, sizeof assets_path, macos_error,
          sizeof macos_error)) {
    fprintf(stderr, "Unable to prepare DKC2 user data: %s\n", macos_error);
    return 2;
  }
  Dkc2LauncherSetAssetsPath(assets_path);
#else
  (void)snesrecomp_anchor_to_exe_dir();
#endif
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
  int result = RunGame(rom_path, &settings);
  (void)Dkc2LauncherSettingsSave(&settings);
  return result;
}
