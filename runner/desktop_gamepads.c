#include "desktop_gamepads.h"
#include "../third_party/sdl_gamecontrollerdb/sn30_pro_windows.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

void Dkc2GamepadsRegisterMappings(void) {
  (void)SDL_GameControllerAddMapping(kDkc2Sn30ProMapping);
}

bool Dkc2GamepadsInit(Dkc2DesktopGamepads *pads) {
  if (!pads) return false;
  memset(pads, 0, sizeof *pads);
  pads->selected[0] = pads->selected[1] = -1;
  SDL_SetMainReady();
  Dkc2GamepadsRegisterMappings();
  pads->initialized = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
  return pads->initialized;
}

void Dkc2GamepadsClose(Dkc2DesktopGamepads *pads) {
  if (!pads) return;
  for (int i = 0; i < kDkc2MaximumGamepads; ++i) {
    if (pads->handles[i]) {
      (void)SDL_GameControllerRumble(pads->handles[i], 0, 0, 0);
      SDL_GameControllerClose(pads->handles[i]);
    }
  }
  if (pads->initialized) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
  memset(pads, 0, sizeof *pads);
  pads->selected[0] = pads->selected[1] = -1;
}

void Dkc2GamepadsPoll(Dkc2DesktopGamepads *pads) {
  if (!pads || !pads->initialized) return;
  SDL_PumpEvents();
  SDL_GameControllerUpdate();
  for (int i = 0; i < kDkc2MaximumGamepads; ++i) {
    if (pads->handles[i] && !SDL_GameControllerGetAttached(pads->handles[i])) {
      SDL_GameControllerClose(pads->handles[i]);
      pads->handles[i] = NULL;
      pads->guids[i][0] = 0;
    }
  }
  int count = SDL_NumJoysticks();
  for (int device = 0; device < count; ++device) {
    if (!SDL_IsGameController(device)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    int free_slot = -1;
    bool opened = false;
    for (int i = 0; i < kDkc2MaximumGamepads; ++i) {
      if (!pads->handles[i]) { if (free_slot < 0) free_slot = i; }
      else if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads->handles[i])) == id)
        opened = true;
    }
    if (opened || free_slot < 0) continue;
    SDL_GameController *handle = SDL_GameControllerOpen(device);
    if (!handle) continue;
    pads->handles[free_slot] = handle;
    fprintf(stdout, "Controller connected: %s\n", SDL_GameControllerName(handle));
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device),
                             pads->guids[free_slot], 40);
  }
  /* This Win32 host consumes native window events; controller state is polled.
   * Drain only SDL joystick/controller events so their queue cannot fill. */
  SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_CONTROLLERSENSORUPDATE);
}

static int16_t InvertAxis(Sint16 value) {
  return value == INT16_MIN ? INT16_MAX : (int16_t)-value;
}

static void ReadState(SDL_GameController *handle, Dkc2GamepadState *out) {
  memset(out, 0, sizeof *out);
  if (!handle || !SDL_GameControllerGetAttached(handle)) return;
  static const struct { SDL_GameControllerButton button; uint32_t mask; } map[] = {
    {SDL_CONTROLLER_BUTTON_A, kDkc2GamepadA}, {SDL_CONTROLLER_BUTTON_B, kDkc2GamepadB},
    {SDL_CONTROLLER_BUTTON_X, kDkc2GamepadX}, {SDL_CONTROLLER_BUTTON_Y, kDkc2GamepadY},
    {SDL_CONTROLLER_BUTTON_BACK, kDkc2GamepadBack}, {SDL_CONTROLLER_BUTTON_START, kDkc2GamepadStart},
    {SDL_CONTROLLER_BUTTON_GUIDE, kDkc2GamepadGuide},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, kDkc2GamepadLeftStick},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, kDkc2GamepadRightStick},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, kDkc2GamepadLeftShoulder},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kDkc2GamepadRightShoulder},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, kDkc2GamepadDpadUp},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, kDkc2GamepadDpadDown},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, kDkc2GamepadDpadLeft},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kDkc2GamepadDpadRight},
  };
  for (size_t i = 0; i < sizeof map / sizeof map[0]; ++i)
    if (SDL_GameControllerGetButton(handle, map[i].button)) out->buttons |= map[i].mask;
  out->left_x = SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_LEFTX);
  out->left_y = InvertAxis(SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_LEFTY));
  out->right_x = SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_RIGHTX);
  out->right_y = InvertAxis(SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_RIGHTY));
  Sint16 left = SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
  Sint16 right = SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  out->left_trigger = left > 0 ? (uint8_t)(left >> 7) : 0;
  out->right_trigger = right > 0 ? (uint8_t)(right >> 7) : 0;
}

size_t Dkc2GamepadsReadAll(Dkc2DesktopGamepads *pads, Dkc2GamepadState *out) {
  size_t count = 0;
  if (!pads || !out) return 0;
  for (int i = 0; i < kDkc2MaximumGamepads; ++i)
    if (pads->handles[i]) ReadState(pads->handles[i], &out[count++]);
  return count;
}

static bool Matches(Dkc2DesktopGamepads *pads, int i, const char *guid) {
  return i >= 0 && i < kDkc2MaximumGamepads && pads->handles[i] &&
      (!guid[0] || strcmp(guid, pads->guids[i]) == 0);
}

size_t Dkc2GamepadsAssign(Dkc2DesktopGamepads *pads, const int sources[2],
                        const char guids[2][40], Dkc2GamepadState out[2]) {
  if (!pads || !sources || !guids || !out) return 0;
  bool used[kDkc2MaximumGamepads] = {false};
  int previous[2] = {pads->selected[0], pads->selected[1]};
  pads->selected[0] = pads->selected[1] = -1;
  /* Preserve connected instances within each priority class, reserving all
   * explicit choices before an automatic player can claim their devices. */
  for (int explicit_pass = 1; explicit_pass >= 0; --explicit_pass) {
    for (int p = 0; p < 2; ++p) {
      if (sources[p] != kDkc2InputSourceGamepad ||
          (guids[p][0] != 0) != (explicit_pass != 0)) continue;
      int i = previous[p];
      if (Matches(pads, i, guids[p]) && !used[i]) {
        pads->selected[p] = i; used[i] = true;
      }
    }
    for (int p = 0; p < 2; ++p) {
      if (sources[p] != kDkc2InputSourceGamepad || pads->selected[p] >= 0 ||
          (guids[p][0] != 0) != (explicit_pass != 0)) continue;
      for (int i = 0; i < kDkc2MaximumGamepads; ++i)
        if (!used[i] && Matches(pads, i, guids[p])) {
          pads->selected[p] = i; used[i] = true; break;
        }
    }
  }
  size_t count = 0;
  for (int p = 0; p < 2; ++p)
    if (sources[p] == kDkc2InputSourceGamepad)
      ReadState(Dkc2GamepadsSelected(pads, p), &out[count++]);
  return count;
}

SDL_GameController *Dkc2GamepadsSelected(Dkc2DesktopGamepads *pads, int player) {
  if (!pads || player < 0 || player >= 2) return NULL;
  int i = pads->selected[player];
  return i >= 0 && i < kDkc2MaximumGamepads ? pads->handles[i] : NULL;
}
