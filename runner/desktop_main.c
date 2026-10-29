#include "dkc2_kongs.h"
#include "dkc2_music.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <SDL.h>

#include "dkc2_game.h"
#include "dkc2_developer.h"
#include "dkc2_coop.h"
#include "dkc2_coop_assist.h"
#include "dkc2_video.h"
#include "diagnostics.h"
#include "desktop_filter.h"
#include "desktop_fps.h"
#include "desktop_input.h"
#include "desktop_gamepads.h"
#include "windows_input.h"
#include "windows_menu.h"
#include "desktop_menu_settings.h"
#include "desktop_launcher.h"
#include "desktop_overlay.h"
#include "desktop_paths.h"
#include "desktop_perf.h"
#include "desktop_present.h"
#include "desktop_present_gl.h"
#include "desktop_rewind.h"
#include "desktop_resources.h"
#include "input_recording.h"
#include "verified_rom.h"

#include "common_rtl.h"
#include "host_report.h"
#include "launcher.h"
#include "snes/snes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DKC2_RELEASE_VERSION
#define DKC2_RELEASE_VERSION "dev"
#endif

enum {
  kFrameBufferWidth = kDkc2VideoMaximumWidth,
  kFrameHeight = kDkc2VideoHeight,
  kBytesPerPixel = 4,
  kAudioRate = 32040,
  kAudioChannels = 2,
  kAudioBlockFrames = 2048,
  kAudioBlockCount = 8,
  kMaximumFrameAudio = 534,
  kInitialBufferedBlocks = 3,
  kHostSpeedMultiplier = 3,
  kRewindSnapshotInterval = 3,
  kRewindSnapshotCapacity = 300,
};

static const double kVideoRate = 60.098811862;
static uint8_t
    s_pixels[kFrameBufferWidth * kFrameHeight * kBytesPerPixel];
static uint8_t
    s_filtered_pixels[kFrameBufferWidth * kFrameHeight * kBytesPerPixel];
static BITMAPINFO s_bitmap_info;
static Dkc2DesktopColorFilter s_color_filter;
static Dkc2DesktopPresenter s_presenter;
static Dkc2DesktopGlPresenter s_gl_presenter;
static Dkc2DesktopOverlay *s_overlay;
static HWND s_window;
static Dkc2WindowsMenu *s_menu;
static Dkc2WindowsKeys s_key_taps;
static int ClampInt(int value, int minimum, int maximum);
static WINDOWPLACEMENT s_windowed_placement = {sizeof(WINDOWPLACEMENT)};
static bool s_running = true;
static bool s_test_hidden;
static bool s_gl_active;
static bool s_recreating_window;
static bool s_present_failed;
static int s_window_scale = 3;
static int s_fullscreen;
static int s_renderer = 1;
static int s_screen_filter = kDkc2ScreenRaw;
static bool s_linear_filter;
static int s_audio_enabled = 1;
static int s_audio_volume = 100;
static int s_player_source[kDkc2DesktopPlayerCount] = {
    kDkc2InputSourceKeyboard, kDkc2InputSourceGamepad};
static Dkc2DesktopGamepads s_gamepads;
static char s_player_guid[2][40];
static int s_player_deadzone[kDkc2DesktopPlayerCount] = {24, 24};
static int s_player_key_bind[kDkc2DesktopPlayerCount]
                            [RECOMP_LAUNCHER_MAX_BINDINGS];
static int s_player_pad_bind[kDkc2DesktopPlayerCount]
                            [RECOMP_LAUNCHER_MAX_BINDINGS];
static int s_assist_key_bind[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS];
static int s_assist_pad_bind[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS];
static bool s_perf_enabled;
static bool s_perf_log_created;
static bool s_input_recording_active;
static bool s_perf_hotkey_previous;
static bool s_menu_chord_previous;
static double s_perf_busy_percent;
static FILE *s_perf_log;
static LARGE_INTEGER s_perf_frequency;
static Dkc2DesktopPerfCounter s_perf_counter;

typedef struct DesktopAudio {
  HWAVEOUT device;
  WAVEHDR headers[kAudioBlockCount];
  int16_t blocks[kAudioBlockCount][kAudioBlockFrames * kAudioChannels];
  int16_t staging[kAudioBlockFrames * kAudioChannels];
  size_t staging_frames;
  unsigned next_block;
  unsigned submitted_blocks;
  bool available;
} DesktopAudio;

static DesktopAudio s_audio;
static bool s_developer_beat_level_requested;

typedef struct DesktopControls {
  uint32_t controller;
  uint32_t host_actions;
} DesktopControls;

typedef enum DesktopSpeedMode {
  kDesktopSpeedNormal,
  kDesktopSpeedRewind,
  kDesktopSpeedFastForward,
} DesktopSpeedMode;

static bool EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static uint64_t PerformanceNow(void) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (uint64_t)now.QuadPart;
}

static uint64_t PerformanceBegin(void) {
  return s_perf_enabled ? PerformanceNow() : 0;
}

static void PerformanceEnd(Dkc2DesktopPerfPhase phase, uint64_t start) {
  if (!s_perf_enabled || start == 0) return;
  uint64_t end = PerformanceNow();
  if (end >= start) Dkc2DesktopPerfAdd(&s_perf_counter, phase, end - start);
}

static void SetPerformanceLogging(bool enabled) {
  if (enabled == s_perf_enabled) return;
  if (!enabled) {
    if (s_perf_log) (void)fclose(s_perf_log);
    s_perf_log = NULL;
    s_perf_enabled = false;
    s_perf_busy_percent = 0.0;
    fprintf(stdout, "Performance logging: off\n");
    return;
  }

  const char *mode = s_perf_log_created ? "a" : "w";
  s_perf_log = fopen("performance.log", mode);
  s_perf_log_created = true;
  if (!s_perf_log) {
    fprintf(stderr, "warning: unable to open performance.log\n");
    return;
  }
  s_perf_enabled = true;
  Dkc2DesktopPerfInit(&s_perf_counter, PerformanceNow());
  const char *backend = s_gl_active ? "OpenGL" : "GDI";
#if defined(_MSC_VER)
  fprintf(s_perf_log,
          "# backend=%s filter=%s gpu_timing=unavailable compiler=MSVC "
          "release_optimization=/O2\n",
          backend, Dkc2DesktopScreenFilterName(s_screen_filter));
#else
  fprintf(s_perf_log,
          "# backend=%s filter=%s gpu_timing=unavailable "
          "compiler=GCC-or-Clang release_optimization=-O3\n",
          backend, Dkc2DesktopScreenFilterName(s_screen_filter));
#endif
  (void)fflush(s_perf_log);
  fprintf(stdout,
          "Performance logging: on (performance.log; backend=%s, "
          "GPU timestamps unavailable)\n",
          backend);
}

static void UpdatePerformanceLogging(bool presented, uint64_t now) {
  if (!s_perf_enabled || !s_perf_log || s_perf_frequency.QuadPart <= 0)
    return;
  Dkc2DesktopPerfSample sample;
  if (!Dkc2DesktopPerfUpdate(
          &s_perf_counter, presented, now,
          (uint64_t)s_perf_frequency.QuadPart, &sample))
    return;
  s_perf_busy_percent = sample.main_thread_busy_percent;
  fprintf(s_perf_log,
          "perf fps=%.2f frames=%u main_thread_busy=%.1f%% active_ms=%.3f "
          "input_ms=%.3f emulation_ms=%.3f rewind_ms=%.3f ppu_ms=%.3f "
          "audio_ms=%.3f present_cpu_ms=%.3f pace_ms=%.3f "
          "untracked_ms=%.3f gpu_ms=n/a backend=%s filter=%s\n",
          sample.presented_fps, sample.presented_frames,
          sample.main_thread_busy_percent, sample.active_ms,
          sample.phase_ms[kDkc2PerfInput],
          sample.phase_ms[kDkc2PerfEmulation],
          sample.phase_ms[kDkc2PerfRewind],
          sample.phase_ms[kDkc2PerfPpu],
          sample.phase_ms[kDkc2PerfAudio],
          sample.phase_ms[kDkc2PerfPresent],
          sample.phase_ms[kDkc2PerfPace], sample.untracked_ms,
          s_gl_active ? "OpenGL" : "GDI",
          Dkc2DesktopScreenFilterName(s_screen_filter));
  (void)fflush(s_perf_log);
}

static bool EnsureSaveDirectory(void) {
  if (CreateDirectoryA("saves", NULL)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool WriteFramePpm(const char *path) {
  const int frame_width = Dkc2VideoWidth();
  FILE *stream = fopen(path, "wb");
  if (!stream) return false;
  bool ok = fprintf(stream, "P6\n%d %d\n255\n", frame_width,
                    kFrameHeight) > 0;
  for (int y = 0; ok && y < kFrameHeight; y++) {
    const uint8_t *row =
        s_pixels + (size_t)y * frame_width * kBytesPerPixel;
    for (int x = 0; ok && x < frame_width; x++) {
      const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
      ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
    }
  }
  if (fclose(stream) != 0) ok = false;
  return ok;
}

static void PaintFrame(HWND window) {
  const int frame_width = Dkc2VideoWidth();
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(window, &paint);
  RECT client;
  GetClientRect(window, &client);
  const uint8_t *present_pixels = Dkc2DesktopColorFilterApply(
      &s_color_filter, s_pixels, s_filtered_pixels,
      Dkc2VideoPixelCount());
  if (!present_pixels) {
    s_present_failed = true;
    EndPaint(window, &paint);
    return;
  }
  (void)Dkc2DesktopGlSetUpscaler(&s_gl_presenter, Dkc2LauncherUpscaler(),
      s_linear_filter, Dkc2LauncherReconstructMode(),
      Dkc2LauncherReconstructStrength() / 100.0f,
      Dkc2LauncherReconstructSoftness() / 100.0f,
      Dkc2LauncherReconstructShading() / 100.0f);
  (void)Dkc2DesktopGlSetDisplay(&s_gl_presenter, Dkc2LauncherDisplay(),
                               Dkc2LauncherCrt());
  bool presented = s_gl_active
      ? Dkc2DesktopGlPresent(&s_gl_presenter, &client, present_pixels,
                             frame_width, kFrameHeight, s_linear_filter,
                             Dkc2DesktopOverlayRenderOpenGl, s_overlay)
      : Dkc2DesktopPresent(&s_presenter, dc, &client, present_pixels,
                           &s_bitmap_info, frame_width, kFrameHeight,
                           s_linear_filter);
  if (!presented) s_present_failed = true;
  EndPaint(window, &paint);
}

static bool SetFullscreen(bool fullscreen) {
  if (fullscreen == (s_fullscreen != 0)) return true;
  if (fullscreen) {
    MONITORINFO monitor = {sizeof monitor};
    if (!GetWindowPlacement(s_window, &s_windowed_placement) ||
        !GetMonitorInfoW(MonitorFromWindow(s_window, MONITOR_DEFAULTTONEAREST),
                        &monitor)) return false;
    Dkc2WindowsMenuSetVisible(s_menu, false);
    SetWindowLongPtrW(s_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(s_window, NULL, monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right - monitor.rcMonitor.left,
        monitor.rcMonitor.bottom - monitor.rcMonitor.top,
        SWP_NOZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrW(s_window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    Dkc2WindowsMenuSetVisible(s_menu, true);
    SetWindowPlacement(s_window, &s_windowed_placement);
    SetWindowPos(s_window, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  s_fullscreen = fullscreen ? 1 : 0;
  return true;
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
  if (message == WM_ENTERMENULOOP || message == WM_EXITMENULOOP ||
      message == WM_KILLFOCUS)
    Dkc2WindowsKeysClear(&s_key_taps);
  if (message == WM_SYSKEYDOWN && wparam == VK_RETURN &&
      (lparam & (1LL << 29)) && !(lparam & (1LL << 30))) {
    PostMessageW(window, WM_COMMAND, kDkc2MenuFullscreen, 0);
    return 0;
  }
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE &&
      !(lparam & (1LL << 30))) {
    if (s_fullscreen && !Dkc2DesktopOverlayIsOpen(s_overlay)) {
      PostMessageW(window, WM_COMMAND, kDkc2MenuFullscreen, 0);
      return 0;
    }

  }
  if (message == WM_KEYDOWN && !(lparam & (1LL << 30)) &&
      Dkc2DeveloperBeatLevelKey() &&
      wparam == (WPARAM)(VK_F1 + Dkc2DeveloperBeatLevelKey() - 1) &&
      !Dkc2DesktopOverlayIsOpen(s_overlay)) {
    s_developer_beat_level_requested = true;
    return 0;
  }
  if (Dkc2DesktopOverlayProcessWin32Message(
          s_overlay, window, message, (uintptr_t)wparam, (intptr_t)lparam))
    return 0;
  if (message == WM_KEYDOWN && !(lparam & (1LL << 30)) &&
      !Dkc2DesktopOverlayIsOpen(s_overlay)) {
    unsigned key = (unsigned)wparam;
    if (key == VK_SHIFT)
      key = ((lparam >> 16) & 0xff) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    if (key == VK_CONTROL) key = (lparam & (1LL << 24)) ? VK_RCONTROL : VK_LCONTROL;
    Dkc2WindowsKeyDown(&s_key_taps, key);
  }
  switch (message) {
    case WM_PAINT:
      PaintFrame(window);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      InvalidateRect(window, NULL, FALSE);
      return 0;
    case WM_CLOSE:
      s_running = false;
      return 0;
    case WM_DESTROY:
      Dkc2WindowsMenuDestroy(s_menu);
      s_menu = NULL;
      if (s_recreating_window) return 0;
      s_running = false;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcA(window, message, wparam, lparam);
  }
}

static bool PumpMessages(void) {
  MSG message;
  while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) s_running = false;
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return s_running;
}

static bool CreateGameWindow(void) {
  HINSTANCE instance = GetModuleHandleA(NULL);
  WNDCLASSA window_class;
  memset(&window_class, 0, sizeof window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hIcon = LoadIconA(instance,
                                MAKEINTRESOURCEA(DKC2_DESKTOP_ICON_RESOURCE_ID));
  window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
  window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  window_class.lpszClassName = "Dkc2SnesrecompWindow";
  if (!RegisterClassA(&window_class) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  int width = (Dkc2VideoIsWidescreen() ? 427 : 320) * s_window_scale;
  int height = 240 * s_window_scale;
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  DWORD style = s_fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
  RECT rectangle = {0, 0, width, height};
  if (s_fullscreen) {
    x = 0;
    y = 0;
    rectangle.right = GetSystemMetrics(SM_CXSCREEN);
    rectangle.bottom = GetSystemMetrics(SM_CYSCREEN);
  } else if (!AdjustWindowRect(&rectangle, style, FALSE)) {
    return false;
  }
  char title[96];
  (void)snprintf(title, sizeof title, "%s", DKC2_PRODUCT_TITLE);
  s_window = CreateWindowExA(
      0, window_class.lpszClassName,
      title, style, x, y,
      rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
      NULL, NULL, instance, NULL);
  if (!s_window) return false;

  memset(&s_bitmap_info, 0, sizeof s_bitmap_info);
  s_bitmap_info.bmiHeader.biSize = sizeof s_bitmap_info.bmiHeader;
  s_bitmap_info.bmiHeader.biWidth = Dkc2VideoWidth();
  s_bitmap_info.bmiHeader.biHeight = -kFrameHeight;
  s_bitmap_info.bmiHeader.biPlanes = 1;
  s_bitmap_info.bmiHeader.biBitCount = 32;
  s_bitmap_info.bmiHeader.biCompression = BI_RGB;

  bool request_gpu = s_renderer != 0 &&
                     !EnvironmentEnabled("DKC2_DESKTOP_FORCE_GDI");
  if (request_gpu) {
    char error[512] = {0};
    if (Dkc2DesktopGlPresenterInit(&s_gl_presenter, s_window,
                                   !s_test_hidden,
                                   error, sizeof error)) {
      s_gl_active = true;
      fprintf(stdout, "Video: OpenGL %s, vsync=%s, %s, %s sampling\n",
              Dkc2DesktopGlVersion(&s_gl_presenter),
              Dkc2DesktopVsyncStatusName(
                  Dkc2DesktopGlVsyncStatus(&s_gl_presenter)),
              Dkc2DesktopScreenFilterName(s_screen_filter),
              Dkc2LauncherUpscaler() == 2 && Dkc2DesktopGlReconstructAvailable(&s_gl_presenter)
                  ? "reconstruct" : (s_linear_filter ? "bilinear" : "nearest"));
    } else {
      fprintf(stderr, "warning: %s; falling back to atomic GDI\n", error);
      if (EnvironmentEnabled("DKC2_DESKTOP_REQUIRE_GPU")) {
        s_recreating_window = true;
        DestroyWindow(s_window);
        s_recreating_window = false;
        s_window = NULL;
        return false;
      }
      /* SetPixelFormat is permanent for an HWND. Recreate it before using
       * GDI so a failed OpenGL attempt cannot weaken the fallback. */
      s_recreating_window = true;
      DestroyWindow(s_window);
      s_recreating_window = false;
      s_window = CreateWindowExA(
          0, window_class.lpszClassName, title, style, x, y,
          rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
          NULL, NULL, instance, NULL);
      if (!s_window) return false;
      s_gl_active = false;
    }
  }
  if (!s_gl_active)
    fprintf(stdout, "Video: atomic GDI compatibility presenter\n");
  s_menu = Dkc2WindowsMenuCreate(s_window);
  if (!s_menu) return false;
  if (s_fullscreen) {
    Dkc2WindowsMenuSetVisible(s_menu, false);
    RECT restore = {100, 100, 100 + width, 100 + height};
    AdjustWindowRect(&restore, WS_OVERLAPPEDWINDOW, TRUE);
    s_windowed_placement.showCmd = SW_SHOWNORMAL;
    s_windowed_placement.rcNormalPosition = restore;
  }
  ShowWindow(s_window, s_test_hidden ? SW_HIDE : SW_SHOW);
  UpdateWindow(s_window);
  return true;
}

static void StopHaptics(bool all) {
  if (!all) return;  /* SDL stops each bounded pulse automatically. */
  for (int i = 0; i < kDkc2MaximumGamepads; ++i)
    if (s_gamepads.handles[i])
      (void)SDL_GameControllerRumble(s_gamepads.handles[i], 0, 0, 0);
}

static void PulseHaptics(unsigned players, bool test) {
  if (!Dkc2LauncherHaptics() || GetForegroundWindow() != s_window) return;
  for (int player = 0; player < 2; ++player) {
    SDL_GameController *handle = Dkc2GamepadsSelected(&s_gamepads, player);
    if (!(players & (1u << player)) || !handle) continue;
    if (SDL_GameControllerRumble(handle, test ? 0x5000 : 0x2800,
                                test ? 0x7000 : 0x5000, test ? 500 : 55) != 0)
      Dkc2DesktopOverlaySetHapticsDevice(s_overlay, player,
                                        SDL_GameControllerName(handle), false);
  }
}

static bool IsPressed(int virtual_key) {
  return (GetAsyncKeyState(virtual_key) & 0x8000) != 0 ||
      Dkc2WindowsKeyTapped(&s_key_taps, (unsigned)virtual_key);
}

static bool IsScancodePressed(int scancode, void *context) {
  (void)context;
  int key = Dkc2WindowsVirtualKeyFromScancode(scancode);
  return key != 0 && IsPressed(key);
}

static DesktopControls ReadControlsImpl(void) {
  DesktopControls controls = {0, 0};
  Dkc2GamepadsPoll(&s_gamepads);
  if (GetForegroundWindow() != s_window || Dkc2WindowsMenuBlocksInput(s_menu)) {
    Dkc2DesktopOverlaySetGamepad(s_overlay, NULL);
    s_menu_chord_previous = false;
    s_perf_hotkey_previous = false;
    return controls;
  }
  if (IsPressed(VK_ESCAPE) || IsPressed(VK_MENU)) return controls;
  bool perf_hotkey = IsPressed('F');
  if (perf_hotkey && !s_perf_hotkey_previous)
    SetPerformanceLogging(!s_perf_enabled);
  s_perf_hotkey_previous = perf_hotkey;

  uint32_t keyboard_input[kDkc2DesktopPlayerCount];
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++)
    keyboard_input[player] = Dkc2MapKeyboardBindings(
        s_player_key_bind[player], IsScancodePressed, NULL);

  Dkc2GamepadState gamepads[kDkc2MaximumGamepads];
  size_t gamepad_count = Dkc2GamepadsReadAll(&s_gamepads, gamepads);
  Dkc2GamepadState assigned[2];
  size_t assigned_count = Dkc2GamepadsAssign(
      &s_gamepads, s_player_source, s_player_guid, assigned);
  for (int player = 0; player < 2; ++player) {
    SDL_GameController *handle = Dkc2GamepadsSelected(&s_gamepads, player);
    Dkc2DesktopOverlaySetHapticsDevice(s_overlay, player,
        handle ? SDL_GameControllerName(handle) : NULL,
        handle && SDL_GameControllerHasRumble(handle));
  }
  controls.controller = Dkc2RoutePlayerInputsWithBindings(
      keyboard_input, assigned, assigned_count, s_player_source,
      s_player_deadzone, s_player_pad_bind);
  controls.host_actions = Dkc2MapAssistBindings(
      s_assist_key_bind, s_assist_pad_bind, IsScancodePressed, NULL,
      gamepads, gamepad_count, 30);
  uint32_t menu_buttons = gamepad_count ? gamepads[0].buttons : 0;
  const Dkc2GamepadState *player_pads[2] = {NULL, NULL};
  size_t pad_index = 0;
  for (int player = 0; player < 2; ++player)
    if (s_player_source[player] == kDkc2InputSourceGamepad)
      player_pads[player] = &assigned[pad_index++];
  Dkc2DesktopOverlaySetPlayerGamepads(s_overlay, player_pads,
      gamepad_count ? &gamepads[0] : NULL);
  uint32_t menu_input = Dkc2UpdateMenuChord(menu_buttons, &s_menu_chord_previous);
  if (menu_input & kDkc2MenuInputToggle)
    Dkc2DesktopOverlayToggle(s_overlay);
  if ((menu_input & kDkc2MenuInputBlock) || Dkc2DesktopOverlayIsOpen(s_overlay)) {
    controls.controller = 0;
    controls.host_actions = 0;
  }
  return controls;
}

static DesktopControls ReadControls(void) {
  DesktopControls controls = ReadControlsImpl();
  Dkc2WindowsKeysClear(&s_key_taps);
  return controls;
}

static bool InitializeAudio(void) {
  WAVEFORMATEX format;
  memset(&format, 0, sizeof format);
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = kAudioChannels;
  format.nSamplesPerSec = kAudioRate;
  format.wBitsPerSample = 16;
  format.nBlockAlign =
      (WORD)(format.nChannels * format.wBitsPerSample / 8u);
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

  MMRESULT result = waveOutOpen(&s_audio.device, WAVE_MAPPER, &format,
                                0, 0, CALLBACK_NULL);
  if (result != MMSYSERR_NOERROR) return false;

  for (unsigned i = 0; i < kAudioBlockCount; i++) {
    WAVEHDR *header = &s_audio.headers[i];
    memset(header, 0, sizeof *header);
    header->lpData = (LPSTR)s_audio.blocks[i];
    header->dwBufferLength = sizeof s_audio.blocks[i];
    result = waveOutPrepareHeader(s_audio.device, header, sizeof *header);
    if (result != MMSYSERR_NOERROR) {
      for (unsigned j = 0; j < i; j++) {
        (void)waveOutUnprepareHeader(s_audio.device, &s_audio.headers[j],
                                     sizeof s_audio.headers[j]);
      }
      (void)waveOutClose(s_audio.device);
      s_audio.device = NULL;
      return false;
    }
  }
  s_audio.available = true;
  return true;
}

static bool WaitForAudioBlock(WAVEHDR *header) {
  if (s_audio.submitted_blocks < kAudioBlockCount) return true;
  while (!(header->dwFlags & WHDR_DONE)) {
    if (!PumpMessages()) return false;
    (void)MsgWaitForMultipleObjectsEx(0, NULL, 2, QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
  }
  return true;
}

static bool SubmitCompleteAudioBlock(void) {
  unsigned index = s_audio.next_block;
  WAVEHDR *header = &s_audio.headers[index];
  if (!WaitForAudioBlock(header)) return false;
  memcpy(s_audio.blocks[index], s_audio.staging,
         sizeof s_audio.blocks[index]);
  header->dwFlags &= ~WHDR_DONE;
  MMRESULT result = waveOutWrite(s_audio.device, header, sizeof *header);
  if (result != MMSYSERR_NOERROR) return false;
  s_audio.next_block = (index + 1) % kAudioBlockCount;
  s_audio.submitted_blocks++;
  s_audio.staging_frames = 0;
  return true;
}

static bool AppendAudio(const int16_t *samples, size_t frames) {
  while (frames != 0) {
    size_t available = kAudioBlockFrames - s_audio.staging_frames;
    size_t portion = frames < available ? frames : available;
    int16_t *destination =
        s_audio.staging + s_audio.staging_frames * kAudioChannels;
    size_t sample_count = portion * kAudioChannels;
    if (s_audio_volume == 100) {
      memcpy(destination, samples, sample_count * sizeof samples[0]);
    } else {
      for (size_t i = 0; i < sample_count; i++) {
        destination[i] =
            (int16_t)(((int)samples[i] * s_audio_volume) / 100);
      }
    }
    s_audio.staging_frames += portion;
    samples += portion * kAudioChannels;
    frames -= portion;
    if (s_audio.staging_frames == kAudioBlockFrames &&
        !SubmitCompleteAudioBlock())
      return false;
  }
  return true;
}

static void ResetAudioQueue(void) {
  if (!s_audio.device) return;
  (void)waveOutReset(s_audio.device);
  s_audio.staging_frames = 0;
  s_audio.next_block = 0;
  s_audio.submitted_blocks = 0;
}

static void ShutdownAudio(void) {
  if (!s_audio.device) return;
  (void)waveOutReset(s_audio.device);
  for (unsigned i = 0; i < kAudioBlockCount; i++) {
    (void)waveOutUnprepareHeader(s_audio.device, &s_audio.headers[i],
                                 sizeof s_audio.headers[i]);
  }
  (void)waveOutClose(s_audio.device);
  s_audio.device = NULL;
  s_audio.available = false;
}

static void WaitUntil(LARGE_INTEGER deadline, LARGE_INTEGER frequency) {
  while (s_running) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (now.QuadPart >= deadline.QuadPart) return;
    LONGLONG remaining = deadline.QuadPart - now.QuadPart;
    DWORD milliseconds =
        (DWORD)((remaining * 1000) / frequency.QuadPart);
    if (milliseconds > 1) milliseconds--;
    (void)MsgWaitForMultipleObjectsEx(0, NULL, milliseconds, QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
    if (!PumpMessages()) return;
  }
}

static void UpdateFpsWindowTitle(Dkc2DesktopFpsCounter *counter,
                                 bool presented, LARGE_INTEGER now,
                                 LARGE_INTEGER frequency) {
  unsigned fps = 0;
  if (!Dkc2DesktopFpsUpdate(counter, presented, (uint64_t)now.QuadPart,
                            (uint64_t)frequency.QuadPart, &fps))
    return;
  char title[112];
  bool assist_tools = Dkc2DesktopOverlayAssistTools(s_overlay);
  if (s_perf_enabled) {
    if (assist_tools)
      (void)snprintf(title, sizeof title,
                     DKC2_PRODUCT_TITLE " (FPS: %u) (CPU: %.0f%%) "
                     "(Assist Tools: On)",
                     fps, s_perf_busy_percent);
    else
      (void)snprintf(title, sizeof title,
                     DKC2_PRODUCT_TITLE " (FPS: %u) (CPU: %.0f%%)",
                     fps, s_perf_busy_percent);
  } else {
    if (assist_tools)
      (void)snprintf(title, sizeof title,
                     DKC2_PRODUCT_TITLE
                     " (FPS: %u) (Assist Tools: On)",
                     fps);
    else
      (void)snprintf(title, sizeof title,
                     DKC2_PRODUCT_TITLE " (FPS: %u)", fps);
  }
  if (s_input_recording_active) {
    size_t used = strlen(title);
    (void)snprintf(title + used, sizeof title - used, " (Recording Input)");
  }
  (void)SetWindowTextA(s_window, title);
}

static void ApplyOverlaySettings(RecompLauncherCSettings *settings) {
  if (!settings || !s_overlay) return;
  RecompLauncherCSettings updated;
  Dkc2DesktopOverlayGetSettings(s_overlay, &updated);
  if ((updated.fullscreen != 0) != (s_fullscreen != 0))
    (void)SetFullscreen(updated.fullscreen != 0);
  updated.fullscreen = s_fullscreen;
  updated.volume =
      updated.volume < 0 ? 0 : (updated.volume > 100 ? 100 : updated.volume);
  updated.texture_filter = updated.texture_filter != 0;
  updated.aspect_index =
      ClampInt(updated.aspect_index, kDkc2VideoAspectNative,
               kDkc2VideoAspectCount - 1);
  updated.widescreen =
      updated.aspect_index != kDkc2VideoAspectNative;
  if (!Dkc2DesktopScreenFilterValid(updated.screen_kind))
    updated.screen_kind = kDkc2ScreenRaw;
  if (updated.screen_kind != s_screen_filter) {
    Dkc2DesktopColorFilterDestroy(&s_color_filter);
    if (Dkc2DesktopColorFilterInit(&s_color_filter, updated.screen_kind))
      s_screen_filter = updated.screen_kind;
    else
      (void)Dkc2DesktopColorFilterInit(&s_color_filter, s_screen_filter);
  }
  s_linear_filter = updated.texture_filter != 0;
  s_audio_volume = updated.volume;
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    int source = updated.player_src[player];
    int deadzone = updated.deadzone[player];
    s_player_source[player] =
        source < 0 ? 0 : (source > 2 ? 2 : source);
    s_player_deadzone[player] =
        deadzone < 0 ? 0 : (deadzone > 100 ? 100 : deadzone);
    updated.player_src[player] = s_player_source[player];
    updated.deadzone[player] = s_player_deadzone[player];
  }
  memcpy(s_player_key_bind, updated.player_key_bind,
         sizeof s_player_key_bind);
  memcpy(s_player_guid, updated.player_gamepad_guid, sizeof s_player_guid);
  memcpy(s_player_pad_bind, updated.player_pad_bind,
         sizeof s_player_pad_bind);
  memcpy(s_assist_key_bind, updated.assist_key_bind,
         sizeof s_assist_key_bind);
  memcpy(s_assist_pad_bind, updated.assist_pad_bind,
         sizeof s_assist_pad_bind);
  if (Dkc2VideoGetAspect() != (Dkc2VideoAspect)updated.aspect_index) {
    Dkc2VideoSetAspect((Dkc2VideoAspect)updated.aspect_index);
    memset(s_pixels, 0, sizeof s_pixels);
    memset(s_filtered_pixels, 0, sizeof s_filtered_pixels);
    s_bitmap_info.bmiHeader.biWidth = Dkc2VideoWidth();
    Dkc2BeginDrawing(
        s_pixels, (size_t)Dkc2VideoWidth() * kBytesPerPixel);
    if (s_window) InvalidateRect(s_window, NULL, FALSE);
  }
  *settings = updated;
}

static uint32_t ApplyMenuCommands(RecompLauncherCSettings *settings) {
  uint32_t actions = 0;
  Dkc2DesktopOverlayGetSettings(s_overlay, settings);
  Dkc2MenuState state = Dkc2MenuReadSettings(settings, s_fullscreen != 0,
      Dkc2DesktopOverlayIsOpen(s_overlay), true,
      Dkc2DesktopGlReconstructAvailable(&s_gl_presenter));
  unsigned command;
  while ((command = Dkc2WindowsMenuTakeCommand(s_menu)) != 0) {
    switch (command) {
      case kDkc2MenuPause: Dkc2DesktopOverlayToggle(s_overlay); break;
      case kDkc2MenuSettings:
        if (!Dkc2DesktopOverlayIsOpen(s_overlay))
          Dkc2DesktopOverlayToggle(s_overlay);
        break;
      case kDkc2MenuSave: actions |= kDkc2HostSaveState; break;
      case kDkc2MenuLoad: actions |= kDkc2HostLoadState; break;
      case kDkc2MenuFullscreen:
        if (SetFullscreen(s_fullscreen == 0)) settings->fullscreen = s_fullscreen;
        break;
      case kDkc2MenuQuit: s_running = false; break;
      case kDkc2MenuAbout:
        MessageBoxA(s_window, "DKC2Recomp " DKC2_RELEASE_VERSION
            "\nNative PC port foundation.\nRequires your own North American v1.0 ROM.",
            "About DKC2Recomp", MB_OK | MB_ICONINFORMATION);
        break;
      default: (void)Dkc2MenuChangeSettings(settings, &state, command); break;
    }
  }
  Dkc2DesktopOverlaySetSettings(s_overlay, settings);
  return actions;
}

static int RunDesktop(const char *rom_path,
                      RecompLauncherCSettings *settings) {
  unsigned long long test_frame_limit = 0;
  const char *test_frames = getenv("DKC2_DESKTOP_TEST_FRAMES");
  if (test_frames && *test_frames) {
    char *end = NULL;
    test_frame_limit = strtoull(test_frames, &end, 10);
    if (!end || *end != '\0' || test_frame_limit == 0 ||
        test_frame_limit > 1000000) {
      fprintf(stderr,
              "DKC2_DESKTOP_TEST_FRAMES must be between 1 and 1000000\n");
      return 2;
    }
  }
  char crt_error[160];
  if (!Dkc2LauncherApplyCrtEnvironment(crt_error, sizeof crt_error)) {
    fprintf(stderr, "%s\n", crt_error);
    return 2;
  }
  s_test_hidden = EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  bool test_rewind_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_REWIND");
  bool test_fast_forward_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_FASTFORWARD");
  bool test_overlay_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_OVERLAY");
  bool sram_enabled = !EnvironmentEnabled("DKC2_DESKTOP_DISABLE_SRAM");
  int persisted_aspect =
      ClampInt(settings->aspect_index, kDkc2VideoAspectNative,
               kDkc2VideoAspectCount - 1);
  Dkc2VideoAspect aspect = (Dkc2VideoAspect)persisted_aspect;
  const char *aspect_override = getenv("DKC2_ASPECT");
  bool aspect_override_active = aspect_override && *aspect_override;
  if (aspect_override_active &&
      !Dkc2VideoAspectFromName(aspect_override, &aspect)) {
    fprintf(stderr, "DKC2_ASPECT must be 4:3, 16:10, 16:9, or 21:9\n");
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
  Dkc2VideoSetEdgePolicy((Dkc2VideoEdgePolicy)Dkc2LauncherWidescreenEdge());
  {
    const char *edge_text = getenv("DKC2_WIDESCREEN_EDGE");
    Dkc2VideoEdgePolicy edge_policy = kDkc2VideoEdgeGlide;
    if (edge_text && *edge_text) {
      if (!Dkc2VideoEdgePolicyFromName(edge_text, &edge_policy)) {
        fprintf(stderr,
                "DKC2_WIDESCREEN_EDGE must be reflect, bars, shift, or glide\n");
        return 2;
      }
      Dkc2VideoSetEdgePolicy(edge_policy);
    }
  }
  Dkc2CoopSetMode((Dkc2CoopMode)Dkc2LauncherCoopMode());
  {
    const char *coop_text = getenv("DKC2_COOP");
    Dkc2CoopMode coop_mode = kDkc2CoopSimultaneous;
    if (coop_text && *coop_text) {
      if (!Dkc2CoopModeFromName(coop_text, &coop_mode)) {
        fprintf(stderr,
                "DKC2_COOP must be simultaneous or classic\n");
        return 2;
      }
      Dkc2CoopSetMode(coop_mode);
    }
  }
  const char *screen_override = getenv("DKC2_SCREEN");
  if (screen_override && *screen_override &&
      !Dkc2DesktopScreenFilterFromName(screen_override, &s_screen_filter)) {
    fprintf(stderr,
            "DKC2_SCREEN must be raw, crt, composite, or trinitron\n");
    return 2;
  }

  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc2ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    fprintf(stderr, "%s: %s\n", rom_error, rom_path);
    MessageBoxA(NULL, rom_error, "Unable to start DKC2", MB_OK | MB_ICONERROR);
    return 2;
  }

  /* Keep saves beside the executable even when the program was launched from
   * a shortcut, terminal, or ROM directory. The ROM is already resident, so
   * changing the process working directory cannot invalidate its path. */
  (void)snesrecomp_anchor_to_exe_dir();

  Dkc2KongsInitialize();
  Dkc2MusicInitialize();
  RtlRegisterGame(Dkc2GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fprintf(stderr, "snesrecomp rejected the verified ROM\n");
    free(rom);
    return 3;
  }
  if (sram_enabled) {
    if (EnsureSaveDirectory()) {
      RtlReadSram();
      fprintf(stdout, "SRAM: persistence enabled at saves/save.srm (%d bytes)\n",
              g_sram_size);
    } else {
      fprintf(stderr,
              "warning: unable to create the saves directory; SRAM "
              "persistence is disabled for this run\n");
      sram_enabled = false;
    }
  }
  if (!Dkc2DesktopColorFilterInit(&s_color_filter, s_screen_filter)) {
    fprintf(stderr, "unable to initialize the %s screen-color filter\n",
            Dkc2DesktopScreenFilterName(s_screen_filter));
    free(rom);
    return 4;
  }
  if (!CreateGameWindow()) {
    fprintf(stderr, "unable to create the DKC2 test window\n");
    Dkc2DesktopColorFilterDestroy(&s_color_filter);
    free(rom);
    return 4;
  }
  s_overlay = Dkc2DesktopOverlayCreate(settings);
  if (!s_overlay) {
    fprintf(stderr, "unable to create the in-game settings model\n");
    Dkc2DesktopGlPresenterDestroy(&s_gl_presenter);
    if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
    Dkc2DesktopColorFilterDestroy(&s_color_filter);
    free(rom);
    return 4;
  }
  {
    bool overlay_ready = s_gl_active
        ? Dkc2DesktopOverlayInitWin32(s_overlay, s_window)
        : Dkc2DesktopOverlayInitWin32Software(s_overlay, s_window);
    if (!overlay_ready) {
      fprintf(stderr, "unable to initialize the in-game overlay\n");
      Dkc2DesktopOverlayDestroy(s_overlay);
      s_overlay = NULL;
      Dkc2DesktopGlPresenterDestroy(&s_gl_presenter);
      if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
      Dkc2DesktopColorFilterDestroy(&s_color_filter);
      free(rom);
      return 4;
    }
  }
  Dkc2DesktopOverlaySetReconstructAvailable(s_overlay,
      Dkc2DesktopGlReconstructAvailable(&s_gl_presenter));
  Dkc2DesktopOverlaySetCrtAvailable(s_overlay,
      Dkc2DesktopGlCrtAvailable(&s_gl_presenter));
  if (s_gl_active && !Dkc2DesktopGlReconstructAvailable(&s_gl_presenter))
    fprintf(stderr, "warning: %s\n", Dkc2DesktopGlShaderError(&s_gl_presenter));
  s_presenter.overlay_draw = Dkc2DesktopOverlayRenderSoftware;
  s_presenter.overlay_user = s_overlay;
  Dkc2BeginDrawing(
      s_pixels, (size_t)Dkc2VideoWidth() * kBytesPerPixel);

  if (s_audio_enabled && !InitializeAudio()) {
    if (test_frame_limit) {
      fprintf(stderr, "Windows audio could not be opened in desktop test\n");
      Dkc2DesktopOverlayDestroy(s_overlay);
      s_overlay = NULL;
      Dkc2DesktopGlPresenterDestroy(&s_gl_presenter);
      if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
      Dkc2DesktopColorFilterDestroy(&s_color_filter);
      free(rom);
      return 6;
    }
    fprintf(stderr,
            "warning: Windows audio could not be opened; continuing silent\n");
  }
  char presentation_backend[96];
  if (s_gl_active) {
    (void)snprintf(
        presentation_backend, sizeof presentation_backend,
        "OpenGL; vsync=%s",
        Dkc2DesktopVsyncStatusName(
            Dkc2DesktopGlVsyncStatus(&s_gl_presenter)));
  } else {
    (void)snprintf(presentation_backend, sizeof presentation_backend,
                   "GDI; compositor-managed");
  }
  Dkc2DiagnosticsSetPresentation(
      presentation_backend,
      Dkc2DesktopScreenFilterName(s_screen_filter), s_audio.available);
  (void)timeBeginPeriod(1);

  /* Hidden acceptance runs use copied external checkpoints, like the
   * headless host. Normal launches continue to use the UI's save slots. */
  if (s_test_hidden) {
    const char *test_state = getenv("DKC2_SAVESTATE_INPUT");
    if (test_state && *test_state && !RtlLoadSnapshot(test_state)) {
      fprintf(stderr, "unable to load desktop test checkpoint: %s\n", test_state);
      return 1;
    }
  }
  LARGE_INTEGER frequency;
  LARGE_INTEGER deadline;
  QueryPerformanceFrequency(&frequency);
  s_perf_frequency = frequency;
  QueryPerformanceCounter(&deadline);
  Dkc2DesktopFpsCounter fps_counter;
  Dkc2DesktopFpsInit(&fps_counter, (uint64_t)deadline.QuadPart);
  if (EnvironmentEnabled("DKC2_DESKTOP_PERF")) SetPerformanceLogging(true);
  double deadline_fraction = 0.0;
  double audio_fraction = 0.0;
  unsigned long long host_frame = 0;
  unsigned rewind_capture_counter = 0;
  int16_t frame_audio[kMaximumFrameAudio * kAudioChannels];
  Dkc2RewindHistory rewind_history;
  memset(&rewind_history, 0, sizeof rewind_history);
  size_t rewind_snapshot_size = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *rewind_scratch = NULL;
  bool rewind_available = false;
  bool test_rewind_completed = false;
  bool test_fast_forward_completed = false;
  bool test_overlay_completed = false;
  unsigned test_overlay_ticks = 0;
  bool runtime_failure = false;
  DesktopSpeedMode previous_mode = kDesktopSpeedNormal;
  uint32_t previous_state_actions = 0;
  bool previous_overlay_open = false;
  Dkc2InputRecorder input_recorder = {0};
  char input_recording_error[512] = {0};
  const char *input_recording_path = getenv("SNESRECOMP_INPUT_REC");

  if (rewind_snapshot_size != 0) {
    rewind_scratch = (uint8_t *)malloc(rewind_snapshot_size);
    if (rewind_scratch &&
        Dkc2RewindHistoryInit(&rewind_history, rewind_snapshot_size,
                              kRewindSnapshotCapacity) &&
        RtlSaveSnapshotToMemory(rewind_scratch, rewind_snapshot_size) ==
            rewind_snapshot_size &&
        Dkc2RewindHistoryPush(&rewind_history, rewind_scratch)) {
      rewind_available = true;
      fprintf(stdout,
              "Rewind: %u snapshots, approximately 15 seconds at 3x\n",
              kRewindSnapshotCapacity);
    } else {
      Dkc2RewindHistoryDestroy(&rewind_history);
      free(rewind_scratch);
      rewind_scratch = NULL;
    }
  }
  if (!rewind_available)
    fprintf(stderr, "warning: rewind history could not be allocated\n");

  if (input_recording_path && *input_recording_path) {
    if (!Dkc2InputRecorderOpen(
            &input_recorder, input_recording_path,
            input_recording_error, sizeof input_recording_error)) {
      fprintf(stderr, "Input recording failed: %s\n", input_recording_error);
      MessageBoxA(s_window, input_recording_error,
                  "Input recording could not start",
                  MB_OK | MB_ICONERROR);
      runtime_failure = true;
      s_running = false;
    } else {
      fprintf(stdout, "Input recording enabled: %s\n", input_recording_path);
      s_input_recording_active = true;
      SetWindowTextA(s_window, DKC2_PRODUCT_TITLE " (Recording Input)");
    }
  }

  if (!Dkc2GamepadsInit(&s_gamepads))
    fprintf(stderr, "Controller initialization failed: %s\n", SDL_GetError());
  fprintf(stdout,
          "Controls: gameplay and Assist bindings are configurable in the "
          "pre-boot launcher. F=Performance log and Escape=Pause menu (OpenGL/GDI). SDL gamepads (USB/Bluetooth) are detected "
          "automatically.\n");
  while (s_running) {
    if (s_test_hidden && Dkc2DeveloperBeatLevelKey()) {
      const char *developer_frame = getenv("DKC2_DESKTOP_TEST_DEVELOPER_FRAME");
      if (developer_frame && host_frame == strtoull(developer_frame, NULL, 10))
        PostMessageW(s_window, WM_KEYDOWN, (WPARAM)(VK_F1 + Dkc2DeveloperBeatLevelKey() - 1), 0);
    }
    if (!PumpMessages()) break;
    Dkc2DiagnosticsHeartbeat(host_frame, Dkc2ResumePc());
    host_report_crash_test_tick();
    uint64_t perf_start = PerformanceBegin();
    uint32_t platform_host_actions = ApplyMenuCommands(settings);
    if (!s_running) break;
    StopHaptics(!Dkc2LauncherHaptics() || GetForegroundWindow() != s_window);
    DesktopControls controls = ReadControls();
    PerformanceEnd(kDkc2PerfInput, perf_start);
    if (test_overlay_requested && !test_overlay_completed &&
        host_frame >= 30) {
      if (test_overlay_ticks == 0) {
        Dkc2DesktopOverlayToggle(s_overlay);
        test_overlay_ticks = 1;
      } else if (Dkc2DesktopOverlayIsOpen(s_overlay)) {
        test_overlay_ticks++;
        if (test_overlay_ticks >= 30) {
          Dkc2DesktopOverlayToggle(s_overlay);
          test_overlay_completed = true;
        }
      }
    }
    uint32_t overlay_actions =
        Dkc2DesktopOverlayTakeActions(s_overlay);
    if (overlay_actions & kDkc2OverlayActionQuit) {
      s_running = false;
      break;
    }
    if (overlay_actions & kDkc2OverlayActionSaveState)
      controls.host_actions |= kDkc2HostSaveState;
    if (overlay_actions & kDkc2OverlayActionLoadState)
      controls.host_actions |= kDkc2HostLoadState;
    ApplyOverlaySettings(settings);
    unsigned haptic_tests = ((overlay_actions & kDkc2OverlayActionTestHapticsP1) ? 1u : 0u) |
                            ((overlay_actions & kDkc2OverlayActionTestHapticsP2) ? 2u : 0u);
    PulseHaptics(haptic_tests, true);
    Dkc2MenuState menu_state = Dkc2MenuReadSettings(settings, s_fullscreen != 0,
        Dkc2DesktopOverlayIsOpen(s_overlay), true,
      Dkc2DesktopGlReconstructAvailable(&s_gl_presenter));
    Dkc2WindowsMenuUpdate(s_menu, &menu_state);
    bool overlay_open = Dkc2DesktopOverlayIsOpen(s_overlay);
    if (overlay_open != previous_overlay_open) {
      if (overlay_open) Dkc2CoopRescuePause();
      ResetAudioQueue();
      audio_fraction = 0.0;
      QueryPerformanceCounter(&deadline);
      deadline_fraction = 0.0;
      previous_overlay_open = overlay_open;
    }
    bool assist_tools = Dkc2DesktopOverlayAssistTools(s_overlay);
    controls.host_actions = Dkc2ApplyAssistGate(
        controls.host_actions, platform_host_actions, assist_tools);
    const uint32_t state_actions =
        controls.host_actions & (kDkc2HostSaveState | kDkc2HostLoadState);
    const uint32_t pressed_state_actions =
        state_actions & ~previous_state_actions;
    previous_state_actions = state_actions;

    if (pressed_state_actions & kDkc2HostSaveState) {
      int slot = Dkc2DesktopOverlaySelectedSlot(s_overlay);
      char path[128];
      RtlSaveSlotPath(slot, path, sizeof path);
      bool saved = EnsureSaveDirectory() && RtlSaveSnapshot(path);
      char status[80];
      (void)snprintf(status, sizeof status,
                     saved ? "Slot %d saved." : "Slot %d save failed.",
                     slot + 1);
      Dkc2DesktopOverlaySetStatus(
          s_overlay, status, saved);
      (void)snprintf(status, sizeof status,
                     saved ? DKC2_PRODUCT_TITLE " - Slot %d saved"
                           : DKC2_PRODUCT_TITLE " - Slot %d save failed",
                     slot + 1);
      SetWindowTextA(s_window, status);
    }
    if (pressed_state_actions & kDkc2HostLoadState) {
      int slot = Dkc2DesktopOverlaySelectedSlot(s_overlay);
      char path[128];
      RtlSaveSlotPath(slot, path, sizeof path);
      bool loaded = RtlLoadSnapshot(path);
      if (!loaded && slot == 0)
        loaded = RtlLoadSnapshot(DKC2_STATE_SLOT0_LEGACY_FILE);
      if (loaded) {
        char status[80];
        (void)snprintf(status, sizeof status, "Slot %d loaded.", slot + 1);
        Dkc2DesktopOverlaySetStatus(s_overlay, status, true);
        ResetAudioQueue();
        audio_fraction = 0.0;
        QueryPerformanceCounter(&deadline);
        deadline_fraction = 0.0;
        rewind_history.count = 0;
        rewind_history.write_index = 0;
        rewind_capture_counter = 0;
        if (rewind_available &&
            RtlSaveSnapshotToMemory(rewind_scratch, rewind_snapshot_size) ==
                rewind_snapshot_size)
          (void)Dkc2RewindHistoryPush(&rewind_history, rewind_scratch);
        Dkc2DrawPpuFrame();
        InvalidateRect(s_window, NULL, FALSE);
        UpdateWindow(s_window);
        (void)snprintf(status, sizeof status,
                       DKC2_PRODUCT_TITLE " - Slot %d loaded", slot + 1);
        SetWindowTextA(s_window, status);
      } else {
        char status[80];
        (void)snprintf(status, sizeof status,
                       "Slot %d could not be loaded.", slot + 1);
        Dkc2DesktopOverlaySetStatus(
            s_overlay, status, false);
        SetWindowTextA(s_window, DKC2_PRODUCT_TITLE " - Load failed");
      }
    }
    if (test_fast_forward_requested && !test_fast_forward_completed &&
        host_frame >= 60)
      controls.host_actions |= kDkc2HostFastForward;
    if (test_rewind_requested && !test_rewind_completed &&
        host_frame >= 120)
      controls.host_actions |= kDkc2HostRewind;

    DesktopSpeedMode mode = kDesktopSpeedNormal;
    if (controls.host_actions & kDkc2HostRewind)
      mode = kDesktopSpeedRewind;
    else if (controls.host_actions & kDkc2HostFastForward)
      mode = kDesktopSpeedFastForward;

    if (mode != previous_mode) {
      ResetAudioQueue();
      audio_fraction = 0.0;
      QueryPerformanceCounter(&deadline);
      deadline_fraction = 0.0;
      previous_mode = mode;
    }

    bool frame_ready = overlay_open;
    if (overlay_open) {
      mode = kDesktopSpeedNormal;
    } else if (mode == kDesktopSpeedRewind) {
      if (rewind_available &&
          Dkc2RewindHistoryPop(&rewind_history, rewind_scratch)) {
        perf_start = PerformanceBegin();
        if (!RtlLoadSnapshotFromMemory(rewind_scratch,
                                       rewind_snapshot_size)) {
          fprintf(stderr, "rewind snapshot restore failed\n");
          runtime_failure = true;
          break;
        }
        PerformanceEnd(kDkc2PerfRewind, perf_start);
        perf_start = PerformanceBegin();
        Dkc2DrawPpuFrame();
        PerformanceEnd(kDkc2PerfPpu, perf_start);
        frame_ready = true;
        if (test_rewind_requested) test_rewind_completed = true;
      }
    } else {
      int frames_to_run = mode == kDesktopSpeedFastForward
                        ? kHostSpeedMultiplier : 1;
      unsigned long long iteration_start_frame = host_frame;
      for (int run = 0; run < frames_to_run && s_running; run++) {
        if (Dkc2InputRecorderIsOpen(&input_recorder) &&
            !Dkc2InputRecorderWrite(
                &input_recorder, controls.controller,
                input_recording_error, sizeof input_recording_error)) {
          fprintf(stderr, "Input recording failed: %s\n",
                  input_recording_error);
          Dkc2DiagnosticsFatal(input_recording_error);
          MessageBoxA(s_window, input_recording_error,
                      "Input recording stopped", MB_OK | MB_ICONERROR);
          runtime_failure = true;
          s_running = false;
          break;
        }
        /* The current upstream-compatible RtlRunFrame return value is not a
         * success flag; runtime health is reported by g_fail and the DKC2 LLE
         * continuation result, as in the headless host. */
        perf_start = PerformanceBegin();
        (void)Dkc2CoopTakeStompEvents();
        if (s_developer_beat_level_requested) {
          s_developer_beat_level_requested = false;
          Dkc2DeveloperResult result = Dkc2DeveloperCommand(&g_cpu, "beat_level");
          bool executed = result == kDkc2DeveloperExecuted;
          Dkc2DesktopOverlaySetStatus(s_overlay,
              executed ? "Developer: level completed." : "Developer: beat level unavailable here.", executed);
          fprintf(stdout, "developer command=beat_level result=%s\n", executed ? "executed" : "unavailable");
        }
        (void)RtlRunFrame(controls.controller);
        PulseHaptics(Dkc2CoopTakeStompEvents(), false);
        PerformanceEnd(kDkc2PerfEmulation, perf_start);
        if (g_fail || !Dkc2LastLleResult()) {
          char message[160];
          (void)snprintf(message, sizeof message,
                         "Runtime stopped at frame %llu (resume PC $%06x).",
                         host_frame + 1, (unsigned)Dkc2ResumePc());
          fprintf(stderr, "%s\n", message);
          Dkc2DiagnosticsFatal(message);
          if (!s_test_hidden)
            MessageBoxA(s_window, message, "DKC2 runtime failure",
                        MB_OK | MB_ICONERROR);
          runtime_failure = true;
          break;
        }

        host_frame++;
        rewind_capture_counter++;
        if (rewind_available &&
            rewind_capture_counter >= kRewindSnapshotInterval) {
          rewind_capture_counter = 0;
          perf_start = PerformanceBegin();
          if (RtlSaveSnapshotToMemory(rewind_scratch,
                                      rewind_snapshot_size) !=
                  rewind_snapshot_size ||
              !Dkc2RewindHistoryPush(&rewind_history, rewind_scratch)) {
            fprintf(stderr,
                    "warning: rewind capture failed; rewind is disabled\n");
            rewind_available = false;
          }
          PerformanceEnd(kDkc2PerfRewind, perf_start);
        }

        /* Snapshots are captured before drawing because the renderer advances
         * HDMA state. A restored snapshot is drawn once above to recreate the
         * same post-draw state before normal execution resumes. */
        perf_start = PerformanceBegin();
        Dkc2DrawPpuFrame();
        PerformanceEnd(kDkc2PerfPpu, perf_start);
        frame_ready = true;

        perf_start = PerformanceBegin();
        audio_fraction += (double)kAudioRate / kVideoRate;
        int audio_frames = (int)audio_fraction;
        audio_fraction -= audio_frames;
        RtlRenderAudio(frame_audio, audio_frames, kAudioChannels);
        Dkc2MusicMix(frame_audio, audio_frames, 32040);
        if (mode == kDesktopSpeedNormal && s_audio.available &&
            !AppendAudio(frame_audio, (size_t)audio_frames)) {
          fprintf(stderr,
                  "warning: Windows audio output stopped; continuing silent\n");
          ShutdownAudio();
        }
        PerformanceEnd(kDkc2PerfAudio, perf_start);

        if (test_frame_limit && host_frame >= test_frame_limit) {
          s_running = false;
          break;
        }
      }
      if (runtime_failure) break;
      if (test_fast_forward_requested &&
          mode == kDesktopSpeedFastForward &&
          host_frame - iteration_start_frame == kHostSpeedMultiplier)
        test_fast_forward_completed = true;
    }

    if (frame_ready) {
      perf_start = PerformanceBegin();
      InvalidateRect(s_window, NULL, FALSE);
      UpdateWindow(s_window);
      PerformanceEnd(kDkc2PerfPresent, perf_start);
      if (s_present_failed) {
        fprintf(stderr, "desktop video presentation failed\n");
        Dkc2DiagnosticsFatal("desktop video presentation failed");
        runtime_failure = true;
        break;
      }
    }

    perf_start = PerformanceBegin();
    if (overlay_open || mode != kDesktopSpeedNormal || !s_audio.available ||
        s_audio.submitted_blocks >= kInitialBufferedBlocks) {
      double ticks = (double)frequency.QuadPart / kVideoRate;
      deadline_fraction += ticks;
      LONGLONG whole_ticks = (LONGLONG)deadline_fraction;
      deadline_fraction -= (double)whole_ticks;
      deadline.QuadPart += whole_ticks;
      WaitUntil(deadline, frequency);
    } else {
      /* Fill a small exact-rate audio queue before starting the wall-clock.
       * This prevents normal scheduler jitter from becoming audible gaps. */
      QueryPerformanceCounter(&deadline);
      deadline_fraction = 0.0;
    }
    PerformanceEnd(kDkc2PerfPace, perf_start);
    LARGE_INTEGER fps_now;
    QueryPerformanceCounter(&fps_now);
    UpdatePerformanceLogging(frame_ready, (uint64_t)fps_now.QuadPart);
    UpdateFpsWindowTitle(&fps_counter, frame_ready, fps_now, frequency);
  }

  if (test_rewind_requested && !test_rewind_completed) {
    fprintf(stderr, "requested desktop rewind test did not restore a state\n");
    runtime_failure = true;
  }
  if (test_fast_forward_requested && !test_fast_forward_completed) {
    fprintf(stderr,
            "requested desktop fast-forward test did not run three frames\n");
    runtime_failure = true;
  }
  if (test_overlay_requested && !test_overlay_completed) {
    fprintf(stderr, "requested desktop overlay test did not complete\n");
    runtime_failure = true;
  }
  unsigned long long recorded_frames = input_recorder.frames;
  s_input_recording_active = false;
  if (!Dkc2InputRecorderClose(
          &input_recorder, input_recording_error,
          sizeof input_recording_error)) {
    fprintf(stderr, "Input recording failed: %s\n", input_recording_error);
    runtime_failure = true;
  } else if (input_recording_path && *input_recording_path) {
    fprintf(stdout, "Input recording completed: %llu frames at %s\n",
            recorded_frames, input_recording_path);
  }
  bool completed_without_failure =
      !runtime_failure && !g_fail && Dkc2LastLleResult();
  const char *frame_output = getenv("DKC2_FRAME_PPM");
  if (frame_output && *frame_output && !WriteFramePpm(frame_output)) {
    fprintf(stderr, "unable to write private desktop frame output: %s\n",
            frame_output);
    completed_without_failure = false;
  }
  if (sram_enabled && completed_without_failure) {
    RtlWriteSram();
    fprintf(stdout, "SRAM: wrote saves/save.srm (%d bytes)\n", g_sram_size);
  }
  if (s_overlay)
    Dkc2DesktopOverlayGetSettings(s_overlay, settings);
  if (aspect_override_active || widescreen_override_active) {
    settings->aspect_index = persisted_aspect;
    settings->widescreen =
        persisted_aspect != kDkc2VideoAspectNative;
  }
  Dkc2DiagnosticsShutdown(completed_without_failure ? "clean_exit"
                                                     : "runtime_failure");
  ShutdownAudio();
  SetPerformanceLogging(false);
  (void)timeEndPeriod(1);
  StopHaptics(true);
  Dkc2GamepadsClose(&s_gamepads);
  Dkc2DesktopOverlayDestroy(s_overlay);
  s_overlay = NULL;
  Dkc2DesktopGlPresenterDestroy(&s_gl_presenter);
  if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
  Dkc2DesktopPresenterDestroy(&s_presenter);
  Dkc2DesktopColorFilterDestroy(&s_color_filter);
  Dkc2RewindHistoryDestroy(&rewind_history);
  free(rewind_scratch);
  free(rom);
  if (test_frame_limit) {
    fprintf(stdout,
            "result=desktop_completed frames=%llu rewind_restore=%s "
            "fast_forward=%s\n",
            host_frame, test_rewind_requested
                      ? (test_rewind_completed ? "passed" : "failed")
                      : "not_requested",
            test_fast_forward_requested
                ? (test_fast_forward_completed ? "passed" : "failed")
                : "not_requested");
  }
  return completed_without_failure ? 0 : 5;
}

static bool SelectRom(char *rom_path, DWORD capacity) {
  OPENFILENAMEA dialog;
  memset(&dialog, 0, sizeof dialog);
  dialog.lStructSize = sizeof dialog;
  dialog.lpstrFilter =
      "SNES ROM files (*.smc;*.sfc)\0*.smc;*.sfc\0"
      "All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = rom_path;
  dialog.nMaxFile = capacity;
  dialog.lpstrTitle = "Select your DKC2 USA v1.0 ROM";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                 OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  return GetOpenFileNameA(&dialog) != FALSE;
}

static bool CopyWidePath(const wchar_t *source, char *destination,
                         int capacity) {
  return WideCharToMultiByte(CP_ACP, 0, source, -1, destination, capacity,
                             NULL, NULL) != 0;
}

#ifdef RECOMP_LAUNCHER
static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}
#endif

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command) {
  (void)instance;
  (void)previous_instance;
  (void)command_line;
  (void)show_command;

  int argument_count = 0;
  LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (!arguments) {
    MessageBoxA(NULL, "Windows could not read the command line.",
                "Unable to start DKC2", MB_OK | MB_ICONERROR);
    return 2;
  }

  bool force_launcher = false;
  int rom_argument = 1;
  if (argument_count >= 2 && lstrcmpiW(arguments[1], L"--launcher") == 0) {
    force_launcher = true;
    rom_argument++;
  }
  if (argument_count > rom_argument + 1) {
    LocalFree(arguments);
    MessageBoxA(NULL,
                "Run DKC2Recomp with an optional ROM path, or use "
                "--launcher followed by an optional ROM path.",
                "How to start DKC2", MB_OK | MB_ICONINFORMATION);
    return 2;
  }

  char rom_path[MAX_PATH] = {0};
  if (argument_count == rom_argument + 1 &&
      !CopyWidePath(arguments[rom_argument], rom_path,
                    (int)sizeof rom_path)) {
    LocalFree(arguments);
    MessageBoxA(NULL, "The ROM path could not be represented by Windows.",
                "Unable to start DKC2", MB_OK | MB_ICONERROR);
    return 2;
  }
  LocalFree(arguments);

  /* The shared ImGui launcher and all runtime state resolve beside the exe,
   * never beside a shortcut, shell, or user-owned ROM. */
  (void)snesrecomp_anchor_to_exe_dir();
  if (!Dkc2DiagnosticsInit("win32", DKC2_RELEASE_VERSION))
    fprintf(stderr, "warning: diagnostics could not be initialized\n");

  RecompLauncherCSettings launcher_settings;
  Dkc2LauncherSettingsDefault(&launcher_settings);
  Dkc2LauncherSettingsLoad(&launcher_settings);
#ifdef RECOMP_LAUNCHER
  if (!rom_path[0])
    (void)Dkc2LauncherReadRomCache(rom_path, sizeof rom_path);

  bool suppress_launcher =
      EnvironmentEnabled("SNESRECOMP_NO_LAUNCHER") ||
      EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  bool show_launcher = !suppress_launcher &&
                       (force_launcher || !launcher_settings.skip_launcher ||
                        !rom_path[0]);
  if (show_launcher) {
    char selected_rom[MAX_PATH] = {0};
    static const char *const renderer_labels[] = {
        "GDI compatibility", "OpenGL"};
    int action = Dkc2LauncherRun(
        &launcher_settings, rom_path, selected_rom, sizeof selected_rom,
        renderer_labels, sizeof renderer_labels / sizeof renderer_labels[0]);
    if (action == 1) return 0;
    if (action == 0 && selected_rom[0]) {
      (void)snprintf(rom_path, sizeof rom_path, "%s", selected_rom);
    } else if (!rom_path[0] &&
               !SelectRom(rom_path, (DWORD)sizeof rom_path)) {
      return 0;
    }
    (void)Dkc2LauncherSettingsSave(&launcher_settings);
  } else if (!rom_path[0] &&
             !SelectRom(rom_path, (DWORD)sizeof rom_path)) {
    return 0;
  }
  s_window_scale = ClampInt(launcher_settings.window_scale, 1, 4);
  s_fullscreen = ClampInt(launcher_settings.fullscreen, 0, 2);
  s_renderer = ClampInt(launcher_settings.renderer, 0, 1);
  s_linear_filter = launcher_settings.texture_filter != 0;
  s_screen_filter = ClampInt(launcher_settings.screen_kind, 0, 3);
  s_audio_enabled = launcher_settings.enable_audio != 0;
  s_audio_volume = ClampInt(launcher_settings.volume, 0, 100);
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    s_player_source[player] =
        ClampInt(launcher_settings.player_src[player], 0, 2);
    s_player_deadzone[player] =
        ClampInt(launcher_settings.deadzone[player], 0, 100);
  }
  memcpy(s_player_key_bind, launcher_settings.player_key_bind,
         sizeof s_player_key_bind);
  memcpy(s_player_guid, launcher_settings.player_gamepad_guid, sizeof s_player_guid);
  memcpy(s_player_pad_bind, launcher_settings.player_pad_bind,
         sizeof s_player_pad_bind);
  memcpy(s_assist_key_bind, launcher_settings.assist_key_bind,
         sizeof s_assist_key_bind);
  memcpy(s_assist_pad_bind, launcher_settings.assist_pad_bind,
         sizeof s_assist_pad_bind);
#else
  if (!rom_path[0] && !SelectRom(rom_path, (DWORD)sizeof rom_path)) return 0;
#endif

  (void)Dkc2LauncherWriteRomCache(rom_path);
  int result = RunDesktop(rom_path, &launcher_settings);
  (void)Dkc2LauncherSettingsSave(&launcher_settings);
  return result;
}
