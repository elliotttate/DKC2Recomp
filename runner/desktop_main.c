#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <xinput.h>

#include "dkc2_game.h"
#include "desktop_input.h"
#include "desktop_present.h"
#include "desktop_rewind.h"
#include "verified_rom.h"

#include "common_rtl.h"
#include "launcher.h"
#include "snes/snes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kFrameWidth = 256,
  kFrameHeight = 224,
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
static uint8_t s_pixels[kFrameWidth * kFrameHeight * kBytesPerPixel];
static BITMAPINFO s_bitmap_info;
static Dkc2DesktopPresenter s_presenter;
static HWND s_window;
static bool s_running = true;
static bool s_test_hidden;

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

static bool EnsureSaveDirectory(void) {
  if (CreateDirectoryA("saves", NULL)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

static void PaintFrame(HWND window) {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(window, &paint);
  RECT client;
  GetClientRect(window, &client);
  (void)Dkc2DesktopPresent(&s_presenter, dc, &client, s_pixels,
                           &s_bitmap_info, kFrameWidth, kFrameHeight);
  EndPaint(window, &paint);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
  (void)wparam;
  (void)lparam;
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
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
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
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
  window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  window_class.lpszClassName = "Dkc2SnesrecompWindow";
  if (!RegisterClassA(&window_class) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  RECT rectangle = {0, 0, 960, 720};
  DWORD style = WS_OVERLAPPEDWINDOW;
  if (!AdjustWindowRect(&rectangle, style, FALSE)) return false;
  s_window = CreateWindowExA(
      0, window_class.lpszClassName,
      "DKC2 native-port test (snesrecomp)", style,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
      NULL, NULL, instance, NULL);
  if (!s_window) return false;

  memset(&s_bitmap_info, 0, sizeof s_bitmap_info);
  s_bitmap_info.bmiHeader.biSize = sizeof s_bitmap_info.bmiHeader;
  s_bitmap_info.bmiHeader.biWidth = kFrameWidth;
  s_bitmap_info.bmiHeader.biHeight = -kFrameHeight;
  s_bitmap_info.bmiHeader.biPlanes = 1;
  s_bitmap_info.bmiHeader.biBitCount = 32;
  s_bitmap_info.bmiHeader.biCompression = BI_RGB;
  ShowWindow(s_window, s_test_hidden ? SW_HIDE : SW_SHOW);
  UpdateWindow(s_window);
  return true;
}

static bool IsPressed(int virtual_key) {
  return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

static DesktopControls ReadControls(void) {
  DesktopControls controls = {0, 0};
  if (GetForegroundWindow() != s_window) return controls;
  if (IsPressed(VK_ESCAPE)) {
    PostMessageA(s_window, WM_CLOSE, 0, 0);
    return controls;
  }

  if (IsPressed('Z')) controls.controller |= 1u << 0;       /* B */
  if (IsPressed('A')) controls.controller |= 1u << 1;       /* Y */
  if (IsPressed(VK_SHIFT)) controls.controller |= 1u << 2;  /* Select */
  if (IsPressed(VK_RETURN)) controls.controller |= 1u << 3; /* Start */
  if (IsPressed(VK_UP)) controls.controller |= 1u << 4;
  if (IsPressed(VK_DOWN)) controls.controller |= 1u << 5;
  if (IsPressed(VK_LEFT)) controls.controller |= 1u << 6;
  if (IsPressed(VK_RIGHT)) controls.controller |= 1u << 7;
  if (IsPressed('X')) controls.controller |= 1u << 8;       /* A */
  if (IsPressed('S')) controls.controller |= 1u << 9;       /* X */
  if (IsPressed('Q')) controls.controller |= 1u << 10;      /* L */
  if (IsPressed('W')) controls.controller |= 1u << 11;      /* R */
  if (IsPressed('1')) controls.host_actions |= kDkc2HostRewind;
  if (IsPressed('2')) controls.host_actions |= kDkc2HostFastForward;

  /* Poll the first connected XInput controller every frame so hot-plugging
   * works without a separate input thread. The face-button layout follows
   * common SNES-on-Xbox conventions: A=B, X=Y, B=A, and Y=X. */
  for (DWORD user = 0; user < XUSER_MAX_COUNT; user++) {
    XINPUT_STATE state;
    memset(&state, 0, sizeof state);
    if (XInputGetState(user, &state) != ERROR_SUCCESS) continue;
    controls.controller |= Dkc2MapGamepad(
        state.Gamepad.wButtons, state.Gamepad.sThumbLX,
        state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    controls.host_actions |= Dkc2MapHostActions(
        state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger,
        XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    break;
  }
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
    memcpy(s_audio.staging + s_audio.staging_frames * kAudioChannels,
           samples, portion * kAudioChannels * sizeof samples[0]);
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

static int RunDesktop(const char *rom_path) {
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
  s_test_hidden = EnvironmentEnabled("DKC2_DESKTOP_TEST_HIDDEN");
  bool test_rewind_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_REWIND");
  bool test_fast_forward_requested =
      EnvironmentEnabled("DKC2_DESKTOP_TEST_FASTFORWARD");
  bool sram_enabled = !EnvironmentEnabled("DKC2_DESKTOP_DISABLE_SRAM");

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
  if (!CreateGameWindow()) {
    fprintf(stderr, "unable to create the DKC2 test window\n");
    free(rom);
    return 4;
  }
  Dkc2BeginDrawing(s_pixels, kFrameWidth * kBytesPerPixel);

  if (!InitializeAudio()) {
    if (test_frame_limit) {
      fprintf(stderr, "Windows audio could not be opened in desktop test\n");
      if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
      free(rom);
      return 6;
    }
    fprintf(stderr,
            "warning: Windows audio could not be opened; continuing silent\n");
  }
  (void)timeBeginPeriod(1);

  LARGE_INTEGER frequency;
  LARGE_INTEGER deadline;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&deadline);
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
  bool runtime_failure = false;
  DesktopSpeedMode previous_mode = kDesktopSpeedNormal;

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

  fprintf(stdout,
          "Controls: arrows=D-pad, Z=B, X=A, A=Y, S=X, Enter=Start, "
          "Shift=Select, Q=L, W=R, 1=Rewind (3x), 2=Fast-forward "
          "(3x), Escape=Quit. XInput gamepads are detected automatically; "
          "left trigger rewinds and right trigger fast-forwards.\n");
  while (s_running) {
    if (!PumpMessages()) break;
    DesktopControls controls = ReadControls();
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

    bool frame_ready = false;
    if (mode == kDesktopSpeedRewind) {
      if (rewind_available &&
          Dkc2RewindHistoryPop(&rewind_history, rewind_scratch)) {
        if (!RtlLoadSnapshotFromMemory(rewind_scratch,
                                       rewind_snapshot_size)) {
          fprintf(stderr, "rewind snapshot restore failed\n");
          runtime_failure = true;
          break;
        }
        Dkc2DrawPpuFrame();
        frame_ready = true;
        if (test_rewind_requested) test_rewind_completed = true;
      }
    } else {
      int frames_to_run = mode == kDesktopSpeedFastForward
                        ? kHostSpeedMultiplier : 1;
      unsigned long long iteration_start_frame = host_frame;
      for (int run = 0; run < frames_to_run && s_running; run++) {
        /* The current upstream-compatible RtlRunFrame return value is not a
         * success flag; runtime health is reported by g_fail and the DKC2 LLE
         * continuation result, as in the headless host. */
        (void)RtlRunFrame(controls.controller);
        if (g_fail || !Dkc2LastLleResult()) {
          char message[160];
          (void)snprintf(message, sizeof message,
                         "Runtime stopped at frame %llu (resume PC $%06x).",
                         host_frame + 1, (unsigned)Dkc2ResumePc());
          fprintf(stderr, "%s\n", message);
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
          if (RtlSaveSnapshotToMemory(rewind_scratch,
                                      rewind_snapshot_size) !=
                  rewind_snapshot_size ||
              !Dkc2RewindHistoryPush(&rewind_history, rewind_scratch)) {
            fprintf(stderr,
                    "warning: rewind capture failed; rewind is disabled\n");
            rewind_available = false;
          }
        }

        /* Snapshots are captured before drawing because the renderer advances
         * HDMA state. A restored snapshot is drawn once above to recreate the
         * same post-draw state before normal execution resumes. */
        Dkc2DrawPpuFrame();
        frame_ready = true;

        audio_fraction += (double)kAudioRate / kVideoRate;
        int audio_frames = (int)audio_fraction;
        audio_fraction -= audio_frames;
        RtlRenderAudio(frame_audio, audio_frames, kAudioChannels);
        if (mode == kDesktopSpeedNormal && s_audio.available &&
            !AppendAudio(frame_audio, (size_t)audio_frames)) {
          fprintf(stderr,
                  "warning: Windows audio output stopped; continuing silent\n");
          ShutdownAudio();
        }

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
      InvalidateRect(s_window, NULL, FALSE);
      UpdateWindow(s_window);
    }

    if (mode != kDesktopSpeedNormal || !s_audio.available ||
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
  bool completed_without_failure =
      !runtime_failure && !g_fail && Dkc2LastLleResult();
  if (sram_enabled && completed_without_failure) {
    RtlWriteSram();
    fprintf(stdout, "SRAM: wrote saves/save.srm (%d bytes)\n", g_sram_size);
  }
  ShutdownAudio();
  (void)timeEndPeriod(1);
  if (s_window && IsWindow(s_window)) DestroyWindow(s_window);
  Dkc2DesktopPresenterDestroy(&s_presenter);
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

  char rom_path[MAX_PATH] = {0};
  if (argument_count == 1) {
    LocalFree(arguments);
    if (!SelectRom(rom_path, (DWORD)sizeof rom_path)) return 0;
  } else if (argument_count == 2) {
    bool copied = CopyWidePath(arguments[1], rom_path, (int)sizeof rom_path);
    LocalFree(arguments);
    if (!copied) {
      MessageBoxA(NULL, "The ROM path could not be represented by Windows.",
                  "Unable to start DKC2", MB_OK | MB_ICONERROR);
      return 2;
    }
  } else {
    LocalFree(arguments);
    MessageBoxA(NULL,
                "Double-click this application and select your private DKC2 "
                "USA v1.0 ROM, or pass one ROM path on the command line.",
                "How to start DKC2", MB_OK | MB_ICONINFORMATION);
    return 2;
  }

  return RunDesktop(rom_path);
}
