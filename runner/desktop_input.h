#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  kDkc2DesktopPlayerCount = 2,
  kDkc2InputSourceNone = 0,
  kDkc2InputSourceKeyboard = 1,
  kDkc2InputSourceGamepad = 2,
  kDkc2GamepadDpadUp = 0x0001,
  kDkc2GamepadDpadDown = 0x0002,
  kDkc2GamepadDpadLeft = 0x0004,
  kDkc2GamepadDpadRight = 0x0008,
  kDkc2GamepadStart = 0x0010,
  kDkc2GamepadBack = 0x0020,
  kDkc2GamepadLeftShoulder = 0x0100,
  kDkc2GamepadRightShoulder = 0x0200,
  kDkc2GamepadA = 0x1000,
  kDkc2GamepadB = 0x2000,
  kDkc2GamepadX = 0x4000,
  kDkc2GamepadY = 0x8000,
  kDkc2HostRewind = 1u << 0,
  kDkc2HostFastForward = 1u << 1,
  kDkc2HostSaveState = 1u << 2,
  kDkc2HostLoadState = 1u << 3,
};

typedef struct Dkc2GamepadState {
  uint16_t buttons;
  int16_t left_x;
  int16_t left_y;
  uint8_t left_trigger;
  uint8_t right_trigger;
} Dkc2GamepadState;

uint32_t Dkc2MapGamepad(uint16_t buttons, int16_t left_x, int16_t left_y,
                        int16_t deadzone);
uint32_t Dkc2MapHostActions(uint8_t left_trigger, uint8_t right_trigger,
                            uint8_t threshold);

/* Routes the launcher's None/Keyboard/Gamepad choices to the two packed
 * 12-bit controller words accepted by RtlRunFrame. Connected gamepads are
 * assigned in XInput user order to players that selected Gamepad. */
uint32_t Dkc2RoutePlayerInputs(
    uint32_t keyboard_input, const Dkc2GamepadState *gamepads,
    size_t gamepad_count, const int player_sources[kDkc2DesktopPlayerCount],
    const int deadzone_percent[kDkc2DesktopPlayerCount],
    uint8_t trigger_threshold, uint32_t *host_actions);
