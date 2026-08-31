#include "desktop_input.h"

static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

enum {
  kStandardPadA = 0,
  kStandardPadB = 1,
  kStandardPadX = 2,
  kStandardPadY = 3,
  kStandardPadBack = 4,
  kStandardPadGuide = 5,
  kStandardPadStart = 6,
  kStandardPadLeftStick = 7,
  kStandardPadRightStick = 8,
  kStandardPadLeftShoulder = 9,
  kStandardPadRightShoulder = 10,
  kStandardPadDpadUp = 11,
  kStandardPadDpadDown = 12,
  kStandardPadDpadLeft = 13,
  kStandardPadDpadRight = 14,
  kStandardAxisLeftX = 0,
  kStandardAxisLeftY = 1,
  kStandardAxisRightX = 2,
  kStandardAxisRightY = 3,
  kStandardAxisTriggerLeft = 4,
  kStandardAxisTriggerRight = 5,
};

static const uint8_t kLogicalPackedBits[12] = {
    4, 5, 6, 7, 8, 0, 9, 1, 10, 11, 3, 2,
};

static bool GamepadBindingPressed(const Dkc2GamepadState *gamepad,
                                  int binding, int16_t deadzone,
                                  uint8_t trigger_threshold) {
  if (!gamepad || binding <= 0) return false;
  if (RECOMP_LAUNCHER_PAD_IS_BUTTON(binding)) {
    uint32_t mask = 0;
    switch (RECOMP_LAUNCHER_PAD_BUTTON_CODE(binding)) {
      case kStandardPadA: mask = kDkc2GamepadA; break;
      case kStandardPadB: mask = kDkc2GamepadB; break;
      case kStandardPadX: mask = kDkc2GamepadX; break;
      case kStandardPadY: mask = kDkc2GamepadY; break;
      case kStandardPadBack: mask = kDkc2GamepadBack; break;
      case kStandardPadGuide: mask = kDkc2GamepadGuide; break;
      case kStandardPadStart: mask = kDkc2GamepadStart; break;
      case kStandardPadLeftStick: mask = kDkc2GamepadLeftStick; break;
      case kStandardPadRightStick: mask = kDkc2GamepadRightStick; break;
      case kStandardPadLeftShoulder:
        mask = kDkc2GamepadLeftShoulder;
        break;
      case kStandardPadRightShoulder:
        mask = kDkc2GamepadRightShoulder;
        break;
      case kStandardPadDpadUp: mask = kDkc2GamepadDpadUp; break;
      case kStandardPadDpadDown: mask = kDkc2GamepadDpadDown; break;
      case kStandardPadDpadLeft: mask = kDkc2GamepadDpadLeft; break;
      case kStandardPadDpadRight: mask = kDkc2GamepadDpadRight; break;
      default: break;
    }
    return mask && (gamepad->buttons & mask) != 0;
  }
  if (!RECOMP_LAUNCHER_PAD_IS_AXIS(binding)) return false;
  int axis = RECOMP_LAUNCHER_PAD_AXIS_CODE(binding);
  bool positive = RECOMP_LAUNCHER_PAD_AXIS_POSITIVE(binding) != 0;
  int value = 0;
  int threshold = deadzone;
  switch (axis) {
    case kStandardAxisLeftX: value = gamepad->left_x; break;
    case kStandardAxisLeftY: value = -gamepad->left_y; break;
    case kStandardAxisRightX: value = gamepad->right_x; break;
    case kStandardAxisRightY: value = -gamepad->right_y; break;
    case kStandardAxisTriggerLeft:
      value = gamepad->left_trigger;
      threshold = trigger_threshold;
      break;
    case kStandardAxisTriggerRight:
      value = gamepad->right_trigger;
      threshold = trigger_threshold;
      break;
    default: return false;
  }
  return positive ? value > threshold : value < -threshold;
}

uint32_t Dkc2MapGamepad(uint32_t buttons, int16_t left_x, int16_t left_y,
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

uint32_t Dkc2MapKeyboardBindings(
    const int bindings[RECOMP_LAUNCHER_MAX_BINDINGS],
    Dkc2KeyPressedFn pressed, void *context) {
  if (!bindings || !pressed) return 0;
  uint32_t input = 0;
  for (int logical = 0; logical < 12; logical++) {
    int scancode = bindings[logical];
    if (scancode > 0 && pressed(scancode, context))
      input |= UINT32_C(1) << kLogicalPackedBits[logical];
  }
  return input;
}

uint32_t Dkc2MapGamepadBindings(
    const Dkc2GamepadState *gamepad, int16_t deadzone, uint8_t axis_threshold,
    const int bindings[RECOMP_LAUNCHER_MAX_BINDINGS]) {
  if (!gamepad || !bindings) return 0;
  uint32_t input = 0;
  for (int logical = 0; logical < 12; logical++) {
    if (GamepadBindingPressed(gamepad, bindings[logical], deadzone,
                              axis_threshold))
      input |= UINT32_C(1) << kLogicalPackedBits[logical];
  }
  return input;
}

uint32_t Dkc2MapAssistBindings(
    const int key_bindings[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS],
    const int pad_bindings[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS],
    Dkc2KeyPressedFn pressed, void *context,
    const Dkc2GamepadState *gamepads, size_t gamepad_count,
    uint8_t axis_threshold) {
  uint32_t actions = 0;
  for (int action = 0; action < 4; action++) {
    bool active = key_bindings && pressed &&
                  key_bindings[action] > 0 &&
                  pressed(key_bindings[action], context);
    for (size_t pad = 0; !active && pad < gamepad_count; pad++) {
      active = pad_bindings &&
               GamepadBindingPressed(&gamepads[pad],
                                     pad_bindings[action], 8000,
                                     axis_threshold);
    }
    if (active) actions |= UINT32_C(1) << action;
  }
  return actions;
}

uint32_t Dkc2ApplyAssistGate(uint32_t mapped_actions,
                             uint32_t platform_actions,
                             bool assist_tools) {
  const uint32_t assist_mask =
      kDkc2HostRewind | kDkc2HostFastForward |
      kDkc2HostSaveState | kDkc2HostLoadState;
  const uint32_t platform_state_mask =
      kDkc2HostSaveState | kDkc2HostLoadState;
  if (!assist_tools)
    mapped_actions &= ~assist_mask;
  return mapped_actions | (platform_actions & platform_state_mask);
}

uint32_t Dkc2RoutePlayerInputsWithBindings(
    const uint32_t keyboard_inputs[kDkc2DesktopPlayerCount],
    const Dkc2GamepadState *gamepads, size_t gamepad_count,
    const int player_sources[kDkc2DesktopPlayerCount],
    const int deadzone_percent[kDkc2DesktopPlayerCount],
    const int pad_bindings[kDkc2DesktopPlayerCount]
                          [RECOMP_LAUNCHER_MAX_BINDINGS]) {
  if (!keyboard_inputs || !player_sources || !deadzone_percent ||
      !pad_bindings)
    return 0;
  uint32_t packed = 0;
  size_t next_gamepad = 0;
  for (int player = 0; player < kDkc2DesktopPlayerCount; player++) {
    uint32_t input = 0;
    if (player_sources[player] == kDkc2InputSourceKeyboard) {
      input = keyboard_inputs[player];
    } else if (player_sources[player] == kDkc2InputSourceGamepad &&
               gamepads && next_gamepad < gamepad_count) {
      int percent = ClampInt(deadzone_percent[player], 0, 100);
      int deadzone = (32767 * percent + 50) / 100;
      input = Dkc2MapGamepadBindings(
          &gamepads[next_gamepad++], (int16_t)deadzone, 30,
          pad_bindings[player]);
    }
    packed |= (input & UINT32_C(0xFFF)) << (player * 12);
  }
  return packed;
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
