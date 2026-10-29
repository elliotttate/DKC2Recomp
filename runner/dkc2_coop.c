#include "dkc2_coop.h"
#include "dkc2_coop_assist.h"
#include "dkc2_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Dkc2CoopMode s_coop_mode = kDkc2CoopSimultaneous;
static uint16_t s_stomp_source;
static uint16_t s_stomp_kong;
static uint16_t s_push_source, s_push_kong;
static uint16_t s_cat_source, s_cat_kong;
static uint16_t s_shared_stomp_owner, s_shared_stomp_partner;
static uint16_t s_mount_source, s_mount_kong;
static uint16_t s_animal_hit_source, s_animal_hit_owner;
static uint16_t s_cart_hit_source, s_cart_hit_kong;
static uint16_t s_held_sprite, s_held_id, s_held_kong;
static uint8_t s_lost_mask;
static uint8_t s_stomp_events;
static bool s_banana_extra_pass;
static uint16_t s_banana_second_bounds[4];
static uint16_t s_rescue_hold[2];
static bool s_rescue_fired[2];
static bool s_rescue_level_valid;
static uint16_t s_rescue_level;

static void CoopCancelRescueCharge(void) {
  memset(s_rescue_hold, 0, sizeof s_rescue_hold);
  s_rescue_level_valid = false;
}

static void CoopResetRescueHold(void) {
  CoopCancelRescueCharge();
  memset(s_rescue_fired, 0, sizeof s_rescue_fired);
}

void Dkc2CoopRescuePause(void) { CoopCancelRescueCharge(); }

/* DKC2_COOP_DEBUG=1 prints the first evaluations of each helper so a
 * route can confirm the generated gate actually consults the policy and
 * which words it selects. Host observation only. */
static int CoopDebugEnabled(void) {
  static int state = -1;
  if (state < 0)
    state = getenv("DKC2_COOP_DEBUG") != NULL && getenv("DKC2_COOP_DEBUG")[0] == '1';
  return state;
}

static int s_debug_gate_count;
static int s_debug_held_count;
static int s_debug_pressed_count;

void Dkc2CoopSetMode(Dkc2CoopMode mode) {
  if (mode < kDkc2CoopSimultaneous || mode >= kDkc2CoopModeCount)
    mode = kDkc2CoopSimultaneous;
  s_coop_mode = mode;
  CoopResetRescueHold();
  s_stomp_source = s_stomp_kong = 0;
  s_push_source = s_push_kong = 0;
  s_cat_source = s_cat_kong = 0;
  s_shared_stomp_owner = s_shared_stomp_partner = 0;
  s_mount_source = s_mount_kong = 0;
  s_animal_hit_source = s_animal_hit_owner = 0;
  s_cart_hit_source = s_cart_hit_kong = 0;
  s_held_sprite = s_held_id = s_held_kong = 0;
  s_banana_extra_pass = false;
  s_stomp_events = 0;
}

void Dkc2CoopResetSession(void) {
  CoopResetRescueHold();
  s_lost_mask = 0;
  s_banana_extra_pass = false;
  s_stomp_events = 0;
  s_stomp_source = s_stomp_kong = 0;
  s_push_source = s_push_kong = 0;
  s_cat_source = s_cat_kong = 0;
  s_shared_stomp_owner = s_shared_stomp_partner = 0;
  s_mount_source = s_mount_kong = 0;
  s_animal_hit_source = s_animal_hit_owner = 0;
  s_cart_hit_source = s_cart_hit_kong = 0;
  s_held_sprite = s_held_id = s_held_kong = 0;
}

uint8_t Dkc2CoopSaveLostMask(void) { return s_lost_mask; }

void Dkc2CoopLoadLostMask(CpuState *cpu, uint8_t mask, bool recorded) {
  Dkc2CoopResetSession();
  s_lost_mask = recorded ? (uint8_t)(mask & 3) : 0;
  if (!cpu || recorded) return;
  /* Old snapshots have no host co-op metadata. An absent Kong that has
   * already left its initial follower order has spent its initial join.
   * Hurt states themselves are observed again by the state dispatcher. */
  if (!(cpu_read16(cpu, 0, 0x08C2) & 0x4000)) {
    uint16_t slot = cpu_read16(cpu, 0, 0x0597);
    if ((slot == kDkc2CoopSlotA || slot == kDkc2CoopSlotB) &&
        cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E)) == 0x13 &&
        cpu_read16(cpu, 0, (uint16_t)(slot + 2)) != 0xD8)
      s_lost_mask |= slot == kDkc2CoopSlotA ? 1 : 2;
  }
}

Dkc2CoopMode Dkc2CoopGetMode(void) { return s_coop_mode; }

bool Dkc2CoopModeFromName(const char *text, Dkc2CoopMode *mode) {
  if (!text || !mode)
    return false;
  if (strcmp(text, "simultaneous") == 0) {
    *mode = kDkc2CoopSimultaneous;
    return true;
  }
  if (strcmp(text, "classic") == 0) {
    *mode = kDkc2CoopClassic;
    return true;
  }
  return false;
}

bool Dkc2CoopSimultaneousActive(uint16_t mode_flag, bool enabled) {
  /* Only "2 PLAYER TEAM" ($060D == 1) becomes simultaneous. 1 PLAYER and
   * 2 PLAYER CONTEST keep the cartridge input path untouched. */
  return enabled && mode_flag == 1;
}

uint16_t Dkc2CoopGateActive(uint16_t active_slot, uint16_t sprite,
                            uint16_t mode_flag, bool enabled) {
  if (!Dkc2CoopSimultaneousActive(mode_flag, enabled))
    return active_slot;
  /* Compare current_sprite against itself: every Kong that reaches the
   * gate takes the input path with its own controller's words. Only the
   * two Kong slots qualify; anything else keeps the cartridge compare
   * (the gate only runs from Kong action states, so this is defensive). */
  if (sprite == kDkc2CoopSlotA || sprite == kDkc2CoopSlotB)
    return sprite;
  return active_slot;
}

/* $13 waits for the follower script; $22 replays the leader's position
 * history ($7FA532/$7FA572), including after a jump lands. Redirect at
 * state dispatch, before that replay can teleport the second player.
 * A waiting player joins on input. Once in normal follow mode it stays
 * independently controllable even when its controller is released.
 * Hurt, carried, barrel and other action states retain their own flow. */
bool Dkc2CoopFollowerNeedsControl(uint16_t follower_state,
                                  uint16_t follower_held) {
  return follower_state == 0x0022 ||
         (follower_state == 0x0013 && follower_held != 0);
}

uint16_t Dkc2CoopSelectHeld(uint16_t original, uint16_t sprite,
                            uint16_t mode_flag, bool enabled,
                            uint16_t p1_held, uint16_t p2_held) {
  if (!Dkc2CoopSimultaneousActive(mode_flag, enabled))
    return original;
  /* TEAM reserves Select for the frame-based catch-up hold; do not begin a
   * cartridge tag/handoff animation while that button is charging. */
  if (sprite == kDkc2CoopSlotA)
    return (uint16_t)(p1_held & ~0x2000u);
  if (sprite == kDkc2CoopSlotB)
    return (uint16_t)(p2_held & ~0x2000u);
  return original;
}

uint16_t Dkc2CoopSelectPressed(uint16_t original, uint16_t sprite,
                               uint16_t mode_flag, bool enabled,
                               uint16_t p1_pressed, uint16_t p2_pressed) {
  if (!Dkc2CoopSimultaneousActive(mode_flag, enabled))
    return original;
  if (sprite == kDkc2CoopSlotA)
    return (uint16_t)(p1_pressed & ~0x2000u);
  if (sprite == kDkc2CoopSlotB)
    return (uint16_t)(p2_pressed & ~0x2000u);
  return original;
}

/* Read shapes mirror the generated code's own addressing so the helpers
 * observe exactly the bytes the wrapped instructions would: direct-page
 * operands resolve in bank $00 at D+offset, absolute operands at DB:offset
 * (DKC2 runs these paths with D = 0 and DB = $00/$7E, which alias the same
 * WRAM bytes for every address used here). */
static uint16_t CoopReadDp(CpuState *cpu, uint16_t offset) {
  return cpu_read16(cpu, 0x00, (uint16)(cpu->D + offset));
}

static uint16_t CoopReadAbs(CpuState *cpu, uint16_t offset) {
  return cpu_read16(cpu, cpu->DB, (uint16)(offset));
}

static bool CoopEnabled(CpuState *cpu) {
  return cpu && Dkc2CoopSimultaneousActive(cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                                          s_coop_mode == kDkc2CoopSimultaneous);
}

static void CoopMakeLeader(CpuState *cpu, uint16_t kong) {
  if (kong != cpu_read16(cpu, 0, 0x0597)) return;
  uint16_t active = cpu_read16(cpu, 0, 0x0593);
  uint16_t active_work = cpu_read16(cpu, 0, 0x0595);
  cpu_write16(cpu, 0, 0x0593, kong);
  cpu_write16(cpu, 0, 0x0595, cpu_read16(cpu, 0, 0x0599));
  cpu_write16(cpu, 0, 0x0597, active);
  cpu_write16(cpu, 0, 0x0599, active_work);
  cpu_write16(cpu, 0, 0x08A4, kong == kDkc2CoopSlotB ? 1 : 0);
  cpu_write16(cpu, 0, 0x08A2, kong == kDkc2CoopSlotB ? 2 : 1);
}

static void CoopPromoteCollider(CpuState *cpu, uint16_t kong) {
  if (kong != cpu_read16(cpu, 0, 0x0597)) return;
  CoopMakeLeader(cpu, kong);
  for (unsigned i = 0; i < 0x18; i += 2) {
    uint16_t a = cpu_read16(cpu, 0, (uint16_t)(0x09AF + i));
    uint16_t b = cpu_read16(cpu, 0, (uint16_t)(0x09C7 + i));
    cpu_write16(cpu, 0, (uint16_t)(0x09AF + i), b);
    cpu_write16(cpu, 0, (uint16_t)(0x09C7 + i), a);
  }
}

uint16_t Dkc2CoopTeamPartnerValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (kong == kDkc2CoopSlotA) return kDkc2CoopSlotB;
  if (kong == kDkc2CoopSlotB) return kDkc2CoopSlotA;
  return original;
}

uint16_t Dkc2CoopTeamStateValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  uint16_t partner = Dkc2CoopTeamPartnerValue(cpu, 0);
  if (!partner) return original;
  /* The original action summons an AI follower, regardless of distance.
   * Independent players must be alive, on foot and within arm's reach.
   * Only substitute the eligibility operand; the native reaction still
   * performs the pickup, animation, carrying physics and release. */
  if (cpu->Y != partner || s_lost_mask || cpu_read16(cpu, 0, 0x0D7A) ||
      cpu_read16(cpu, 0, 0x006E) ||
      !(cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
      cpu_read16(cpu, 0, (uint16_t)(kong + 0x2E)) != 0 ||
      (original != 0 && original != 0x13 && original != 0x22))
    return 0xFFFF;
  int dx = (int)cpu_read16(cpu, 0, (uint16_t)(kong + 6)) -
           (int)cpu_read16(cpu, 0, (uint16_t)(partner + 6));
  int dy = (int)cpu_read16(cpu, 0, (uint16_t)(kong + 10)) -
           (int)cpu_read16(cpu, 0, (uint16_t)(partner + 10));
  if (dx < -24 || dx > 24 || dy < -16 || dy > 16) return 0xFFFF;
  /* Team reactions use the active/inactive work pair. Promote the carrier
   * without moving either actor or changing their fixed controller ports.
   * Guest selectors preserve this ownership across saves and rewind. */
  CoopMakeLeader(cpu, kong);
  return 0x22;
}

static uint16_t CoopHeldOwner(CpuState *cpu) {
  if (!cpu || !Dkc2CoopSimultaneousActive(
                  cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                  s_coop_mode == kDkc2CoopSimultaneous))
    return 0;
  uint16_t object = cpu_read16(cpu, 0, 0x0D7A);
  if (object == kDkc2CoopSlotA || object == kDkc2CoopSlotB) {
    uint16_t owner = cpu_read16(cpu, 0, 0x0593);
    return object == cpu_read16(cpu, 0, 0x0597) &&
           owner == (object == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA)
               ? owner : 0;
  }
  if (object < 0x0E9E || object >= 0x16B2 ||
      (object - 0x0DE2) % 0x5E != 0 || !cpu_read16(cpu, 0, object))
    return 0;
  /* Ground/air carry and throw states identify the owner in a restored guest
   * snapshot too. The pickup record covers the transition before the Kong
   * has entered its carrying state; it is not the only source of truth. */
  uint16_t candidate = 0;
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
    uint16_t state = cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E));
    if ((state >= 0x0C && state <= 0x0F) || state == 0x3F) {
      if (candidate) { candidate = 0; break; }
      candidate = slot;
    }
  }
  if (candidate) return candidate;
  if (object != s_held_sprite || cpu_read16(cpu, 0, object) != s_held_id)
    return 0;
  return s_held_kong;
}

uint16_t Dkc2CoopRecordPickupValue(CpuState *cpu, uint16_t object) {
  s_held_sprite = s_held_id = s_held_kong = 0;
  if (cpu && Dkc2CoopSimultaneousActive(
                 cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                 s_coop_mode == kDkc2CoopSimultaneous)) {
    uint16_t owner = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
    if ((owner == kDkc2CoopSlotA || owner == kDkc2CoopSlotB) &&
        object >= 0x0E9E && object < 0x16B2 &&
        (object - 0x0DE2) % 0x5E == 0) {
      uint16_t id = cpu_read16(cpu, 0, object);
      if (id) {
        s_held_sprite = object;
        s_held_id = id;
        s_held_kong = owner;
      }
    }
  }
  return object;
}

uint16_t Dkc2CoopHeldOwnerValue(CpuState *cpu, uint16_t original) {
  uint16_t owner = CoopHeldOwner(cpu);
  return owner ? owner : original;
}

uint16_t Dkc2CoopHeldObjectValue(CpuState *cpu, uint16_t original) {
  uint16_t owner = CoopHeldOwner(cpu);
  if (owner) {
    uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
    if ((sprite == kDkc2CoopSlotA || sprite == kDkc2CoopSlotB) && sprite != owner)
      return 0;
  }
  return original;
}

static bool CoopLiveActor(CpuState *cpu, uint16_t slot) {
  uint16_t type = cpu_read16(cpu, 0, slot);
  return (type == 0xE4 || type == 0xE8) &&
         (cpu_read16(cpu, 0, (uint16_t)(slot + 0x30)) & 0x10);
}

static bool CoopMountedAnimal(CpuState *cpu) {
  uint16_t animal = cpu_read16(cpu, 0, 0x006C);
  uint16_t type = cpu_read16(cpu, 0, 0x006E);
  return type >= 0x190 && type <= 0x1A0 && !(type & 3) &&
      animal >= 0xE9E && animal < 0x16B2 && (animal - 0xDE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, animal) == type;
}

static bool CoopAnimalOnly(CpuState *cpu) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x006C) ||
      !(cpu_read16(cpu, 0, 0x08C2) & 0x0200)) return false;
  uint16_t type = cpu_read16(cpu, 0, 0x006E);
  uint16_t owner = cpu_read16(cpu, 0, 0x0593);
  return type >= 0x190 && type <= 0x1A0 && !(type & 3) &&
      (owner == kDkc2CoopSlotA || owner == kDkc2CoopSlotB);
}

void Dkc2CoopAnimalOnlyHit(CpuState *cpu) {
  /* Called only at the accepted, surviving animal-hit continuation. Native
   * health, recoil and invincibility still execute once for the same actor. */
  uint16_t source = s_animal_hit_source, owner = s_animal_hit_owner;
  s_animal_hit_source = s_animal_hit_owner = 0;
  if (!CoopAnimalOnly(cpu) || !source || owner != cpu_read16(cpu, 0, 0x0593) ||
      source != cpu_read16(cpu, 0, 0x0A84)) return;
  uint16_t controller = cpu_read16(cpu, 0, 0x08A2);
  if (controller != 1 && controller != 2)
    controller = cpu_read16(cpu, 0, 0x0593) == kDkc2CoopSlotA ? 1 : 2;
  controller = (uint16_t)(3 - controller);
  cpu_write16(cpu, 0, 0x08A2, controller);
  cpu_write16(cpu, 0, 0x08A4, controller == 2 ? 1 : 0);
}

static bool CoopRescueMovable(CpuState *cpu, uint16_t slot) {
  if (!CoopLiveActor(cpu, slot)) return false;
  /* Ordinary on-foot movement, swimming and carrying. Scripted hurt,
   * barrel, team-carry and animal transitions retain their guest ownership. */
  switch (cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E))) {
    case 0: case 1: case 2: case 4: case 6:
    case 0x0C: case 0x0D: case 0x0E:
    case 0x3D:
      return true;
    default:
      return false;
  }
}

static bool CoopCartActor(CpuState *cpu, uint16_t cart) {
  return cart >= 0xE9E && cart < 0x16B2 && (cart - 0xDE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, cart) == 0x158 &&
      (cpu_read16(cpu, 0, cart + 0x30) & 0x1000);
}

static bool CoopSharedKart(CpuState *cpu, uint16_t cart) {
  return CoopCartActor(cpu, cart) && (cpu_read16(cpu, 0, cart + 0x2E) & 0xFF) == 2;
}

static bool CoopCartRider(CpuState *cpu, uint16_t slot) {
  if (!CoopLiveActor(cpu, slot) || (s_lost_mask & (slot == kDkc2CoopSlotA ? 1 : 2)))
    return false;
  uint16_t state = cpu_read16(cpu, 0, slot + 0x2E);
  return (state == 0x5B || state == 0x5C || state == 0x67 || state == 0x68) &&
      CoopCartActor(cpu, cpu_read16(cpu, 0, slot + 0x42));
}

static bool CoopCartFlight(CpuState *cpu, uint16_t slot) {
  uint16_t state = cpu_read16(cpu, 0, slot + 0x2E);
  return (state == 0x67 || state == 0x68) && CoopLiveActor(cpu, slot) &&
      !(s_lost_mask & (slot == kDkc2CoopSlotA ? 1 : 2));
}

static void CoopCartAnimation(CpuState *cpu, uint16_t slot, uint16_t animation) {
  uint16_t work = cpu_read16(cpu, 0, slot == cpu_read16(cpu, 0, 0x593) ? 0x595 : 0x599);
  if (work != 0x16B2 && work != 0x16D8) return;
  cpu_write16(cpu, 0, work, animation);
  if (cpu_read16(cpu, 0, slot) == 0xE8) animation += 0xA3;
  cpu_write16(cpu, 0, slot + 0x36, animation);
  cpu_write16(cpu, 0, slot + 0x38, 0);
  cpu_write16(cpu, 0, slot + 0x3A, 0x100);
  cpu_write16(cpu, 0, slot + 0x3C, cpu_read16(cpu, 0xF9, animation * 4));
  cpu_write16(cpu, 0, slot + 0x3E, 0);
  cpu_write16(cpu, 0, slot + 0x40, cpu_read16(cpu, 0xF9, animation * 4 + 2));
}

bool Dkc2CoopCartIndependent(CpuState *cpu) {
  return CoopEnabled(cpu) && CoopCartActor(cpu, CoopReadDp(cpu, kDkc2CoopCurrentSprite));
}

uint16_t Dkc2CoopCartBoardingLeaderValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t cart = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (!CoopSharedKart(cpu, cart)) return original;
  uint16_t front = cpu_read16(cpu, 0, cart + 0x42) & 0x7FFF;
  if ((front == kDkc2CoopSlotA || front == kDkc2CoopSlotB) && CoopCartRider(cpu, front))
    return front;
  uint16_t collider = CoopReadDp(cpu, 0x6A);
  return (collider == kDkc2CoopSlotA || collider == kDkc2CoopSlotB) &&
      CoopLiveActor(cpu, collider) && !(s_lost_mask & (collider == kDkc2CoopSlotA ? 1 : 2))
      ? collider : original;
}

/* Native mode $02 jumps the vehicle from its front rider's jump timestamp.
 * The front seat is assigned to the first Kong to board and survives reloads. */
uint16_t Dkc2CoopCartLeaderWorkValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t cart = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (!CoopSharedKart(cpu, cart)) return original;
  uint16_t leader = cpu_read16(cpu, 0, cart + 0x42) & 0x7FFF;
  if ((leader != kDkc2CoopSlotA && leader != kDkc2CoopSlotB) ||
      !CoopCartRider(cpu, leader) || cpu_read16(cpu, 0, leader + 0x42) != cart)
    return original;
  uint16_t work = cpu_read16(cpu, 0, leader == cpu_read16(cpu, 0, 0x593) ? 0x595 : 0x599);
  if (work != 0x16B2 && work != 0x16D8) return original;
  if (!cpu_read16(cpu, 0, 0x0513) &&
      (cpu_read16(cpu, 0, leader == kDkc2CoopSlotA ? kDkc2CoopP1Pressed : kDkc2CoopP2Pressed) & 0x8000))
    cpu_write16(cpu, 0, work + 0x22, CoopReadDp(cpu, 0x002A));
  return work;
}

bool Dkc2CoopCartClip(CpuState *cpu) {
  /* The native active hitbox also includes the cart body. Airborne riders
   * must be hit at their own position, just like the separate follower. */
  uint16_t kong = cpu ? cpu_read16(cpu, 0, 0x0593) : 0;
  return CoopEnabled(cpu) && (CoopCartRider(cpu, kong) || CoopCartFlight(cpu, kong));
}

uint16_t Dkc2CoopCartWorkValue(CpuState *cpu, uint16_t original) {
  uint16_t kong = cpu ? CoopReadDp(cpu, kDkc2CoopCurrentSprite) : 0;
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return original;
  if (!CoopEnabled(cpu) || !CoopCartFlight(cpu, kong)) return original;
  return cpu_read16(cpu, 0, kong == cpu_read16(cpu, 0, 0x0593) ? 0x0595 : 0x0599);
}

uint16_t Dkc2CoopCartStompValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, 0x006A);
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return original;
  uint16_t state = cpu_read16(cpu, 0, kong + 0x2E);
  /* Only the contacted airborne rider loses the native cart-body attack
   * classification. Ordinary collision logic then accepts a real stomp and
   * queues its normal bounce; seated riders retain native cart collisions. */
  return (state == 0x67 || state == 0x68) && CoopCartFlight(cpu, kong) ? 0 : original;
}

bool Dkc2CoopCartEntryBlocked(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t cart = cpu_read16(cpu, 0, 0x0A86), kong = cpu_read16(cpu, 0, 0x0A88);
  if (!CoopCartActor(cpu, cart) || (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB))
    return false;
  if (s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) return true;
  uint16_t state = cpu_read16(cpu, 0, kong + 0x2E);
  if (state == 0x11 || state == 0x31) return true; /* Already inside a cannon. */
  if (CoopSharedKart(cpu, cart) && cpu_read16(cpu, 0, cart + 0x42) == kong)
    CoopPromoteCollider(cpu, kong); /* Accepted first boarder owns the native front state. */
  if (!CoopCartFlight(cpu, kong)) return false; /* Preserve native initial recruitment. */
  /* Own-seat landing belongs to the independent arc, not the AI follower's
   * automatic reunion. Another cart may accept a real descending contact. */
  if (cpu_read16(cpu, 0, kong + 0x42) == cart ||
      (int16_t)cpu_read16(cpu, 0, kong + 0x24) < 0) return true;
  /* Native collision accepts the feet before the actor origin reaches the
   * seat. Finish that accepted landing now, before the cart's next contact
   * validation can eject its newly recruited rider. */
  cpu_write16(cpu, 0, kong + 10, cpu_read16(cpu, 0, cart + 10));
  cpu_write16(cpu, 0, kong + 8, 0);
  cpu_write16(cpu, 0, kong + 0x24, 0);
  return false;
}

/* Accepted native $23 ownership: cart collisions and self-sourced terrain. */
void Dkc2CoopCartHitOwner(CpuState *cpu) {
  uint16_t kong = s_cart_hit_kong, source = s_cart_hit_source;
  s_cart_hit_source = s_cart_hit_kong = 0;
  if (CoopEnabled(cpu) && source && source == cpu_read16(cpu, 0, 0x0A84) &&
      (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      ((source == kong && CoopLiveActor(cpu, kong) &&
        !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2))) || CoopCartRider(cpu, kong)))
    CoopPromoteCollider(cpu, kong);
}

static bool CoopCartPitFromCart(CpuState *cpu, uint16_t cart);

bool Dkc2CoopCartSeats(CpuState *cpu) {
  if (!Dkc2CoopCartIndependent(cpu)) return false;
  uint16_t cart = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if ((int16_t)(cpu_read16(cpu, 0, cart + 10) - cpu_read16(cpu, 0, 0x17C0)) >= 0x120 &&
      CoopCartPitFromCart(cpu, cart)) return true;
  for (unsigned seat = 0; seat < 2; ++seat) {
    uint16_t slot = cpu_read16(cpu, 0, cart + 0x42 + seat * 2) & 0x7FFF;
    if (slot != kDkc2CoopSlotA && slot != kDkc2CoopSlotB) continue;
    if (cpu_read16(cpu, 0, slot + 0x42) != cart) {
      /* A new cart can accept a landing before the old cart is deleted.
       * Its stale seat reference must never pull that rider back into a pit. */
      /* During initial recruitment the accepted reaction has not yet replaced
       * the Kong's barrel pointer. Clear only a proven different cart owner. */
      if (CoopCartRider(cpu, slot))
        cpu_write16(cpu, 0, cart + 0x42 + seat * 2, 0);
      continue;
    }
    uint16_t state = cpu_read16(cpu, 0, slot + 0x2E);
    /* Native $70 waits for the cart to carry its rider beyond the viewport
     * before starting the exit fade. It remains seated without jump input. */
    bool exiting = state == 0x70 && CoopLiveActor(cpu, slot) &&
        !(s_lost_mask & (slot == kDkc2CoopSlotA ? 1 : 2));
    if (!CoopCartRider(cpu, slot) && !exiting) continue;
    uint16_t x = cpu_read16(cpu, 0, cart + 6), y = cpu_read16(cpu, 0, cart + 10);
    uint16_t slope = cpu_read16(cpu, 0, cart + 0x36);
    if (seat && slope < 0x13) {
      unsigned index = slope + (cpu_read16(cpu, 0, slot) == 0xE4 ? 0 : 0x26);
      x += cpu_read16(cpu, 0xBE, (uint16_t)(0xDAAD + index * 2));
      y += cpu_read16(cpu, 0xBE, (uint16_t)(0xDAD3 + index * 2));
    }
    bool airborne = state == 0x67 || state == 0x68;
    cpu_write16(cpu, 0, slot + 6, x);
    if (airborne && ((int16_t)cpu_read16(cpu, 0, slot + 0x24) < 0 ||
                    (int16_t)(cpu_read16(cpu, 0, slot + 10) - y) < 0)) continue;
    if (airborne) {
      cpu_write16(cpu, 0, cart + 0x42 + seat * 2, slot);
      cpu_write16(cpu, 0, slot + 0x2E, seat ? 0x5C : 0x5B);
      cpu_write16(cpu, 0, slot + 0x1C, 0);
      CoopCartAnimation(cpu, slot, 0x4E);
    }
    cpu_write16(cpu, 0, slot + 10, y);
    cpu_write16(cpu, 0, slot + 8, 0);
    cpu_write16(cpu, 0, slot + 0x24, 0);
    uint16_t work = cpu_read16(cpu, 0, slot == cpu_read16(cpu, 0, 0x0593) ? 0x0595 : 0x0599);
    if (work == 0x16B2 || work == 0x16D8) cpu_write16(cpu, 0, work + 0x0E, 0);
    cpu_write16(cpu, 0, slot + 0x0C, cpu_read16(cpu, 0, cart + 0x0C));
    cpu_write16(cpu, 0, slot + 0x0E, cpu_read16(cpu, 0, cart + 0x0E));
    uint16_t attributes = cpu_read16(cpu, 0, slot + 0x12);
    cpu_write16(cpu, 0, slot + 0x12, (uint16_t)((attributes & ~0x4000) |
        (cpu_read16(cpu, 0, cart + 0x12) & 0x4000)));
    cpu_write16(cpu, 0, slot + 0x44, cpu_read16(cpu, 0, cart + 0x10) & 0x8007);
    cpu_write16(cpu, 0, slot + 0x46, cpu_read16(cpu, 0, cart + 0x52) & 0x8007);
    cpu_write16(cpu, 0, slot + 0x48, slope);
    cpu_write16(cpu, 0, slot + 0x4A, cpu_read16(cpu, 0, cart + 0x3E));
  }
  return true;
}

bool Dkc2CoopCartSurvivor(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t fallen = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (fallen != kDkc2CoopSlotA && fallen != kDkc2CoopSlotB) return false;
  uint16_t survivor = fallen == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  if (!CoopCartRider(cpu, survivor)) return false;
  uint16_t cart = cpu_read16(cpu, 0, survivor + 0x42);
  s_lost_mask |= fallen == kDkc2CoopSlotA ? 1 : 2;
  cpu_write16(cpu, 0, cart + 0x42, survivor);
  cpu_write16(cpu, 0, cart + 0x44, 0);
  cpu_write16(cpu, 0, cart + 0x3E, cpu_read16(cpu, 0, cart + 0x3E) & ~0x4000);
  cpu_write16(cpu, 0, 0x08C2, cpu_read16(cpu, 0, 0x08C2) & ~0x4000);
  if (cpu_read16(cpu, 0, survivor + 0x2E) != 0x67 &&
      cpu_read16(cpu, 0, survivor + 0x2E) != 0x68)
    cpu_write16(cpu, 0, survivor + 0x2E, 0x5B);
  CoopPromoteCollider(cpu, survivor);
  /* The normal TEAM handoff freezes sprites until a follower wait finishes.
   * This survivor is already playing; retain the native grace period without
   * freezing the cart or requiring a controller-confirmation animation. */
  if (cpu_read16(cpu, 0, 0x0A36) == 7) {
    cpu_write16(cpu, 0, 0x0A36, 0);
    cpu_write16(cpu, 0, 0x0A38, 0);
  }
  cpu_write16(cpu, 0, survivor + 0x30, 0x9E);
  uint16_t work = cpu_read16(cpu, 0, 0x0595);
  if (work == 0x16B2 || work == 0x16D8) {
    cpu_write16(cpu, 0, work + 0x14, 0x60);
    cpu_write16(cpu, 0, work + 0x16, 0x70);
  }
  return true;
}

static bool CoopCartPitFromCart(CpuState *cpu, uint16_t cart) {
  if (!CoopEnabled(cpu)) return false;
  if (cart < 0xE9E || cart >= 0x16B2 || (cart - 0xDE2) % 0x5E ||
      cpu_read16(cpu, 0, cart) != 0x158) return false;
  uint8_t falling = 0, safe = 0;
  uint16_t survivor = 0;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t slot = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    if ((s_lost_mask & (1u << who)) || !CoopLiveActor(cpu, slot)) continue;
    uint16_t state = cpu_read16(cpu, 0, slot + 0x2E);
    bool seated = (state == 0x5B || state == 0x5C) && cpu_read16(cpu, 0, slot + 0x42) == cart;
    bool below = (int16_t)(cpu_read16(cpu, 0, slot + 10) - cpu_read16(cpu, 0, 0x17C0)) >= 0x120;
    if (seated || below || (cpu_read16(cpu, 0, slot + 0x1E) & 0x80)) {
      falling |= (uint8_t)(1u << who);
    } else if (CoopCartFlight(cpu, slot) || CoopRescueMovable(cpu, slot) || CoopCartRider(cpu, slot)) {
      safe |= (uint8_t)(1u << who);
      survivor = slot;
    }
  }
  if (!safe) {
    /* The native final-loss sequence owns the active Kong. Park any other
     * actual faller so it cannot remain visibly/live-logically seated. */
    s_lost_mask |= falling;
    uint16_t inactive = cpu_read16(cpu, 0, 0x0597);
    if ((inactive == kDkc2CoopSlotA || inactive == kDkc2CoopSlotB) &&
        (falling & (inactive == kDkc2CoopSlotA ? 1 : 2))) {
      cpu_write16(cpu, 0, inactive + 0x2E, 0x13);
      cpu_write16(cpu, 0, inactive + 0x30, 0);
      cpu_write16(cpu, 0, inactive + 0x1A, 0);
      cpu_write16(cpu, 0, inactive + 0x16, 0);
    }
    return false;
  }
  s_lost_mask |= falling;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t slot = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    if (falling & (1u << who)) {
      cpu_write16(cpu, 0, slot + 0x2E, 0x13);
      cpu_write16(cpu, 0, slot + 0x30, 0);
      cpu_write16(cpu, 0, slot + 0x1A, 0);
      cpu_write16(cpu, 0, slot + 0x16, 0);
    }
    if ((safe & (1u << who)) && cpu_read16(cpu, 0, slot + 0x42) == cart) {
      /* A jumping Kong leaves the falling cart with its forward momentum.
       * Preserve its height, vertical impulse and own airborne animation. */
      cpu_write16(cpu, 0, slot + 0x42, 0);
      cpu_write16(cpu, 0, slot + 0x20, cpu_read16(cpu, 0, cart + 0x20));
      cpu_write16(cpu, 0, slot + 0x26, cpu_read16(cpu, 0, cart + 0x20));
      uint16_t work = cpu_read16(cpu, 0, slot == cpu_read16(cpu, 0, 0x0593) ? 0x0595 : 0x0599);
      if (work == 0x16B2 || work == 0x16D8) cpu_write16(cpu, 0, work + 0x0E, 0);
    }
  }
  cpu_write16(cpu, 0, cart + 0x42, 0);
  cpu_write16(cpu, 0, cart + 0x44, 0);
  if (cpu_read16(cpu, 0, 0x0D5E) == cart) cpu_write16(cpu, 0, 0x0D5E, 0);
  if (falling) {
    cpu_write16(cpu, 0, 0x08C2, cpu_read16(cpu, 0, 0x08C2) & ~0x4000);
    CoopPromoteCollider(cpu, survivor);
  }
  return true;
}

bool Dkc2CoopCartPit(CpuState *cpu) {
  return cpu && CoopCartPitFromCart(cpu, cpu_read16(cpu, 0, 0x0A84));
}

bool Dkc2CoopCartDetach(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return false;
  uint16_t cart = cpu_read16(cpu, 0, kong + 0x42);
  if (!CoopCartRider(cpu, kong) || !CoopCartFlight(cpu, kong)) return false;
  /* Native departure clears the front seat even if the rear Kong leaves.
   * Release only this rider, carrying the cart's horizontal momentum across
   * the gap while native jump/bounce physics retain its vertical motion. */
  for (unsigned seat = 0; seat < 2; ++seat) {
    uint16_t address = (uint16_t)(cart + 0x42 + seat * 2);
    if ((cpu_read16(cpu, 0, address) & 0x7FFF) == kong) cpu_write16(cpu, 0, address, 0);
  }
  cpu_write16(cpu, 0, kong + 0x42, 0);
  cpu_write16(cpu, 0, kong + 0x20, cpu_read16(cpu, 0, cart + 0x20));
  cpu_write16(cpu, 0, kong + 0x26, cpu_read16(cpu, 0, cart + 0x20));
  uint16_t work = Dkc2CoopCartWorkValue(cpu, 0);
  if (work == 0x16B2 || work == 0x16D8) cpu_write16(cpu, 0, work + 0x0E, 0);
  return true;
}

static bool CoopMountedRider(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t animal = cpu_read16(cpu, 0, 0x006C);
  uint16_t type = cpu_read16(cpu, 0, 0x006E);
  uint16_t rider = cpu_read16(cpu, 0, 0x0593);
  if ((type != 0x190 && type != 0x194 && type != 0x19C) || animal < 0x0E9E || animal >= 0x16B2 ||
      (animal - 0x0DE2) % 0x5E || cpu_read16(cpu, 0, animal) != type ||
      (rider != kDkc2CoopSlotA && rider != kDkc2CoopSlotB) ||
      !CoopLiveActor(cpu, rider)) return false;
  uint16_t state = cpu_read16(cpu, 0, rider + 0x2E);
  return state == 9 || state == 0x0A || state == 0x0B || (type == 0x194 && (state == 0x61 || state == 0x62));
}

static void CoopReleaseLostMount(CpuState *cpu, uint16_t rider) {
  uint16_t animal = cpu_read16(cpu, 0, 0x006C);
  uint16_t type = cpu_read16(cpu, 0, 0x006E);
  if ((type != 0x190 && type != 0x194 && type != 0x19C) || animal < 0x0E9E || animal >= 0x16B2 ||
      (animal - 0x0DE2) % 0x5E || cpu_read16(cpu, 0, animal) != type) return;
  /* Native dismount ($B8:94BB/$B8:954E) returns the palette indices to
   * their original owners before clearing the shared mount selectors.
   * Leave the animal in native unmounted idle, with its own terrain/deletion
   * handling, rather than letting it snap the new camera leader into itself. */
  uint16_t rider_attr = cpu_read16(cpu, 0, rider + 0x12);
  uint16_t animal_attr = cpu_read16(cpu, 0, animal + 0x12);
  uint16_t swap = (uint16_t)((rider_attr ^ animal_attr) & 0x0E00);
  cpu_write16(cpu, 0, rider + 0x12, (uint16_t)(rider_attr ^ swap));
  cpu_write16(cpu, 0, animal + 0x12, (uint16_t)(animal_attr ^ swap));
  cpu_write16(cpu, 0, animal + 2, 0xD0);
  cpu_write16(cpu, 0, animal + 0x1C, 0);
  cpu_write16(cpu, 0, animal + 0x2E, 0);
  cpu_write16(cpu, 0, 0x006C, 0);
  cpu_write16(cpu, 0, 0x006E, 0);
  cpu_write16(cpu, 0, 0x08C2, (uint16_t)(cpu_read16(cpu, 0, 0x08C2) & ~0x0200));
}

uint16_t Dkc2CoopPitCameraY(CpuState *cpu, uint16_t original) {
  if (!CoopMountedRider(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (kong != cpu_read16(cpu, 0, 0x0597) || !CoopLiveActor(cpu, kong) ||
      (s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2))) return original;
  /* The rider can scroll away from a safe independent actor. Use the native
   * level's maximum camera Y for this lower-screen cutoff; terrain pits still
   * take the original accepted-death path. Never alter the actual camera. */
  uint16_t bottom = cpu_read16(cpu, 0, 0x0AFE);
  return bottom > original && bottom < 0x7EE0 ? bottom : original;
}

bool Dkc2CoopPitSurvivor(CpuState *cpu) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x0515) == 1) return false;
  uint16_t fallen = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (fallen != kDkc2CoopSlotA && fallen != kDkc2CoopSlotB) return false;
  /* Onscreen harmful terrain takes native interaction $23: its hurt/death
   * animation and handoff must run before the actor can be parked. The same
   * native routine also handles offscreen pits, which need survivor recovery. */
  if (!(cpu_read16(cpu, 0, fallen + 0x1E) & 0x80) &&
      (cpu_read16(cpu, 0, fallen + 0x10) & 0x100) &&
      (int16_t)(cpu_read16(cpu, 0, fallen + 10) -
                Dkc2CoopPitCameraY(cpu, cpu_read16(cpu, 0, 0x17C0))) < 0x120) {
    CoopPromoteCollider(cpu, fallen);
    return false;
  }
  uint16_t survivor = fallen == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  uint16_t state = cpu_read16(cpu, 0, survivor + 0x2E);
  uint8_t survivor_bit = survivor == kDkc2CoopSlotA ? 1 : 2;
  if ((s_lost_mask & survivor_bit) || !CoopLiveActor(cpu, survivor) ||
      (!CoopRescueMovable(cpu, survivor) &&
       !(survivor == cpu_read16(cpu, 0, 0x0593) && CoopMountedRider(cpu)) &&
       !CoopCartRider(cpu, survivor) && !CoopCartFlight(cpu, survivor) &&
       state != 0x3C && state != 0x45 &&
       !(state >= 0x41 && state <= 0x44) && state != 0x7A &&
       !(state >= 0x35 && state <= 0x38)) ||
      (cpu_read16(cpu, 0, survivor + 0x1E) & 0x80) ||
      (int16_t)(cpu_read16(cpu, 0, survivor + 10) - cpu_read16(cpu, 0, 0x17C0)) >= 0x120)
    return false;
  if (CoopCartRider(cpu, survivor)) return Dkc2CoopCartSurvivor(cpu);
  /* Called only at native accepted pit-death blocks. Park the fallen Kong
   * through the original inactive-Kong path, retaining the live actor's pose,
   * position and controller. DK barrels remain responsible for revival. */
  s_lost_mask |= fallen == kDkc2CoopSlotA ? 1 : 2;
  /* Accepted loss can occur after the rider has already changed action.
   * Validate the mount record itself rather than requiring a riding pose. */
  if (fallen == cpu_read16(cpu, 0, 0x0593))
    CoopReleaseLostMount(cpu, fallen);
  CoopPromoteCollider(cpu, survivor);
  cpu_write16(cpu, 0, fallen + 0x30, 0);
  cpu_write16(cpu, 0, 0x08C2, (uint16_t)(cpu_read16(cpu, 0, 0x08C2) & ~0x4000));
  return true;
}

static bool CoopCatActor(CpuState *cpu, uint16_t cat);

static bool CoopCameraEligible(CpuState *cpu, uint16_t selector) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x0024) != 0x8819 ||
      s_lost_mask || !(cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
      (selector != kDkc2CoopSlotA && selector != kDkc2CoopSlotB)) return false;
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
    uint16_t state = cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E));
    uint16_t type = cpu_read16(cpu, 0, slot);
    bool cannon = (type == 0xE4 || type == 0xE8) &&
        (state == 0x10 || state == 0x11 || state == 0x12 || state == 0x30);
    if ((!CoopLiveActor(cpu, slot) && !cannon) ||
        (!CoopRescueMovable(cpu, slot) && !(state >= 0x35 && state <= 0x38) &&
         state != 9 && state != 0x0A && state != 0x0B && state != 0x3C &&
         state != 0x16 && state != 0x45 &&
         !(state == 0x48 && CoopCatActor(cpu, cpu_read16(cpu, 0, 0xAE8))) &&
         !(state >= 0x41 && state <= 0x44) && state != 0x7A && !cannon))
      return false;
  }
  return true;
}

static uint16_t CoopCameraPosition(CpuState *cpu, uint16_t original, uint16_t offset) {
  if (!CoopCameraEligible(cpu, cpu ? cpu->Y : 0)) return original;
  bool fraction = offset == 5 || offset == 9;
  uint16_t integer_offset = fraction ? (uint16_t)(offset + 1) : offset;
  uint64_t a = ((uint64_t)cpu_read16(cpu, 0, (uint16_t)(kDkc2CoopSlotA + integer_offset)) << 16) |
      cpu_read16(cpu, 0, (uint16_t)(kDkc2CoopSlotA + integer_offset - 2));
  uint64_t b = ((uint64_t)cpu_read16(cpu, 0, (uint16_t)(kDkc2CoopSlotB + integer_offset)) << 16) |
      cpu_read16(cpu, 0, (uint16_t)(kDkc2CoopSlotB + integer_offset - 2));
  return (uint16_t)(((a + b) / 2) >> (fraction ? 8 : 16));
}

void Dkc2CoopCameraCenter(CpuState *cpu) {
  if (!cpu || !CoopCameraEligible(cpu, cpu_read16(cpu, 0, 0x0A98))) return;
  /* Native camera desired coordinates are 24.8, at $0ADE/$0AE2. Replace
   * single-player lead/dead-zone placement before native bounds, region
   * selection and scroll streaming consume them. Keep the midpoint target
   * itself and all actor/profile/CPU state intact. */
  for (unsigned axis = 0; axis < 2; ++axis) {
    uint16_t offset = axis ? 10 : 6;
    uint64_t a = ((uint64_t)cpu_read16(cpu, 0, kDkc2CoopSlotA + offset) << 16) |
        cpu_read16(cpu, 0, kDkc2CoopSlotA + offset - 2);
    uint64_t b = ((uint64_t)cpu_read16(cpu, 0, kDkc2CoopSlotB + offset) << 16) |
        cpu_read16(cpu, 0, kDkc2CoopSlotB + offset - 2);
    int64_t desired = (int64_t)((a + b) / 2 >> 8) - (axis ? 112 : 128) * 256;
    if (desired < 0) desired = 0;
    uint16_t address = axis ? 0x0AE2 : 0x0ADE;
    cpu_write16(cpu, 0, address, (uint16_t)desired);
    cpu_write16(cpu, 0, address + 2, (uint16_t)((uint64_t)desired >> 16));
  }
}

uint16_t Dkc2CoopCameraXValue(CpuState *cpu, uint16_t original) {
  return CoopCameraPosition(cpu, original, 6);
}

uint16_t Dkc2CoopCameraYValue(CpuState *cpu, uint16_t original) {
  return CoopCameraPosition(cpu, original, 10);
}

uint16_t Dkc2CoopCameraXFraction(CpuState *cpu, uint16_t original) {
  return CoopCameraPosition(cpu, original, 5);
}

uint16_t Dkc2CoopCameraYFraction(CpuState *cpu, uint16_t original) {
  return CoopCameraPosition(cpu, original, 9);
}

static bool CoopBonusEntrance(CpuState *cpu) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x0515) != 1 || s_lost_mask ||
      !(cpu_read16(cpu, 0, 0x08C2) & 0x4000)) return false;
  uint16_t source = cpu_read16(cpu, 0, 0x0A84);
  return source >= 0x0E9E && source < 0x16B2 &&
      (source - 0x0DE2) % 0x5E == 0 && cpu_read16(cpu, 0, source) == 0x140 &&
      cpu_read16(cpu, 0, (uint16_t)(source + 6)) == cpu_read16(cpu, 0, 0x0533) &&
      cpu_read16(cpu, 0, (uint16_t)(source + 10)) == cpu_read16(cpu, 0, 0x0535);
}

static bool CoopKleeverHook(CpuState *cpu, uint16_t hook) {
  return cpu_read16(cpu, 0, 0x00D3) == 0x21 &&
      cpu_read16(cpu, 0, hook) == 0x24 &&
      cpu_read16(cpu, 0, hook + 0x36) == 0x2AB &&
      cpu_read16(cpu, 0, hook + 0x4E) == 0xE000;
}

static bool CoopHookActor(CpuState *cpu, uint16_t hook) {
  return hook >= 0x0E9E && hook < 0x16B2 && (hook - 0x0DE2) % 0x5E == 0 &&
      (cpu_read16(cpu, 0, hook) == 0x14C || CoopKleeverHook(cpu, hook));
}

static bool CoopTransportSource(CpuState *cpu, uint16_t source) {
  if (source < 0x0E9E || source >= 0x16B2 || (source - 0x0DE2) % 0x5E) return false;
  uint16_t type = cpu_read16(cpu, 0, source);
  return type == 0x13C || type == 0x140 || type == 0x174 || type == 0x178;
}

static bool CoopSharedTransport(CpuState *cpu) {
  if (!CoopEnabled(cpu) || s_lost_mask || cpu_read16(cpu, 0, 0x006E) ||
      !(cpu_read16(cpu, 0, 0x08C2) & 0x4000)) return false;
  return CoopTransportSource(cpu, cpu_read16(cpu, 0, 0x0A84));
}

static bool CoopMountedBird(CpuState *cpu) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x006E) != 0x198) return false;
  uint16_t bird = cpu_read16(cpu, 0, 0x006C);
  return bird >= 0x0E9E && bird < 0x16B2 && (bird - 0x0DE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, bird) == 0x198;
}

static bool CoopSharedBird(CpuState *cpu) {
  return CoopMountedBird(cpu) && !s_lost_mask &&
      (cpu_read16(cpu, 0, 0x08C2) & 0x4000);
}

bool Dkc2CoopCannonIndependent(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  if (CoopAnimalOnly(cpu)) return false;
  /* This callback is the cartridge's accepted follower-cannon reaction
   * ($0F, cleared before dispatch). $6A may name a later collision; the reaction itself
   * identifies the inactive Kong. Promote it before the native owner work
   * pair is selected, without recruiting the other independent player. */
  uint16_t source = cpu_read16(cpu, 0, 0x0A84);
  /* The same reaction also joins the carrier to a thrown partner. Its
   * native setup is required; skipping it strands both actors in flight. */
  if (source < 0x0E9E || source >= 0x16B2 || (source - 0x0DE2) % 0x5E)
    return false;
  if (!CoopTransportSource(cpu, source)) return false;
  /* Mounted return flights need the cartridge's complete owner/follower
   * setup. Skipping it leaves the rider in $30 with no barrel completion
   * context after a bonus return. Never promote the on-foot partner over
   * the single native animal owner here. */
  if (CoopMountedAnimal(cpu)) return false;
  /* Transport barrels carry both present players, as the cartridge's
   * recruitment/launch path does. A lost partner is never recruited. */
  if (CoopSharedTransport(cpu)) return false;
  uint16_t kong = cpu_read16(cpu, 0, 0x0597);
  if (source >= 0x0E9E && source < 0x16B2 &&
      (source - 0x0DE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, source) == 0x13C &&
      (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) &&
      CoopLiveActor(cpu, kong))
    CoopPromoteCollider(cpu, kong);
  return true;
}

uint16_t Dkc2CoopCannonTargetValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t source = cpu_read16(cpu, 0, 0x0A84);
  if (source < 0x0E9E || source >= 0x16B2 || (source - 0x0DE2) % 0x5E)
    return original;
  uint16_t type = cpu_read16(cpu, 0, source);
  return type == 0x13C || type == 0x174 || type == 0x178 || CoopHookActor(cpu, source) ? source : original;
}

static bool CoopRescueMove(CpuState *cpu, unsigned who) {
  uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
  uint16_t target = who == 0 ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  uint16_t state = cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E));
  bool swimming = state == 0x3D;
  uint16_t target_state = cpu_read16(cpu, 0, (uint16_t)(target + 0x2E));
  bool target_swimming = target_state == 0x3D;
  bool honey = (state >= 0x41 && state <= 0x44) || state == 0x7A;
  bool live_honey = honey && CoopLiveActor(cpu, slot);
  bool target_wall = target_state == 0x41 && CoopLiveActor(cpu, target);
  bool target_honey_floor = (target_state == 0x43 || target_state == 0x7A) &&
      CoopLiveActor(cpu, target);
  bool rope = state >= 0x35 && state <= 0x38;
  bool live_rope = rope && CoopLiveActor(cpu, slot);
  bool waiting = state == 0x13 && CoopLiveActor(cpu, slot);
  uint16_t animal = cpu_read16(cpu, 0, 0x006C);
  uint16_t animal_type = cpu_read16(cpu, 0, 0x006E);
  bool mounted_target = CoopMountedRider(cpu) &&
      target == cpu_read16(cpu, 0, 0x0593) &&
      (animal_type == 0x194 || cpu_read16(cpu, 0, (uint16_t)(target + 0x2E)) == 9);
  /* Only the on-foot partner moves. Preserve the rider, its animal and
   * global mount selectors; other animal/transition combinations wait. */
  if ((animal || animal_type) && !mounted_target) return false;
  if ((!CoopRescueMovable(cpu, slot) && !live_rope && !live_honey && !waiting) ||
      (!CoopRescueMovable(cpu, target) && !mounted_target && !target_wall && !target_honey_floor) ||
      (!target_swimming && !target_wall && !(mounted_target && animal_type == 0x194) &&
       !(cpu_read16(cpu, 0, (uint16_t)(target + 0x1E)) & 0x0101)))
    return false;
  uint16_t work = cpu_read16(cpu, 0,
      slot == cpu_read16(cpu, 0, 0x0593) ? 0x0595 : 0x0599);
  if (target_swimming && (rope || !swimming) &&
      work != 0x16B2 && work != 0x16D8)
    return false;
  uint16_t old_x = cpu_read16(cpu, 0, (uint16_t)(slot + 6));
  uint16_t old_y = cpu_read16(cpu, 0, (uint16_t)(slot + 10));
  uint16_t target_x = cpu_read16(cpu, 0, (uint16_t)(target + 6));
  uint16_t new_y = cpu_read16(cpu, 0, (uint16_t)(target + 10));
  /* A wall is a proven attachment surface, not a grounded platform. Share
   * its exact anchor instead of placing the rescued player inside terrain. */
  int new_x = target_wall ? target_x : (int)target_x + (old_x <= target_x ? -16 : 16);
  if (new_x < 16) new_x = 16;
  if (new_x > 0xFFEF) new_x = 0xFFEF;
  uint16_t owner = CoopHeldOwner(cpu);
  uint16_t object = cpu_read16(cpu, 0, 0x0D7A);
  cpu_write16(cpu, 0, (uint16_t)(slot + 4), 0);
  cpu_write16(cpu, 0, (uint16_t)(slot + 6), (uint16_t)new_x);
  cpu_write16(cpu, 0, (uint16_t)(slot + 8), 0);
  cpu_write16(cpu, 0, (uint16_t)(slot + 10), new_y);
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x20), 0);
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x24), 0);
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x26), 0);
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x1E), 0);
  if (rope || waiting || honey || target_wall || swimming != target_swimming) {
    /* Initialize the destination movement context without the native water
     * entry's global follower transition. Read this Kong's own swim physics
     * from its $FF property record, as $B8:B5DA does, and restart animation
     * using the ROM descriptor layout read by $B9:D0C6. */
    uint16_t animation = target_wall ? 0x2B : target_swimming ? 0x43 : 1;
    if (cpu_read16(cpu, 0, slot) == 0xE8) animation += 0xA3;
    uint16_t descriptor = (uint16_t)(animation * 4);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x2E), target_wall ? 0x41 : target_swimming ? 0x3D : 0);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x32), 0);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x36), animation);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x38), 0);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x3A), 0x100);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x3C),
                cpu_read16(cpu, 0xF9, descriptor));
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x3E), 0);
    cpu_write16(cpu, 0, (uint16_t)(slot + 0x40),
                cpu_read16(cpu, 0xF9, (uint16_t)(descriptor + 2)));
    if (target_swimming) {
      uint16_t properties = cpu_read16(cpu, 0, (uint16_t)(slot + 0x54));
      cpu_write16(cpu, 0, (uint16_t)(work + 8),
                  cpu_read16(cpu, 0xFF, (uint16_t)(properties + 0x76)));
      cpu_write16(cpu, 0, (uint16_t)(work + 10),
                  cpu_read16(cpu, 0xFF, (uint16_t)(properties + 0x78)));
    }
  }
  if (target_wall) {
    /* Native wall reaction $08 clears the render offset and velocities and
     * selects animation $2B. Keep this character's palette/attributes while
     * matching the wall-facing direction and its side-contact flags. */
    cpu_write16(cpu, 0, slot + 0x0E, 0);
    cpu_write16(cpu, 0, slot + 0x12,
        (uint16_t)((cpu_read16(cpu, 0, slot + 0x12) & ~0x4000) |
                   (cpu_read16(cpu, 0, target + 0x12) & 0x4000)));
    cpu_write16(cpu, 0, slot + 0x1E, cpu_read16(cpu, 0, target + 0x1E) & 6);
  }
  if (honey && !target_swimming && (work == 0x16B2 || work == 0x16D8)) {
    uint16_t properties = cpu_read16(cpu, 0, slot + 0x54);
    cpu_write16(cpu, 0, work + 8, cpu_read16(cpu, 0xFF, properties));
    cpu_write16(cpu, 0, work + 10, cpu_read16(cpu, 0xFF, properties + 2));
  }
  /* Keep held objects attached across this frame boundary. The normal guest
   * attachment routine continues to own their pose and release afterward. */
  if (owner == slot && object >= 0x0E9E) {
    uint16_t x = cpu_read16(cpu, 0, (uint16_t)(object + 6));
    uint16_t y = cpu_read16(cpu, 0, (uint16_t)(object + 10));
    cpu_write16(cpu, 0, (uint16_t)(object + 6), (uint16_t)(x + new_x - old_x));
    cpu_write16(cpu, 0, (uint16_t)(object + 10), (uint16_t)(y + new_y - old_y));
  }
  return true;
}

static bool CoopFinishBonusEntrance(CpuState *cpu, uint16_t slot, uint16_t state) {
  bool bonus = CoopBonusEntrance(cpu);
  uint16_t source = cpu_read16(cpu, 0, 0x0A84);
  bool launch = CoopEnabled(cpu) && !s_lost_mask && state == 0x13 &&
      (cpu_read16(cpu, 0, 0x08C2) & 0x4000) &&
      source >= 0x0E9E && source < 0x16B2 && (source - 0x0DE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, source) == 0x140 &&
      cpu_read16(cpu, 0, source + 0x4E) == 0x40;
  if ((!bonus && !launch) || slot != cpu_read16(cpu, 0, 0x0597) ||
      (state != 0x32 && state != 0x31 && state != 0x13) ||
      !CoopLiveActor(cpu, slot)) return false;
  uint16_t leader = cpu_read16(cpu, 0, 0x0593);
  if (!CoopLiveActor(cpu, leader) ||
      cpu_read16(cpu, 0, (uint16_t)(leader + 0x2E)) != 0 ||
      !(cpu_read16(cpu, 0, (uint16_t)(leader + 0x1E)) & 0x0101)) return false;
  int spawn_x = cpu_read16(cpu, 0, 0x0533), spawn_y = cpu_read16(cpu, 0, 0x0535);
  if (!bonus) {
    spawn_x = cpu_read16(cpu, 0, source + 6);
    spawn_y = cpu_read16(cpu, 0, source + 10);
  }
  int lx = cpu_read16(cpu, 0, (uint16_t)(leader + 6));
  int ly = cpu_read16(cpu, 0, (uint16_t)(leader + 10));
  int x = cpu_read16(cpu, 0, (uint16_t)(slot + 6));
  int y = cpu_read16(cpu, 0, (uint16_t)(slot + 10));
  if (abs(lx - spawn_x) > (bonus ? 48 : 128) || ly > spawn_y || spawn_y - ly > 128 ||
      abs(x - spawn_x) > 48 || abs(y - spawn_y) > (bonus ? 48 : 128)) return false;
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x2E), 0x13);
  if (CoopRescueMove(cpu, slot == kDkc2CoopSlotA ? 0 : 1)) {
    /* A launcher may land at the edge of a narrow platform. Reuse the
     * grounded actor's proven coordinate instead of offsetting off its edge. */
    if (!bonus) cpu_write16(cpu, 0, slot + 6, (uint16_t)lx);
    return true;
  }
  cpu_write16(cpu, 0, (uint16_t)(slot + 0x2E), state);
  return false;
}

uint8_t Dkc2CoopRescueFrame(CpuState *cpu, bool p1_select, bool p2_select) {
  if (!cpu) { CoopResetRescueHold(); return 0; }
  if (CoopAnimalOnly(cpu)) { CoopCancelRescueCharge(); return 0; }
  const bool select[2] = {p1_select, p2_select};
  for (unsigned who = 0; who < 2; ++who) {
    if (!select[who]) s_rescue_fired[who] = false;
  }
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x0024) != 0x8819 ||
      cpu_read16(cpu, 0, 0x0096) == 11 || s_lost_mask ||
      (cpu_read16(cpu, 0, 0x08C2) & 0x4040) != 0x4000) {
    CoopCancelRescueCharge();
    return 0;
  }
  uint16_t object = cpu_read16(cpu, 0, 0x0D7A);
  if (object == kDkc2CoopSlotA || object == kDkc2CoopSlotB) {
    CoopCancelRescueCharge();
    return 0;
  }
  uint16_t level = cpu_read16(cpu, 0, 0x00D3);
  if (s_rescue_level_valid && level != s_rescue_level) CoopCancelRescueCharge();
  s_rescue_level = level;
  s_rescue_level_valid = true;
  unsigned ready = 0;
  for (unsigned who = 0; who < 2; ++who) {
    if (!select[who]) {
      s_rescue_hold[who] = 0;
      s_rescue_fired[who] = false;
    } else if (!s_rescue_fired[who]) {
      if (s_rescue_hold[who] < kDkc2CoopRescueHoldFrames) ++s_rescue_hold[who];
      if (s_rescue_hold[who] == kDkc2CoopRescueHoldFrames) ready |= 1u << who;
    }
  }
  /* If both charge together, bring the camera follower to the leader once,
   * rather than swapping positions or immediately teleporting them twice. */
  unsigned who = ready == 3
      ? (cpu_read16(cpu, 0, 0x0593) == kDkc2CoopSlotB ? 0u : 1u)
      : (ready == 1 ? 0u : 1u);
  if (!ready || !CoopRescueMove(cpu, who)) return 0;
  s_rescue_fired[who] = true;
  if (ready == 3) s_rescue_fired[1 - who] = true;
  return (uint8_t)(1u << who);
}

uint16_t Dkc2CoopGateActiveValue(CpuState *cpu, uint16_t active_slot) {
  if (!cpu)
    return active_slot;
  uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  uint16_t mode = CoopReadAbs(cpu, kDkc2CoopModeFlag);
  bool enabled = s_coop_mode == kDkc2CoopSimultaneous;
  uint16_t result = Dkc2CoopGateActive(active_slot, sprite, mode, enabled);
  if (CoopAnimalOnly(cpu)) result = cpu_read16(cpu, 0, 0x0593);
  if (CoopDebugEnabled() && s_debug_gate_count < 60) {
    s_debug_gate_count++;
    fprintf(stderr, "coop_gate #%d sprite=%04x active=%04x mode=%u -> %04x\n",
            s_debug_gate_count, sprite, active_slot, mode, result);
  }
  return result;
}

static void CoopResumeRopeTransition(CpuState *cpu, uint16_t sprite) {
  /* Older simultaneous snapshots can have already skipped the completion
   * callback and parked at the script's terminal $83/$D12B wait. Replay just
   * that native callback, preserving the finished pose and rope coordinates.
   * Validate its instructions instead of assuming a character-specific cursor. */
  uint16_t animation = cpu_read16(cpu, 0, (uint16_t)(sprite + 0x36));
  if (animation >= 0xA3) animation = (uint16_t)(animation - 0xA3);
  if ((animation != 0x34 && animation != 0x35) ||
      cpu_read16(cpu, 0, (uint16_t)(sprite + 0x38)) != 0 ||
      cpu_read16(cpu, 0, (uint16_t)(sprite + 0x3E)) != 0xDD63)
    return;
  uint16_t cursor = cpu_read16(cpu, 0, (uint16_t)(sprite + 0x3C));
  uint16_t completion = animation == 0x34 ? 0xDD7E : 0xDD90;
  if (cursor < 3 ||
      (cpu_read16(cpu, 0xF9, (uint16_t)(cursor - 3)) & 0xFF) != 0x81 ||
      cpu_read16(cpu, 0xF9, (uint16_t)(cursor - 2)) != completion ||
      (cpu_read16(cpu, 0xF9, cursor) & 0xFF) != 0x83 ||
      cpu_read16(cpu, 0xF9, (uint16_t)(cursor + 1)) != 0xD12B)
    return;
  cpu_write16(cpu, 0, (uint16_t)(sprite + 0x3C), (uint16_t)(cursor - 3));
}

static bool CoopCartPartnerLanding(CpuState *cpu, uint16_t kong) {
  if (!CoopCartFlight(cpu, kong) || (int16_t)cpu_read16(cpu, 0, kong + 0x24) < 0)
    return false;
  uint16_t peer = kong == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  uint16_t peer_state = cpu_read16(cpu, 0, peer + 0x2E);
  if ((peer_state != 0x5B && peer_state != 0x5C) || !CoopCartRider(cpu, peer)) return false;
  uint16_t cart = cpu_read16(cpu, 0, peer + 0x42);
  if (cpu_read16(cpu, 0, kong + 0x42) == cart) return false;
  unsigned seat;
  if ((cpu_read16(cpu, 0, cart + 0x42) & 0x7FFF) == peer &&
      !cpu_read16(cpu, 0, cart + 0x44)) seat = 1;
  else if ((cpu_read16(cpu, 0, cart + 0x44) & 0x7FFF) == peer &&
           !cpu_read16(cpu, 0, cart + 0x42)) seat = 0;
  else return false;
  uint16_t x = cpu_read16(cpu, 0, cart + 6), y = cpu_read16(cpu, 0, cart + 10);
  uint16_t slope = cpu_read16(cpu, 0, cart + 0x36);
  if (seat && slope < 0x13) {
    unsigned index = slope + (cpu_read16(cpu, 0, kong) == 0xE4 ? 0 : 0x26);
    x += cpu_read16(cpu, 0xBE, (uint16_t)(0xDAAD + index * 2));
    y += cpu_read16(cpu, 0xBE, (uint16_t)(0xDAD3 + index * 2));
  }
  int dx = (int16_t)(cpu_read16(cpu, 0, kong + 6) - x);
  int dy = (int16_t)(cpu_read16(cpu, 0, kong + 10) - y);
  /* A teammate's accepted landing can catch a descending near miss at the
   * empty seat. This excludes seated non-jumpers, rising arcs and pit fallers. */
  if (dx < -32 || dx > 32 || dy < -32 || dy > 24) return false;
  uint16_t old = cpu_read16(cpu, 0, kong + 0x42);
  if (CoopCartActor(cpu, old)) {
    for (unsigned i = 0; i < 2; ++i)
      if ((cpu_read16(cpu, 0, old + 0x42 + i * 2) & 0x7FFF) == kong)
        cpu_write16(cpu, 0, old + 0x42 + i * 2, 0);
  }
  cpu_write16(cpu, 0, cart + 0x42 + seat * 2, kong);
  cpu_write16(cpu, 0, kong + 0x42, cart);
  cpu_write16(cpu, 0, kong + 6, x); cpu_write16(cpu, 0, kong + 4, 0);
  cpu_write16(cpu, 0, kong + 10, y); cpu_write16(cpu, 0, kong + 8, 0);
  cpu_write16(cpu, 0, kong + 0x24, 0);
  cpu_write16(cpu, 0, kong + 0x1C, 0); cpu_write16(cpu, 0, kong + 0x1E, 0);
  cpu_write16(cpu, 0, kong + 0x2E, seat ? 0x5C : 0x5B);
  uint16_t work = cpu_read16(cpu, 0, kong == cpu_read16(cpu, 0, 0x593) ? 0x595 : 0x599);
  if (work == 0x16B2 || work == 0x16D8) cpu_write16(cpu, 0, work + 0x0E, 0);
  CoopCartAnimation(cpu, kong, 0x4E);
  /* Native waiting -> moving transition when the front rider boards. */
  if (!seat && cpu_read16(cpu, 0, cart + 0x2E) == 0x101)
    cpu_write16(cpu, 0, cart + 0x2E, 0x201);
  if (!seat || !CoopCartActor(cpu, cpu_read16(cpu, 0, 0xD5E)))
    cpu_write16(cpu, 0, 0xD5E, cart);
  return true;
}

uint16_t Dkc2CoopSelectStateWord(CpuState *cpu, uint16_t original) {
  if (!cpu || !Dkc2CoopSimultaneousActive(
                  cpu_read16(cpu, 0x00, kDkc2CoopModeFlag),
                  s_coop_mode == kDkc2CoopSimultaneous))
    return original;
  /* This wraps LDA $2E,x before X becomes the dispatch-table index.
   * The dispatcher has already set DB to $B8, so use bank-$00 WRAM. */
  uint16_t sprite = cpu->X;
  if (sprite != kDkc2CoopSlotA && sprite != kDkc2CoopSlotB)
    return original;
  if (original == 0x3C && CoopLiveActor(cpu, sprite) &&
      !(s_lost_mask & (sprite == kDkc2CoopSlotA ? 1 : 2))) {
    /* A hook can interrupt thrown flight before its ordinary grounded
     * recovery restores interaction bits. Restore them for this independent
     * occupant, including saved hangs made before that repair existed. */
    cpu_write16(cpu, 0, sprite + 0x30, cpu_read16(cpu, 0, sprite + 0x30) | 0x18);
  }
  if ((original == 0x5B || original == 0x5C || original == 0x67 || original == 0x68) &&
      (s_lost_mask & (sprite == kDkc2CoopSlotA ? 1 : 2)) && !CoopLiveActor(cpu, sprite)) {
    cpu_write16(cpu, 0, sprite + 0x2E, 0x13);
    return 0x13;
  }
  if (original == 0x11 || original == 0x31) {
    /* Cannon capture ends coaster ownership. In particular, $0D5E must not
     * survive until this falling cart's slot is reused by a future cart:
     * native clipping would then attach the shot Kong to that remote cart. */
    for (uint16_t cart = 0xE9E; cart < 0x16B2; cart += 0x5E) {
      if (!CoopCartActor(cpu, cart)) continue;
      for (unsigned seat = 0; seat < 2; ++seat)
        if ((cpu_read16(cpu, 0, cart + 0x42 + seat * 2) & 0x7FFF) == sprite)
          cpu_write16(cpu, 0, cart + 0x42 + seat * 2, 0);
      if (cpu_read16(cpu, 0, sprite + 0x42) == cart)
        cpu_write16(cpu, 0, sprite + 0x42, 0);
    }
    uint16_t cart = cpu_read16(cpu, 0, 0xD5E);
    if (CoopCartActor(cpu, cart) &&
        !(CoopCartRider(cpu, kDkc2CoopSlotA) && cpu_read16(cpu, 0, kDkc2CoopSlotA + 0x42) == cart) &&
        !(CoopCartRider(cpu, kDkc2CoopSlotB) && cpu_read16(cpu, 0, kDkc2CoopSlotB + 0x42) == cart))
      cpu_write16(cpu, 0, 0xD5E, 0);
  }
  if ((original == 0x67 || original == 0x68) && CoopCartPartnerLanding(cpu, sprite))
    return cpu_read16(cpu, 0, sprite + 0x2E);
  if ((original == 0x5B || original == 0x5C) && CoopCartRider(cpu, sprite) &&
      !CoopSharedKart(cpu, cpu_read16(cpu, 0, sprite + 0x42)) &&
      !cpu_read16(cpu, 0, 0x0513) &&
      (cpu_read16(cpu, 0, sprite == kDkc2CoopSlotA ? kDkc2CoopP1Pressed : kDkc2CoopP2Pressed) & 0x8000)) {
    uint16_t properties = cpu_read16(cpu, 0, sprite + 0x54);
    uint16_t work = cpu_read16(cpu, 0, sprite == cpu_read16(cpu, 0, 0x593) ? 0x595 : 0x599);
    if (work == 0x16B2 || work == 0x16D8) {
      /* The native coaster launches both through interaction $16. Give each
       * seated Kong that same character's jump impulse and gravity instead;
       * native $67 still handles its air physics, animation and collisions. */
      cpu_write16(cpu, 0, sprite + 0x2E, 0x67);
      cpu_write16(cpu, 0, sprite + 0x1E, 0);
      cpu_write16(cpu, 0, sprite + 0x24, cpu_read16(cpu, 0xFF, properties + 8));
      cpu_write16(cpu, 0, work + 8, cpu_read16(cpu, 0xFF, properties + 4));
      cpu_write16(cpu, 0, work + 10, cpu_read16(cpu, 0xFF, properties + 2));
      cpu_write16(cpu, 0, work + 0x0E, 0);
      /* Track gates inspect this native jump timestamp, which the seated
       * action would have recorded before the original paired launch. */
      cpu_write16(cpu, 0, work + 0x22, CoopReadDp(cpu, 0x002A));
      CoopCartAnimation(cpu, sprite, 6);
      return 0x67;
    }
  }
  if (CoopAnimalOnly(cpu) && sprite == cpu_read16(cpu, 0, 0x0597)) {
    cpu_write16(cpu, 0, sprite + 0x30, 0);
    if (original == 0 || original == 1 || original == 2 || original == 4 ||
        original == 6 || original == 0x13 || original == 0x22 || original == 0x2A) {
      cpu_write16(cpu, 0, sprite + 0x2E, 0x13);
      return 0x13;
    }
    return original;
  }
  /* Native follower flight uses a small terminal speed which is not normal
   * on-foot physics. Repair an already-landed old snapshot from this Kong's
   * own runtime property record, never another character's defaults. */
  if (original == 0 && CoopLiveActor(cpu, sprite) &&
      !cpu_read16(cpu, 0, 0x006E) &&
      (cpu_read16(cpu, 0, sprite + 0x1E) & 0x0101)) {
    uint16_t properties = cpu_read16(cpu, 0, sprite + 0x54);
    uint16_t work = cpu_read16(cpu, 0,
        sprite == cpu_read16(cpu, 0, 0x0593) ? 0x0595 : 0x0599);
    if (properties && (work == 0x16B2 || work == 0x16D8)) {
      cpu_write16(cpu, 0, work + 10, cpu_read16(cpu, 0xFF, properties + 2));
    }
  }
  /* Repair a snapshot from before mounted pit cleanup was added: the live
   * on-foot survivor must not inherit the lost inactive Kong's animal. */
  uint16_t inactive = cpu_read16(cpu, 0, 0x0597);
  if (sprite == cpu_read16(cpu, 0, 0x0593) && CoopRescueMovable(cpu, sprite) &&
      (inactive == kDkc2CoopSlotA || inactive == kDkc2CoopSlotB) &&
      (s_lost_mask & (inactive == kDkc2CoopSlotA ? 1 : 2)) &&
      cpu_read16(cpu, 0, inactive + 0x2E) == 0x13 &&
      !cpu_read16(cpu, 0, inactive + 0x30))
    CoopReleaseLostMount(cpu, inactive);
  if (CoopFinishBonusEntrance(cpu, sprite, original)) return 0;
  if (original == 0x36)
    CoopResumeRopeTransition(cpu, sprite);
  uint16_t held = cpu_read16(cpu, 0x00, sprite == kDkc2CoopSlotA
                                          ? kDkc2CoopP1Held : kDkc2CoopP2Held);
  /* Select belongs to the host's timed rescue. It must not wake a live
   * offscreen wait into native pit processing before the charge completes. */
  held &= (uint16_t)~0x2000;
  uint8_t lost_bit = sprite == kDkc2CoopSlotA ? 1 : 2;
  if (original == 0x24 || original == 0x25 || original == 0x05 || original == 0x59 ||
      original == 0x6A || original == 0x6B || original == 0x3B)
    s_lost_mask |= lost_bit;
  if (original == 0x3E) { /* The cartridge accepted a DK-barrel rescue. */
    s_lost_mask &= (uint8_t)~lost_bit;
    /* Native $3E decrements +$42, then finishes at +$48/+$4A. Refresh that
     * destination on its final tick: the living Kong can have moved since
     * the barrel broke. Do not retain the entrance arc's horizontal impulse
     * when independent controls replace native follower tracking. */
    uint16_t target = sprite == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint8_t target_bit = target == kDkc2CoopSlotA ? 1 : 2;
    if ((int16_t)cpu_read16(cpu, 0, sprite + 0x42) <= 0 &&
        !(s_lost_mask & target_bit) && CoopLiveActor(cpu, target)) {
      cpu_write16(cpu, 0, sprite + 0x48, cpu_read16(cpu, 0, target + 6));
      cpu_write16(cpu, 0, sprite + 0x4A, cpu_read16(cpu, 0, target + 10));
      cpu_write16(cpu, 0, sprite + 4, cpu_read16(cpu, 0, target + 4));
      cpu_write16(cpu, 0, sprite + 8, cpu_read16(cpu, 0, target + 8));
      cpu_write16(cpu, 0, sprite + 0x20, 0);
      cpu_write16(cpu, 0, sprite + 0x24, 0);
      cpu_write16(cpu, 0, sprite + 0x26, 0);
    }
  }
  /* A new life can restore only the active Kong while both old lost bits
   * remain set. Its live gameplay pose is sufficient evidence of respawn;
   * an inactive actor still needs both-present evidence. Never revive wait. */
  if ((original == 0 || original == 1 || original == 2 || original == 4 || original == 6 ||
       original == 0x0C || original == 0x0D || original == 0x0E ||
       original == 0x3C || original == 0x3D || original == 0x45 ||
       (original >= 0x41 && original <= 0x44) || original == 0x7A ||
       (original >= 0x35 && original <= 0x38) ||
       original == 0x5B || original == 0x5C || original == 0x67 || original == 0x68 ||
       (sprite == cpu_read16(cpu, 0, 0x0593) && CoopMountedRider(cpu))) &&
      ((cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
       sprite == cpu_read16(cpu, 0, 0x0593)) &&
      CoopLiveActor(cpu, sprite))
    s_lost_mask &= (uint8_t)~lost_bit;
  if (original == 0x13 && (s_lost_mask & lost_bit))
    return original;
  /* The carrier waits in $13 while the passenger jumps into its hands.
   * Independent movement input must not cancel that native animation. */
  uint16_t object = cpu_read16(cpu, 0, 0x0D7A);
  if (original == 0x13 &&
      (object == kDkc2CoopSlotA || object == kDkc2CoopSlotB) &&
      CoopHeldOwner(cpu) == sprite)
    return original;
  uint16_t leader_state = cpu_read16(cpu, 0, cpu_read16(cpu, 0, 0x0593) + 0x2E);
  uint16_t leader = cpu_read16(cpu, 0, 0x0593);
  bool mounted_arrival = (original == 0x22 || original == 0x2A) &&
      sprite == cpu_read16(cpu, 0, 0x0597) && !s_lost_mask &&
      (cpu_read16(cpu, 0, 0x08C2) & 0x4000) &&
      cpu_read16(cpu, 0, sprite + 0x30) == 0 && CoopLiveActor(cpu, leader) &&
      CoopMountedAnimal(cpu) &&
      (leader_state == 9 || leader_state == 0x0A || leader_state == 0x0B ||
       leader_state == 0x11 || leader_state == 0x12 || leader_state == 0x30);
  if (mounted_arrival) {
    /* Keep the dormant return follower in native flight until the animal
     * owner lands. Releasing it over a second barrel starts a competing
     * owner transition and strands the rider in $30. */
    if ((leader_state != 9 && leader_state != 0x0B) ||
        !(cpu_read16(cpu, 0, leader + 0x1E) & 0x0101)) return original;
    for (uint16_t offset = 4; offset <= 10; offset += 2)
      cpu_write16(cpu, 0, sprite + offset, cpu_read16(cpu, 0, leader + offset));
    cpu_write16(cpu, 0, sprite + 0x20, 0);
    cpu_write16(cpu, 0, sprite + 0x24, 0);
    cpu_write16(cpu, 0, sprite + 0x26, 0);
  }
  /* Scripted hold/walk states own the intro until they release the leader.
   * Early independent follow/wait input leaves ordinary gravity on the
   * partner, which scripted walking applies without terrain correction. */
  if ((original == 0x13 || original == 0x22) &&
      sprite == cpu_read16(cpu, 0, 0x0597) &&
      (leader_state == 0x74 || leader_state == 0x75) &&
      CoopLiveActor(cpu, cpu_read16(cpu, 0, 0x0593)))
    return original;
  if (original == 0x22 && sprite == cpu_read16(cpu, 0, 0x0597) &&
      CoopSharedTransport(cpu) && leader_state == 0x12 && !object &&
      cpu_read16(cpu, 0, cpu_read16(cpu, 0, 0x0A84)) == 0x140) {
    if (cpu_read16(cpu, 0, leader + 0x48) == 0) {
      /* Both Kongs need the native shot state, not follower tracking or
       * ordinary fall physics. Carry over the launch locks, velocities and
       * shot parameters; each player's native $12 update then owns steering,
       * gravity and its landing/animation transition. */
      static const uint16_t shot_offsets[] = {
        4, 6, 8, 10, 0x1C, 0x1E, 0x20, 0x24, 0x26, 0x30,
        0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E
      };
      for (unsigned i = 0; i < sizeof shot_offsets / sizeof shot_offsets[0]; ++i) {
        uint16_t offset = shot_offsets[i];
        cpu_write16(cpu, 0, sprite + offset, cpu_read16(cpu, 0, leader + offset));
      }
      uint16_t leader_work = cpu_read16(cpu, 0, 0x0595);
      uint16_t partner_work = cpu_read16(cpu, 0, 0x0599);
      if ((leader_work == 0x16B2 && partner_work == 0x16D8) ||
          (leader_work == 0x16D8 && partner_work == 0x16B2)) {
        cpu_write16(cpu, 0, partner_work + 8, cpu_read16(cpu, 0, leader_work + 8));
        cpu_write16(cpu, 0, partner_work + 10, cpu_read16(cpu, 0, leader_work + 10));
      }
      cpu_write16(cpu, 0, sprite + 0x2E, 0x12);
      return 0x12;
    }
  }
  if (original == 0x22 && sprite == cpu_read16(cpu, 0, 0x0597) &&
      ((CoopSharedTransport(cpu) &&
        (leader_state == 0x10 || leader_state == 0x11 || leader_state == 0x12 || leader_state == 0x30)) ||
       (CoopSharedBird(cpu) && leader_state == 0x10)))
    return original;
  uint16_t state = mounted_arrival || Dkc2CoopFollowerNeedsControl(original, held) ? 0 : original;
  if (original == 0x21 && !object && !(s_lost_mask & lost_bit) &&
      (cpu_read16(cpu, 0, (uint16_t)(sprite + 0x1E)) & 0x0101) &&
      !(cpu_read16(cpu, 0, (uint16_t)(sprite + 0x24)) & 0x8000)) {
    /* A thrown partner normally sits waiting for the leader to touch it.
     * Once grounded, give the independent player its normal interaction
     * mask and action gate. Preserve the native flight and enemy hit path. */
    state = 0;
    uint16_t flags = cpu_read16(cpu, 0, (uint16_t)(sprite + 0x30));
    cpu_write16(cpu, 0, (uint16_t)(sprite + 0x30), (uint16_t)(flags | 0x18));
  }
  if ((original == 0x20 || original == 0x2C) && !object && !s_lost_mask &&
      cpu_read16(cpu, 0, 0x0024) == 0x8819 &&
      (cpu_read16(cpu, 0, 0x08C2) & 0x4000) &&
      !cpu_read16(cpu, 0, 0x006E) && CoopLiveActor(cpu, sprite)) {
    uint16_t partner = sprite == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    if (CoopLiveActor(cpu, partner) && cpu_read16(cpu, 0, partner + 0x2E) == 0x3C) {
      for (uint16_t hook = 0x0E9E; hook < 0x16B2; hook += 0x5E) {
        if (CoopHookActor(cpu, hook) &&
            cpu_read16(cpu, 0, partner + 6) == cpu_read16(cpu, 0, hook + 6) &&
            cpu_read16(cpu, 0, partner + 10) ==
                (uint16_t)(cpu_read16(cpu, 0, hook + 10) + 23)) {
          /* The accepted hanger grab promotes the passenger. The old thrower
           * can no longer complete the native team reunion while it hangs.
           * Resume its own terrain/air physics, including from a saved wait. */
          state = 0;
          uint16_t flags = cpu_read16(cpu, 0, sprite + 0x30);
          cpu_write16(cpu, 0, sprite + 0x30, (uint16_t)(flags | 0x18));
          break;
        }
      }
    }
  }
  if (original == 0x6F) {
    /* TEAM's turn-handoff prompt waits for the newly active controller.
     * Both controllers are already participating: resume its existing
     * post-handoff jump and invincibility, including from an old snapshot. */
    state = sprite == cpu_read16(cpu, 0, 0x0593) && CoopMountedBird(cpu) ? 0x10 : 6;
    if (cpu_read16(cpu, 0, 0x0A36) == 7) {
      cpu_write16(cpu, 0, 0x0A36, 0);
      cpu_write16(cpu, 0, 0x0A38, 0);
    }
    uint16_t flags = cpu_read16(cpu, 0, 0x08C2);
    cpu_write16(cpu, 0, 0x08C2, (uint16_t)(flags & ~0x0100));
    cpu_write16(cpu, 0, (uint16_t)(sprite + 0x1C), 0);
  }
  if (state != original && CoopHeldOwner(cpu) == sprite)
    state = 0x000C; /* independently controlled, still carrying its object */
  if (state != original)
    cpu_write16(cpu, 0x00, (uint16_t)(cpu->D + sprite + 0x002E), state);
  if (state == 0 && sprite == cpu_read16(cpu, 0x00, 0x0597)) {
    /* A joined co-op player is present. Otherwise a DK barrel attempts to
     * rescue that already-controlled Kong and teleports it to the barrel. */
    uint16_t kong_flags = cpu_read16(cpu, 0x00, 0x08C2);
    if (!(kong_flags & 0x4000))
      cpu_write16(cpu, 0x00, 0x08C2, (uint16_t)(kong_flags | 0x4000));
    /* The stock follower has no interaction mask and a back render order.
     * Promote only that dormant combination, preserving action-specific
     * masks, including damage invincibility, and custom render ordering. */
    uint16_t flags_address = (uint16_t)(cpu->D + sprite + 0x0030);
    uint16_t flags = cpu_read16(cpu, 0x00, flags_address);
    if (flags == 0 || flags == 6)
      cpu_write16(cpu, 0x00, flags_address, 0x001E);
    uint16_t order_address = (uint16_t)(cpu->D + sprite + 0x0002);
    if (cpu_read16(cpu, 0x00, order_address) == 0x00D8)
      cpu_write16(cpu, 0x00, order_address, 0x00E4);
  }
  return state;
}

bool Dkc2CoopUseFollowerClipping(CpuState *cpu) {
  if (!cpu || !Dkc2CoopSimultaneousActive(
                  cpu_read16(cpu, 0x00, kDkc2CoopModeFlag),
                  s_coop_mode == kDkc2CoopSimultaneous))
    return false;
  uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  return (sprite == kDkc2CoopSlotA || sprite == kDkc2CoopSlotB) &&
         sprite == cpu_read16(cpu, 0x00, 0x0597) &&
         cpu_read16(cpu, 0x00, (uint16_t)(cpu->D + sprite + 0x0030)) != 0;
}

uint16_t Dkc2CoopRecoveryFollowerValue(CpuState *cpu, uint16_t original) {
  if (!cpu || !Dkc2CoopSimultaneousActive(
                  cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                  s_coop_mode == kDkc2CoopSimultaneous))
    return original;
  uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (sprite != kDkc2CoopSlotA && sprite != kDkc2CoopSlotB)
    return original;
  uint16_t state = CoopReadDp(cpu, (uint16_t)(sprite + 0x2E));
  /* Stock follower recovery changes the animation but deliberately leaves
   * the action state intact. A controlled Kong must finish roll/bounce and
   * barrel recovery through the normal idle transition after landing. */
  return state == 0x0004 || state == 0x0012 || state == 0x0016 || state == 0x003F || state == 0x63 ? 0 : original;
}

uint16_t Dkc2CoopFollowerPaletteOffset(CpuState *cpu, uint16_t original) {
  /* The normal inactive-Kong palette is 15 colors past its bright palette.
   * The adapter wraps only that offset, leaving status-effect palettes alone. */
  return cpu && Dkc2CoopSimultaneousActive(
                    cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                    s_coop_mode == kDkc2CoopSimultaneous) ? 0 : original;
}

uint16_t Dkc2CoopRecordInteractionSource(CpuState *cpu, uint16_t source) {
  /* Called only when the cartridge accepts a new highest-priority reaction.
   * $0A84 names the enemy, while $6A names the Kong that actually collided.
   * Capture that Kong now: later enemy checks can overwrite $6A. */
  s_stomp_source = s_stomp_kong = 0;
  s_push_source = s_push_kong = 0;
  s_cat_source = s_cat_kong = 0;
  s_mount_source = s_mount_kong = 0;
  s_animal_hit_source = s_animal_hit_owner = 0;
  s_cart_hit_source = s_cart_hit_kong = 0;
  if (CoopEnabled(cpu) && CoopReadAbs(cpu, 0x0A82) == 0x13) {
    uint16_t kong = CoopReadDp(cpu, 0x6A);
    if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) && CoopLiveActor(cpu, kong) &&
        !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2))) {
      s_cat_source = source;
      s_cat_kong = kong;
    }
  }
  if (CoopEnabled(cpu) && CoopReadAbs(cpu, 0x0A82) == 0x23) {
    uint16_t kong = CoopReadDp(cpu, 0x006A);
    bool self_terrain = (source == kDkc2CoopSlotA || source == kDkc2CoopSlotB) &&
        source == CoopReadDp(cpu, kDkc2CoopCurrentSprite) && CoopLiveActor(cpu, source) &&
        !(s_lost_mask & (source == kDkc2CoopSlotA ? 1 : 2));
    if (self_terrain) {
      /* Ceiling/side hazards queue $23 with the current Kong as their source,
       * rather than going through the floor/pit damage path. */
      s_cart_hit_source = source;
      s_cart_hit_kong = source;
    } else if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) && CoopCartRider(cpu, kong)) {
      s_cart_hit_source = source;
      s_cart_hit_kong = kong;
    }
  }
  if (CoopAnimalOnly(cpu) && CoopReadAbs(cpu, 0x0A82) == 0x23) {
    s_animal_hit_source = source;
    s_animal_hit_owner = cpu_read16(cpu, 0, 0x0593);
  }
  if (cpu && CoopEnabled(cpu) &&
      (CoopReadAbs(cpu, 0x0A82) == 0x1D || CoopReadAbs(cpu, 0x0A82) == 0x1E)) {
    uint16_t kong = CoopReadDp(cpu, 0x006A);
    if (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) {
      s_push_source = source;
      s_push_kong = kong;
    }
  }
  if (cpu && Dkc2CoopSimultaneousActive(
                 cpu_read16(cpu, 0, kDkc2CoopModeFlag),
                 s_coop_mode == kDkc2CoopSimultaneous) &&
      (CoopReadAbs(cpu, 0x0A82) == 0x001B ||
       CoopReadAbs(cpu, 0x0A82) == 0x0017)) {
    uint16_t kong = CoopReadDp(cpu, 0x006A);
    if (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) {
      if (CoopReadAbs(cpu, 0x0A82) == 0x0017) {
        s_mount_source = source;
        s_mount_kong = kong;
      } else {
        s_stomp_source = source;
        s_stomp_kong = kong;
      }
    }
  }
  return source;
}

static bool CoopCatActor(CpuState *cpu, uint16_t cat) {
  return cat >= 0xE9E && cat < 0x16B2 && (cat - 0xDE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, cat) == 0x204;
}

void Dkc2CoopCatCaptureOwner(CpuState *cpu) {
  uint16_t source = s_cat_source, kong = s_cat_kong;
  s_cat_source = s_cat_kong = 0;
  if (CoopEnabled(cpu) && source == cpu_read16(cpu, 0, 0xA84) && CoopCatActor(cpu, source) &&
      (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) && CoopLiveActor(cpu, kong) &&
      !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)))
    CoopPromoteCollider(cpu, kong);
}

bool Dkc2CoopCatIndependent(CpuState *cpu) {
  return CoopEnabled(cpu) &&
      (CoopCatActor(cpu, CoopReadDp(cpu, kDkc2CoopCurrentSprite)) ||
       CoopCatActor(cpu, cpu_read16(cpu, 0, 0xA84)));
}

static uint16_t CoopCatCapturedOwner(CpuState *cpu, uint16_t original) {
  for (uint16_t kong = kDkc2CoopSlotA; kong <= kDkc2CoopSlotB; kong += 0x5E)
    if (CoopLiveActor(cpu, kong) && !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) &&
        cpu_read16(cpu, 0, kong + 0x2E) == 0x48)
      return kong;
  return original;
}

uint16_t Dkc2CoopCatOwnerValue(CpuState *cpu, uint16_t original) {
  /* Native captured state persists through reload and survives a separate
   * hook contact promoting the free teammate. Never infer this from $6A. */
  return Dkc2CoopCatIndependent(cpu) ? CoopCatCapturedOwner(cpu, original) : original;
}

void Dkc2CoopCatReleaseOwner(CpuState *cpu) {
  if (!CoopEnabled(cpu) || !CoopCatActor(cpu, cpu_read16(cpu, 0, 0xA84))) return;
  uint16_t owner = CoopCatCapturedOwner(cpu, 0);
  if (owner) CoopPromoteCollider(cpu, owner);
}

uint16_t Dkc2CoopCatCameraValue(CpuState *cpu, uint16_t original) {
  return CoopEnabled(cpu) && CoopCatActor(cpu, original)
      ? CoopCatCapturedOwner(cpu, original) : original;
}

bool Dkc2CoopPushIndependent(CpuState *cpu) {
  uint16_t kong = s_push_kong, source = s_push_source;
  return cpu && CoopEnabled(cpu) && !s_lost_mask &&
      (cpu_read16(cpu, 0, 0x08C2) & 0x4000) &&
      (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      kong == cpu_read16(cpu, 0, 0x0597) && CoopLiveActor(cpu, kong) &&
      source >= 0x0E9E && source < 0x16B2 && (source - 0x0DE2) % 0x5E == 0 &&
      source == cpu_read16(cpu, 0, 0x0A84);
}

bool Dkc2CoopPushUsesFollower(CpuState *cpu) {
  bool follower = Dkc2CoopPushIndependent(cpu);
  s_push_source = s_push_kong = 0;
  return follower;
}

static bool CoopPoleActor(CpuState *cpu, uint16_t pole) {
  return pole >= 0x0E9E && pole < 0x16B2 && (pole - 0x0DE2) % 0x5E == 0 &&
      cpu_read16(cpu, 0, pole) == 0x2D8 &&
      (cpu_read16(cpu, 0, pole + 0x2E) == 5 || cpu_read16(cpu, 0, pole + 0x2E) == 6) &&
      cpu_read16(cpu, 0, pole + 0x54) >= 0x8000;
}

static uint16_t CoopFindPole(CpuState *cpu, uint16_t kong, bool attached) {
  if ((kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) ||
      (s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) || !CoopLiveActor(cpu, kong)) return 0;
  int velocity = (int16_t)cpu_read16(cpu, 0, kong + 0x20);
  if (velocity < 0) velocity = -velocity;
  int width = attached ? 1 : (velocity < 0x40 ? 12 : 4);
  uint16_t best = 0;
  int best_distance = 32767;
  uint16_t preferred = cpu_read16(cpu, 0, kong + 0x2E) == 0x45
      ? cpu_read16(cpu, 0, kong + 0x4C) : 0;
  for (uint16_t pole = 0xE9E; pole < 0x16B2; pole += 0x5E) {
    if (!CoopPoleActor(cpu, pole)) continue;
    int dx = (int16_t)(cpu_read16(cpu, 0, kong + 6) - cpu_read16(cpu, 0, pole + 6));
    if (dx < 0) dx = -dx;
    if (dx >= width) continue;
    uint16_t table = cpu_read16(cpu, 0, pole + 0x54);
    uint8_t bank = (uint8_t)cpu_read16(cpu, 0, 0x90);
    int top = (int16_t)cpu_read16(cpu, bank, table);
    int bottom = (int16_t)cpu_read16(cpu, bank, table + 2);
    int relative_y = (int16_t)(cpu_read16(cpu, 0, kong + 10) - cpu_read16(cpu, 0, pole + 10));
    int margin = attached ? 32 : 0;
    if (top >= bottom || relative_y <= top - margin || relative_y > bottom + margin) continue;
    if (pole == preferred) return pole;
    int distance = relative_y < 0 ? -relative_y : relative_y;
    if (distance < best_distance) { best = pole; best_distance = distance; }
  }
  return best;
}

uint16_t Dkc2CoopPolePendingValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return original;
  uint16_t pole = CoopFindPole(cpu, kong, false);
  uint16_t state = cpu_read16(cpu, 0, kong + 0x2E);
  uint16_t previous = cpu_read16(cpu, 0, kong + 0x4C);
  /* The native no-contact branch skips PoleCurrentValue entirely. Expire
   * the jump's regrab latch here, where both contact and departure run,
   * rather than carrying a reused sprite-slot anchor into a later pole. */
  if (state != 0x45 && (state != 6 || !pole) && CoopPoleActor(cpu, previous))
    cpu_write16(cpu, 0, kong + 0x4C, 0);
  if (pole) {
    /* Feed the normal eligibility and accepted-entry routine for this Kong. */
    cpu_write16(cpu, 0, 0x0BA2, pole);
    return pole;
  }
  if (original && kong == cpu_read16(cpu, 0, 0x0597)) return 0;
  return CoopPoleActor(cpu, original) ? 0 : original;
}

uint16_t Dkc2CoopPoleCurrentValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return original;
  if (cpu_read16(cpu, 0, kong + 0x2E) == 0x45) {
    uint16_t pole = CoopFindPole(cpu, kong, true);
    if (pole) {
      cpu_write16(cpu, 0, kong + 0x4C, pole);
      return pole;
    }
  } else if (cpu_read16(cpu, 0, kong + 0x2E) == 6) {
    /* Native $0BA0 also suppresses regrabbing until a jump leaves contact.
     * Keep that short-lived association per Kong in this action's spare word. */
    uint16_t previous = cpu_read16(cpu, 0, kong + 0x4C);
    if (CoopPoleActor(cpu, previous)) {
      if (CoopFindPole(cpu, kong, false) == previous) return previous;
      cpu_write16(cpu, 0, kong + 0x4C, 0);
    }
  }
  return CoopPoleActor(cpu, original) ? 0 : original;
}

uint16_t Dkc2CoopPoleReactionValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = cpu_read16(cpu, 0, 0x0A84);
  uint16_t pole = CoopFindPole(cpu, kong, false);
  if (!pole) return original;
  cpu_write16(cpu, 0, 0x0BA2, pole);
  if (cpu_read16(cpu, 0, kong + 0x2E) == 0x45)
    cpu_write16(cpu, 0, kong + 0x4C, pole);
  return pole;
}

uint16_t Dkc2CoopPolePlayerValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  return (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      cpu_read16(cpu, 0, kong + 0x2E) == 0x45 ? kong : original;
}

static bool CoopIndependentHook(CpuState *cpu) {
  return CoopEnabled(cpu) && CoopHookActor(cpu, CoopReadDp(cpu, kDkc2CoopCurrentSprite));
}

bool Dkc2CoopHookIndependent(CpuState *cpu) {
  if (!CoopIndependentHook(cpu)) return false;
  uint16_t kong = cpu_read16(cpu, 0, 0x0597);
  if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) && CoopLiveActor(cpu, kong))
    if (!CoopMountedRider(cpu)) CoopPromoteCollider(cpu, kong);
  return true;
}

bool Dkc2CoopHookUsesFollower(CpuState *cpu) {
  return CoopIndependentHook(cpu) && CoopMountedRider(cpu);
}

uint16_t Dkc2CoopHookAnimalValue(CpuState *cpu, uint16_t original) {
  return Dkc2CoopHookUsesFollower(cpu) ? 0 : original;
}

uint16_t Dkc2CoopHookCallbackValue(CpuState *cpu, uint16_t original) {
  if (!CoopIndependentHook(cpu)) return original;
  /* The collision already accepted this Kong. Native follower callback
   * $98BD launches the leader toward its AI partner; independent players
   * instead use the same direct attachment as a native leader collision. */
  uint16_t collider = CoopReadDp(cpu, 0x6A);
  if ((collider != kDkc2CoopSlotA && collider != kDkc2CoopSlotB) ||
      !CoopLiveActor(cpu, collider) || (s_lost_mask & (collider == kDkc2CoopSlotA ? 1 : 2)))
    return original;
  if (!CoopMountedRider(cpu)) CoopPromoteCollider(cpu, collider);
  return 0x9914;
}

static bool CoopHookOccupied(CpuState *cpu) {
  if (!CoopIndependentHook(cpu)) return false;
  uint16_t hook = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E)
    if (CoopLiveActor(cpu, slot) && cpu_read16(cpu, 0, (uint16_t)(slot + 0x2E)) == 0x3C &&
        cpu_read16(cpu, 0, (uint16_t)(slot + 6)) == cpu_read16(cpu, 0, (uint16_t)(hook + 6)) &&
        cpu_read16(cpu, 0, (uint16_t)(slot + 10)) ==
            (uint16_t)(cpu_read16(cpu, 0, (uint16_t)(hook + 10)) + 23)) return true;
  return false;
}

static bool CoopHookCanShare(CpuState *cpu) {
  /* A native release clears $0D82. Let the hook's original cooldown run
   * before another collision pass; the remaining Kong keeps its own pose. */
  return CoopHookOccupied(cpu) && cpu_read16(cpu, 0, 0x0D82) ==
      CoopReadDp(cpu, kDkc2CoopCurrentSprite);
}

bool Dkc2CoopHookShared(CpuState *cpu) {
  return CoopHookCanShare(cpu) &&
      !(cpu_read16(cpu, 0, CoopReadDp(cpu, kDkc2CoopCurrentSprite) + 0x4E) & 0xFF00);
}

bool Dkc2CoopHookSharedAfterCamera(CpuState *cpu) {
  /* These boss hooks carry native camera flags. Scan for a second player
   * only after the occupied handler has applied those flags and framing. */
  return CoopHookCanShare(cpu) &&
      (cpu_read16(cpu, 0, CoopReadDp(cpu, kDkc2CoopCurrentSprite) + 0x4E) & 0xFF00);
}

void Dkc2CoopHookRelease(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if ((kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) ||
      !CoopLiveActor(cpu, kong) || cpu_read16(cpu, 0, kong + 0x2E) != 0x3C) return;
  /* The first departure's timer may have expired while its partner hung.
   * Re-arm the native release transition for each departure. The action
   * clears $0D82 immediately after this call; state 1 then starts cooldown. */
  for (uint16_t hook = 0x0E9E; hook < 0x16B2; hook += 0x5E) {
    if (CoopHookActor(cpu, hook) &&
        cpu_read16(cpu, 0, kong + 6) == cpu_read16(cpu, 0, hook + 6) &&
        cpu_read16(cpu, 0, kong + 10) == (uint16_t)(cpu_read16(cpu, 0, hook + 10) + 23))
      cpu_write16(cpu, 0, hook + 0x2E, 1);
  }
}

uint16_t Dkc2CoopHookLeaderFlags(CpuState *cpu, uint16_t original) {
  if (Dkc2CoopHookUsesFollower(cpu)) return (uint16_t)(original & ~0x80);
  if (CoopIndependentHook(cpu)) {
    uint16_t collider = CoopReadDp(cpu, 0x6A);
    uint16_t leader = cpu_read16(cpu, 0, 0x593);
    uint16_t follower = cpu_read16(cpu, 0, 0x597);
    if (collider == follower && (follower == kDkc2CoopSlotA || follower == kDkc2CoopSlotB) &&
        CoopLiveActor(cpu, follower) && !(s_lost_mask & (follower == kDkc2CoopSlotA ? 1 : 2)) &&
        cpu_read16(cpu, 0, follower + 0x2E) == 0x1F &&
        cpu_read16(cpu, 0, leader + 0x2E) == 0x20)
      return (uint16_t)(original & ~0x80); /* Its own thrower is waiting for this flight. */
  }
  if (CoopIndependentHook(cpu)) {
    /* Hanging is local to the leader's hook. It must not veto the other
     * player's contact with a different hook along the route. */
    uint16_t leader = cpu_read16(cpu, 0, 0x0593);
    if (CoopLiveActor(cpu, leader) &&
        !(s_lost_mask & (leader == kDkc2CoopSlotA ? 1 : 2)) &&
        cpu_read16(cpu, 0, (uint16_t)(leader + 0x2E)) == 0x3C)
      return (uint16_t)(original & ~0x80);
  }
  return original;
}

static bool CoopIndependentGoal(CpuState *cpu) {
  if (!CoopEnabled(cpu) || !(cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
      cpu_read16(cpu, 0, 0x006C) || cpu_read16(cpu, 0, 0x006E)) return false;
  uint16_t goal = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  return goal >= 0x0E9E && goal < 0x16B2 &&
      (goal - 0x0DE2) % 0x5E == 0 && cpu_read16(cpu, 0, goal) == 0x160 &&
      cpu_read16(cpu, 0, (uint16_t)(goal + 0x2E)) == 1;
}

static bool CoopGoalEligible(CpuState *cpu, uint16_t kong) {
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return false;
  uint8_t bit = kong == kDkc2CoopSlotA ? 1 : 2;
  return !(s_lost_mask & bit) && CoopLiveActor(cpu, kong) &&
      !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x1E)) & 0x0101) &&
      !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x24)) & 0x8000);
}

uint16_t Dkc2CoopGoalCandidateValue(CpuState *cpu, uint16_t original) {
  if (!CoopIndependentGoal(cpu) || CoopGoalEligible(cpu, original)) return original;
  uint16_t follower = cpu_read16(cpu, 0, 0x0597);
  return CoopGoalEligible(cpu, follower) ? follower : original;
}

uint16_t Dkc2CoopGoalCollisionFlags(CpuState *cpu, uint16_t original) {
  /* The departed cart can still overlap a newly captured cannon occupant.
   * Filter that collider before native recruitment writes a cart seat or
   * queues $15, so it cannot steal the occupant back into the falling cart. */
  if (CoopEnabled(cpu) && CoopCartActor(cpu, CoopReadDp(cpu, kDkc2CoopCurrentSprite)) &&
      (cpu->X == kDkc2CoopSlotA || cpu->X == kDkc2CoopSlotB)) {
    uint16_t state = cpu_read16(cpu, 0, cpu->X + 0x2E);
    if (state == 0x11 || state == 0x31) return 0;
  }
  /* The goal's native two-Kong collision pass still decides overlap. Apply
   * the original descending/airborne gate independently to each collider. */
  if (CoopIndependentHook(cpu)) {
    uint16_t kong = cpu->X;
    if (CoopMountedRider(cpu) && kong == cpu_read16(cpu, 0, 0x0593)) return 0;
    if ((kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) ||
        !CoopLiveActor(cpu, kong) || (s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) ||
        cpu_read16(cpu, 0, (uint16_t)(kong + 0x2E)) == 0x3C) return 0;
    /* Horizontal team flight omits the native hook interaction bit. Let
     * the existing hook hitbox test actual contact, as for an upward throw. */
    if (cpu_read16(cpu, 0, kong + 0x2E) == 0x1F) return original | 8;
  }
  if (CoopIndependentGoal(cpu) && !CoopGoalEligible(cpu, cpu->X)) return 0;
  return original;
}

uint16_t Dkc2CoopGoalAcceptedValue(CpuState *cpu, uint16_t original) {
  if (!CoopIndependentGoal(cpu) || !cpu->_flag_C) return original;
  uint16_t winner = CoopReadDp(cpu, 0x006A);
  if (!CoopGoalEligible(cpu, winner) || winner != cpu_read16(cpu, 0, 0x0597))
    return original;
  /* Cached collision records follow the active/inactive selectors too.
   * Swap them without moving actors, so the rest of this frame stays valid. */
  CoopPromoteCollider(cpu, winner);
  return original;
}

bool Dkc2CoopUseBothMountColliders(CpuState *cpu) {
  if (CoopIndependentGoal(cpu)) return true;
  if (!CoopEnabled(cpu)) return false;
  uint16_t kong = cpu_read16(cpu, 0, 0x0597);
  uint16_t animal = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if ((kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) ||
      !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x30)) & 4)) return false;
  uint16_t state = cpu_read16(cpu, 0, (uint16_t)(kong + 0x2E));
  if (state >= 0x70 || (cpu_read16(cpu, 0xB8, (uint16_t)(0x9620 + state * 4)) & 9))
    return false;
  /* A stationary Kong beside the animal must not mask the other player's
   * valid landing. The existing two-Kong collision routine tries this
   * candidate first and then the leader if their hitboxes do not overlap. */
  uint16_t type = cpu_read16(cpu, 0, animal);
  if (type == 0x198) return !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x1E)) & 0x1001);
  return (type == 0x1A0 || !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x24)) & 0x8000)) &&
         cpu_read16(cpu, 0, (uint16_t)(kong + 10)) < cpu_read16(cpu, 0, (uint16_t)(animal + 10)) &&
         !(cpu_read16(cpu, 0, (uint16_t)(kong + 0x1E)) & 0x100);
}

uint16_t Dkc2CoopBananaSecondWidth(CpuState *cpu, uint16_t original) {
  s_banana_extra_pass = false;
  if (!CoopEnabled(cpu)) return original;
  uint16_t follower = cpu_read16(cpu, 0, 0x0597);
  if ((follower != kDkc2CoopSlotA && follower != kDkc2CoopSlotB) ||
      !cpu_read16(cpu, 0, (uint16_t)(follower + 0x30)) ||
      (s_lost_mask & (follower == kDkc2CoopSlotA ? 1 : 2)) ||
      !cpu_read16(cpu, 0, 0x09B7) || !cpu_read16(cpu, 0, 0x09EF) ||
      !cpu_read16(cpu, 0, 0x09CF) || !original)
    return original;

  /* The banana list accepts two rectangles: leader, then animal, with the
   * follower admitted only if either is absent. Temporarily use the second
   * scratch rectangle for the independently controlled follower. The normal
   * group walker clears collected bits, so overlapping passes cannot award
   * the same banana twice. Restore the animal rectangle before its pass.
   * This starts/finishes within one group; snapshots occur between frames. */
  uint16_t left = cpu_read16(cpu, 0, 0x09CB);
  uint16_t top = cpu_read16(cpu, 0, 0x09CD);
  uint16_t width = (uint16_t)(cpu_read16(cpu, 0, 0x09CF) - left);
  uint16_t height = (uint16_t)(cpu_read16(cpu, 0, 0x09D1) - top);
  if (!width || !height) return original;
  const uint16_t bounds[] = {left, top, width, height};
  for (unsigned i = 0; i < 4; ++i) {
    uint16_t address = (uint16_t)(0x0D3C + i * 4);
    s_banana_second_bounds[i] = cpu_read16(cpu, 0, address);
    cpu_write16(cpu, 0, address, bounds[i]);
  }
  s_banana_extra_pass = true;
  return width;
}

bool Dkc2CoopBananaNextPass(CpuState *cpu) {
  if (!cpu || !s_banana_extra_pass) return false;
  s_banana_extra_pass = false;
  for (unsigned i = 0; i < 4; ++i)
    cpu_write16(cpu, 0, (uint16_t)(0x0D3C + i * 4), s_banana_second_bounds[i]);
  return true;
}

static uint16_t CoopHandoffPosition(CpuState *cpu, uint16_t original, uint16_t offset) {
  if (CoopEnabled(cpu)) {
    uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
    if (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB)
      return cpu_read16(cpu, 0, (uint16_t)(kong + offset));
  }
  return original;
}

static bool CoopControlsCurrentKong(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  return kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB;
}

bool Dkc2CoopHandoffInPlace(CpuState *cpu) { return CoopControlsCurrentKong(cpu); }

static void CoopScreenSlack(CpuState *cpu, int *left, int *right) {
  *left = *right = 0;
  if (!CoopControlsCurrentKong(cpu) || !Dkc2VideoTerrainReady()) return;
  int extra = Dkc2VideoExtra(), bias = Dkc2VideoPresentationBias();
  int camera = cpu_read16(cpu, 0, 0x17BA);
  int maximum = cpu_read16(cpu, 0, 0x0AFC);
  int west = camera > 0x100 ? camera - 0x100 : 0;
  int east = maximum > camera ? maximum - camera : 0;
  *left = extra - bias;
  *right = extra + bias;
  if (*left < 0) *left = 0;
  if (*right < 0) *right = 0;
  if (*left > west) *left = west;
  if (*right > east) *right = east;
}

uint16_t Dkc2CoopScreenLeftValue(CpuState *cpu, uint16_t original) {
  int left, right;
  CoopScreenSlack(cpu, &left, &right);
  /* This operand is subtracted before the camera. Signed wrapping is the
   * guest's normal 16-bit arithmetic for a left margin beyond screen X=0. */
  return (uint16_t)(original - left);
}

uint16_t Dkc2CoopScreenSpanValue(CpuState *cpu, uint16_t original) {
  int left, right;
  CoopScreenSlack(cpu, &left, &right);
  return (uint16_t)(original + left + right);
}

uint16_t Dkc2CoopHandoffXValue(CpuState *cpu, uint16_t original) {
  return CoopHandoffPosition(cpu, original, 6);
}

uint16_t Dkc2CoopHandoffYValue(CpuState *cpu, uint16_t original) {
  return CoopHandoffPosition(cpu, original, 10);
}

uint16_t Dkc2CoopMountCandidateValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  /* The state-flags helper is also called by barrel logic. Scope the
   * alternate collider to an unmounted animal's own update. */
  uint16_t type = cpu_read16(cpu, 0, CoopReadDp(cpu, kDkc2CoopCurrentSprite));
  if (cpu_read16(cpu, 0, 0x006E) || type < 0x190 || type > 0x1A0 || (type & 3))
    return original;
  uint16_t kong = CoopReadDp(cpu, 0x6A);
  if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      (cpu_read16(cpu, 0, (uint16_t)(kong + 0x30)) & 4))
    return kong;
  return original;
}

void Dkc2CoopPrepareAnimalMount(CpuState *cpu) {
  uint16_t source = s_mount_source, kong = s_mount_kong;
  s_mount_source = s_mount_kong = 0;
  if (!CoopEnabled(cpu) || !source ||
      source != cpu_read16(cpu, 0, 0x0A84) ||
      (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) ||
      cpu_read16(cpu, 0, 0x006E) || kong != cpu_read16(cpu, 0, 0x0597))
    return;
  /* The cartridge has one animal/rider context. Make its actual rider the
   * camera leader before the accepted mount runs. Swap selector metadata,
   * preserving both players' positions, action states and collision masks.
   * Controller routing remains bound to the two fixed Kong slots. These
   * guest selectors also make ownership survive saves and rewind. */
  CoopMakeLeader(cpu, kong);
}

uint16_t Dkc2CoopAnimalFollowerFlags(CpuState *cpu, uint16_t original) {
  if (CoopSharedTransport(cpu)) return original;
  if (CoopSharedBird(cpu)) {
    uint16_t follower = cpu_read16(cpu, 0, 0x0597);
    if (CoopLiveActor(cpu, follower) && CoopRescueMovable(cpu, follower))
      cpu_write16(cpu, 0, follower + 0x2E, 0x22);
    return original;
  }
  return CoopEnabled(cpu) ? (uint16_t)(original & ~0x4000) : original;
}

uint16_t Dkc2CoopAnimalTypeValue(CpuState *cpu, uint16_t original) {
  if (CoopEnabled(cpu)) {
    uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
    if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
        kong == cpu_read16(cpu, 0, 0x0597)) return 0;
  }
  return original;
}

uint8_t Dkc2CoopTakeStompEvents(void) {
  uint8_t events = s_stomp_events;
  s_stomp_events = 0;
  return events;
}

uint16_t Dkc2CoopQuakeRecoveryValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  return (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      cpu_read16(cpu, 0, kong + 0x2E) == 0x63 ? 0 : original;
}

static bool CoopQuakeEligible(CpuState *cpu, uint16_t kong) {
  if (kong != kDkc2CoopSlotA && kong != kDkc2CoopSlotB) return false;
  return CoopLiveActor(cpu, kong) &&
      !(s_lost_mask & (kong == kDkc2CoopSlotA ? 1 : 2)) &&
      CoopRescueMovable(cpu, kong) &&
      (cpu_read16(cpu, 0, kong + 0x1E) & 1) &&
      !(cpu_read16(cpu, 0, kong + 0x30) & 0x80);
}

uint16_t Dkc2CoopQuakeCandidateValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu) || cpu_read16(cpu, 0, 0x006E)) return original;
  uint16_t follower = cpu_read16(cpu, 0, 0x0597);
  return !CoopQuakeEligible(cpu, original) && CoopQuakeEligible(cpu, follower)
      ? follower : original;
}

bool Dkc2CoopQuakeUsesFollower(CpuState *cpu) {
  uint16_t active = cpu_read16(cpu, 0, 0x0593);
  return Dkc2CoopQuakeCandidateValue(cpu, active) != active;
}

bool Dkc2CoopQuakeRepeat(CpuState *cpu) {
  return CoopEnabled(cpu) && !cpu_read16(cpu, 0, 0x006E) &&
      cpu_read16(cpu, 0, cpu_read16(cpu, 0, 0x0593) + 0x2E) == 0x63 &&
      CoopQuakeEligible(cpu, cpu_read16(cpu, 0, 0x0597));
}

/* The native stun timer is shared. Two independent stun updates must not
 * count it down twice; a lone stunned follower still runs the native clock. */
bool Dkc2CoopQuakeSkipTimer(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  uint16_t active = cpu_read16(cpu, 0, 0x0593);
  uint16_t follower = cpu_read16(cpu, 0, 0x0597);
  return (active == kDkc2CoopSlotA || active == kDkc2CoopSlotB) &&
      (follower == kDkc2CoopSlotA || follower == kDkc2CoopSlotB) && active != follower &&
      CoopReadDp(cpu, kDkc2CoopCurrentSprite) == follower &&
      cpu_read16(cpu, 0, active + 0x2E) == 0x63 &&
      cpu_read16(cpu, 0, follower + 0x2E) == 0x63;
}

static bool CoopStompAirborne(CpuState *cpu, uint16_t kong) {
  uint16_t state = cpu_read16(cpu, 0, kong + 0x2E);
  /* Native shot $12 remains active during descent until landing or impact.
   * It can share a stomp just like an ordinary descending jump. */
  return CoopLiveActor(cpu, kong) &&
      (state == 1 || state == 4 || state == 6 || state == 0x12 || state == 0x16 ||
       CoopCartFlight(cpu, kong)) &&
      !(cpu_read16(cpu, 0, kong + 0x24) & 0x8000) &&
      !(cpu_read16(cpu, 0, kong + 0x1E) & 0x0101);
}

static int CoopPixelDistance(uint16_t a, uint16_t b) {
  int distance = (int)(int16_t)(a - b);
  return distance < 0 ? -distance : distance;
}

static bool CoopStompPartnerEligible(CpuState *cpu, uint16_t owner, uint16_t source) {
  if (!CoopEnabled(cpu) || s_lost_mask || cpu_read16(cpu, 0, 0x006E) ||
      !(cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
      source < 0x0E9E || source >= 0x16B2 || (source - 0x0DE2) % 0x5E)
    return false;
  uint16_t partner = owner == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  return CoopStompAirborne(cpu, owner) && CoopStompAirborne(cpu, partner) &&
      CoopPixelDistance(cpu_read16(cpu, 0, owner + 6), cpu_read16(cpu, 0, partner + 6)) <=
          DKC2_COOP_STOMP_HORIZONTAL_PX &&
      CoopPixelDistance(cpu_read16(cpu, 0, owner + 10), cpu_read16(cpu, 0, partner + 10)) <=
          DKC2_COOP_STOMP_VERTICAL_PX &&
      CoopPixelDistance(cpu_read16(cpu, 0, source + 6), cpu_read16(cpu, 0, partner + 6)) <=
          DKC2_COOP_STOMP_HORIZONTAL_PX + 16 &&
      (int16_t)(cpu_read16(cpu, 0, partner + 10) - cpu_read16(cpu, 0, source + 10)) <= 8;
}

void Dkc2CoopShareStomp(CpuState *cpu) {
  uint16_t owner = s_shared_stomp_owner, partner = s_shared_stomp_partner;
  s_shared_stomp_owner = s_shared_stomp_partner = 0;
  if (!cpu || !owner || !partner || !CoopEnabled(cpu) || s_lost_mask ||
      cpu_read16(cpu, 0, 0x006E) || !(cpu_read16(cpu, 0, 0x08C2) & 0x4000) ||
      !CoopLiveActor(cpu, owner) || !CoopStompAirborne(cpu, partner) ||
      (cpu_read16(cpu, 0, owner + 0x2E) != 0x16 &&
       !(cpu_read16(cpu, 0, owner + 0x2E) == 0x68 && CoopCartFlight(cpu, owner))) ||
      !(cpu_read16(cpu, 0, owner + 0x24) & 0x8000)) return;
  uint16_t active = cpu_read16(cpu, 0, 0x0593);
  uint16_t owner_work = cpu_read16(cpu, 0, owner == active ? 0x0595 : 0x0599);
  uint16_t partner_work = cpu_read16(cpu, 0, partner == active ? 0x0595 : 0x0599);
  if (!((owner_work == 0x16B2 && partner_work == 0x16D8) ||
        (owner_work == 0x16D8 && partner_work == 0x16B2))) return;
  /* Mirror only the accepted bounce impulse and its native gravity/terminal
   * speed. Keep the partner's jump pose, position, horizontal speed and identity. */
  cpu_write16(cpu, 0, partner + 0x24, cpu_read16(cpu, 0, owner + 0x24));
  cpu_write16(cpu, 0, partner_work + 8, cpu_read16(cpu, 0, owner_work + 8));
  cpu_write16(cpu, 0, partner_work + 10, cpu_read16(cpu, 0, owner_work + 10));
  cpu_write16(cpu, 0, partner + 0x2E, CoopCartFlight(cpu, partner) ? 0x68 : 0x16);
  s_stomp_events |= partner == kDkc2CoopSlotA ? 1 : 2;
}

bool Dkc2CoopRopeUsesFollower(CpuState *cpu) {
  if (!CoopEnabled(cpu)) return false;
  /* Terrain contact queues the actual Kong in $0A84. Unlike an enemy
   * reaction, its source is the player itself. This guest-owned value
   * survives a snapshot taken between contact and reaction dispatch. */
  uint16_t source = cpu_read16(cpu, 0, 0x0A84);
  return (source == kDkc2CoopSlotA || source == kDkc2CoopSlotB) &&
         source == cpu_read16(cpu, 0, 0x0597);
}

uint16_t Dkc2CoopRopeAnimationFollowerValue(CpuState *cpu, uint16_t original) {
  if (!CoopEnabled(cpu)) return original;
  uint16_t kong = cpu->X;
  uint16_t state = cpu_read16(cpu, 0, (uint16_t)(kong + 0x2E));
  if ((kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      ((state >= 0x35 && state <= 0x38) || state == 0x3D || state == 0x45))
    return 0xFFFF;
  return original;
}

bool Dkc2CoopBounceUsesFollower(CpuState *cpu) {
  uint16_t source = s_stomp_source, kong = s_stomp_kong;
  s_stomp_source = s_stomp_kong = 0;
  s_shared_stomp_owner = s_shared_stomp_partner = 0;
  if (cpu && (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      source == cpu_read16(cpu, 0, 0x0A84) &&
      CoopStompPartnerEligible(cpu, kong, source)) {
    s_shared_stomp_owner = kong;
    s_shared_stomp_partner = kong == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
  }
  bool follower = cpu && Dkc2CoopSimultaneousActive(
      cpu_read16(cpu, 0, kDkc2CoopModeFlag), s_coop_mode == kDkc2CoopSimultaneous) &&
      (kong == kDkc2CoopSlotA || kong == kDkc2CoopSlotB) &&
      source == CoopReadAbs(cpu, 0x0A84) && kong == cpu_read16(cpu, 0, 0x0597);
  /* This hook runs only in player_interaction_1B, after the cartridge has
   * accepted the stomp. Observe its owner without changing guest state. */
  if (cpu) {
    uint16_t mode = cpu_read16(cpu, 0, kDkc2CoopModeFlag);
    uint16_t owner = cpu_read16(cpu, 0, follower ? 0x0597 : 0x0593);
    uint16_t controller = mode == 0 ? 1 : cpu_read16(cpu, 0, 0x08A2);
    if (Dkc2CoopSimultaneousActive(mode, s_coop_mode == kDkc2CoopSimultaneous) && !CoopAnimalOnly(cpu))
      controller = owner == kDkc2CoopSlotA ? 1 : 2;
    if ((owner == kDkc2CoopSlotA || owner == kDkc2CoopSlotB) &&
        controller >= 1 && controller <= 2)
      s_stomp_events |= (uint8_t)(1u << (controller - 1));
  }
  return follower;
}

uint16_t Dkc2CoopSelectHeldWord(CpuState *cpu, uint16_t original) {
  if (!cpu)
    return original;
  uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (CoopAnimalOnly(cpu)) {
    if (sprite != cpu_read16(cpu, 0, 0x0593)) return 0;
    uint16_t controller = cpu_read16(cpu, 0, 0x08A2);
    return (uint16_t)(CoopReadAbs(cpu, controller == 1 ? kDkc2CoopP1Held : kDkc2CoopP2Held) & ~0x2000u);
  }
  uint16_t mode = CoopReadAbs(cpu, kDkc2CoopModeFlag);
  uint16_t result = Dkc2CoopSelectHeld(
      original, sprite, mode, s_coop_mode == kDkc2CoopSimultaneous,
      CoopReadAbs(cpu, kDkc2CoopP1Held), CoopReadAbs(cpu, kDkc2CoopP2Held));
  if (CoopSharedTransport(cpu) && sprite == cpu_read16(cpu, 0, 0x0593) &&
      (sprite == kDkc2CoopSlotA || sprite == kDkc2CoopSlotB) &&
      cpu_read16(cpu, 0, sprite + 0x2E) == 0x11 &&
      cpu_read16(cpu, 0, cpu_read16(cpu, 0, 0x0A84)) == 0x140) {
    uint16_t partner = sprite == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    if (cpu_read16(cpu, 0, partner + 0x2E) == 0x31) {
      /* Let either occupant aim using the cartridge's normal rotation rules.
       * Opposite directions cancel per axis; other buttons stay port-owned. */
      uint16_t partner_held = CoopReadAbs(cpu, sprite == kDkc2CoopSlotA
          ? kDkc2CoopP2Held : kDkc2CoopP1Held);
      result |= partner_held & 0x0F00;
      if ((result & 0x0300) == 0x0300) result &= (uint16_t)~0x0300;
      if ((result & 0x0C00) == 0x0C00) result &= (uint16_t)~0x0C00;
    }
  }
  if (CoopDebugEnabled() && s_debug_held_count < 40) {
    s_debug_held_count++;
    fprintf(stderr, "coop_held #%d sprite=%04x mode=%u orig=%04x -> %04x\n",
            s_debug_held_count, sprite, mode, original, result);
  }
  return result;
}

uint16_t Dkc2CoopSelectPressedWord(CpuState *cpu, uint16_t original) {
  if (!cpu)
    return original;
  if (CoopAnimalOnly(cpu)) {
    if (CoopReadDp(cpu, kDkc2CoopCurrentSprite) != cpu_read16(cpu, 0, 0x0593)) return 0;
    uint16_t controller = cpu_read16(cpu, 0, 0x08A2);
    return (uint16_t)(CoopReadAbs(cpu, controller == 1 ? kDkc2CoopP1Pressed : kDkc2CoopP2Pressed) & ~0x2000u);
  }
  uint16_t result = Dkc2CoopSelectPressed(original,
                               CoopReadDp(cpu, kDkc2CoopCurrentSprite),
                               CoopReadAbs(cpu, kDkc2CoopModeFlag),
                               s_coop_mode == kDkc2CoopSimultaneous,
                               CoopReadAbs(cpu, kDkc2CoopP1Pressed),
                               CoopReadAbs(cpu, kDkc2CoopP2Pressed));
  uint16_t sprite = CoopReadDp(cpu, kDkc2CoopCurrentSprite);
  if (CoopSharedTransport(cpu) && sprite == cpu_read16(cpu, 0, 0x0593) &&
      (sprite == kDkc2CoopSlotA || sprite == kDkc2CoopSlotB) &&
      cpu_read16(cpu, 0, sprite + 0x2E) == 0x11) {
    /* Share the fire action. Air steering, item actions and all on-foot
     * input retain their fixed controller owner. */
    uint16_t partner_pressed = CoopReadAbs(cpu, sprite == kDkc2CoopSlotA
        ? kDkc2CoopP2Pressed : kDkc2CoopP1Pressed);
    result |= partner_pressed & 0x8000;
  }
  return result;
}
