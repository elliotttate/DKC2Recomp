#include "desktop_overlay.h"

#include "desktop_input.h"
#include "desktop_launcher.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

#include <cstdio>
#include <cstring>
#include <new>

enum Dkc2OverlayPlatform {
  kDkc2OverlayPlatformNone,
  kDkc2OverlayPlatformSdl,
  kDkc2OverlayPlatformWin32,
};

struct Dkc2DesktopOverlay {
  Dkc2DesktopOverlayModel model;
  RecompLauncherCSettings settings;
  Dkc2OverlayPlatform platform;
  void *window;
  uint64_t last_counter;
  char status[128];
  bool status_success;
  bool initialized;
};

static const char *KeyBindingLabel(int scancode) {
  if (scancode <= 0) return "(unbound)";
  const char *name =
      SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
  return name && name[0] ? name : "(unbound)";
}

static void PadBindingLabel(int binding, char *out, size_t capacity) {
  if (!out || capacity == 0) return;
  if (RECOMP_LAUNCHER_PAD_IS_BUTTON(binding)) {
    const char *name = SDL_GameControllerGetStringForButton(
        static_cast<SDL_GameControllerButton>(
            RECOMP_LAUNCHER_PAD_BUTTON_CODE(binding)));
    std::snprintf(out, capacity, "%s",
                  name && name[0] ? name : "button");
  } else if (RECOMP_LAUNCHER_PAD_IS_AXIS(binding)) {
    const char *name = SDL_GameControllerGetStringForAxis(
        static_cast<SDL_GameControllerAxis>(
            RECOMP_LAUNCHER_PAD_AXIS_CODE(binding)));
    std::snprintf(out, capacity, "%s%c",
                  name && name[0] ? name : "axis",
                  RECOMP_LAUNCHER_PAD_AXIS_POSITIVE(binding) ? '+' : '-');
  } else {
    std::snprintf(out, capacity, "(unbound)");
  }
}

static bool IsKeyboardCapture(const Dkc2DesktopOverlay *overlay) {
  return overlay &&
         (overlay->model.binding_capture == kDkc2OverlayCapturePlayerKey ||
          overlay->model.binding_capture == kDkc2OverlayCaptureAssistKey);
}

static bool IsPadCapture(const Dkc2DesktopOverlay *overlay) {
  return overlay &&
         Dkc2DesktopOverlayModelBindingCaptureIsPad(&overlay->model);
}

static void AcceptKeyBinding(Dkc2DesktopOverlay *overlay, int scancode) {
  if (!overlay || scancode <= SDL_SCANCODE_UNKNOWN ||
      scancode >= SDL_NUM_SCANCODES)
    return;
  if (overlay->model.binding_capture == kDkc2OverlayCapturePlayerKey) {
    overlay->settings.player_key_bind[overlay->model.capture_player]
                                         [overlay->model.capture_index] =
        scancode;
  } else if (overlay->model.binding_capture ==
             kDkc2OverlayCaptureAssistKey) {
    overlay->settings.assist_key_bind[overlay->model.capture_index] =
        scancode;
  } else {
    return;
  }
  Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
}

static void AcceptPadBinding(Dkc2DesktopOverlay *overlay, int binding) {
  if (!overlay || binding <= 0) return;
  if (overlay->model.binding_capture == kDkc2OverlayCapturePlayerPad) {
    overlay->settings.player_pad_bind[overlay->model.capture_player]
                                         [overlay->model.capture_index] =
        binding;
  } else if (overlay->model.binding_capture ==
             kDkc2OverlayCaptureAssistPad) {
    overlay->settings.assist_pad_bind[overlay->model.capture_index] =
        binding;
  } else {
    return;
  }
  Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
}

#ifdef _WIN32
static SDL_Scancode Win32VirtualKeyToScancode(uintptr_t key,
                                              intptr_t lparam) {
  bool extended = (lparam & (1LL << 24)) != 0;
  if (key >= 'A' && key <= 'Z')
    return static_cast<SDL_Scancode>(
        SDL_SCANCODE_A + static_cast<int>(key - 'A'));
  if (key >= '1' && key <= '9')
    return static_cast<SDL_Scancode>(
        SDL_SCANCODE_1 + static_cast<int>(key - '1'));
  if (key == '0') return SDL_SCANCODE_0;
  if (key >= VK_F1 && key <= VK_F12)
    return static_cast<SDL_Scancode>(
        SDL_SCANCODE_F1 + static_cast<int>(key - VK_F1));
  if (key >= VK_F13 && key <= VK_F24)
    return static_cast<SDL_Scancode>(
        SDL_SCANCODE_F13 + static_cast<int>(key - VK_F13));
  switch (key) {
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
    case VK_NUMPAD1: return SDL_SCANCODE_KP_1;
    case VK_NUMPAD2: return SDL_SCANCODE_KP_2;
    case VK_NUMPAD3: return SDL_SCANCODE_KP_3;
    case VK_NUMPAD4: return SDL_SCANCODE_KP_4;
    case VK_NUMPAD5: return SDL_SCANCODE_KP_5;
    case VK_NUMPAD6: return SDL_SCANCODE_KP_6;
    case VK_NUMPAD7: return SDL_SCANCODE_KP_7;
    case VK_NUMPAD8: return SDL_SCANCODE_KP_8;
    case VK_NUMPAD9: return SDL_SCANCODE_KP_9;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_RETURN:
      return extended ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_SHIFT:
      return ((lparam >> 16) & 0xff) == 0x36
          ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_CONTROL:
      return extended ? SDL_SCANCODE_RCTRL : SDL_SCANCODE_LCTRL;
    case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_MENU: return extended ? SDL_SCANCODE_RALT : SDL_SCANCODE_LALT;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_END: return SDL_SCANCODE_END;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
    case VK_SCROLL: return SDL_SCANCODE_SCROLLLOCK;
    case VK_SNAPSHOT: return SDL_SCANCODE_PRINTSCREEN;
    case VK_PAUSE: return SDL_SCANCODE_PAUSE;
    case VK_LWIN: return SDL_SCANCODE_LGUI;
    case VK_RWIN: return SDL_SCANCODE_RGUI;
    case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
    case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
    case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
    case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
    case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
    case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
    case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
    case VK_OEM_3: return SDL_SCANCODE_GRAVE;
    case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
    case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
    case VK_OEM_2: return SDL_SCANCODE_SLASH;
    case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
    case VK_ADD: return SDL_SCANCODE_KP_PLUS;
    case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
    case VK_DECIMAL: return SDL_SCANCODE_KP_DECIMAL;
    case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}
#endif

static void ApplyStyle(void) {
  ImGuiStyle &style = ImGui::GetStyle();
  ImGui::StyleColorsDark(&style);
  style.WindowRounding = 10.0f;
  style.ChildRounding = 8.0f;
  style.FrameRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.WindowPadding = ImVec2(20.0f, 18.0f);
  style.ItemSpacing = ImVec2(10.0f, 10.0f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.045f, 0.085f, 0.98f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.070f, 0.120f, 0.96f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.34f, 0.16f, 0.68f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.48f, 0.24f, 0.88f, 1.0f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.75f, 0.72f, 1.0f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.95f, 0.78f, 1.0f);
  style.Colors[ImGuiCol_TabSelected] = ImVec4(0.34f, 0.16f, 0.68f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.13f, 0.52f, 1.0f);
}

static bool BeginContext(Dkc2DesktopOverlay *overlay) {
  if (!overlay || overlay->initialized) return false;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ApplyStyle();
  overlay->last_counter = SDL_GetPerformanceCounter();
  return true;
}

extern "C" Dkc2DesktopOverlay *Dkc2DesktopOverlayCreate(
    const RecompLauncherCSettings *settings) {
  Dkc2DesktopOverlay *overlay = new (std::nothrow) Dkc2DesktopOverlay();
  if (!overlay) return nullptr;
  std::memset(overlay, 0, sizeof *overlay);
  if (settings)
    overlay->settings = *settings;
  else
    Dkc2LauncherSettingsDefault(&overlay->settings);
  Dkc2DesktopOverlayModelInit(
      &overlay->model, overlay->settings.assist_tools != 0);
  overlay->status_success = true;
  return overlay;
}

extern "C" bool Dkc2DesktopOverlayInitSdl(Dkc2DesktopOverlay *overlay,
                                           void *window, void *gl_context) {
  if (!overlay || !window || !gl_context || !BeginContext(overlay))
    return false;
  if (!ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window *>(window),
                                    gl_context) ||
      !ImGui_ImplOpenGL3_Init("#version 130")) {
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    return false;
  }
  overlay->platform = kDkc2OverlayPlatformSdl;
  overlay->window = window;
  overlay->initialized = true;
  return true;
}

extern "C" bool Dkc2DesktopOverlayInitWin32(Dkc2DesktopOverlay *overlay,
                                             void *window) {
#ifdef _WIN32
  if (!overlay || !window || !BeginContext(overlay)) return false;
  if (!ImGui_ImplOpenGL3_Init("#version 130")) {
    ImGui::DestroyContext();
    return false;
  }
  ImGui::GetIO().BackendPlatformName = "dkc2_win32_minimal";
  overlay->platform = kDkc2OverlayPlatformWin32;
  overlay->window = window;
  overlay->initialized = true;
  return true;
#else
  (void)overlay;
  (void)window;
  return false;
#endif
}

extern "C" void Dkc2DesktopOverlayDestroy(Dkc2DesktopOverlay *overlay) {
  if (!overlay) return;
  if (overlay->initialized) {
    ImGui_ImplOpenGL3_Shutdown();
    if (overlay->platform == kDkc2OverlayPlatformSdl)
      ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }
  delete overlay;
}

extern "C" bool Dkc2DesktopOverlayProcessSdlEvent(
    Dkc2DesktopOverlay *overlay, const void *event_pointer) {
  if (!overlay || !event_pointer || !overlay->initialized ||
      overlay->platform != kDkc2OverlayPlatformSdl)
    return false;
  const SDL_Event *event = static_cast<const SDL_Event *>(event_pointer);
  if (overlay->model.open && IsKeyboardCapture(overlay) &&
      event->type == SDL_KEYDOWN && event->key.repeat == 0) {
    if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE)
      Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
    else
      AcceptKeyBinding(overlay, event->key.keysym.scancode);
    return true;
  }
  if (event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
      event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
    Dkc2DesktopOverlayModelToggle(&overlay->model);
    return true;
  }
  if (event->type == SDL_CONTROLLERBUTTONDOWN &&
      event->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
    if (overlay->model.open && IsPadCapture(overlay)) return true;
    Dkc2DesktopOverlayModelToggle(&overlay->model);
    return true;
  }
  if (overlay->model.open) {
    ImGui_ImplSDL2_ProcessEvent(event);
    return true;
  }
  return false;
}

extern "C" bool Dkc2DesktopOverlayProcessWin32Message(
    Dkc2DesktopOverlay *overlay, void *window_pointer, unsigned message,
    uintptr_t wparam, intptr_t lparam) {
#ifdef _WIN32
  if (!overlay || !overlay->initialized ||
      overlay->platform != kDkc2OverlayPlatformWin32)
    return false;
  HWND window = static_cast<HWND>(window_pointer);
  if (overlay->model.open && IsKeyboardCapture(overlay) &&
      message == WM_KEYDOWN && (lparam & (1LL << 30)) == 0) {
    if (wparam == VK_ESCAPE) {
      Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
    } else {
      SDL_Scancode scancode = Win32VirtualKeyToScancode(wparam, lparam);
      if (scancode != SDL_SCANCODE_UNKNOWN)
        AcceptKeyBinding(overlay, scancode);
    }
    return true;
  }
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE &&
      (lparam & (1LL << 30)) == 0) {
    Dkc2DesktopOverlayModelToggle(&overlay->model);
    return true;
  }
  if (!overlay->model.open) return false;
  ImGuiIO &io = ImGui::GetIO();
  switch (message) {
    case WM_MOUSEMOVE:
      io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(lparam)),
                          static_cast<float>(GET_Y_LPARAM(lparam)));
      return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      io.AddMouseButtonEvent(0, message == WM_LBUTTONDOWN);
      return true;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      io.AddMouseButtonEvent(1, message == WM_RBUTTONDOWN);
      return true;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      io.AddMouseButtonEvent(2, message == WM_MBUTTONDOWN);
      return true;
    case WM_MOUSEWHEEL:
      io.AddMouseWheelEvent(0.0f, static_cast<float>(
          GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA));
      return true;
    case WM_KEYDOWN:
    case WM_KEYUP: {
      bool down = message == WM_KEYDOWN;
      ImGuiKey key = ImGuiKey_None;
      switch (wparam) {
        case VK_TAB: key = ImGuiKey_Tab; break;
        case VK_LEFT: key = ImGuiKey_LeftArrow; break;
        case VK_RIGHT: key = ImGuiKey_RightArrow; break;
        case VK_UP: key = ImGuiKey_UpArrow; break;
        case VK_DOWN: key = ImGuiKey_DownArrow; break;
        case VK_RETURN: key = ImGuiKey_Enter; break;
        case VK_SPACE: key = ImGuiKey_Space; break;
        case VK_ESCAPE: key = ImGuiKey_Escape; break;
        default: break;
      }
      if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
      return true;
    }
    case WM_CHAR:
      io.AddInputCharacter(static_cast<unsigned int>(wparam));
      return true;
    case WM_KILLFOCUS:
      io.AddFocusEvent(false);
      return true;
    case WM_SETFOCUS:
      io.AddFocusEvent(true);
      return true;
    default:
      break;
  }
  (void)window;
#else
  (void)overlay;
  (void)window_pointer;
  (void)message;
  (void)wparam;
  (void)lparam;
#endif
  return false;
}

static void FeedImGuiGamepadNavigation(uint32_t buttons) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiKey_GamepadDpadUp,
                 (buttons & kDkc2GamepadDpadUp) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadDown,
                 (buttons & kDkc2GamepadDpadDown) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,
                 (buttons & kDkc2GamepadDpadLeft) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadRight,
                 (buttons & kDkc2GamepadDpadRight) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadFaceDown,
                 (buttons & kDkc2GamepadA) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadFaceRight,
                 (buttons & kDkc2GamepadB) != 0);
}

extern "C" void Dkc2DesktopOverlaySetGamepad(
    Dkc2DesktopOverlay *overlay, const Dkc2GamepadState *gamepad) {
  if (!overlay || !overlay->initialized) return;
  uint32_t buttons = gamepad ? gamepad->buttons : 0;
  if (!overlay->model.open || !IsPadCapture(overlay)) {
    FeedImGuiGamepadNavigation(buttons);
    return;
  }

  /* Release ImGui navigation while capturing. Otherwise the input selected
   * as a binding could also activate the currently focused menu widget. */
  FeedImGuiGamepadNavigation(0);
  int left_x = gamepad ? gamepad->left_x : 0;
  int left_y = gamepad ? -gamepad->left_y : 0;
  int right_x = gamepad ? gamepad->right_x : 0;
  int right_y = gamepad ? -gamepad->right_y : 0;
  int left_trigger = gamepad ? gamepad->left_trigger : 0;
  int right_trigger = gamepad ? gamepad->right_trigger : 0;
  bool neutral =
      buttons == 0 && left_x >= -16000 && left_x <= 16000 &&
      left_y >= -16000 && left_y <= 16000 &&
      right_x >= -16000 && right_x <= 16000 &&
      right_y >= -16000 && right_y <= 16000 &&
      left_trigger <= 30 && right_trigger <= 30;
  if (!Dkc2DesktopOverlayModelArmPadCapture(&overlay->model, neutral) ||
      neutral)
    return;

  struct ButtonCandidate {
    uint32_t mask;
    int standard_button;
  };
  static const ButtonCandidate candidates[] = {
      {kDkc2GamepadA, SDL_CONTROLLER_BUTTON_A},
      {kDkc2GamepadB, SDL_CONTROLLER_BUTTON_B},
      {kDkc2GamepadX, SDL_CONTROLLER_BUTTON_X},
      {kDkc2GamepadY, SDL_CONTROLLER_BUTTON_Y},
      {kDkc2GamepadBack, SDL_CONTROLLER_BUTTON_BACK},
      {kDkc2GamepadGuide, SDL_CONTROLLER_BUTTON_GUIDE},
      {kDkc2GamepadStart, SDL_CONTROLLER_BUTTON_START},
      {kDkc2GamepadLeftStick, SDL_CONTROLLER_BUTTON_LEFTSTICK},
      {kDkc2GamepadRightStick, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
      {kDkc2GamepadLeftShoulder, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
      {kDkc2GamepadRightShoulder, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
      {kDkc2GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP},
      {kDkc2GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
      {kDkc2GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
      {kDkc2GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
  };
  for (const ButtonCandidate &candidate : candidates) {
    if ((buttons & candidate.mask) != 0) {
      AcceptPadBinding(
          overlay, RECOMP_LAUNCHER_PAD_BUTTON(candidate.standard_button));
      return;
    }
  }

  struct AxisCandidate {
    int value;
    int standard_axis;
    int threshold;
  };
  const AxisCandidate axes[] = {
      {left_x, SDL_CONTROLLER_AXIS_LEFTX, 20000},
      {left_y, SDL_CONTROLLER_AXIS_LEFTY, 20000},
      {right_x, SDL_CONTROLLER_AXIS_RIGHTX, 20000},
      {right_y, SDL_CONTROLLER_AXIS_RIGHTY, 20000},
      {left_trigger, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30},
      {right_trigger, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30},
  };
  for (const AxisCandidate &axis : axes) {
    if (axis.value > axis.threshold || axis.value < -axis.threshold) {
      AcceptPadBinding(
          overlay, RECOMP_LAUNCHER_PAD_AXIS(
                       axis.standard_axis, axis.value > 0 ? 1 : 0));
      return;
    }
  }
}

extern "C" void Dkc2DesktopOverlayToggle(Dkc2DesktopOverlay *overlay) {
  if (overlay) Dkc2DesktopOverlayModelToggle(&overlay->model);
}

extern "C" bool Dkc2DesktopOverlayIsOpen(
    const Dkc2DesktopOverlay *overlay) {
  return overlay && overlay->model.open;
}

extern "C" bool Dkc2DesktopOverlayAssistTools(
    const Dkc2DesktopOverlay *overlay) {
  return overlay && overlay->model.assist_tools;
}

extern "C" int Dkc2DesktopOverlaySelectedSlot(
    const Dkc2DesktopOverlay *overlay) {
  return overlay ? overlay->model.selected_slot : 0;
}

extern "C" void Dkc2DesktopOverlayGetSettings(
    const Dkc2DesktopOverlay *overlay, RecompLauncherCSettings *settings) {
  if (overlay && settings) *settings = overlay->settings;
}

extern "C" uint32_t Dkc2DesktopOverlayTakeActions(
    Dkc2DesktopOverlay *overlay) {
  return overlay
      ? Dkc2DesktopOverlayModelTakeActions(&overlay->model) : 0;
}

extern "C" void Dkc2DesktopOverlaySetStatus(
    Dkc2DesktopOverlay *overlay, const char *status, bool success) {
  if (!overlay) return;
  std::snprintf(overlay->status, sizeof overlay->status, "%s",
                status ? status : "");
  overlay->status_success = success;
}

static void DrawMainPage(Dkc2DesktopOverlay *overlay) {
  ImGui::TextWrapped("The game is paused at a completed frame boundary.");
  ImGui::Spacing();
  if (ImGui::Button("Resume Game", ImVec2(240.0f, 48.0f)))
    Dkc2DesktopOverlayModelRequest(
        &overlay->model, kDkc2OverlayActionResume);
  if (ImGui::Button("Quit to Desktop", ImVec2(240.0f, 42.0f)))
    Dkc2DesktopOverlayModelRequest(
        &overlay->model, kDkc2OverlayActionQuit);
}

static void DrawSettingsPage(Dkc2DesktopOverlay *overlay) {
  RecompLauncherCSettings &settings = overlay->settings;
  ImGui::TextUnformatted("Display");
  ImGui::Separator();
  ImGui::SliderInt("Window scale", &settings.window_scale, 1, 4, "%dx");
  static const char *fullscreen_labels[] = {
      "Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
  settings.fullscreen =
      settings.fullscreen < 0 ? 0 : (settings.fullscreen > 2 ? 2
                                                             : settings.fullscreen);
  if (ImGui::BeginCombo("Fullscreen",
                        fullscreen_labels[settings.fullscreen])) {
    for (int i = 0; i < 3; i++) {
      if (ImGui::Selectable(fullscreen_labels[i], settings.fullscreen == i))
        settings.fullscreen = i;
    }
    ImGui::EndCombo();
  }
  static const char *renderer_labels[] = {"GDI compatibility", "OpenGL"};
  settings.renderer = settings.renderer ? 1 : 0;
  if (ImGui::BeginCombo("Renderer", renderer_labels[settings.renderer])) {
    for (int i = 0; i < 2; i++) {
      if (ImGui::Selectable(renderer_labels[i], settings.renderer == i))
        settings.renderer = i;
    }
    ImGui::EndCombo();
  }
  bool bilinear = settings.texture_filter != 0;
  if (ImGui::Checkbox("Bilinear texture filtering", &bilinear))
    settings.texture_filter = bilinear ? 1 : 0;
  bool widescreen = settings.widescreen != 0;
  if (ImGui::Checkbox("Widescreen (16:9, experimental)", &widescreen))
    settings.widescreen = widescreen ? 1 : 0;
  ImGui::TextDisabled(
      "Streamable levels extend to 16:9; bounded screens remain centered.");
  static const char *screen_labels[] = {
      "Raw", "CRT", "Composite", "Trinitron"};
  if (settings.screen_kind < 0 || settings.screen_kind > 3)
    settings.screen_kind = 0;
  if (ImGui::BeginCombo("Screen model",
                        screen_labels[settings.screen_kind])) {
    for (int i = 0; i < 4; i++) {
      if (ImGui::Selectable(screen_labels[i], settings.screen_kind == i))
        settings.screen_kind = i;
    }
    ImGui::EndCombo();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Audio");
  ImGui::Separator();
  bool audio = settings.enable_audio != 0;
  if (ImGui::Checkbox("Enable audio", &audio))
    settings.enable_audio = audio ? 1 : 0;
  static const int rates[] = {32040, 32000, 44100, 48000};
  char rate_label[32];
  std::snprintf(rate_label, sizeof rate_label, "%d Hz", settings.audio_freq);
  if (ImGui::BeginCombo("Sample rate", rate_label)) {
    for (int rate : rates) {
      char label[32];
      std::snprintf(label, sizeof label, "%d Hz", rate);
      if (ImGui::Selectable(label, settings.audio_freq == rate))
        settings.audio_freq = rate;
    }
    ImGui::EndCombo();
  }
  ImGui::SliderInt("Volume", &settings.volume, 0, 100, "%d%%");
  bool skip = settings.skip_launcher != 0;
  if (ImGui::Checkbox("Skip launcher on boot", &skip))
    settings.skip_launcher = skip ? 1 : 0;

  ImGui::Spacing();
  ImGui::TextUnformatted("Hotkeys");
  ImGui::Separator();
  ImGui::BulletText("Overlay: Escape (controller: Guide or Start+Back)");
  ImGui::BulletText("Performance log: F");
  ImGui::BulletText(
      "Gameplay and Assist bindings: configurable in Controls");

  ImGui::Spacing();
  ImGui::TextWrapped(
      "Volume, widescreen, screen model, texture filtering, and controller "
      "choices apply while this menu is open. Window scale, fullscreen, "
      "renderer, sample rate, and audio enablement changes take effect on the "
      "next launch. "
      "DKC2 audio currently remains at the SNES-native 32040 Hz; other sample "
      "rate choices are retained for launcher compatibility.");
  if (ImGui::Button("Restore All Settings to Defaults")) {
    Dkc2LauncherSettingsDefault(&overlay->settings);
    Dkc2DesktopOverlayModelSetAssistTools(
        &overlay->model, overlay->settings.assist_tools != 0);
  }
}

static void DrawAssistPage(Dkc2DesktopOverlay *overlay) {
  bool enabled = overlay->model.assist_tools;
  if (ImGui::Checkbox("Enable Assist Tools / Cheats", &enabled)) {
    Dkc2DesktopOverlayModelSetAssistTools(&overlay->model, enabled);
    overlay->settings.assist_tools = enabled ? 1 : 0;
  }
  ImGui::TextWrapped(
      "Enabling this mode permits rewind, fast-forward, and file save "
      "states. The window title discloses when Assist Tools are active.");
  ImGui::Separator();
  if (!enabled) ImGui::BeginDisabled();
  if (ImGui::Button("< Previous Slot", ImVec2(150.0f, 38.0f)))
    Dkc2DesktopOverlayModelShiftSlot(&overlay->model, -1);
  ImGui::SameLine();
  ImGui::Text("Slot %d of 5", overlay->model.selected_slot + 1);
  ImGui::SameLine();
  if (ImGui::Button("Next Slot >", ImVec2(150.0f, 38.0f)))
    Dkc2DesktopOverlayModelShiftSlot(&overlay->model, 1);
  char save_label[48];
  char load_label[48];
  std::snprintf(save_label, sizeof save_label, "Save Slot %d",
                overlay->model.selected_slot + 1);
  std::snprintf(load_label, sizeof load_label, "Load Slot %d",
                overlay->model.selected_slot + 1);
  if (ImGui::Button(save_label, ImVec2(190.0f, 42.0f)))
    Dkc2DesktopOverlayModelRequest(
        &overlay->model, kDkc2OverlayActionSaveState);
  ImGui::SameLine();
  if (ImGui::Button(load_label, ImVec2(190.0f, 42.0f)))
    Dkc2DesktopOverlayModelRequest(
        &overlay->model, kDkc2OverlayActionLoadState);
  ImGui::Text("Slot file: saves/dkc2s%d.sav",
              overlay->model.selected_slot);
  static const char *actions[] = {
      "Rewind", "Fast-forward", "Save state", "Load state"};
  ImGui::Separator();
  ImGui::TextUnformatted(
      "Current bindings (configure in the Controls tab)");
  for (int action = 0; action < 4; action++) {
    char pad[48];
    PadBindingLabel(overlay->settings.assist_pad_bind[action],
                    pad, sizeof pad);
    ImGui::BulletText("%s: keyboard %s; controller %s",
                      actions[action],
                      KeyBindingLabel(
                          overlay->settings.assist_key_bind[action]),
                      pad);
  }
  if (!enabled) ImGui::EndDisabled();
  if (overlay->status[0]) {
    ImGui::Separator();
    ImVec4 color = overlay->status_success
        ? ImVec4(0.35f, 0.95f, 0.65f, 1.0f)
        : ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
    ImGui::TextColored(color, "%s", overlay->status);
  }
}

static bool IsCaptureTarget(const Dkc2DesktopOverlay *overlay,
                            Dkc2OverlayBindingCapture capture, int player,
                            int index) {
  return overlay->model.binding_capture == capture &&
         overlay->model.capture_player == player &&
         overlay->model.capture_index == index;
}

static void DrawKeyBindingButton(Dkc2DesktopOverlay *overlay, int player,
                                 int index, bool assist) {
  Dkc2OverlayBindingCapture capture =
      assist ? kDkc2OverlayCaptureAssistKey
             : kDkc2OverlayCapturePlayerKey;
  bool active = IsCaptureTarget(overlay, capture, assist ? 0 : player, index);
  const char *label = active
      ? "Press a key..."
      : KeyBindingLabel(assist
            ? overlay->settings.assist_key_bind[index]
            : overlay->settings.player_key_bind[player][index]);
  ImGui::PushID(assist ? 1000 + index : player * 100 + index);
  if (ImGui::Button(label, ImVec2(-1.0f, 0.0f)))
    Dkc2DesktopOverlayModelBeginBindingCapture(
        &overlay->model, capture, assist ? 0 : player, index);
  ImGui::PopID();
}

static void DrawPadBindingButton(Dkc2DesktopOverlay *overlay, int player,
                                 int index, bool assist) {
  Dkc2OverlayBindingCapture capture =
      assist ? kDkc2OverlayCaptureAssistPad
             : kDkc2OverlayCapturePlayerPad;
  bool active = IsCaptureTarget(overlay, capture, assist ? 0 : player, index);
  char label[64];
  if (active) {
    std::snprintf(label, sizeof label,
                  overlay->model.pad_capture_armed
                      ? "Press / move..."
                      : "Release controls...");
  } else {
    PadBindingLabel(
        assist ? overlay->settings.assist_pad_bind[index]
               : overlay->settings.player_pad_bind[player][index],
        label, sizeof label);
  }
  ImGui::PushID(assist ? 2000 + index : 500 + player * 100 + index);
  if (ImGui::Button(label, ImVec2(-1.0f, 0.0f)))
    Dkc2DesktopOverlayModelBeginBindingCapture(
        &overlay->model, capture, assist ? 0 : player, index);
  ImGui::PopID();
}

static void DrawBindingTable(Dkc2DesktopOverlay *overlay, int player,
                             bool assist) {
  static const char *player_actions[] = {
      "Up", "Down", "Left", "Right", "A", "B",
      "X", "Y", "L", "R", "Start", "Select"};
  static const char *assist_actions[] = {
      "Rewind", "Fast-forward", "Save state", "Load state"};
  const char *const *actions = assist ? assist_actions : player_actions;
  int count = assist ? 4 : 12;
  if (ImGui::BeginTable(
          assist ? "AssistBindings" : "PlayerBindings", 3,
          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch,
                            0.8f);
    ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch,
                            1.25f);
    ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch,
                            1.25f);
    ImGui::TableHeadersRow();
    for (int action = 0; action < count; action++) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(actions[action]);
      ImGui::TableSetColumnIndex(1);
      DrawKeyBindingButton(overlay, player, action, assist);
      ImGui::TableSetColumnIndex(2);
      DrawPadBindingButton(overlay, player, action, assist);
    }
    ImGui::EndTable();
  }
}

static void ResetPlayerBindings(Dkc2DesktopOverlay *overlay, int player) {
  RecompLauncherCSettings defaults;
  Dkc2LauncherSettingsDefault(&defaults);
  std::memcpy(overlay->settings.player_key_bind[player],
              defaults.player_key_bind[player],
              sizeof overlay->settings.player_key_bind[player]);
  std::memcpy(overlay->settings.player_pad_bind[player],
              defaults.player_pad_bind[player],
              sizeof overlay->settings.player_pad_bind[player]);
  Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
}

static void ResetAssistBindings(Dkc2DesktopOverlay *overlay) {
  RecompLauncherCSettings defaults;
  Dkc2LauncherSettingsDefault(&defaults);
  std::memcpy(overlay->settings.assist_key_bind,
              defaults.assist_key_bind,
              sizeof overlay->settings.assist_key_bind);
  std::memcpy(overlay->settings.assist_pad_bind,
              defaults.assist_pad_bind,
              sizeof overlay->settings.assist_pad_bind);
  Dkc2DesktopOverlayModelCancelBindingCapture(&overlay->model);
}

static void DrawPlayerControls(Dkc2DesktopOverlay *overlay, int player) {
  static const char *source_labels[] = {"None", "Keyboard", "Gamepad"};
  ImGui::PushID(player);
  int &source = overlay->settings.player_src[player];
  if (source < 0 || source > 2) source = 0;
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("Input source", source_labels[source])) {
    for (int i = 0; i < 3; i++) {
      if (ImGui::Selectable(source_labels[i], source == i)) source = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150.0f);
  ImGui::SliderInt("Deadzone", &overlay->settings.deadzone[player],
                   0, 100, "%d%%");
  ImGui::SameLine();
  if (ImGui::Button("Reset Player Controls"))
    ResetPlayerBindings(overlay, player);
  ImGui::TextDisabled(
      "Select a binding, then press a key or use the first controller.");
  ImGui::BeginChild("PlayerBindingScroll", ImVec2(0.0f, 0.0f), false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
  DrawBindingTable(overlay, player, false);
  ImGui::EndChild();
  ImGui::PopID();
}

static void DrawControlsPage(Dkc2DesktopOverlay *overlay) {
  if (ImGui::BeginTabBar("ControlGroups")) {
    if (ImGui::BeginTabItem("Player 1")) {
      DrawPlayerControls(overlay, 0);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Player 2")) {
      DrawPlayerControls(overlay, 1);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Assist")) {
      bool enabled = overlay->model.assist_tools;
      if (ImGui::Checkbox("Enable Assist Tools / Cheats", &enabled)) {
        Dkc2DesktopOverlayModelSetAssistTools(&overlay->model, enabled);
        overlay->settings.assist_tools = enabled ? 1 : 0;
      }
      ImGui::SameLine();
      if (ImGui::Button("Reset Assist Controls"))
        ResetAssistBindings(overlay);
      ImGui::TextDisabled(
          "These bindings are active only while Assist Tools are enabled.");
      DrawBindingTable(overlay, 0, true);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Fixed Shortcuts")) {
      ImGui::TextWrapped(
          "These support shortcuts remain fixed so the pause menu and "
          "diagnostics cannot become unreachable:");
      ImGui::BulletText(
          "Open / close pause menu: Escape; controller Guide or Start+Back");
      ImGui::BulletText("Performance metrics log: F");
      ImGui::Spacing();
      ImGui::TextWrapped(
          "All SNES gameplay inputs for both players and all Assist "
          "bindings can be changed in the other Controls tabs.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

static void DrawCreditsPage(void) {
  ImGui::TextUnformatted("Donkey Kong Country 2: Diddy's Kong Quest");
  ImGui::TextWrapped(
      "Original game developed by Rare and published by Nintendo.");
  ImGui::Separator();
  ImGui::TextWrapped(
      "Native-port foundation: DKC2Recomp, SNESrecomp, recomp-ui, and their "
      "contributors.");
  ImGui::Spacing();
  ImGui::TextWrapped(
      "The complete named original-game credits can be added here when the "
      "curated list is supplied.");
}

extern "C" void Dkc2DesktopOverlayRenderOpenGl(
    void *overlay_pointer, int width, int height) {
  Dkc2DesktopOverlay *overlay =
      static_cast<Dkc2DesktopOverlay *>(overlay_pointer);
  if (!overlay || !overlay->initialized || !overlay->model.open ||
      width <= 0 || height <= 0)
    return;

  ImGui_ImplOpenGL3_NewFrame();
  if (overlay->platform == kDkc2OverlayPlatformSdl) {
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
  } else {
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    uint64_t counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    io.DeltaTime = overlay->last_counter && frequency
        ? static_cast<float>(
              static_cast<double>(counter - overlay->last_counter) /
              static_cast<double>(frequency))
        : 1.0f / 60.0f;
    overlay->last_counter = counter;
    ImGui::NewFrame();
  }

  ImGui::GetBackgroundDrawList()->AddRectFilled(
      ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width),
                                  static_cast<float>(height)),
      IM_COL32(0, 0, 0, 150));
  float menu_width = width < 760 ? static_cast<float>(width) - 32.0f : 720.0f;
  float menu_height =
      height < 560 ? static_cast<float>(height) - 32.0f : 520.0f;
  ImGui::SetNextWindowPos(
      ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always,
      ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);
  ImGui::Begin("DKC2 Pause Menu", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextColored(ImVec4(0.70f, 0.38f, 1.0f, 1.0f),
                     "DONKEY KONG COUNTRY 2");
  ImGui::SameLine();
  ImGui::TextDisabled("| Native-port test");
  if (overlay->model.assist_tools) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.20f, 1.0f),
                       "| Assist Tools: On");
  }
  ImGui::Separator();
  if (ImGui::BeginTabBar("OverlayTabs")) {
    if (ImGui::BeginTabItem("Main")) {
      DrawMainPage(overlay);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings")) {
      DrawSettingsPage(overlay);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Assist Tools / Cheats")) {
      DrawAssistPage(overlay);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Controls")) {
      DrawControlsPage(overlay);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Credits")) {
      DrawCreditsPage();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
