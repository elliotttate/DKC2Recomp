#pragma once
#include <SDL.h>
#include "desktop_input.h"

enum { kDkc2MaximumGamepads = 8 };
typedef struct Dkc2DesktopGamepads {
  SDL_GameController *handles[kDkc2MaximumGamepads];
  char guids[kDkc2MaximumGamepads][40];
  int selected[2];
  bool initialized;
} Dkc2DesktopGamepads;

/* Register before the launcher's SDL initialization as well as gameplay. */
void Dkc2GamepadsRegisterMappings(void);
bool Dkc2GamepadsInit(Dkc2DesktopGamepads *pads);
void Dkc2GamepadsClose(Dkc2DesktopGamepads *pads);
void Dkc2GamepadsPoll(Dkc2DesktopGamepads *pads);
size_t Dkc2GamepadsReadAll(Dkc2DesktopGamepads *pads, Dkc2GamepadState *out);
/* Explicit missing devices remain neutral. Automatic assignments preserve
 * connected instance ownership when another controller is unplugged. */
size_t Dkc2GamepadsAssign(Dkc2DesktopGamepads *pads, const int sources[2],
                        const char guids[2][40], Dkc2GamepadState out[2]);
SDL_GameController *Dkc2GamepadsSelected(Dkc2DesktopGamepads *pads, int player);
