#ifndef DKC2_ANIMAL_CRATE_CHEAT_H
#define DKC2_ANIMAL_CRATE_CHEAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Assist Tools / Cheats option: spawn an animal buddy crate at the active
 * Kong while the game is paused in a level. Off by default.
 *
 * Cartridge facts for the supported USA v1.0 ROM, read from banks $80, $B3,
 * $B8, $B9, $BB, $FB and $FF:
 *
 * - $08C2 bit $0040: set only by the in-level Start handler ($B8:CC0B) and
 *   cleared on unpause/exit ($80:8AE3, $80:8AAD).
 * - $0510: the active player's newly pressed buttons, refreshed every frame
 *   including paused ones ($80:8A0E/$8A21/$8A39); the pause loop reads Start
 *   ($1000) and Select ($2000) here. B = $8000, Left = $0200,
 *   Right = $0100.
 * - $0593: the active Kong's record in the 25-slot sprite table
 *   ($0D84..$16B2, stride $5E). +$00 type (zero = free), +$06 world X,
 *   +$0A world Y. The cartridge allocator ($BB:8297) scans from $0DE2.
 * - spawn_special_sprite_address ($BB:8432, JSL, Y = bank $FF init script)
 *   allocates a slot and a sprite VRAM slot and runs the script; the crate
 *   itself uses it to hatch its animal ($B9:EEC7).
 * - Animal crate (type $0148, animal_crate_main $B3:976E) init scripts, all
 *   referenced by the level placement table at $FB:E800 ($FB:EFE4..EFF6):
 *     $FF:939C Squitter (animation $02AC, hatches type $0190)
 *     $FF:9384 Rattly   (animation $02AD, hatches type $0194)
 *     $FF:9330 Squawks  (animation $02AE, hatches type $0198)
 *     $FF:9326 Rambi    (animation $02AF, hatches type $019C)
 *     $FF:9364 Enguarde (animation $02B0, crate state 3, type $01A0)
 *   The crate copies its animation into +$42; on breaking open,
 *   $FF:0D64[+$42 - $02AC] selects the animal's own script. (The recomp
 *   symbol table names the $0190 routine rambi_main; play shows $0190 is
 *   Squitter and $019C Rambi.)
 *
 * dismount_animal_action ($B8:C44A) dismounts any ridden animal (DP $6E
 * type, $6C record) on a newly pressed X ($0040 in $0983), except that
 * Squawks ($0198) needs bit 0 of its +$4A: $B8:C461 LDA $4A,x / LSR /
 * $B8:C464 BCC (return). Player code also reads that bit ($B8:9411,
 * $B8:9B63), so the option opens only this branch.
 */
enum {
  kDkc2PauseCrateLevelFlagsAddr = 0x08C2u,
  kDkc2PauseCratePausedFlagBit = 0x0040u,
  kDkc2PauseCrateActivePressedAddr = 0x0510u,
  kDkc2PauseCrateActiveKongAddr = 0x0593u,

  kDkc2PauseCrateInputBBit = 0x8000u,
  kDkc2PauseCrateInputSelectBit = 0x2000u,
  kDkc2PauseCrateInputStartBit = 0x1000u,
  kDkc2PauseCrateInputLeftBit = 0x0200u,
  kDkc2PauseCrateInputRightBit = 0x0100u,

  kDkc2PauseCrateSpriteTableStart = 0x0D84u,
  kDkc2PauseCrateFirstFreeSlot = 0x0DE2u,
  kDkc2PauseCrateSpriteTableEnd = 0x16B2u,
  kDkc2PauseCrateSpriteStride = 0x005Eu,
  kDkc2PauseCrateSpriteTypeOffset = 0x00u,
  kDkc2PauseCrateSpriteXOffset = 0x06u,
  kDkc2PauseCrateSpriteYOffset = 0x0Au,

  kDkc2PauseCrateSpawnRoutine = 0xBB8432u,
  kDkc2PauseCrateScriptBank = 0xFFu,
  kDkc2PauseCrateScriptRambi = 0x9326u,
  kDkc2PauseCrateScriptRattly = 0x9384u,
  kDkc2PauseCrateScriptSquawks = 0x9330u,
  kDkc2PauseCrateScriptSquitter = 0x939Cu,
  kDkc2PauseCrateScriptEnguarde = 0x9364u,
  kDkc2PauseCrateType = 0x0148u,

  kDkc2SquawksType = 0x0198u,
  kDkc2SquawksDismountGatePc = 0xB8C464u, /* BCC after LDA $4A,x / LSR */

  kDkc2PauseCrateMinWramSize = 0x16B2u,
};

typedef enum Dkc2PauseCrateKind {
  kDkc2PauseCrateRambi = 0,
  kDkc2PauseCrateRattly = 1,
  kDkc2PauseCrateSquawks = 2,
  kDkc2PauseCrateSquitter = 3,
  kDkc2PauseCrateEnguarde = 4,
  kDkc2PauseCrateCount = 5,
} Dkc2PauseCrateKind;

typedef struct Dkc2PauseCrateRequest {
  Dkc2PauseCrateKind kind;
  uint16_t script_ptr; /* bank $FF */
  uint16_t spawn_x;
  uint16_t spawn_y;
} Dkc2PauseCrateRequest;

/* NULL keeps `fallback`; "0" disables; any other value enables. */
bool Dkc2PauseCrateSpawnEnabledFromEnv(const char *env_value, bool fallback);

/* "Rambi", "Rattly", "Squawks", "Squitter", "Enguarde", or NULL. */
const char *Dkc2PauseCrateName(Dkc2PauseCrateKind kind);

/* The crate's bank $FF init script, or 0 for an invalid kind. */
uint16_t Dkc2PauseCrateScriptPtr(Dkc2PauseCrateKind kind);

/* The crate's animation word ($02AC..$02B0), or 0 for an invalid kind. */
uint16_t Dkc2PauseCrateAnimalId(Dkc2PauseCrateKind kind);

/* The carry the Squawks dismount branch ($B8:C464) should see: set (take
 * the cartridge's own dismount) when the option is enabled, otherwise the
 * cartridge's carry unchanged. */
uint8_t Dkc2SquawksDismountCarry(uint8_t cartridge_carry, bool enabled);

/* Defined in dkc2_game.c with the live option state; the generated
 * dismount_animal_action calls it (scripts/apply_dkc2_animal_crate_overrides.py)
 * and the interpreter hook at $B8:C464 applies the same result. */
uint8_t Dkc2SquawksDismountGate(uint8_t cartridge_carry);

/* True when enabled, the pause bit is set, and $0593 names an occupied,
 * aligned sprite record. */
bool Dkc2IsPauseCrateMenuActive(const uint8_t *wram, size_t wram_size,
                                bool enabled);

/* Steps *selected on a newly pressed Left or Right (wrapping), ignoring
 * frames that also press Start or Select or press both directions.
 * Returns true when the selection changed. */
bool Dkc2UpdatePauseCrateSelection(const uint8_t *wram, size_t wram_size,
                                   bool enabled,
                                   Dkc2PauseCrateKind *selected);

/* True, with *out filled, when the menu is active, B is newly pressed
 * without Start or Select, the kind is valid, and the sprite table has a
 * free slot (so the cartridge allocator will not fail). */
bool Dkc2DetectPauseCrateSpawn(const uint8_t *wram, size_t wram_size,
                               bool enabled, Dkc2PauseCrateKind selected,
                               Dkc2PauseCrateRequest *out);

/* After the cartridge spawn returns `slot` (DP $68), checks that it holds a
 * freshly made crate and places it at the request's position, with the
 * subpixel words at the cartridge's placement default ($8000, as
 * normal_sprite_spawn $BB:B8DA does). Returns false and writes nothing when
 * the slot is not a crate. */
bool Dkc2PlacePauseCrate(uint8_t *wram, size_t wram_size, uint16_t slot,
                         const Dkc2PauseCrateRequest *request);

#ifdef __cplusplus
}
#endif

#endif
