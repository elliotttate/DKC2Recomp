#include "desktop_gamepads.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); return 1; } } while (0)

static int Attach(int product, const char *name) {
  SDL_VirtualJoystickDesc desc;
  SDL_zero(desc);
  desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
  desc.naxes = SDL_CONTROLLER_AXIS_MAX;
  desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
  desc.vendor_id = 0xF00D;
  desc.product_id = (Uint16)product;
  desc.name = name;
  desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
  desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
  return SDL_JoystickAttachVirtualEx(&desc);
}

static int DeviceIndex(SDL_JoystickID id) {
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_JoystickGetDeviceInstanceID(i) == id) return i;
  return -1;
}

int main(int argc, char **argv) {
  Dkc2DesktopGamepads pads;
  CHECK(Dkc2GamepadsInit(&pads));
  Dkc2GamepadsPoll(&pads);
#ifdef _WIN32
  char *mapping = SDL_GameControllerMappingForGUID(SDL_JoystickGetGUIDFromString(
      "03009761c82d00000161000000000000"));
  CHECK(mapping && strstr(mapping, "8BitDo SN30 Pro"));
  SDL_free(mapping);
#endif
  if (argc > 1 && strcmp(argv[1], "--list") == 0) {
    for (int i = 0; i < kDkc2MaximumGamepads; ++i)
      if (pads.handles[i]) printf("%s guid=%s\n", SDL_GameControllerName(pads.handles[i]), pads.guids[i]);
    Dkc2GamepadsClose(&pads);
    return 0;
  }
  /* Attach after initialization: exercise the production hot-plug path,
   * with the same SDL polling used after the Win32 launcher's SDL_Quit. */
  int a = Attach(1, "Synthetic player A");
  int b = Attach(2, "Synthetic player B");
  CHECK(a >= 0 && b >= 0);
  SDL_Joystick *ja = SDL_JoystickOpen(a), *jb = SDL_JoystickOpen(b);
  CHECK(ja && jb);
  SDL_JoystickID ida = SDL_JoystickInstanceID(ja);
  SDL_JoystickID idb = SDL_JoystickInstanceID(jb);
  char guids[2][40];
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(ja), guids[0], 40);
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(jb), guids[1], 40);
  CHECK(strcmp(guids[0], guids[1]) != 0);
  Dkc2GamepadsPoll(&pads);
  CHECK(SDL_JoystickSetVirtualButton(ja, SDL_CONTROLLER_BUTTON_A, 1) == 0);
  CHECK(SDL_JoystickSetVirtualButton(jb, SDL_CONTROLLER_BUTTON_X, 1) == 0);
  CHECK(SDL_JoystickSetVirtualAxis(jb, SDL_CONTROLLER_AXIS_LEFTY, INT16_MIN) == 0);
  CHECK(SDL_JoystickSetVirtualAxis(ja, SDL_CONTROLLER_AXIS_TRIGGERLEFT, INT16_MAX) == 0);
  Dkc2GamepadsPoll(&pads);
  int sources[2] = {kDkc2InputSourceGamepad, kDkc2InputSourceGamepad};
  Dkc2GamepadState out[2];
  CHECK(Dkc2GamepadsAssign(&pads, sources, guids, out) == 2);
  CHECK(out[0].buttons == kDkc2GamepadA);
  CHECK(out[1].buttons == kDkc2GamepadX);
  CHECK(out[1].left_y == INT16_MAX && out[0].left_trigger == 255);
  CHECK(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(Dkc2GamepadsSelected(&pads, 0))) == ida);
  CHECK(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(Dkc2GamepadsSelected(&pads, 1))) == idb);
  int bindings[2][RECOMP_LAUNCHER_MAX_BINDINGS] = {{0}};
  bindings[0][5] = RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A);
  bindings[1][7] = RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X);
  int zones[2] = {24, 24}; uint32_t keyboard[2] = {0, 0};
  CHECK(Dkc2RoutePlayerInputsWithBindings(keyboard, out, 2, sources, zones, bindings) == 0x2001);

  /* Saved device choices must override enumeration order. */
  char reversed[2][40];
  memcpy(reversed[0], guids[1], 40); memcpy(reversed[1], guids[0], 40);
  Dkc2GamepadsAssign(&pads, sources, reversed, out);
  CHECK(out[0].buttons == kDkc2GamepadX && out[1].buttons == kDkc2GamepadA);
  Dkc2GamepadsAssign(&pads, sources, guids, out);
  CHECK(SDL_JoystickDetachVirtual(DeviceIndex(ida)) == 0);
  Dkc2GamepadsPoll(&pads);
  CHECK(Dkc2GamepadsAssign(&pads, sources, guids, out) == 2);
  CHECK(out[0].buttons == 0 && out[1].buttons == kDkc2GamepadX);
  CHECK(Dkc2GamepadsSelected(&pads, 0) == NULL);
  CHECK(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(Dkc2GamepadsSelected(&pads, 1))) == idb);
  a = Attach(1, "Synthetic player A");
  CHECK(a >= 0);
  SDL_Joystick *again = SDL_JoystickOpen(a);
  CHECK(again);
  CHECK(SDL_JoystickSetVirtualButton(again, SDL_CONTROLLER_BUTTON_B, 1) == 0);
  Dkc2GamepadsPoll(&pads);
  Dkc2GamepadsAssign(&pads, sources, guids, out);
  CHECK(out[0].buttons == kDkc2GamepadB && out[1].buttons == kDkc2GamepadX);
  char automatic[2][40] = {{0}};
  Dkc2GamepadsAssign(&pads, sources, automatic, out);
  CHECK(out[0].buttons == kDkc2GamepadB && out[1].buttons == kDkc2GamepadX);
  memcpy(automatic[1], guids[0], 40);
  Dkc2GamepadsAssign(&pads, sources, automatic, out);
  CHECK(out[1].buttons == kDkc2GamepadB);
  CHECK(Dkc2GamepadsSelected(&pads, 0) != Dkc2GamepadsSelected(&pads, 1));
  Dkc2GamepadsAssign(&pads, sources, guids, out);
  sources[0] = kDkc2InputSourceKeyboard;
  CHECK(Dkc2GamepadsAssign(&pads, sources, guids, out) == 1);
  CHECK(Dkc2GamepadsSelected(&pads, 0) == NULL && out[0].buttons == kDkc2GamepadX);
  keyboard[0] = 0x80;
  CHECK(Dkc2RoutePlayerInputsWithBindings(keyboard, out, 1, sources, zones, bindings) == 0x2080);
  SDL_JoystickID id_again = SDL_JoystickInstanceID(again);
  CHECK(SDL_JoystickDetachVirtual(DeviceIndex(id_again)) == 0);
  CHECK(SDL_JoystickDetachVirtual(DeviceIndex(idb)) == 0);
  SDL_JoystickClose(ja); SDL_JoystickClose(jb); SDL_JoystickClose(again);
  Dkc2GamepadsClose(&pads);
  SDL_Quit();
  CHECK(Dkc2GamepadsInit(&pads));
  Dkc2GamepadsPoll(&pads);
  Dkc2GamepadsClose(&pads);
  puts("SDL gamepads: two ports, saved selection, hot-plug, disconnect, reconnect and SDL restart passed");
  return 0;
}
