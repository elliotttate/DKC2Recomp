#include "desktop_input.h"

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
