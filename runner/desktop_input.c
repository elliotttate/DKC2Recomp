#include "desktop_input.h"

static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

uint32_t Dkc2MapGamepad(uint16_t buttons, int16_t left_x, int16_t left_y,
                        int16_t deadzone) {
  uint32_t input = 0;
  if (buttons & kDkc2GamepadA) input |= 1u << 0; /* SNES B */
  if (buttons & kDkc2GamepadX) input |= 1u << 1; /* SNES Y */
  if (buttons & kDkc2GamepadBack) input |= 1u << 2;
  if (buttons & kDkc2GamepadStart) input |= 1u << 3;
  if ((buttons & kDkc2GamepadDpadUp) || left_y > deadzone)
    input |= 1u << 4;
  if ((buttons & kDkc2GamepadDpadDown) || left_y < -deadzone)
    input |= 1u << 5;
  if ((buttons & kDkc2GamepadDpadLeft) || left_x < -deadzone)
    input |= 1u << 6;
  if ((buttons & kDkc2GamepadDpadRight) || left_x > deadzone)
    input |= 1u << 7;
  if (buttons & kDkc2GamepadB) input |= 1u << 8; /* SNES A */
  if (buttons & kDkc2GamepadY) input |= 1u << 9; /* SNES X */
  if (buttons & kDkc2GamepadLeftShoulder) input |= 1u << 10;
  if (buttons & kDkc2GamepadRightShoulder) input |= 1u << 11;
  return input;
}

uint32_t Dkc2MapHostActions(uint8_t left_trigger, uint8_t right_trigger,
                            uint8_t threshold) {
  uint32_t actions = 0;
  if (left_trigger > threshold) actions |= kDkc2HostRewind;
  if (right_trigger > threshold) actions |= kDkc2HostFastForward;
  return actions;
}

uint32_t Dkc2RoutePlayerInputs(
    uint32_t keyboard_input, const Dkc2GamepadState *gamepads,
    size_t gamepad_count, const int player_sources[kDkc2DesktopPlayerCount],
    const int deadzone_percent[kDkc2DesktopPlayerCount],
    uint8_t trigger_threshold, uint32_t *host_actions) {
  if (!player_sources || !deadzone_percent) return 0;
  uint32_t packed = 0;
  uint32_t actions = 0;
  size_t next_gamepad = 0;
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    uint32_t input = 0;
    if (player_sources[player] == kDkc2InputSourceKeyboard) {
      input = keyboard_input;
    } else if (player_sources[player] == kDkc2InputSourceGamepad &&
               gamepads && next_gamepad < gamepad_count) {
      const Dkc2GamepadState *gamepad = &gamepads[next_gamepad++];
      int percent = ClampInt(deadzone_percent[player], 0, 100);
      int deadzone = (32767 * percent + 50) / 100;
      input = Dkc2MapGamepad(gamepad->buttons, gamepad->left_x,
                             gamepad->left_y, (int16_t)deadzone);
      actions |= Dkc2MapHostActions(gamepad->left_trigger,
                                    gamepad->right_trigger,
                                    trigger_threshold);
    }
    packed |= (input & UINT32_C(0xFFF)) << (player * 12);
  }
  if (host_actions) *host_actions = actions;
  return packed;
}
