#include "desktop_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectInput(const char *name, uint32_t expected,
                        uint16_t buttons, int16_t x, int16_t y) {
  uint32_t actual = Dkc2MapGamepad(buttons, x, y, 7849);
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected $%03x, got $%03x\n",
                  name, (unsigned)expected, (unsigned)actual);
    exit(EXIT_FAILURE);
  }
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
  (void)puts("Desktop gamepad mapping tests passed");
  return EXIT_SUCCESS;
}
