#ifndef DKC2_PALETTE_OVERFLOW_H
#define DKC2_PALETTE_OVERFLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sprite palette overflow repair, from bank $BB disassembly of the supported
 * USA v1.0 ROM.
 *
 * The OBJ palette allocator ($BB:8A6F, A = palette id) keeps the id held by
 * each of the eight hardware OBJ palettes in $0B64[8] and a reference count
 * in $0B74[8] (negative = locked). A request shares a slot already holding
 * the id, or takes the first slot whose id is zero and queues the colours
 * ($BB:8AF6). When all eight are taken, $BB:8A86 reports error 2 through a
 * stubbed handler ($80:8BE4 is RTL), stores the id in $05F7, increments
 * slot 0's count and returns A = 0 with carry set. Every caller then writes
 * A & $0E00 into the sprite's OAM attribute word +$12, so the sprite is drawn
 * with palette 0, the shared HUD/banana palette ($6682), and the allocator
 * never retries. Widescreen keeps more objects alive than the 4:3 screen, and
 * the pause-menu crates add animals a stage does not expect, so the eight
 * palettes run out where the cartridge never ran them out (a breakable wall
 * drawn in red, white and yellow).
 *
 * The repair records which sprite took the fallback and, once a hardware
 * palette frees, requests the palette again through the cartridge's own
 * allocator, moves the sprite to the slot it returns and gives slot 0 back the
 * reference the fallback took. Releasing the sprite later frees the new slot
 * through +$12 as usual.
 */
enum {
  kDkc2PaletteSlotIdsAddr = 0x0B64u,
  kDkc2PaletteSlotRefsAddr = 0x0B74u,
  kDkc2PaletteSlotCount = 8,
  kDkc2PaletteOverflowIdAddr = 0x05F7u,
  kDkc2PaletteSharedHudId = 0x6682u,
  kDkc2PaletteAttrOffset = 0x0012u,
  kDkc2PaletteAttrMask = 0x0E00u,
  kDkc2PaletteSpriteTableAddr = 0x0D84u,
  kDkc2PaletteSpriteTableEnd = 0x16B2u,
  kDkc2PaletteSpriteStride = 0x005Eu,
  kDkc2PaletteRequestRoutine = 0xBB8A6Fu,
  /* First instruction after the fallback's error call ($BB:8A89 JSL). */
  kDkc2PaletteOverflowHookPc = 0xBB8A8Du,
  /* One entry per sprite record ($0D84..$16B2 in $5E steps: 25), so every
   * record that takes the fallback in one frame can be tracked. */
  kDkc2PaletteOverflowMaxTracked =
      (kDkc2PaletteSpriteTableEnd - kDkc2PaletteSpriteTableAddr +
       kDkc2PaletteSpriteStride - 1) / kDkc2PaletteSpriteStride,
};

/* Where the overflowing request was made from: the 16-bit return address
 * the JSR into $BB:8A6F pushed (already +1, i.e. the next instruction), and,
 * when that is a JSL wrapper ($BB:8A61 or $BB:8A65), the wrapper's own long
 * return (bank << 16 | next instruction). dp68/dp64/y are the direct-page
 * words $68/$64 and Y at the time of the overflow. */
typedef struct Dkc2PaletteOverflowCaller {
  uint16_t jsr_return;
  uint32_t jsl_return;
  uint16_t dp68;
  uint16_t dp64;
  uint16_t y;
} Dkc2PaletteOverflowCaller;

/* Returns the sprite record the caller will write the fallback palette into,
 * or 0 for callers that do not write a sprite record (the HUD's own $6682
 * requests, the four-argument script command that branches on carry, and
 * any return address not audited here). */
uint16_t Dkc2PaletteOverflowSpriteForCaller(
    const Dkc2PaletteOverflowCaller *caller);

/* True for the start address of a sprite record in $0D84..$16B2. */
bool Dkc2PaletteIsSpriteRecord(uint16_t record);

typedef struct Dkc2PaletteOverflowEntry {
  uint16_t record;
  uint16_t palette_id;
  /* Sprite type seen when the entry was first confirmed, 0 until then. */
  uint16_t type;
} Dkc2PaletteOverflowEntry;

typedef struct Dkc2PaletteOverflowTracker {
  Dkc2PaletteOverflowEntry entries[kDkc2PaletteOverflowMaxTracked];
  uint8_t count;
} Dkc2PaletteOverflowTracker;

void Dkc2PaletteOverflowReset(Dkc2PaletteOverflowTracker *tracker);

/* Records a fallback. A newer fallback for the same record replaces the old
 * one. Returns false for a non-record or a full tracker. */
bool Dkc2PaletteOverflowNote(Dkc2PaletteOverflowTracker *tracker,
                             uint16_t record, uint16_t palette_id);

/* Drops entries whose sprite was released, replaced (type changed) or given a
 * non-zero palette since, confirms new entries, and picks one whose palette
 * the allocator can now place without overflowing: the id is already held by
 * an unlocked slot, or some slot is free. Returns false when none can.
 * Dropping the last entry for the id in $05F7 clears $05F7 (see
 * Dkc2PaletteOverflowRecoverSaved). */
bool Dkc2PaletteOverflowPickRetry(Dkc2PaletteOverflowTracker *tracker,
                                  uint8_t *wram, size_t wram_size,
                                  Dkc2PaletteOverflowEntry *out);

/* Applies the allocator's answer for a retried entry: on success (carry
 * clear) writes A & $0E00 into the sprite's +$12 and takes back the
 * reference the original fallback added to slot 0; on another overflow
 * (carry set) takes back the reference that one added. Removes the entry
 * either way, and on success clears $05F7 when it names that entry's id and
 * no other entry still waits for it. Slot 0's lock bit (bit 15) is kept
 * when its count is decremented. Returns true when the sprite moved off
 * palette 0. */
bool Dkc2PaletteOverflowAdopt(Dkc2PaletteOverflowTracker *tracker,
                              uint8_t *wram, size_t wram_size,
                              const Dkc2PaletteOverflowEntry *entry,
                              uint16_t result_a, bool carry);

/* A save made after an overflow carries the stuck sprite but no host record
 * of it. Adopts it once after a load when the evidence is unambiguous: $05F7
 * holds a non-zero id that no slot holds, slot 0 holds the shared HUD palette,
 * and exactly one live sprite record is drawn with palette 0. Returns true
 * when an entry was added.
 *
 * The cartridge writes $05F7 only at $BB:8A8D and never reads or clears it
 * (no other access in the generated code), so the repair clears it itself
 * once the overflow it names is resolved. A save written after that cannot
 * offer a stale id. Saves written before the repair cleared it (or by the
 * cartridge alone) still can, which this check cannot rule out. */
bool Dkc2PaletteOverflowRecoverSaved(Dkc2PaletteOverflowTracker *tracker,
                                     const uint8_t *wram, size_t wram_size);

/* Called by the private generated request_palette_direct_M0X0 at $BB:8A8D
 * (scripts/apply_dkc2_palette_overflow_overrides.py) and by an interpreter
 * hook at the same PC; defined in runner/dkc2_game.c. Reads guest state only. */
struct CpuState;
void Dkc2SpritePaletteOverflowHook(struct CpuState *cpu);

#ifdef __cplusplus
}
#endif

#endif
