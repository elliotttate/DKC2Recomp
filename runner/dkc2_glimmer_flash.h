#ifndef DKC2_GLIMMER_FLASH_H
#define DKC2_GLIMMER_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Glimmer's light in the dark ship hold (Glimmer's Galleon), established
 * from bank $80 disassembly of the supported USA v1.0 ROM:
 *
 * - $0989: Glimmer's sprite slot (zero when there is no Glimmer).
 * - $1A,Y: Glimmer's animation id ($80:DF82).
 * - handle_glimmer_light ($80:DF54) rebuilds an indirect HDMA table every
 *   frame at $7E:80F2 + (($002A & 1) << 8): entries of [count, pointer]
 *   ending with a zero count. The pointers address light tables in bank
 *   $80: $E135 outside the beam, $E1B4 inside it ($E233/$E332 for the wide
 *   beam of the turn animations $1C14-$1C23).
 * - The ship-hold NMI ($80:BF9E) runs that table on HDMA channel 2,
 *   indirect, into WH0 ($2126) or WH1 ($2127), from the buffer the previous
 *   frame's logic wrote. It also writes TS ($212D, subscreen layers) = $13
 *   every frame, except that when the animation is exactly $1C1C, a
 *   mid-turn frame, $80:BFCA writes TS = $00 instead. With nothing on the
 *   subscreen, colour math takes the fixed colour across the whole picture:
 *   captures from a Glimmer's Galleon save show that frame as solid white,
 *   the flash on every turn.
 * - On the same animation $80:DF8A writes only the single entry
 *   [$01, $E1B4] and the terminator, so the beam's window also covers the
 *   whole screen for that frame.
 */
enum {
  kDkc2GlimmerSlotAddr = 0x0989u,
  kDkc2GlimmerAnimationOffset = 0x001Au,
  kDkc2GlimmerFacingCameraAnimation = 0x1C1Cu,
  kDkc2GlimmerLightTableAddr = 0x80F2u,
  kDkc2GlimmerLightBufferStride = 0x0100u,
  kDkc2GlimmerLightBeamPointer = 0xE1B4u,
  kDkc2GlimmerLightShadowPointer = 0xE135u,
  /* The largest table the routine writes is seven entries and the
   * terminator; anything longer is not one of its tables. */
  kDkc2GlimmerLightTableMaxBytes = 0x20u,
  kDkc2GlimmerLightHdmaChannel = 2,
  kDkc2GlimmerLightWindowLeft1 = 0x26u, /* WH0 */
  kDkc2GlimmerLightWindowRight1 = 0x27u, /* WH1 */
  kDkc2GlimmerLightTableBank = 0x7Eu,
  kDkc2GlimmerLightSubscreen = 0x13u, /* BG1, BG2 and OBJ */
};

/* The HDMA channel state the ship-hold NMI leaves for the frame. */
typedef struct Dkc2GlimmerLightChannel {
  bool active;
  bool indirect;
  uint8_t b_address;
  uint32_t table_address; /* 24-bit A1T */
} Dkc2GlimmerLightChannel;

/* Host memory between frames: the window register the light drove on the
 * last frame that was not a flash frame, or 0 when not yet seen. */
typedef struct Dkc2GlimmerFlashHold {
  uint8_t window_register;
} Dkc2GlimmerFlashHold;

/* Returns true unless env_value is explicitly "0"; NULL gives fallback. */
bool Dkc2GlimmerFlashReductionFromEnv(const char *env_value, bool fallback);

/* Call after a frame's NMI and logic, before the frame is drawn, with the
 * PPU's TS value and channel 2's state; the host writes back whatever this
 * changes. On the ship-hold NMI's flash frame (Glimmer present, channel 2
 * running his light table, subscreen empty) it restores TS to
 * kDkc2GlimmerLightSubscreen, the value the NMI writes on every other frame,
 * and points the channel back at the window register the previous frame
 * drove. The NMI rewrites both, and resets WH0/WH1, every frame, so nothing
 * carries over. On other light frames it records that register.
 * Returns true if *sub_layers or channel->b_address changed. */
bool Dkc2HoldGlimmerFlashFrame(Dkc2GlimmerFlashHold *hold,
                               const uint8_t *wram, size_t wram_size,
                               Dkc2GlimmerLightChannel *channel,
                               uint8_t *sub_layers);

/* Replaces the whole-screen flash table Glimmer's light writes on animation
 * $1C1C with the other buffer's table: the beam on screen now. The next
 * NMI runs the replaced buffer, so with Dkc2HoldGlimmerFlashFrame the flash
 * frame repeats the previous frame's beam. When the other buffer is
 * unusable, the table takes the light's outside-the-beam row instead.
 *
 * wram is the 128 KiB $7E:0000-$7F:FFFF image. Call after the frame's game
 * logic and before the next NMI. Returns true if WRAM was changed.
 * Does nothing unless Glimmer is present, on animation $1C1C, and a light
 * buffer holds exactly the flash table. */
bool Dkc2SuppressGlimmerTurnFlash(uint8_t *wram, size_t wram_size);

#ifdef __cplusplus
}
#endif

#endif
