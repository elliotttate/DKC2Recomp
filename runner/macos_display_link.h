#ifndef DKC2_MACOS_DISPLAY_LINK_H
#define DKC2_MACOS_DISPLAY_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A display link for the game window: the display's own refresh ticks,
 * delivered on a dedicated thread and handed to the host loop as a counted
 * sequence. Each tick names the host time of the refresh it precedes, so a
 * frame presented right after a tick is shown on that refresh, every frame
 * on the same phase. Needs macOS 14; earlier systems report no link and
 * the host keeps its own clock. */
typedef struct Dkc2MacDisplayTick {
  uint64_t sequence;  /* counts from 1; 0 means no tick has arrived */
  double timestamp;   /* host seconds when the tick was delivered */
  double target;      /* host seconds of the refresh the tick precedes */
  double duration;    /* seconds between ticks as the link reports them */
  double interval;    /* seconds since the previous tick, 0 for the first */
} Dkc2MacDisplayTick;

/* Start the link for a native NSWindow, asking the display for the given
 * frame rate (a ProMotion panel then ticks at that rate instead of 120). */
bool Dkc2MacDisplayLinkStart(void *native_window, double preferred_hz,
                             char *error, size_t error_capacity);

/* Block until a tick with at least the given sequence has arrived or the
 * timeout passes; the latest tick is copied out either way. Returns true
 * when the awaited tick is there. */
bool Dkc2MacDisplayLinkWait(uint64_t sequence, double timeout_seconds,
                            Dkc2MacDisplayTick *tick);

/* Copy the latest tick without waiting; false when the link is not running. */
bool Dkc2MacDisplayLinkLatest(Dkc2MacDisplayTick *tick);

void Dkc2MacDisplayLinkStop(void);

/* The host clock the ticks are stamped with, in seconds. */
double Dkc2MacHostSeconds(void);

#endif
