#include "desktop_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectInput(const char *name, uint32_t expected,
                        uint32_t buttons, int16_t x, int16_t y) {
  uint32_t actual = Dkc2MapGamepad(buttons, x, y, 7849);
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected $%03x, got $%03x\n",
                  name, (unsigned)expected, (unsigned)actual);
    exit(EXIT_FAILURE);
  }
}

static bool SyntheticKeyPressed(int scancode, void *context) {
  const int *pressed = (const int *)context;
  return scancode == *pressed;
}

int main(void) {
  ExpectInput("neutral and stick deadzone", 0, 0, 7849, -7849);
  ExpectInput("face buttons", UINT32_C(0x303),
              kDkc2GamepadA | kDkc2GamepadB |
                  kDkc2GamepadX | kDkc2GamepadY,
              0, 0);
  ExpectInput("menu and shoulders", UINT32_C(0xC0C),
              kDkc2GamepadBack | kDkc2GamepadStart |
                  kDkc2GamepadLeftShoulder |
                  kDkc2GamepadRightShoulder,
              0, 0);
  ExpectInput("D-pad", UINT32_C(0x0F0),
              kDkc2GamepadDpadUp | kDkc2GamepadDpadDown |
                  kDkc2GamepadDpadLeft | kDkc2GamepadDpadRight,
              0, 0);
  ExpectInput("left stick", UINT32_C(0x090), 0, 7850, 7850);
  ExpectInput("left stick negative", UINT32_C(0x060), 0, -7850, -7850);
  if (Dkc2MapHostActions(30, 30, 30) != 0 ||
      Dkc2MapHostActions(31, 0, 30) != kDkc2HostRewind ||
      Dkc2MapHostActions(0, 31, 30) != kDkc2HostFastForward ||
      Dkc2MapHostActions(255, 255, 30) !=
          (kDkc2HostRewind | kDkc2HostFastForward)) {
    (void)fputs("host trigger mapping failed\n", stderr);
    return EXIT_FAILURE;
  }

  const Dkc2GamepadState pads[2] = {
      {.buttons = kDkc2GamepadA, .left_trigger = 31},
      {.buttons = kDkc2GamepadB, .right_trigger = 31},
  };
  const int deadzones[2] = {24, 24};
  uint32_t actions = 0;
  const int keyboard_gamepad[2] = {
      kDkc2InputSourceKeyboard, kDkc2InputSourceGamepad};
  uint32_t routed = Dkc2RoutePlayerInputs(
      UINT32_C(0x008), pads, 2, keyboard_gamepad, deadzones, 30, &actions);
  if (routed != UINT32_C(0x001008) || actions != kDkc2HostRewind) {
    (void)fprintf(stderr,
                  "keyboard/P2 gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_gamepads[2] = {
      kDkc2InputSourceGamepad, kDkc2InputSourceGamepad};
  routed = Dkc2RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, two_gamepads, deadzones, 30, &actions);
  if (routed != UINT32_C(0x100001) ||
      actions != (kDkc2HostRewind | kDkc2HostFastForward)) {
    (void)fprintf(stderr,
                  "two-gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_keyboards[2] = {
      kDkc2InputSourceKeyboard, kDkc2InputSourceKeyboard};
  routed = Dkc2RoutePlayerInputs(
      UINT32_C(0x842), NULL, 0, two_keyboards, deadzones, 30, &actions);
  if (routed != UINT32_C(0x842842) || actions != 0) {
    (void)fprintf(stderr,
                  "two-keyboard route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int no_sources[2] = {kDkc2InputSourceNone, kDkc2InputSourceNone};
  routed = Dkc2RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, no_sources, deadzones, 30, &actions);
  if (routed != 0 || actions != 0) {
    (void)fputs("disabled-player routing failed\n", stderr);
    return EXIT_FAILURE;
  }

  int key_bindings[RECOMP_LAUNCHER_MAX_BINDINGS] = {0};
  key_bindings[4] = 42; /* logical SNES A -> packed bit 8 */
  int pressed_key = 42;
  if (Dkc2MapKeyboardBindings(key_bindings, SyntheticKeyPressed,
                              &pressed_key) != UINT32_C(0x100)) {
    (void)fputs("custom keyboard binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int pad_bindings[RECOMP_LAUNCHER_MAX_BINDINGS] = {0};
  pad_bindings[5] = RECOMP_LAUNCHER_PAD_BUTTON(1); /* physical B -> SNES B */
  if (Dkc2MapGamepadBindings(&pads[1], 7849, 30, pad_bindings) != 1) {
    (void)fputs("custom gamepad binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int assist_keys[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS] = {0};
  int assist_pads[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS] = {0};
  assist_keys[2] = 42;
  assist_pads[0] = RECOMP_LAUNCHER_PAD_AXIS(4, 1);
  if (Dkc2MapAssistBindings(
          assist_keys, assist_pads, SyntheticKeyPressed, &pressed_key,
          pads, 2, 30) != (kDkc2HostRewind | kDkc2HostSaveState)) {
    (void)fputs("custom Assist binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  (void)puts("Desktop gamepad mapping tests passed");
  return EXIT_SUCCESS;
}
