#include "dkc2_coop.h"
#include "dkc2_coop_assist.h"
#include "dkc2_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Synthetic guest memory for the wrapper tests: one 64 KiB WRAM bank.
 * WRAM addresses in banks $00 and $7E alias the same bytes. The synthetic
 * animal eligibility table uses separate, otherwise unused high addresses,
 * so the stub ignores the bank argument; the
 * direct-page read resolves at D+offset exactly like the 65816 contract.
 */
static uint8_t s_fake_wram[0x10000];

uint16 cpu_read16(CpuState *cpu, uint8 bank, uint16 addr) {
  (void)cpu;
  (void)bank;
  return (uint16)(s_fake_wram[addr] |
                  (uint16)((uint16)s_fake_wram[(uint16)(addr + 1)] << 8));
}

void cpu_write16(CpuState *cpu, uint8 bank, uint16 addr, uint16 value) {
  (void)cpu;
  (void)bank;
  s_fake_wram[addr] = (uint8_t)value;
  s_fake_wram[(uint16_t)(addr + 1u)] = (uint8_t)(value >> 8);
}

static void WriteWord(uint16_t address, uint16_t value) {
  s_fake_wram[address] = (uint8_t)value;
  s_fake_wram[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

static int s_failures = 0;

static void ExpectU16(const char *what, uint16_t actual, uint16_t expected) {
  if (actual != expected) {
    fprintf(stderr, "%s: expected 0x%04x, got 0x%04x\n", what, expected,
            actual);
    s_failures++;
  }
}

static void ExpectBool(const char *what, int actual, int expected) {
  if ((actual != 0) != (expected != 0)) {
    fprintf(stderr, "%s: expected %d, got %d\n", what, expected,
            actual != 0);
    s_failures++;
  }
}

static void TestModeAccessors(void) {
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectBool("mode round-trips classic", Dkc2CoopGetMode() == kDkc2CoopClassic,
             1);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  ExpectBool("mode round-trips simultaneous",
             Dkc2CoopGetMode() == kDkc2CoopSimultaneous, 1);
  Dkc2CoopSetMode((Dkc2CoopMode)-1);
  ExpectBool("invalid mode clamps to simultaneous",
             Dkc2CoopGetMode() == kDkc2CoopSimultaneous, 1);
  Dkc2CoopSetMode(kDkc2CoopModeCount);
  ExpectBool("out-of-range mode clamps to simultaneous",
             Dkc2CoopGetMode() == kDkc2CoopSimultaneous, 1);
}

static void TestModeFromName(void) {
  Dkc2CoopMode mode = kDkc2CoopClassic;
  ExpectBool("parses simultaneous",
             Dkc2CoopModeFromName("simultaneous", &mode), 1);
  ExpectBool("simultaneous value", mode == kDkc2CoopSimultaneous, 1);
  ExpectBool("parses classic", Dkc2CoopModeFromName("classic", &mode), 1);
  ExpectBool("classic value", mode == kDkc2CoopClassic, 1);
  ExpectBool("rejects invalid name",
             Dkc2CoopModeFromName("alternating", &mode), 0);
  ExpectBool("rejects empty name", Dkc2CoopModeFromName("", &mode), 0);
  ExpectBool("rejects null text", Dkc2CoopModeFromName(NULL, &mode), 0);
  ExpectBool("rejects null out", Dkc2CoopModeFromName("classic", NULL), 0);
}

static void TestSimultaneousActive(void) {
  ExpectBool("team mode activates", Dkc2CoopSimultaneousActive(1, 1), 1);
  ExpectBool("disabled policy never activates",
             Dkc2CoopSimultaneousActive(1, 0), 0);
  ExpectBool("1 player mode untouched", Dkc2CoopSimultaneousActive(0, 1), 0);
  ExpectBool("contest mode untouched", Dkc2CoopSimultaneousActive(2, 1), 0);
  ExpectBool("out-of-range mode untouched",
             Dkc2CoopSimultaneousActive(3, 1), 0);
}

static void TestGateActive(void) {
  const uint16_t active = 0x0DE2;
  /* Classic: the cartridge compare decides, unchanged. */
  ExpectU16("classic keeps active slot",
            Dkc2CoopGateActive(active, 0x0E40, 1, 0), active);
  /* Simultaneous: both Kong slots compare equal and take the input path. */
  ExpectU16("slot A gate opens",
            Dkc2CoopGateActive(active, 0x0DE2, 1, 1), 0x0DE2);
  ExpectU16("slot B gate opens",
            Dkc2CoopGateActive(active, 0x0E40, 1, 1), 0x0E40);
  /* Simultaneous but not a Kong slot: fall back to the cartridge compare. */
  ExpectU16("non-kong sprite keeps cartridge compare",
            Dkc2CoopGateActive(active, 0x1234, 1, 1), active);
  /* Non-TEAM modes never open the gate. */
  ExpectU16("1 player keeps cartridge compare",
            Dkc2CoopGateActive(active, 0x0DE2, 0, 1), active);
  ExpectU16("contest keeps cartridge compare",
            Dkc2CoopGateActive(active, 0x0E40, 2, 1), active);
}

static void TestSelectWords(void) {
  /* Classic: the active words pass through unchanged. */
  ExpectU16("classic keeps held word",
            Dkc2CoopSelectHeld(0xAAAA, 0x0E40, 1, 0, 0x1111, 0x2222),
            0xAAAA);
  ExpectU16("classic keeps pressed word",
            Dkc2CoopSelectPressed(0xBBBB, 0x0DE2, 1, 0, 0x3333, 0x4444),
            0xBBBB);
  /* Simultaneous: each Kong slot reads its own controller's words. */
  ExpectU16("slot A held comes from controller 1",
            Dkc2CoopSelectHeld(0xAAAA, 0x0DE2, 1, 1, 0x1111, 0x2222),
            0x1111);
  ExpectU16("slot B held comes from controller 2",
            Dkc2CoopSelectHeld(0xAAAA, 0x0E40, 1, 1, 0x1111, 0x2222),
            0x0222);
  ExpectU16("slot A pressed comes from controller 1",
            Dkc2CoopSelectPressed(0xBBBB, 0x0DE2, 1, 1, 0x3333, 0x4444),
            0x1333);
  ExpectU16("slot B pressed comes from controller 2",
            Dkc2CoopSelectPressed(0xBBBB, 0x0E40, 1, 1, 0x3333, 0x4444),
            0x4444);
  /* Simultaneous but not a Kong slot: keep the active words. */
  ExpectU16("non-kong sprite keeps held word",
            Dkc2CoopSelectHeld(0xAAAA, 0x1234, 1, 1, 0x1111, 0x2222),
            0xAAAA);
  /* Non-TEAM modes keep the active words. */
  ExpectU16("1 player keeps held word",
            Dkc2CoopSelectHeld(0xAAAA, 0x0DE2, 0, 1, 0x1111, 0x2222),
            0xAAAA);
  ExpectU16("contest keeps pressed word",
            Dkc2CoopSelectPressed(0xBBBB, 0x0E40, 2, 1, 0x3333, 0x4444),
            0xBBBB);
}

static void TestFollowerNeedsControl(void) {
  ExpectBool("passive wait plus input takes control",
             Dkc2CoopFollowerNeedsControl(0x0013, 0x0400), 1);
  ExpectBool("passive wait without input stays passive",
             Dkc2CoopFollowerNeedsControl(0x0013, 0x0000), 0);
  ExpectBool("scripted states are never redirected",
             Dkc2CoopFollowerNeedsControl(0x003E, 0x0400), 0);
  ExpectBool("input states are never redirected",
             Dkc2CoopFollowerNeedsControl(0x0000, 0x0400), 0);
  ExpectBool("follow-history state returns to player control",
             Dkc2CoopFollowerNeedsControl(0x0022, 0x0400), 1);
  ExpectBool("released controller does not return to follow-history",
             Dkc2CoopFollowerNeedsControl(0x0022, 0x0000), 1);
}

static void TestWrappersAgainstFakeWram(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  cpu.D = 0x0000;
  cpu.DB = 0x00;
  memset(s_fake_wram, 0, sizeof s_fake_wram);

  WriteWord(0x0064, 0x0E40); /* current_sprite: slot B */
  WriteWord(0x060D, 1);      /* 2 PLAYER TEAM */
  WriteWord(0x0502, 0x0101); /* P1 held */
  WriteWord(0x0504, 0x0202); /* P2 held */
  WriteWord(0x0506, 0x0303); /* P1 pressed */
  WriteWord(0x0508, 0x0404); /* P2 pressed */

  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopResetSession();
  cpu.X = kDkc2CoopSlotB;
  cpu.DB = 0xB8; /* PHK/PLB precedes the dispatch read. */
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x13);
  WriteWord(kDkc2CoopSlotB + 0x02, 0xD8);
  WriteWord(0x0597, kDkc2CoopSlotB);
  ExpectU16("waiting follower joins before its script executes",
            Dkc2CoopSelectStateWord(&cpu, 0x13), 0);
  ExpectU16("state word matches selected handler",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x2E), 0);
  ExpectU16("joined player can interact",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x30), 0x1E);
  ExpectU16("joined player is present for DK barrels",
            cpu_read16(&cpu, 0, 0x08C2), 0x4000);
  WriteWord(kDkc2CoopSlotB + 0x30, 6);
  (void)Dkc2CoopSelectStateWord(&cpu, 0);
  ExpectU16("follower mask after a leader swap regains enemy collisions",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x30), 0x1E);
  ExpectU16("joined player uses normal render order",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x02), 0xE4);
  ExpectBool("joined follower refreshes its own hitbox",
             Dkc2CoopUseFollowerClipping(&cpu), 1);
  WriteWord(kDkc2CoopSlotB + 0x30, 0x9E);
  (void)Dkc2CoopSelectStateWord(&cpu, 0);
  ExpectU16("hurt invincibility mask retained",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x30), 0x9E);
  WriteWord(kDkc2CoopSlotB + 0x30, 0);
  (void)Dkc2CoopSelectStateWord(&cpu, 0x59);
  ExpectU16("scripted hurt state not promoted",
            cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x30), 0);
  ExpectBool("disabled follower does not create a hitbox",
             Dkc2CoopUseFollowerClipping(&cpu), 0);
  ExpectBool("null clipping context fails safe",
             Dkc2CoopUseFollowerClipping(NULL), 0);
  WriteWord(0x0504, 0);
  ExpectU16("neutral follower remains independent after landing",
            Dkc2CoopSelectStateWord(&cpu, 0x22), 0);
  for (uint16_t state = 0; state < 0x80; ++state) {
    if (state == 0x22 || state == 0x6F) continue;
    Dkc2CoopResetSession(); /* Do not carry earlier synthetic death dispatches forward. */
    ExpectU16("other states preserved with neutral input",
              Dkc2CoopSelectStateWord(&cpu, state), state);
  }
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectU16("classic follow script preserved",
            Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  for (uint16_t mode = 0; mode <= 2; mode += 2) {
    WriteWord(0x060D, mode);
    ExpectU16("other game modes preserve follower state",
              Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
  }
  WriteWord(0x060D, 1);
  ExpectU16("simultaneous follower uses bright palette",
            Dkc2CoopFollowerPaletteOffset(&cpu, 0x1E), 0);
  cpu.X = 0x1234;
  ExpectU16("non-Kong state stays untouched",
            Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
  ExpectU16("null state wrapper fails safe",
            Dkc2CoopSelectStateWord(NULL, 0x22), 0x22);
  cpu.DB = 0;
  WriteWord(0x0504, 0x0202);

  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  ExpectU16("wrapper gate opens for slot B",
            Dkc2CoopGateActiveValue(&cpu, 0x0DE2), 0x0E40);
  ExpectU16("wrapper selects controller 2 held",
            Dkc2CoopSelectHeldWord(&cpu, 0x050E), 0x0202);
  ExpectU16("wrapper selects controller 2 pressed",
            Dkc2CoopSelectPressedWord(&cpu, 0x0510), 0x0404);

  WriteWord(0x0064, 0x0DE2); /* current_sprite: slot A */
  ExpectU16("wrapper gate opens for slot A",
            Dkc2CoopGateActiveValue(&cpu, 0x0E40), 0x0DE2);
  ExpectU16("wrapper selects controller 1 held",
            Dkc2CoopSelectHeldWord(&cpu, 0x050E), 0x0101);
  ExpectU16("wrapper selects controller 1 pressed",
            Dkc2CoopSelectPressedWord(&cpu, 0x0510), 0x0303);

  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectU16("classic wrapper gate keeps active slot",
            Dkc2CoopGateActiveValue(&cpu, 0x0E40), 0x0E40);
  ExpectU16("classic wrapper keeps held word",
            Dkc2CoopSelectHeldWord(&cpu, 0x050E), 0x050E);
  ExpectU16("classic wrapper keeps pressed word",
            Dkc2CoopSelectPressedWord(&cpu, 0x0510), 0x0510);

  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 0); /* 1 PLAYER */
  ExpectU16("1 player wrapper gate keeps active slot",
            Dkc2CoopGateActiveValue(&cpu, 0x0E40), 0x0E40);
  ExpectU16("1 player wrapper keeps held word",
            Dkc2CoopSelectHeldWord(&cpu, 0x050E), 0x050E);

  /* A non-zero direct page still resolves current_sprite like hardware. */
  WriteWord(0x060D, 1);
  cpu.D = 0x0100;
  WriteWord(0x0164, 0x0E40);
  ExpectU16("wrapper honors direct page for current_sprite",
            Dkc2CoopGateActiveValue(&cpu, 0x0DE2), 0x0E40);

  /* NULL cpu fails safe to the cartridge values. */
  ExpectU16("null cpu gate fails safe", Dkc2CoopGateActiveValue(NULL, 0x1234),
            0x1234);
  ExpectU16("null cpu held fails safe", Dkc2CoopSelectHeldWord(NULL, 0x050E),
            0x050E);
  ExpectU16("null cpu pressed fails safe",
            Dkc2CoopSelectPressedWord(NULL, 0x0510), 0x0510);

  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
}

static void TestStompOwnership(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  WriteWord(0x0597, kDkc2CoopSlotB);
  WriteWord(0x006A, kDkc2CoopSlotB);
  WriteWord(0x0A82, 0x1B);
  WriteWord(0x0A84, 0x0E9E);
  ExpectU16("reaction enemy preserved",
            Dkc2CoopRecordInteractionSource(&cpu, 0x0E9E), 0x0E9E);
  WriteWord(0x006A, kDkc2CoopSlotA); /* later enemy check */
  ExpectBool("bounce belongs to the Kong at queue time",
             Dkc2CoopBounceUsesFollower(&cpu), 1);
  ExpectU16("P2 stomp routes rumble to P2", Dkc2CoopTakeStompEvents(), 2);
  ExpectU16("stomp events consumed once", Dkc2CoopTakeStompEvents(), 0);
  ExpectBool("reaction ownership consumed once",
             Dkc2CoopBounceUsesFollower(&cpu), 0);
  (void)Dkc2CoopRecordInteractionSource(&cpu, 0x0E9E);
  ExpectBool("active Kong keeps stock bounce", Dkc2CoopBounceUsesFollower(&cpu), 0);
  WriteWord(0x006A, kDkc2CoopSlotB);
  (void)Dkc2CoopRecordInteractionSource(&cpu, 0x0E9E);
  WriteWord(0x0A82, 0x20);
  (void)Dkc2CoopRecordInteractionSource(&cpu, 0x0EFC);
  ExpectBool("higher priority reaction replaces ownership",
             Dkc2CoopBounceUsesFollower(&cpu), 0);
  WriteWord(0x0A82, 0x1B);
  (void)Dkc2CoopRecordInteractionSource(&cpu, 0x0EFC);
  ExpectBool("different reaction source fails safe", Dkc2CoopBounceUsesFollower(&cpu), 0);
  for (uint16_t mode = 0; mode <= 2; ++mode) {
    WriteWord(0x060D, mode);
    Dkc2CoopSetMode(mode == 1 ? kDkc2CoopClassic : kDkc2CoopSimultaneous);
    ExpectU16("classic, solo and contest keep follower palette",
              Dkc2CoopFollowerPaletteOffset(&cpu, 0x1E), 0x1E);
    (void)Dkc2CoopRecordInteractionSource(&cpu, 0x0E9E);
    ExpectBool("classic, solo and contest keep stock bounce",
               Dkc2CoopBounceUsesFollower(&cpu), 0);
  }
  for (uint16_t mode = 0; mode <= 2; ++mode) {
    WriteWord(0x060D, mode);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    WriteWord(0x0593, kDkc2CoopSlotB);
    WriteWord(0x08A2, 2);
    (void)Dkc2CoopBounceUsesFollower(&cpu);
    ExpectU16("solo uses P1; alternating modes use active controller",
              Dkc2CoopTakeStompEvents(), mode == 0 ? 1 : 2);
    (void)Dkc2CoopBounceUsesFollower(&cpu);
    Dkc2CoopResetSession();
    ExpectU16("load/reset clears pending rumble", Dkc2CoopTakeStompEvents(), 0);
  }
  ExpectU16("null queue preserves source", Dkc2CoopRecordInteractionSource(NULL, 7), 7);
  ExpectBool("null bounce fails safe", Dkc2CoopBounceUsesFollower(NULL), 0);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
}

static void TestRecovery(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  WriteWord(0x0064, kDkc2CoopSlotB);
  for (uint16_t state = 0; state < 0x80; ++state) {
    WriteWord(kDkc2CoopSlotB + 0x2E, state);
    ExpectU16("roll, barrel, bounce, throw and stun recovery use the active transition",
              Dkc2CoopRecoveryFollowerValue(&cpu, kDkc2CoopSlotB),
              state == 4 || state == 0x12 || state == 0x16 || state == 0x3F || state == 0x63 ? 0 : kDkc2CoopSlotB);
    ExpectU16("recovery gate does not prematurely change the action state",
              cpu_read16(&cpu, 0, kDkc2CoopSlotB + 0x2E), state);
  }
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x16);
  for (uint16_t mode = 0; mode <= 2; ++mode) {
    WriteWord(0x060D, mode);
    Dkc2CoopSetMode(mode == 1 ? kDkc2CoopClassic : kDkc2CoopSimultaneous);
    ExpectU16("stock recovery modes unaffected",
              Dkc2CoopRecoveryFollowerValue(&cpu, kDkc2CoopSlotB), kDkc2CoopSlotB);
  }
  ExpectU16("null recovery is unchanged", Dkc2CoopRecoveryFollowerValue(NULL, 7), 7);
}

static void TestHeldObjectOwnership(void) {
  CpuState cpu;
  const uint16_t object = 0x0F5A;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  WriteWord(0x0597, kDkc2CoopSlotB);
  WriteWord(0x0064, kDkc2CoopSlotB);
  WriteWord(object, 0x1A8);
  ExpectU16("accepted pickup keeps the object's slot",
            Dkc2CoopRecordPickupValue(&cpu, object), object);
  WriteWord(0x0D7A, object);
  ExpectU16("P2 owns its picked-up object",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotA), kDkc2CoopSlotB);
  ExpectU16("P2 still sees its held object", Dkc2CoopHeldObjectValue(&cpu, object), object);
  WriteWord(0x0064, kDkc2CoopSlotA);
  ExpectU16("P1 recovery does not adopt P2's barrel",
            Dkc2CoopHeldObjectValue(&cpu, object), 0);
  WriteWord(0x0064, object);
  ExpectU16("barrel position uses its owner",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotA), kDkc2CoopSlotB);
  cpu.X = kDkc2CoopSlotB;
  ExpectU16("follower transition preserves independent carry",
            Dkc2CoopSelectStateWord(&cpu, 0x22), 0x0C);
  WriteWord(0x0D7A, 0);
  ExpectU16("released object no longer overrides owner",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotA), kDkc2CoopSlotA);
  WriteWord(0x0D7A, object);
  WriteWord(object, 0x1BC);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0);
  ExpectU16("recycled sprite does not inherit stale ownership",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotA), kDkc2CoopSlotA);
  WriteWord(0x0064, kDkc2CoopSlotA);
  (void)Dkc2CoopRecordPickupValue(&cpu, object);
  ExpectU16("P1 pickup retains P1 ownership",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotB), kDkc2CoopSlotA);
  Dkc2CoopLoadLostMask(&cpu, 0, true); /* drop all transient pickup history */
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x0F);
  ExpectU16("loaded throw reconstructs P2 ownership from guest state",
            Dkc2CoopHeldOwnerValue(&cpu, kDkc2CoopSlotA), kDkc2CoopSlotB);
  for (uint16_t owner = kDkc2CoopSlotA; owner <= kDkc2CoopSlotB; owner += 0x5E) {
    uint16_t other = owner == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    for (uint16_t state = 0x0C; state <= 0x0F; ++state) {
      Dkc2CoopLoadLostMask(&cpu, 0, true);
      WriteWord(owner + 0x2E, state);
      WriteWord(other + 0x2E, 0);
      WriteWord(0x0064, owner);
      ExpectU16("loaded ground/air carrier retains its item",
                Dkc2CoopHeldObjectValue(&cpu, object), object);
      ExpectU16("loaded ground/air carrier owns its item",
                Dkc2CoopHeldOwnerValue(&cpu, other), owner);
      WriteWord(0x0064, other);
      WriteWord(other == kDkc2CoopSlotA ? 0x0502 : 0x0504, 0x4000);
      ExpectU16("partner run/pick cannot see the carrier's item",
                Dkc2CoopHeldObjectValue(&cpu, object), 0);
      ExpectU16("partner input leaves the shared item intact",
                cpu_read16(&cpu, 0, 0x0D7A), object);
    }
  }
  for (uint16_t mode = 0; mode <= 2; ++mode) {
    WriteWord(0x060D, mode);
    Dkc2CoopSetMode(mode == 1 ? kDkc2CoopClassic : kDkc2CoopSimultaneous);
    (void)Dkc2CoopRecordPickupValue(&cpu, object);
    ExpectU16("classic, solo and contest preserve held owner",
              Dkc2CoopHeldOwnerValue(&cpu, 123), 123);
    ExpectU16("classic, solo and contest preserve held object",
              Dkc2CoopHeldObjectValue(&cpu, object), object);
  }
  ExpectU16("null pickup is unchanged", Dkc2CoopRecordPickupValue(NULL, object), object);
  ExpectU16("null held owner is unchanged", Dkc2CoopHeldOwnerValue(NULL, 7), 7);
  ExpectU16("null held object is unchanged", Dkc2CoopHeldObjectValue(NULL, 7), 7);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
}

static void TestHurtLifecycle(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopResetSession();
  WriteWord(0x060D, 1);
  WriteWord(0x0597, kDkc2CoopSlotB);
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
    cpu.X = slot;
    WriteWord(slot == kDkc2CoopSlotA ? 0x0502 : 0x0504, 0xFFFF);
    ExpectU16("hurt animation stays in charge", Dkc2CoopSelectStateWord(&cpu, 0x24), 0x24);
    ExpectU16("runaway animation stays in charge", Dkc2CoopSelectStateWord(&cpu, 0x25), 0x25);
    ExpectU16("held input cannot revive a lost Kong", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
    uint8_t saved = Dkc2CoopSaveLostMask();
    Dkc2CoopLoadLostMask(&cpu, saved, true);
    ExpectU16("saved loss survives reload", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    Dkc2CoopSetMode(kDkc2CoopSimultaneous);
    ExpectU16("changing policy does not revive a lost Kong", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
    ExpectU16("accepted barrel rescue preserves its animation", Dkc2CoopSelectStateWord(&cpu, 0x3E), 0x3E);
    ExpectU16("rescued Kong becomes independently controllable", Dkc2CoopSelectStateWord(&cpu, 0x22), 0);
  }
  WriteWord(0x08C2, 0x100);
  WriteWord(cpu.X + 0x1C, 0xC000);
  WriteWord(cpu.X + 0x30, 0x9E);
  ExpectU16("handoff continues existing jump", Dkc2CoopSelectStateWord(&cpu, 0x6F), 6);
  ExpectU16("handoff clears turn pause", cpu_read16(&cpu, 0, 0x08C2), 0);
  ExpectU16("handoff clears wait blink", cpu_read16(&cpu, 0, cpu.X + 0x1C), 0);
  ExpectU16("handoff retains damage grace period", cpu_read16(&cpu, 0, cpu.X + 0x30), 0x9E);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x13);
  WriteWord(kDkc2CoopSlotB + 2, 0xE4);
  Dkc2CoopLoadLostMask(&cpu, 0, false);
  ExpectU16("legacy lost Kong cannot rejoin", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
  Dkc2CoopResetSession();
  ExpectU16("fresh session permits initial join", Dkc2CoopSelectStateWord(&cpu, 0x13), 0);
}

static void TestAnimalsAndHandoff(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopResetSession();
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  const uint16_t animal = 0x0FB8;
  for (uint16_t kong = kDkc2CoopSlotA; kong <= kDkc2CoopSlotB; kong += 0x5E) {
    uint16_t other = kong == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    WriteWord(0x0593, other);
    WriteWord(0x0595, 0x16B2);
    WriteWord(0x0597, kong);
    WriteWord(0x0599, 0x16D8);
    WriteWord(0x0064, animal);
    WriteWord(0x006A, kong);
    WriteWord(0x006E, 0);
    WriteWord(animal, 0x19C);
    WriteWord(animal + 10, 200);
    WriteWord(kong + 6, 400);
    WriteWord(kong + 10, 180);
    WriteWord(kong + 0x24, 0x100);
    WriteWord(kong + 0x2E, 6);
    WriteWord(kong + 0x30, 0x1E);
    ExpectBool("either follower can land on animal", Dkc2CoopUseBothMountColliders(&cpu), true);
    WriteWord(kong + 10, 200);
    ExpectBool("standing Kong cannot mask rider landing", Dkc2CoopUseBothMountColliders(&cpu), false);
    WriteWord(kong + 10, 180);
    WriteWord(kong + 0x24, 0xFF00);
    ExpectBool("rising Kong does not mask another landing", Dkc2CoopUseBothMountColliders(&cpu), false);
    WriteWord(kong + 0x24, 0x100);
    WriteWord(0x9620 + 6 * 4, 9);
    ExpectBool("cartridge ineligible state is respected", Dkc2CoopUseBothMountColliders(&cpu), false);
    WriteWord(0x9620 + 6 * 4, 0);
    ExpectU16("mount gate uses collider", Dkc2CoopMountCandidateValue(&cpu, other), kong);
    WriteWord(animal, 0x1A8);
    ExpectU16("barrel state lookup retains leader", Dkc2CoopMountCandidateValue(&cpu, other), other);
    WriteWord(animal, 0x19C);
    WriteWord(0x006E, 0x19C);
    ExpectU16("mounted animal state lookup retains rider", Dkc2CoopMountCandidateValue(&cpu, other), other);
    WriteWord(0x006E, 0);
    WriteWord(0x0A82, 0x17);
    WriteWord(0x0A84, animal);
    Dkc2CoopRecordInteractionSource(&cpu, animal);
    WriteWord(0x006A, other); /* a later collision cannot steal the accepted mount */
    Dkc2CoopPrepareAnimalMount(&cpu);
    ExpectU16("rider becomes leader", cpu_read16(&cpu, 0, 0x0593), kong);
    ExpectU16("old leader becomes independent partner", cpu_read16(&cpu, 0, 0x0597), other);
    ExpectU16("rider work selector follows owner", cpu_read16(&cpu, 0, 0x0595), 0x16D8);
    ExpectU16("partner work selector follows owner", cpu_read16(&cpu, 0, 0x0599), 0x16B2);
    ExpectU16("rider controller remains fixed", cpu_read16(&cpu, 0, 0x08A2), kong == kDkc2CoopSlotA ? 1 : 2);
    ExpectU16("mount preparation preserves position", cpu_read16(&cpu, 0, kong + 6), 400);
    ExpectU16("mount preparation preserves collision", cpu_read16(&cpu, 0, kong + 0x30), 0x1E);
    WriteWord(0x0064, other);
    ExpectU16("partner keeps on-foot physics", Dkc2CoopAnimalTypeValue(&cpu, 0x19C), 0);
    WriteWord(0x0064, kong);
    ExpectU16("rider keeps animal physics", Dkc2CoopAnimalTypeValue(&cpu, 0x19C), 0x19C);
    ExpectU16("handoff retains survivor x", Dkc2CoopHandoffXValue(&cpu, 999), 400);
    ExpectU16("handoff retains survivor y", Dkc2CoopHandoffYValue(&cpu, 999), 180);
    WriteWord(0x0064, animal);
    ExpectU16("animal sprite sees mounted type", Dkc2CoopAnimalTypeValue(&cpu, 0x19C), 0x19C);
  }
  ExpectU16("partner not snapped to animal", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4100), 0x100);
  cpu.X = kDkc2CoopSlotB;
  WriteWord(0x0A36, 7);
  WriteWord(0x0A38, 55);
  Dkc2CoopSelectStateWord(&cpu, 0x6F);
  ExpectU16("turn freeze is cleared", cpu_read16(&cpu, 0, 0x0A36), 0);
  ExpectU16("turn freeze timer is cleared", cpu_read16(&cpu, 0, 0x0A38), 0);
  WriteWord(0x0A36, 3);
  Dkc2CoopSelectStateWord(&cpu, 0x6F);
  ExpectU16("unrelated freeze retained", cpu_read16(&cpu, 0, 0x0A36), 3);
  for (uint16_t mode = 0; mode < 3; ++mode) {
    WriteWord(0x060D, mode);
    Dkc2CoopSetMode(mode == 1 ? kDkc2CoopClassic : kDkc2CoopSimultaneous);
    ExpectBool("stock animal collider retained", Dkc2CoopUseBothMountColliders(&cpu), false);
    ExpectU16("stock animal flags retained", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4100), 0x4100);
    ExpectU16("stock handoff position retained", Dkc2CoopHandoffXValue(&cpu, 999), 999);
    ExpectU16("stock mount candidate retained", Dkc2CoopMountCandidateValue(&cpu, 123), 123);
    ExpectU16("stock animal type retained", Dkc2CoopAnimalTypeValue(&cpu, 0x19C), 0x19C);
  }
  ExpectBool("null mounting safe", Dkc2CoopUseBothMountColliders(NULL), false);
  ExpectU16("null animal type safe", Dkc2CoopAnimalTypeValue(NULL, 3), 3);
  Dkc2CoopPrepareAnimalMount(NULL);
}

static void TestWideMovementBounds(void) {
  CpuState cpu;
  memset(&cpu, 0, sizeof cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  WriteWord(0x17BA, 1000);
  WriteWord(0x0AFC, 6000);
  for (uint16_t kong = kDkc2CoopSlotA; kong <= kDkc2CoopSlotB; kong += 0x5E) {
    WriteWord(0x0064, kong);
    for (int aspect = kDkc2VideoAspectNative; aspect < kDkc2VideoAspectCount; ++aspect) {
      Dkc2VideoSetAspect((Dkc2VideoAspect)aspect);
      Dkc2VideoSetTerrainReady(true);
      Dkc2VideoSetPresentationBias(0);
      int extra = aspect == kDkc2VideoAspectNative ? 0 : aspect == kDkc2VideoAspect16x10 ? 26 : aspect == kDkc2VideoAspect16x9 ? 43 : 95;
      ExpectU16("wide movement left follows aspect", Dkc2CoopScreenLeftValue(&cpu, 16), (uint16_t)(16-extra));
      ExpectU16("wide movement span follows aspect", Dkc2CoopScreenSpanValue(&cpu, 224), (uint16_t)(224+2*extra));
    }
  }
  Dkc2VideoSetAspect(kDkc2VideoAspect16x9);
  Dkc2VideoSetTerrainReady(true);
  WriteWord(0x17BA, 0x100);
  Dkc2VideoSetPresentationBias(43);
  ExpectU16("west wall remains bounded", Dkc2CoopScreenLeftValue(&cpu, 16), 16);
  ExpectU16("west shift opens east margin", Dkc2CoopScreenSpanValue(&cpu, 224), 310);
  WriteWord(0x17BA, 6000);
  Dkc2VideoSetPresentationBias(-43);
  ExpectU16("east shift opens west margin", Dkc2CoopScreenLeftValue(&cpu, 16), (uint16_t)-70);
  ExpectU16("east wall remains bounded", (uint16_t)(Dkc2CoopScreenLeftValue(&cpu, 16) +
             Dkc2CoopScreenSpanValue(&cpu, 224)), 240);
  Dkc2VideoSetPresentationBias(0);
  ExpectU16("reflected art cannot extend east level limit",
            (uint16_t)(Dkc2CoopScreenLeftValue(&cpu, 16) + Dkc2CoopScreenSpanValue(&cpu, 224)), 240);
  WriteWord(0x17BA, 0x100);
  WriteWord(0x0AFC, 0x100);
  ExpectU16("single-screen room stays bounded", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  WriteWord(0x17BA, 1000);
  WriteWord(0x0AFC, 6000);
  Dkc2VideoSetTerrainReady(false);
  ExpectU16("unproven margins are not playable", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  Dkc2VideoSetTerrainReady(true);
  WriteWord(0x0064, 0x0FB8);
  ExpectU16("non-Kong keeps native limits", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  WriteWord(0x0064, kDkc2CoopSlotB);
  WriteWord(0x060D, 0);
  ExpectU16("solo keeps native limits", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  WriteWord(0x060D, 2);
  ExpectU16("contest keeps native limits", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  WriteWord(0x060D, 1);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectU16("classic TEAM keeps native limits", Dkc2CoopScreenSpanValue(&cpu, 224), 224);
  ExpectU16("null width query safe", Dkc2CoopScreenSpanValue(NULL, 224), 224);
  Dkc2VideoSetAspect(kDkc2VideoAspectNative);
}

static void TestBananaCollectionPass(void) {
  CpuState cpu = {0};
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopResetSession();
  WriteWord(0x060D, 1);
  WriteWord(0x09B7, 100);  /* leader hitbox present */
  WriteWord(0x09EF, 110);  /* animal hitbox present */
  WriteWord(0x09CB, 300);  /* distant follower rectangle */
  WriteWord(0x09CD, 400);
  WriteWord(0x09CF, 320);
  WriteWord(0x09D1, 435);
  const uint16_t original[] = {90, 200, 20, 30};
  const uint16_t follower[] = {300, 400, 20, 35};
  for (unsigned player = 0; player < 2; ++player) {
    uint16_t slot = player ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    WriteWord(0x0597, slot);
    WriteWord((uint16_t)(slot + 0x30), 30);
    for (unsigned i = 0; i < 4; ++i) WriteWord((uint16_t)(0x0D3C + i * 4), original[i]);
    ExpectU16("include on-foot banana collider", Dkc2CoopBananaSecondWidth(&cpu, 20), 20);
    for (unsigned i = 0; i < 4; ++i)
      ExpectU16("follower gets separate bounds", cpu_read16(&cpu, 0, (uint16_t)(0x0D3C + i * 4)), follower[i]);
    ExpectBool("animal still receives a collection pass", Dkc2CoopBananaNextPass(&cpu), 1);
    for (unsigned i = 0; i < 4; ++i)
      ExpectU16("original scratch bounds restored", cpu_read16(&cpu, 0, (uint16_t)(0x0D3C + i * 4)), original[i]);
    ExpectBool("repeat only once per group", Dkc2CoopBananaNextPass(&cpu), 0);
    WriteWord((uint16_t)(slot + 0x30), 0);
    Dkc2CoopBananaSecondWidth(&cpu, 20);
    ExpectBool("absent follower cannot collect", Dkc2CoopBananaNextPass(&cpu), 0);
    WriteWord((uint16_t)(slot + 0x30), 30);
  }
  const uint16_t required[] = {0x09B7, 0x09EF, 0x09CF};
  for (unsigned i = 0; i < 3; ++i) {
    uint16_t value = cpu_read16(&cpu, 0, required[i]);
    WriteWord(required[i], 0);
    Dkc2CoopBananaSecondWidth(&cpu, 20);
    ExpectBool("two or fewer colliders use stock path", Dkc2CoopBananaNextPass(&cpu), 0);
    WriteWord(required[i], value);
  }
  for (unsigned mode = 0; mode < 3; mode += 2) {
    WriteWord(0x060D, (uint16_t)mode);
    Dkc2CoopBananaSecondWidth(&cpu, 20);
    ExpectBool("solo/contest unchanged", Dkc2CoopBananaNextPass(&cpu), 0);
  }
  WriteWord(0x060D, 1);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  Dkc2CoopBananaSecondWidth(&cpu, 20);
  ExpectBool("classic unchanged", Dkc2CoopBananaNextPass(&cpu), 0);
  ExpectU16("null banana query", Dkc2CoopBananaSecondWidth(NULL, 20), 20);
  ExpectBool("null banana continuation", Dkc2CoopBananaNextPass(NULL), 0);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopLoadLostMask(&cpu, 2, true);
  Dkc2CoopBananaSecondWidth(&cpu, 20);
  ExpectBool("saved lost follower excluded", Dkc2CoopBananaNextPass(&cpu), 0);
  Dkc2CoopResetSession();
}

static void TestTeamCarry(void) {
  CpuState cpu = {0};
  memset(s_fake_wram, 0, sizeof(s_fake_wram));
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopResetSession();
  WriteWord(0x060D, 1);
  WriteWord(0x08C2, 0x4000);
  WriteWord(0x0593, kDkc2CoopSlotA);
  WriteWord(0x0595, 0x100);
  WriteWord(0x0597, kDkc2CoopSlotB);
  WriteWord(0x0599, 0x200);
  WriteWord(0x0064, kDkc2CoopSlotB);
  WriteWord(kDkc2CoopSlotA + 6, 100);
  WriteWord(kDkc2CoopSlotB + 6, 125);
  cpu.Y = kDkc2CoopSlotA;
  ExpectU16("P2 targets P1", Dkc2CoopTeamPartnerValue(&cpu, kDkc2CoopSlotB), kDkc2CoopSlotA);
  ExpectU16("distant pickup rejected", Dkc2CoopTeamStateValue(&cpu, 0), 0xFFFF);
  ExpectU16("rejected pickup leaves leader", cpu_read16(&cpu, 0, 0x0593), kDkc2CoopSlotA);
  WriteWord(kDkc2CoopSlotB + 6, 124);
  WriteWord(kDkc2CoopSlotB + 10, 17);
  ExpectU16("vertical separation rejected", Dkc2CoopTeamStateValue(&cpu, 0), 0xFFFF);
  WriteWord(kDkc2CoopSlotB + 10, 16);
  ExpectU16("airborne partner rejected", Dkc2CoopTeamStateValue(&cpu, 6), 0xFFFF);
  WriteWord(0x0D7A, 0x0E9E);
  ExpectU16("occupied hands rejected", Dkc2CoopTeamStateValue(&cpu, 0), 0xFFFF);
  WriteWord(0x0D7A, 0);
  WriteWord(0x006E, 1);
  ExpectU16("mounted pair rejected", Dkc2CoopTeamStateValue(&cpu, 0), 0xFFFF);
  WriteWord(0x006E, 0);
  Dkc2CoopLoadLostMask(&cpu, 1, true);
  ExpectU16("lost partner rejected", Dkc2CoopTeamStateValue(&cpu, 0x13), 0xFFFF);
  Dkc2CoopResetSession();
  ExpectU16("nearby independent pickup accepted", Dkc2CoopTeamStateValue(&cpu, 0), 0x22);
  ExpectU16("P2 carrier becomes leader", cpu_read16(&cpu, 0, 0x0593), kDkc2CoopSlotB);
  ExpectU16("carrier work swapped", cpu_read16(&cpu, 0, 0x0595), 0x200);
  ExpectU16("passenger work swapped", cpu_read16(&cpu, 0, 0x0599), 0x100);
  ExpectU16("carrier controller unchanged", cpu_read16(&cpu, 0, 0x08A2), 2);
  ExpectU16("pickup does not teleport", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  WriteWord(0x0D7A, kDkc2CoopSlotA);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x13);
  WriteWord(0x0504, 0x0100);
  cpu.X = kDkc2CoopSlotB;
  ExpectU16("movement preserves pickup animation", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
  ExpectU16("carrier owns passenger", Dkc2CoopHeldOwnerValue(&cpu, 0), kDkc2CoopSlotB);
  Dkc2CoopResetSession();
  ExpectU16("carry survives host reset", Dkc2CoopHeldOwnerValue(&cpu, 0), kDkc2CoopSlotB);
  WriteWord(0x0064, kDkc2CoopSlotA);
  ExpectU16("passenger does not own itself", Dkc2CoopHeldObjectValue(&cpu, kDkc2CoopSlotA), 0);
  WriteWord(0x0D7A, 0);
  cpu.X = kDkc2CoopSlotA;
  WriteWord(kDkc2CoopSlotA + 0x30, 0x16);
  ExpectU16("airborne thrown player keeps physics", Dkc2CoopSelectStateWord(&cpu, 0x21), 0x21);
  WriteWord(kDkc2CoopSlotA + 0x1E, 1);
  WriteWord(kDkc2CoopSlotA + 0x24, 0xFA00);
  ExpectU16("bouncing thrown player keeps physics", Dkc2CoopSelectStateWord(&cpu, 0x21), 0x21);
  WriteWord(kDkc2CoopSlotA + 0x24, 0);
  ExpectU16("landed thrown player regains control", Dkc2CoopSelectStateWord(&cpu, 0x21), 0);
  ExpectU16("landed thrown player regains collisions", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 0x30), 0x1E);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0);
  cpu.Y = kDkc2CoopSlotB;
  ExpectU16("P1 can carry in reverse", Dkc2CoopTeamStateValue(&cpu, 0), 0x22);
  ExpectU16("P1 carrier becomes leader", cpu_read16(&cpu, 0, 0x0593), kDkc2CoopSlotA);
  for (uint16_t mode = 0; mode <= 2; mode += 2) {
    WriteWord(0x060D, mode);
    ExpectU16("solo/contest partner unchanged", Dkc2CoopTeamPartnerValue(&cpu, 123), 123);
    ExpectU16("solo/contest eligibility unchanged", Dkc2CoopTeamStateValue(&cpu, 0x2A), 0x2A);
  }
  WriteWord(0x060D, 1);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectU16("classic eligibility unchanged", Dkc2CoopTeamStateValue(&cpu, 0), 0);
  ExpectU16("null eligibility unchanged", Dkc2CoopTeamStateValue(NULL, 0), 0);
}

static void TestRopeTransitionReload(void) {
  CpuState cpu = {0};
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
    for (uint16_t direction = 0; direction <= 1; ++direction) {
      memset(s_fake_wram, 0, sizeof(s_fake_wram));
      Dkc2CoopSetMode(kDkc2CoopSimultaneous);
      cpu.X = slot;
      WriteWord(0x060D, 1);
      WriteWord(slot + 0x2E, 0x36);
      WriteWord(slot + 0x36, (uint16_t)((slot == kDkc2CoopSlotA ? 0x34 : 0xD7) + direction));
      WriteWord(slot + 0x3C, 0x8003);
      WriteWord(slot + 0x3E, 0xDD63);
      /* Synthetic script: native completion followed by terminal wait. */
      WriteWord(0x8000, 0x0081);
      WriteWord(0x8001, direction ? 0xDD90 : 0xDD7E);
      WriteWord(0x8003, 0x0083);
      WriteWord(0x8004, 0xD12B);
      WriteWord(slot + 0x38, 0x100);
      Dkc2CoopSelectStateWord(&cpu, 0x36);
      ExpectU16("unfinished junction is not rewound", cpu_read16(&cpu, 0, slot + 0x3C), 0x8003);
      WriteWord(slot + 0x38, 0);
      WriteWord(0x8004, 0xD100);
      Dkc2CoopSelectStateWord(&cpu, 0x36);
      ExpectU16("foreign wait is not rewound", cpu_read16(&cpu, 0, slot + 0x3C), 0x8003);
      WriteWord(0x8004, 0xD12B);
      for (uint16_t mode = 0; mode <= 2; mode += 2) {
        WriteWord(0x060D, mode);
        Dkc2CoopSelectStateWord(&cpu, 0x36);
        ExpectU16("solo/contest junction is untouched", cpu_read16(&cpu, 0, slot + 0x3C), 0x8003);
      }
      WriteWord(0x060D, 1);
      Dkc2CoopSetMode(kDkc2CoopClassic);
      Dkc2CoopSelectStateWord(&cpu, 0x36);
      ExpectU16("classic junction is untouched", cpu_read16(&cpu, 0, slot + 0x3C), 0x8003);
      Dkc2CoopSetMode(kDkc2CoopSimultaneous);
      Dkc2CoopResetSession();
      ExpectU16("saved junction keeps native transition state", Dkc2CoopSelectStateWord(&cpu, 0x36), 0x36);
      ExpectU16("saved junction replays completion", cpu_read16(&cpu, 0, slot + 0x3C), 0x8000);
      Dkc2CoopSelectStateWord(&cpu, 0x36);
      ExpectU16("completion rewinds once", cpu_read16(&cpu, 0, slot + 0x3C), 0x8000);
    }
  }
}

static void TestRopeOwnership(void) {
  CpuState cpu = {0};
  memset(s_fake_wram, 0, sizeof(s_fake_wram));
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  WriteWord(0x060D, 1);
  for (uint16_t follower = kDkc2CoopSlotA; follower <= kDkc2CoopSlotB; follower += 0x5E) {
    uint16_t leader = follower == kDkc2CoopSlotA ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    WriteWord(0x0593, leader);
    WriteWord(0x0597, follower);
    WriteWord(0x0064, leader); /* Last-updated sprite need not be the grabber. */
    WriteWord(0x0A84, follower);
    cpu.X = follower;
    for (uint16_t state = 0x35; state <= 0x38; ++state) {
      WriteWord(follower + 0x2E, state);
      ExpectU16("independent climber advances animation and junction", Dkc2CoopRopeAnimationFollowerValue(&cpu, follower), 0xFFFF);
    }
    WriteWord(follower + 0x2E, 0x22);
    ExpectU16("other animation states unchanged", Dkc2CoopRopeAnimationFollowerValue(&cpu, follower), follower);
    WriteWord(follower + 0x2E, 0x3D);
    ExpectU16("independent swimmer completes native facing callback", Dkc2CoopRopeAnimationFollowerValue(&cpu, follower), 0xFFFF);
    WriteWord(follower + 0x2E, 0x45);
    ExpectU16("independent pole climber advances native animation", Dkc2CoopRopeAnimationFollowerValue(&cpu, follower), 0xFFFF);
    ExpectBool("rope grab belongs to contact source", Dkc2CoopRopeUsesFollower(&cpu), 1);
    Dkc2CoopResetSession();
    ExpectBool("queued rope grab survives snapshot reset", Dkc2CoopRopeUsesFollower(&cpu), 1);
    ExpectU16("rope grab keeps camera leader", cpu_read16(&cpu, 0, 0x0593), leader);
    WriteWord(0x0A84, leader);
    ExpectBool("leader rope grab keeps native work", Dkc2CoopRopeUsesFollower(&cpu), 0);
  }
  WriteWord(0x0597, 0x0E9E);
  cpu.X = 0x0E9E;
  WriteWord(cpu.X + 0x2E, 0x35);
  ExpectU16("non-Kong animation unchanged", Dkc2CoopRopeAnimationFollowerValue(&cpu, cpu.X), cpu.X);
  WriteWord(0x0A84, 0x0E9E);
  ExpectBool("non-Kong rope source rejected", Dkc2CoopRopeUsesFollower(&cpu), 0);
  WriteWord(0x0597, kDkc2CoopSlotB);
  WriteWord(0x0A84, kDkc2CoopSlotB);
  cpu.X = kDkc2CoopSlotB;
  WriteWord(cpu.X + 0x2E, 0x35);
  for (uint16_t mode = 0; mode <= 2; mode += 2) {
    WriteWord(0x060D, mode);
    ExpectBool("solo/contest rope work unchanged", Dkc2CoopRopeUsesFollower(&cpu), 0);
    ExpectU16("solo/contest rope animation unchanged", Dkc2CoopRopeAnimationFollowerValue(&cpu, cpu.X), cpu.X);
  }
  WriteWord(0x060D, 1);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectBool("classic rope work unchanged", Dkc2CoopRopeUsesFollower(&cpu), 0);
  ExpectU16("classic rope animation unchanged", Dkc2CoopRopeAnimationFollowerValue(&cpu, cpu.X), cpu.X);
  ExpectU16("null rope animation unchanged", Dkc2CoopRopeAnimationFollowerValue(NULL, 123), 123);
  ExpectBool("null rope work unchanged", Dkc2CoopRopeUsesFollower(NULL), 0);
}

static void RescueSetup(CpuState *cpu) {
  memset(cpu, 0, sizeof *cpu);
  memset(s_fake_wram, 0, sizeof s_fake_wram);
  Dkc2CoopSetMode(kDkc2CoopSimultaneous);
  Dkc2CoopResetSession();
  WriteWord(0x060D, 1);
  WriteWord(0x0024, 0x8819);
  WriteWord(0x08C2, 0x4000);
  WriteWord(0x0593, kDkc2CoopSlotA);
  WriteWord(0x0597, kDkc2CoopSlotB);
  WriteWord(kDkc2CoopSlotA, 0xE4);
  WriteWord(kDkc2CoopSlotB, 0xE8);
  for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
    WriteWord(slot + 0x30, 0x1E);
    WriteWord(slot + 0x1E, 1);
    WriteWord(slot + 10, 200);
  }
  WriteWord(kDkc2CoopSlotA + 6, 100);
  WriteWord(kDkc2CoopSlotB + 6, 300);
}

static void TestCameraMidpoint(void) {
  CpuState cpu;
  RescueSetup(&cpu); cpu.Y = kDkc2CoopSlotA;
  WriteWord(kDkc2CoopSlotA + 10, 100); WriteWord(kDkc2CoopSlotB + 10, 501);
  ExpectU16("camera X follows the midpoint", Dkc2CoopCameraXValue(&cpu, 100), 200);
  ExpectU16("camera Y follows the midpoint", Dkc2CoopCameraYValue(&cpu, 100), 300);
  ExpectU16("camera retains half-pixel midpoint fraction", Dkc2CoopCameraYFraction(&cpu, 0), 0x2C80);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x11); WriteWord(kDkc2CoopSlotB + 0x30, 0);
  ExpectU16("occupied cannon still contributes camera position", Dkc2CoopCameraXValue(&cpu, 100), 200);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0); WriteWord(kDkc2CoopSlotB + 0x30, 0x1E);
  cpu.Y = kDkc2CoopSlotB;
  ExpectU16("camera midpoint does not depend on leader", Dkc2CoopCameraXValue(&cpu, 300), 200);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x3C);
  ExpectU16("hanging Kong contributes to midpoint", Dkc2CoopCameraXValue(&cpu, 300), 200);
  for (unsigned poles = 1; poles <= 3; ++poles) {
    WriteWord(kDkc2CoopSlotA + 0x2E, (poles & 1) ? 0x45 : 0);
    WriteWord(kDkc2CoopSlotB + 0x2E, (poles & 2) ? 0x45 : 0);
    for (unsigned leader = 0; leader < 2; ++leader) {
      cpu.Y = leader ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      ExpectU16("either/both pole climbers contribute camera X", Dkc2CoopCameraXValue(&cpu, 123), 200);
      ExpectU16("either/both pole climbers contribute camera Y", Dkc2CoopCameraYValue(&cpu, 123), 300);
      ExpectU16("pole midpoint retains fractional Y", Dkc2CoopCameraYFraction(&cpu, 0), 0x2C80);
    }
  }
  for (unsigned bouncing = 1; bouncing <= 3; ++bouncing) {
    WriteWord(kDkc2CoopSlotA + 0x2E, (bouncing & 1) ? 0x16 : 0);
    WriteWord(kDkc2CoopSlotB + 0x2E, (bouncing & 2) ? 0x16 : 0);
    for (unsigned leader = 0; leader < 2; ++leader) {
      cpu.Y = leader ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      ExpectU16("either/both stomping Kongs contribute camera X", Dkc2CoopCameraXValue(&cpu, 123), 200);
      ExpectU16("either/both stomping Kongs contribute camera Y", Dkc2CoopCameraYValue(&cpu, 123), 300);
      ExpectU16("stomp midpoint retains fractional Y", Dkc2CoopCameraYFraction(&cpu, 0), 0x2C80);
    }
  }
  const uint16_t honey_states[] = {0x41, 0x42, 0x43, 0x44, 0x7A};
  for (unsigned a = 0; a <= 5; ++a) {
    for (unsigned b = 0; b <= 5; ++b) {
      WriteWord(kDkc2CoopSlotA + 0x2E, a ? honey_states[a - 1] : 0);
      WriteWord(kDkc2CoopSlotB + 0x2E, b ? honey_states[b - 1] : 0);
      for (unsigned leader = 0; leader < 2; ++leader) {
        cpu.Y = leader ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        ExpectU16("honey states contribute camera X", Dkc2CoopCameraXValue(&cpu, 123), 200);
        ExpectU16("honey states contribute camera Y", Dkc2CoopCameraYValue(&cpu, 123), 300);
        ExpectU16("honey midpoint retains fractional X", Dkc2CoopCameraXFraction(&cpu, 0), 0xC800);
        ExpectU16("honey midpoint retains fractional Y", Dkc2CoopCameraYFraction(&cpu, 0), 0x2C80);
      }
    }
  }
  WriteWord(kDkc2CoopSlotA + 0x2E, 0);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0);
  WriteWord(kDkc2CoopSlotA + 6, 65000); WriteWord(kDkc2CoopSlotB + 6, 65500);
  ExpectU16("camera midpoint avoids 16-bit sum overflow", Dkc2CoopCameraXValue(&cpu, 65000), 65250);
  for (unsigned blocked = 0; blocked < 6; ++blocked) {
    RescueSetup(&cpu); cpu.Y = kDkc2CoopSlotA;
    if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
    if (blocked == 1) WriteWord(0x060D, 0);
    if (blocked == 2) Dkc2CoopLoadLostMask(&cpu, 2, true);
    if (blocked == 3) WriteWord(kDkc2CoopSlotB + 0x30, 0);
    if (blocked == 4) WriteWord(kDkc2CoopSlotB + 0x2E, 0x46);
    if (blocked == 5) cpu.Y = 0x0F5A;
    ExpectU16("camera keeps native target outside independent play", Dkc2CoopCameraXValue(&cpu, 123), 123);
  }
  ExpectU16("null camera context is unchanged", Dkc2CoopCameraXValue(NULL, 123), 123);
  RescueSetup(&cpu);
  ExpectBool("unrelated reaction retains native setup", Dkc2CoopCannonIndependent(&cpu), false);
  WriteWord(0x0A84, kDkc2CoopSlotB);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x2B);
  ExpectBool("thrown Kong keeps native barrel reunion setup", Dkc2CoopCannonIndependent(&cpu), false);
  WriteWord(0x0A84, 0x0FB8); WriteWord(0x0FB8, 0x13C);
  ExpectU16("cannon entry targets the cannon not the partner", Dkc2CoopCannonTargetValue(&cpu, kDkc2CoopSlotB), 0x0FB8);
  WriteWord(0x0A82, 0); WriteWord(0x006A, kDkc2CoopSlotA);
  ExpectBool("two live players retain native shared cannon entry", Dkc2CoopCannonIndependent(&cpu), false);
  ExpectU16("shared cannon retains native leader selector", cpu_read16(&cpu, 0, 0x0593), kDkc2CoopSlotA);
  ExpectU16("shared cannon retains native partner launch", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4000), 0x4000);
  WriteWord(0x006E, 0x194);
  ExpectU16("stale cannon source cannot recruit a mounted partner", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4000), 0);
  WriteWord(0x006E, 0);
  for (uint16_t type = 0x174; type <= 0x178; type += 4) {
    WriteWord(0x0FB8, type);
    ExpectBool("transport variants keep shared entry", Dkc2CoopCannonIndependent(&cpu), false);
    ExpectU16("transport variants keep paired launch", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4000), 0x4000);
  }
  Dkc2CoopLoadLostMask(&cpu, 2, true);
  ExpectBool("lost partner excluded from transport recruitment", Dkc2CoopCannonIndependent(&cpu), true);
  ExpectU16("lost partner excluded from paired launch", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4000), 0);
  ExpectU16("cannon ownership preserves other actor position", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  WriteWord(0x0FB8, 0x1E4);
  ExpectBool("other prop keeps native entry setup", Dkc2CoopCannonIndependent(&cpu), false);
  ExpectU16("non-cannon entry target remains native", Dkc2CoopCannonTargetValue(&cpu, kDkc2CoopSlotB), kDkc2CoopSlotB);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectBool("classic cannon keeps follower script", Dkc2CoopCannonIndependent(&cpu), false);
}

static void TestGoalOwnership(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t winner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t leader = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0593, leader); WriteWord(0x0597, winner);
    WriteWord(0x0595, 0x16B2); WriteWord(0x0599, 0x16D8);
    WriteWord(0x0064, 0x0F5A); WriteWord(0x0F5A, 0x160); WriteWord(0x0F88, 1);
    WriteWord(winner + 0x1E, 0); WriteWord(winner + 0x24, 0x200);
    ExpectU16("goal gate admits descending independent follower", Dkc2CoopGoalCandidateValue(&cpu, leader), winner);
    ExpectBool("goal uses native two-Kong collision pass", Dkc2CoopUseBothMountColliders(&cpu), true);
    cpu.X = leader;
    ExpectU16("grounded leader cannot mask or trigger airborne goal contact", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
    cpu.X = winner;
    ExpectU16("descending follower retains native collision flags", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0x1E);
    WriteWord(winner + 0x24, 0xFF00);
    ExpectU16("ascending follower cannot trigger goal", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
    WriteWord(winner + 0x24, 0x200);
    WriteWord(0x006A, winner); cpu._flag_C = 0;
    (void)Dkc2CoopGoalAcceptedValue(&cpu, 0x0F5A);
    ExpectU16("miss does not change leader", cpu_read16(&cpu, 0, 0x0593), leader);
    WriteWord(0x09AF, 0x1111); WriteWord(0x09C7, 0x2222);
    cpu._flag_C = 1;
    ExpectU16("accepted goal retains operand", Dkc2CoopGoalAcceptedValue(&cpu, 0x0F5A), 0x0F5A);
    ExpectU16("native winner owns end sequence", cpu_read16(&cpu, 0, 0x0593), winner);
    ExpectU16("winner work block follows leadership", cpu_read16(&cpu, 0, 0x0595), 0x16D8);
    ExpectU16("collision cache follows winner", cpu_read16(&cpu, 0, 0x09AF), 0x2222);
    ExpectU16("goal does not move winner", cpu_read16(&cpu, 0, winner + 6), who ? 300 : 100);
    Dkc2CoopLoadLostMask(&cpu, (uint8_t)(1u << who), true);
    cpu.X = winner;
    ExpectU16("lost actor cannot trigger goal", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic goal collision unchanged", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0x1E);
    ExpectU16("classic goal gate unchanged", Dkc2CoopGoalCandidateValue(&cpu, leader), leader);
  }
  ExpectU16("null goal gate safe", Dkc2CoopGoalCandidateValue(NULL, 7), 7);
  ExpectU16("null goal accepted safe", Dkc2CoopGoalAcceptedValue(NULL, 7), 7);
}

static uint8_t RescueTick(CpuState *cpu) {
  return Dkc2CoopRescueFrame(cpu,
      (cpu_read16(cpu, 0, 0x0502) & 0x2000) != 0,
      (cpu_read16(cpu, 0, 0x0504) & 0x2000) != 0);
}

static unsigned RescueFrames(CpuState *cpu, unsigned frames) {
  unsigned moved = 0;
  for (unsigned i = 0; i < frames; ++i) moved |= RescueTick(cpu);
  return moved;
}

static void TestSelectRescue(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    cpu.D = 0x100; cpu.DB = 0xB8;
    uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    uint16_t target = who == 0 ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t input = who == 0 ? 0x0502 : 0x0504;
    WriteWord(input, 0x2000);
    WriteWord(slot + 0x20, 0x300);
    WriteWord(slot + 0x24, 0x600);
    WriteWord(slot + 0x26, 0x300);
    WriteWord(slot + 0x2E, 1); /* falling player keeps its native action */
    WriteWord(slot + 0x3C, 0x1234);
    ExpectU16("rescue never fires before the configured hold", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
    ExpectU16("configured hold moves its own player", RescueTick(&cpu), (uint16_t)(1u << who));
    ExpectU16("rescue places player beside teammate", cpu_read16(&cpu, 0, slot + 6), who == 0 ? 284 : 116);
    ExpectU16("rescue preserves teammate", cpu_read16(&cpu, 0, target + 6), who == 0 ? 300 : 100);
    ExpectU16("rescue matches teammate height", cpu_read16(&cpu, 0, slot + 10), 200);
    ExpectU16("rescue cancels downward momentum", cpu_read16(&cpu, 0, slot + 0x24), 0);
    ExpectU16("rescue cancels horizontal momentum", cpu_read16(&cpu, 0, slot + 0x20), 0);
    ExpectU16("rescue cancels horizontal movement", cpu_read16(&cpu, 0, slot + 0x26), 0);
    ExpectU16("rescue preserves action", cpu_read16(&cpu, 0, slot + 0x2E), 1);
    ExpectU16("rescue preserves animation cursor", cpu_read16(&cpu, 0, slot + 0x3C), 0x1234);
    WriteWord(slot + 6, 50);
    ExpectU16("held Select fires only once", RescueFrames(&cpu, 400), 0);
    ExpectU16("continued hold leaves position alone", cpu_read16(&cpu, 0, slot + 6), 50);
    WriteWord(0x08C2, 0x4040); (void)RescueTick(&cpu); WriteWord(0x08C2, 0x4000);
    ExpectU16("pause does not rearm an already fired hold", RescueFrames(&cpu, 400), 0);
    WriteWord(input, 0);
    (void)RescueTick(&cpu);
    WriteWord(input, 0x2000);
    ExpectU16("release permits another complete hold", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), (uint16_t)(1u << who));
  }
  RescueSetup(&cpu);
  WriteWord(0x0502, 0x2000);
  (void)RescueFrames(&cpu, kDkc2CoopRescueHoldFrames / 2);
  WriteWord(0x0502, 0); (void)RescueTick(&cpu);
  WriteWord(0x0502, 0x2000);
  ExpectU16("separate short holds do not add up", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
  ExpectU16("new uninterrupted hold fires", RescueTick(&cpu), 1);

  for (unsigned interruption = 0; interruption < 5; ++interruption) {
    RescueSetup(&cpu);
    WriteWord(0x0502, 0x2000);
    (void)RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1);
    if (interruption == 0) {
      WriteWord(0x08C2, 0x4040); (void)RescueTick(&cpu); WriteWord(0x08C2, 0x4000);
    } else if (interruption == 1) {
      Dkc2CoopLoadLostMask(&cpu, 0, true);
    } else if (interruption == 2) {
      WriteWord(0x24, 0); (void)RescueTick(&cpu); WriteWord(0x24, 0x8819);
    } else if (interruption == 3) {
      WriteWord(0xD3, 2);
    } else {
      Dkc2CoopRescuePause();
    }
    ExpectU16("pause/load/gameplay/level change resets hold", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
    ExpectU16("reset hold can charge again", RescueTick(&cpu), 1);
  }
  RescueSetup(&cpu);
  WriteWord(0x0502, 0x2000); WriteWord(0x0504, 0x2000);
  ExpectU16("both holding rescues camera follower once", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 2);
  ExpectU16("both holding leaves camera leader in place", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  ExpectU16("both holding never ping-pongs", RescueFrames(&cpu, 400), 0);

  RescueSetup(&cpu);
  WriteWord(0x0502, 0x2000); WriteWord(kDkc2CoopSlotB + 0x1E, 0);
  ExpectU16("waits for grounded destination", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
  WriteWord(kDkc2CoopSlotB + 0x1E, 1);
  ExpectU16("charged rescue fires when teammate lands", RescueTick(&cpu), 1);

  for (unsigned blocked = 0; blocked < 8; ++blocked) {
    RescueSetup(&cpu); WriteWord(0x0502, 0x2000);
    if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
    if (blocked == 1) WriteWord(0x060D, 0);
    if (blocked == 2) WriteWord(0x060D, 2);
    if (blocked == 3) Dkc2CoopLoadLostMask(&cpu, 1, true);
    if (blocked == 4) WriteWord(0x006C, kDkc2CoopSlotA);
    if (blocked == 5) WriteWord(0x0D7A, kDkc2CoopSlotB);
    if (blocked == 6) WriteWord(kDkc2CoopSlotA + 0x2E, 0x24);
    if (blocked == 7) WriteWord(kDkc2CoopSlotB, 0);
    ExpectU16("ineligible modes and actors cannot teleport", RescueFrames(&cpu, 200), 0);
    ExpectU16("blocked rescue leaves actor in place", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  }
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned variation = 0; variation < 7; ++variation) {
      RescueSetup(&cpu);
      uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      uint16_t rider = who == 0 ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      WriteWord(0x0593, rider); WriteWord(0x0597, slot);
      WriteWord(0x006C, 0x0FB8); WriteWord(0x006E, 0x19C);
      WriteWord(0x0FB8, 0x19C);
      WriteWord(rider + 0x2E, 9);
      WriteWord(slot + 0x2E, 0x13);
      WriteWord(slot + 0x3E, 0xE000);
      WriteWord(who == 0 ? 0x0502 : 0x0504, 0x2000);
      if (variation == 1) Dkc2CoopLoadLostMask(&cpu, (uint8_t)(1u << who), true);
      if (variation == 2) WriteWord(slot + 0x30, 0);
      if (variation == 3) WriteWord(rider + 0x1E, 0);
      if (variation == 4) WriteWord(0x006E, 0x198);
      if (variation == 5) WriteWord(0x0FB8, 0);
      if (variation == 6) WriteWord(rider + 0x2E, 0x0B);
      ExpectU16("mounted partner rescue waits the configured duration", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
      ExpectU16("only live waiting partner beside grounded Rambi qualifies", RescueTick(&cpu), variation == 0 ? (uint16_t)(1u << who) : 0);
      ExpectU16("rescue preserves Rambi ownership", cpu_read16(&cpu, 0, 0x006C), 0x0FB8);
      ExpectU16("rescue preserves rider state", cpu_read16(&cpu, 0, rider + 0x2E), variation == 6 ? 0x0B : 9);
      ExpectU16("rescue preserves camera leader", cpu_read16(&cpu, 0, 0x0593), rider);
      if (variation == 0) {
        ExpectU16("waiting rescue restores normal movement", cpu_read16(&cpu, 0, slot + 0x2E), 0);
        ExpectU16("waiting rescue removes stale callback", cpu_read16(&cpu, 0, slot + 0x3E), 0);
        ExpectU16("mounted destination matches rider height", cpu_read16(&cpu, 0, slot + 10), 200);
        WriteWord(who == 0 ? 0x0502 : 0x0504, 0);
        WriteWord(who == 0 ? 0x0504 : 0x0502, 0x2000);
        ExpectU16("rider cannot teleport the mounted animal", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
      }
    }
  }
  for (unsigned who = 0; who < 2; ++who) {
    for (uint16_t state = 0x35; state <= 0x38; ++state) {
      RescueSetup(&cpu);
      uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      uint16_t animation = who == 0 ? 1 : 0xA4;
      WriteWord((uint16_t)(animation * 4), 0x8123);
      WriteWord((uint16_t)(animation * 4 + 2), 0x00B9);
      WriteWord(who == 0 ? 0x0502 : 0x0504, 0x2000);
      WriteWord(slot + 0x2E, state);
      WriteWord(slot + 0x32, 7);
      WriteWord(slot + 0x3E, 0xDD63);
      ExpectU16("rope rescue waits the configured duration", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
      ExpectU16("either Kong can leave every rope state", RescueTick(&cpu), (uint16_t)(1u << who));
      ExpectU16("rope rescue enters normal state", cpu_read16(&cpu, 0, slot + 0x2E), 0);
      ExpectU16("rope rescue clears rope action", cpu_read16(&cpu, 0, slot + 0x32), 0);
      ExpectU16("rope rescue selects own character animation", cpu_read16(&cpu, 0, slot + 0x36), animation);
      ExpectU16("rope rescue initializes ROM animation cursor", cpu_read16(&cpu, 0, slot + 0x3C), 0x8123);
      ExpectU16("rope rescue removes rope callback", cpu_read16(&cpu, 0, slot + 0x3E), 0);
      ExpectU16("rope rescue initializes script bank", cpu_read16(&cpu, 0, slot + 0x40), 0xB9);
    }
  }
  RescueSetup(&cpu);
  WriteWord(0x0502, 0x2000);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x35);
  ExpectU16("rope cannot be the destination even with stale ground flags", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
  for (unsigned who = 0; who < 2; ++who) {
    const uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    const uint8_t bit = (uint8_t)(1u << who);
    const uint16_t states[] = {0, 1, 2, 4, 6, 0x0C, 0x0D, 0x0E, 0x35, 0x36, 0x37, 0x38, 0x3C, 0x3D, 0x41, 0x42, 0x43, 0x44, 0x45, 0x7A};
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i) {
      RescueSetup(&cpu);
      Dkc2CoopLoadLostMask(&cpu, 3, true);
      cpu.X = slot;
      (void)Dkc2CoopSelectStateWord(&cpu, states[i]);
      ExpectU16("live restored actor clears only its stale lost bit", Dkc2CoopSaveLostMask(), (uint16_t)(3 & ~bit));
    }
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i) {
      RescueSetup(&cpu); WriteWord(0x08C2, 0);
      WriteWord(0x0593, slot); WriteWord(0x0597, who ? kDkc2CoopSlotA : kDkc2CoopSlotB);
      Dkc2CoopLoadLostMask(&cpu, 3, true); cpu.X = slot;
      (void)Dkc2CoopSelectStateWord(&cpu, states[i]);
      ExpectU16("single living respawn clears only its stale loss", Dkc2CoopSaveLostMask(), (uint16_t)(3 & ~bit));
    }
    for (unsigned missing = 0; missing < 4; ++missing) {
      RescueSetup(&cpu);
      Dkc2CoopLoadLostMask(&cpu, bit, true);
      cpu.X = slot;
      if (missing == 0) {
        WriteWord(0x08C2, 0);
        WriteWord(0x0593, who ? kDkc2CoopSlotA : kDkc2CoopSlotB);
      }
      if (missing == 1) WriteWord(slot + 0x30, 0);
      if (missing == 2) WriteWord(slot, 0);
      (void)Dkc2CoopSelectStateWord(&cpu, missing == 3 ? 0x13 : 0);
      ExpectU16("absent dead or waiting Kong retains lost bit", Dkc2CoopSaveLostMask(), bit);
    }
  }
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned direction = 0; direction < 3; ++direction) {
      RescueSetup(&cpu);
      uint16_t slot = who == 0 ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      uint16_t target = who == 0 ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      bool target_water = direction != 0;
      uint16_t animation = (uint16_t)((target_water ? 0x43 : 1) + (who ? 0xA3 : 0));
      WriteWord(0x0595, 0x16B2); WriteWord(0x0599, 0x16D8);
      WriteWord(slot + 0x54, 0x9000);
      WriteWord(0x9076, 0x40); WriteWord(0x9078, 0x180);
      WriteWord(who == 0 ? 0x0502 : 0x0504, 0x2000);
      WriteWord(slot + 0x2E, direction == 1 ? 0 : 0x3D);
      WriteWord(target + 0x2E, target_water ? 0x3D : 0);
      WriteWord(target + 0x1E, target_water ? 0 : 1);
      WriteWord((uint16_t)(animation * 4), 0x8456);
      WriteWord((uint16_t)(animation * 4 + 2), 0x00B9);
      WriteWord(slot + 0x3E, 0x1234);
      ExpectU16("water rescue still waits the configured duration", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
      ExpectU16("either Kong can rescue into or out of water", RescueTick(&cpu), (uint16_t)(1u << who));
      ExpectU16("water rescue enters destination movement state", cpu_read16(&cpu, 0, slot + 0x2E), target_water ? 0x3D : 0);
      ExpectU16("water rescue preserves target state", cpu_read16(&cpu, 0, target + 0x2E), target_water ? 0x3D : 0);
      if (direction != 2) {
        ExpectU16("water boundary selects own destination animation", cpu_read16(&cpu, 0, slot + 0x36), animation);
        ExpectU16("water boundary drops old callbacks", cpu_read16(&cpu, 0, slot + 0x3E), 0);
        if (target_water) {
          uint16_t work = who ? 0x16D8 : 0x16B2;
          ExpectU16("water entry uses own swim gravity", cpu_read16(&cpu, 0, work + 8), 0x40);
          ExpectU16("water entry uses own swim terminal speed", cpu_read16(&cpu, 0, work + 10), 0x180);
        }
      } else {
        ExpectU16("swim to swim preserves animation callback", cpu_read16(&cpu, 0, slot + 0x3E), 0x1234);
      }
    }
  }
  RescueSetup(&cpu);
  WriteWord(0x0502, 0x2000);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x3D);
  ExpectU16("water entry rejects missing work context", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
  ExpectU16("invalid water work leaves actor untouched", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  RescueSetup(&cpu);
  WriteWord(0x0502, 0x6000);
  WriteWord(kDkc2CoopSlotA + 0x2E, 0x0C);
  WriteWord(0x0D7A, 0x0F5A); WriteWord(0x0F5A, 0x1A8);
  WriteWord(0x0F60, 112); WriteWord(0x0F64, 180);
  ExpectU16("item carrier can rescue", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 1);
  ExpectU16("rescue translates held item with owner", cpu_read16(&cpu, 0, 0x0F60), 296);
  ExpectU16("rescue preserves held item height offset", cpu_read16(&cpu, 0, 0x0F64), 180);
  ExpectU16("rescue retains shared held slot", cpu_read16(&cpu, 0, 0x0D7A), 0x0F5A);
  ExpectU16("Select is reserved without masking other held inputs",
            Dkc2CoopSelectHeld(0, kDkc2CoopSlotA, 1, true, 0x6100, 0), 0x4100);
  ExpectU16("Select cannot trigger the native tag action",
            Dkc2CoopSelectPressed(0, kDkc2CoopSlotB, 1, true, 0, 0xA000), 0x8000);
  RescueSetup(&cpu);
  unsigned early = 0;
  for (unsigned i = 0; i < kDkc2CoopRescueHoldFrames - 1; ++i)
    early |= Dkc2CoopRescueFrame(&cpu, true, false);
  ExpectU16("port Select timing ignores stale guest words", (uint16_t)early, 0);
  ExpectU16("port Select fires without a guest input poll", Dkc2CoopRescueFrame(&cpu, true, false), 1);
  WriteWord(0x0502, 0x2000); /* stale guest-held word during a lag frame */
  (void)Dkc2CoopRescueFrame(&cpu, false, false);
  ExpectU16("one-frame port release rearms despite stale guest word", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 1);
  ExpectU16("null rescue is inert", RescueTick(NULL), 0);
}

static void TestCatGrabOwnership(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned variant = 0; variant < 5; ++variant) {
      RescueSetup(&cpu);
      uint16_t victim = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      WriteWord(0x593, other); WriteWord(0x597, victim);
      WriteWord(0xE9E, variant == 2 ? 0x1E4 : 0x204);
      WriteWord(0xA82, variant == 3 ? 0x1B : 0x13); WriteWord(0xA84, 0xE9E);
      WriteWord(0x64, 0xE9E); WriteWord(0x6A, victim);
      if (variant == 4) Dkc2CoopSetMode(kDkc2CoopClassic);
      Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
      WriteWord(0x6A, other); /* Later collision checks must not steal ownership. */
      WriteWord(0xA82, 0); /* Native dispatch consumes its pending reaction. */
      if (variant == 1) Dkc2CoopResetSession();
      Dkc2CoopCatCaptureOwner(&cpu);
      ExpectU16("cat grab belongs only to accepted collider", cpu_read16(&cpu, 0, 0x593),
                variant == 0 ? victim : other);
      if (variant == 0) {
        WriteWord(victim + 0x2E, 0x48);
        WriteWord(0x593, other); WriteWord(0x597, victim);
        ExpectU16("native captured state persists spin owner despite other promotion",
                  Dkc2CoopCatOwnerValue(&cpu, other), victim);
        ExpectBool("cat skips native teammate recruitment", Dkc2CoopCatIndependent(&cpu), true);
        WriteWord(0xAE8, 0xE9E); WriteWord(0x24, 0x8819); cpu.Y = victim;
        ExpectU16("cat camera selects actual captured actor", Dkc2CoopCatCameraValue(&cpu, 0xE9E), victim);
        ExpectU16("cat camera includes distant teammate midpoint", Dkc2CoopCameraXValue(&cpu, 900), 200);
        ExpectU16("cat never moves other actor in policy", cpu_read16(&cpu, 0, other + 6), who ? 100 : 300);
        Dkc2CoopCatCaptureOwner(&cpu);
        ExpectU16("capture ownership is consumed once", cpu_read16(&cpu, 0, 0x593), other);
        Dkc2CoopCatReleaseOwner(&cpu);
        ExpectU16("cat launch follows captured owner after unrelated promotion", cpu_read16(&cpu, 0, 0x593), victim);
      }
    }
  }
}

static void TestSeparateHookContacts(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t leader = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x593, leader); WriteWord(0x597, other);
    WriteWord(leader + 0x2E, 0x3C); WriteWord(leader + 6, 1600); WriteWord(leader + 10, 455);
    WriteWord(other + 0x2E, 0x3C); WriteWord(other + 0x30, 0x16); cpu.X = other;
    ExpectU16("saved thrown hook occupant stays hanging", Dkc2CoopSelectStateWord(&cpu, 0x3C), 0x3C);
    ExpectU16("saved throw regains future hook interaction", cpu_read16(&cpu, 0, other + 0x30), 0x1E);
    WriteWord(other + 0x2E, 6); WriteWord(0x6A, other);
    WriteWord(0x64, 0x1016); WriteWord(0x1016, 0x14C);
    WriteWord(0x1016 + 6, 1704); WriteWord(0x1016 + 10, 512);
    ExpectU16("hanging leader cannot block another hook", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
    ExpectBool("different target has no occupant to share", Dkc2CoopHookShared(&cpu), false);
    ExpectU16("different hook uses actual collider callback", Dkc2CoopHookCallbackValue(&cpu, 0x98BD), 0x9914);
    ExpectU16("new hook promotes grabbing player", cpu_read16(&cpu, 0, 0x593), other);
    ExpectU16("first hook occupant keeps pose", cpu_read16(&cpu, 0, leader + 0x2E), 0x3C);
    WriteWord(0x593, leader); WriteWord(0x597, other); WriteWord(leader + 0x2E, 0x20);
    ExpectU16("unrelated busy action remains blocked", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 0x81);
    WriteWord(leader + 0x2E, 0x3C); Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic retains global hanger gate", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 0x81);
  }
}

static void TestThrownHookContact(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t thrown = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t carrier = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x593, carrier); WriteWord(0x597, thrown);
    WriteWord(0x64, 0xFB8); WriteWord(0xFB8, 0x14C); WriteWord(0xFB8 + 0x4E, 0xE8E8);
    WriteWord(0x6A, thrown); WriteWord(carrier + 0x2E, 0x20); WriteWord(thrown + 0x2E, 0x1F);
    cpu.X = thrown;
    ExpectU16("thrown port receives hook interaction mask", Dkc2CoopGoalCollisionFlags(&cpu, 6), 14);
    ExpectU16("waiting carrier cannot block hook contact", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
    ExpectU16("accepted thrown collider uses direct native hanger callback",
              Dkc2CoopHookCallbackValue(&cpu, 0x98BD), 0x9914);
    ExpectU16("accepted collider owns direct attachment", cpu_read16(&cpu, 0, 0x593), thrown);
    ExpectU16("attachment leaves thrower position intact", cpu_read16(&cpu, 0, carrier + 6), who ? 100 : 300);
    WriteWord(0x6A, 0xFB8);
    ExpectU16("invalid callback source keeps native path", Dkc2CoopHookCallbackValue(&cpu, 0x98BD), 0x98BD);
    WriteWord(0x6A, thrown); Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic retains follower animation", Dkc2CoopHookCallbackValue(&cpu, 0x98BD), 0x98BD);
    ExpectU16("classic thrown collision unchanged", Dkc2CoopGoalCollisionFlags(&cpu, 6), 6);
  }
}

static void TestBonusArrivalAndHooks(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t mover = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t leader = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0593, leader); WriteWord(0x0597, mover);
    WriteWord(0x0515, 1); WriteWord(0x0A84, 0x0FB8);
    WriteWord(0x0FB8, 0x140); WriteWord(0x0FBE, 464); WriteWord(0x0FC2, 647);
    WriteWord(0x0533, 464); WriteWord(0x0535, 647);
    WriteWord(leader + 6, 464); WriteWord(leader + 10, 589);
    WriteWord(mover + 6, 432); WriteWord(mover + 10, 682);
    WriteWord(mover + 0x2E, 0x13); cpu.X = mover;
    ExpectU16("bonus arrival restores either player without input", Dkc2CoopSelectStateWord(&cpu, 0x13), 0);
    ExpectU16("bonus partner is placed on arrival platform", cpu_read16(&cpu, 0, mover + 10), 589);
    ExpectU16("bonus arrival uses beside-player offset", cpu_read16(&cpu, 0, mover + 6), 448);
    ExpectU16("bonus arrival does not move leader", cpu_read16(&cpu, 0, leader + 6), 464);
    WriteWord(0x0515, 0); WriteWord(0x0FB8 + 0x4E, 0x40);
    WriteWord(0x0FBE, 360); WriteWord(0x0FC2, 632);
    WriteWord(mover + 6, 360); WriteWord(mover + 10, 740);
    WriteWord(mover + 0x2E, 0x13);
    ExpectU16("team barrel launch restores waiting partner", Dkc2CoopSelectStateWord(&cpu, 0x13), 0);
    ExpectU16("team barrel partner lands beside carrier", cpu_read16(&cpu, 0, mover + 10), 589);
    WriteWord(0x0515, 1); WriteWord(0x0FBE, 464); WriteWord(0x0FC2, 647);
    WriteWord(mover + 0x2E, 0x13);
    Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
    ExpectU16("bonus does not revive a lost partner", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
  }
  RescueSetup(&cpu); cpu.X = kDkc2CoopSlotB;
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x13);
  ExpectU16("ordinary waiting still requires input", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
  WriteWord(0x0064, 0x0EFC); WriteWord(0x0A84, 0x0EFC); WriteWord(0x0EFC, 0x14C);
  ExpectU16("hook entry animation targets the prop", Dkc2CoopCannonTargetValue(&cpu, kDkc2CoopSlotA), 0x0EFC);
  ExpectBool("free hook has no current occupant", Dkc2CoopHookShared(&cpu), false);
  ExpectBool("follower hook entry stays independent", Dkc2CoopHookIndependent(&cpu), true);
  ExpectU16("touching follower owns hook", cpu_read16(&cpu, 0, 0x0593), kDkc2CoopSlotB);
  ExpectU16("hook entry preserves other position", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 6), 100);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x3C); WriteWord(0x0EFC + 6, 300); WriteWord(0x0EFC + 10, 177);
  WriteWord(0x0D82, 0x0EFC);
  ExpectBool("occupied fixed hook can check second player", Dkc2CoopHookShared(&cpu), true);
  ExpectU16("hanging leader permits second hook grab", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
  WriteWord(0x0D82, 0);
  ExpectBool("release preserves native hook cooldown", Dkc2CoopHookShared(&cpu), false);
  WriteWord(0x0064, kDkc2CoopSlotB);
  WriteWord(0x0EFC + 0x2E, 0);
  Dkc2CoopHookRelease(&cpu);
  ExpectU16("last occupant re-arms expired native release transition", cpu_read16(&cpu, 0, 0x0EFC + 0x2E), 1);
  WriteWord(0x0EFC + 0x2E, 0);
  WriteWord(0x0EFC + 0x4E, 0x4000);
  Dkc2CoopHookRelease(&cpu);
  ExpectU16("framed hook release re-arms native cooldown", cpu_read16(&cpu, 0, 0x0EFC + 0x2E), 1);
  WriteWord(0x0EFC + 0x2E, 0);
  WriteWord(0x0EFC + 0x4E, 0);
  WriteWord(0x0064, kDkc2CoopSlotA);
  Dkc2CoopHookRelease(&cpu);
  ExpectU16("non-hanging actor cannot re-arm hook", cpu_read16(&cpu, 0, 0x0EFC + 0x2E), 0);
  WriteWord(0x0064, 0x0EFC);
  ExpectU16("remaining occupant permits later native regrab", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
  WriteWord(0x0D82, 0x0EFC);
  ExpectU16("already hanging actor cannot mask new collider", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
  cpu.X = kDkc2CoopSlotA;
  ExpectU16("other live actor retains native hook collision", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0x1E);
  WriteWord(0x0EFC + 0x4E, 0x4000);
  ExpectBool("framed hook waits for camera before scanning", Dkc2CoopHookShared(&cpu), false);
  ExpectBool("framed hook admits second collider after camera", Dkc2CoopHookSharedAfterCamera(&cpu), true);
  WriteWord(kDkc2CoopSlotA + 0x2E, 0x1F);
  ExpectU16("horizontal throw permits real hook hitbox contact", Dkc2CoopGoalCollisionFlags(&cpu, 6), 14);
  WriteWord(kDkc2CoopSlotB + 0x2E, 0x20); WriteWord(0x6A, kDkc2CoopSlotA);
  ExpectU16("own waiting thrower cannot veto sideways hook contact", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
  WriteWord(0x6A, kDkc2CoopSlotB);
  ExpectU16("other collision keeps busy leader rejection", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 0x81);
  Dkc2CoopLoadLostMask(&cpu, 1, true);
  ExpectU16("lost thrown actor cannot grab", Dkc2CoopGoalCollisionFlags(&cpu, 6), 0);
  Dkc2CoopSetMode(kDkc2CoopClassic);
  ExpectBool("classic hook keeps native follower entry", Dkc2CoopHookIndependent(&cpu), false);
}

static void TestMountedHookAndCamera(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t follower = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t rider = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0593, rider); WriteWord(0x0597, follower);
    WriteWord(0x006C, 0x0E9E); WriteWord(0x006E, 0x194); WriteWord(0x0E9E, 0x194);
    WriteWord(rider + 0x2E, 9);
    Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
    cpu.X = rider;
    Dkc2CoopSelectStateWord(&cpu, 9);
    ExpectU16("confirmed live rider clears stale saved loss", Dkc2CoopSaveLostMask(), 0);
    WriteWord(0x0064, 0x0F5A); WriteWord(0x0F5A, 0x14C); WriteWord(0x0FA8, 0x1000);
    ExpectU16("mounted hook checks on-foot collider", Dkc2CoopHookAnimalValue(&cpu, 0x194), 0);
    ExpectU16("rider does not block follower hanger", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
    ExpectBool("mounted hook entry stays independent", Dkc2CoopHookIndependent(&cpu), true);
    ExpectBool("mounted hook selects follower work", Dkc2CoopHookUsesFollower(&cpu), true);
    WriteWord(0x6A, follower);
    ExpectU16("mounted hook uses native direct attachment", Dkc2CoopHookCallbackValue(&cpu, 0x98BD), 0x9914);
    ExpectU16("hook preserves rider leader", cpu_read16(&cpu, 0, 0x0593), rider);
    cpu.X = rider;
    ExpectU16("rider cannot be captured by hook", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
    cpu.X = follower;
    ExpectU16("on-foot partner can be captured", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0x1E);
    WriteWord(0x0064, follower); WriteWord(0x0AFE, 1000);
    ExpectU16("mount camera does not cull safe partner", Dkc2CoopPitCameraY(&cpu, 100), 1000);
    WriteWord(rider + 0x2E, 0x62);
    ExpectU16("Rattly superjump preserves partner cutoff", Dkc2CoopPitCameraY(&cpu, 100), 1000);
    WriteWord(0x0064, rider);
    ExpectU16("rider keeps native pit cutoff", Dkc2CoopPitCameraY(&cpu, 100), 100);
    WriteWord(0x0064, follower); WriteWord(rider + 0x2E, 0x62);
    WriteWord(rider + 0x1E, 0);
    WriteWord(follower + 0x2E, 0x13);
    WriteWord(follower + 2, 0xE4);
    WriteWord(who ? 0x0504 : 0x0502, 0x2000);
    cpu.X = follower;
    ExpectU16("Select charge does not wake offscreen wait into pit logic", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
    ExpectU16("Rattly rescue waits the configured duration", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
    ExpectU16("Rattly waiting partner can teleport", RescueTick(&cpu), who ? 2 : 1);
    ExpectU16("Rattly rescue restores movement", cpu_read16(&cpu, 0, follower + 0x2E), 0);
    ExpectU16("Rattly rescue preserves mount", cpu_read16(&cpu, 0, 0x006C), 0x0E9E);
    WriteWord(rider + 0x2E, 9); WriteWord(rider + 0x1E, 0x101);
    WriteWord(follower + 0x2E, 1); WriteWord(follower + 0x1E, 0x80);
    WriteWord(0x17C0, 100);
    ExpectBool("real pit still loses on-foot player beside rider", Dkc2CoopPitSurvivor(&cpu), true);
    ExpectU16("mounted pit retains real loss evidence", Dkc2CoopSaveLostMask(), who ? 2 : 1);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic camera cutoff stays native", Dkc2CoopPitCameraY(&cpu, 100), 100);
  }
}

static void TestMountedPitCleanup(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned kind = 0; kind < 3; ++kind) {
      for (uint16_t pose = 9; pose <= 0x0B; ++pose) {
        RescueSetup(&cpu);
        uint16_t rider = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        uint16_t type = kind == 0 ? 0x194 : kind == 1 ? 0x19C : 0x190;
        WriteWord(0x0593, rider); WriteWord(0x0597, other); WriteWord(0x0064, rider);
        WriteWord(rider + 0x2E, pose); WriteWord(0x006C, 0x0FB8); WriteWord(0x006E, type);
        WriteWord(0x0FB8, type); WriteWord(0x0FB8 + 0x2E, 1);
        WriteWord(rider + 0x12, 0x2600); WriteWord(0x0FB8 + 0x12, 0x6200);
        WriteWord(other + 0x12, 0x4400); WriteWord(0x08C2, 0x4200); WriteWord(0x17C0, 100);
        ExpectBool("mounted pit leaves teammate alive", Dkc2CoopPitSurvivor(&cpu), true);
        ExpectU16("fallen rider cannot transfer mount", cpu_read16(&cpu, 0, 0x006C), 0);
        ExpectU16("survivor uses on-foot physics", cpu_read16(&cpu, 0, 0x006E), 0);
        ExpectU16("mount flag clears with dead rider", cpu_read16(&cpu, 0, 0x08C2) & 0x200, 0);
        ExpectU16("survivor retains own palette", cpu_read16(&cpu, 0, other + 0x12), 0x4400);
        ExpectU16("fallen Kong regains original palette index", cpu_read16(&cpu, 0, rider + 0x12), 0x2200);
        ExpectU16("abandoned animal uses unmounted lifecycle", cpu_read16(&cpu, 0, 0x0FB8 + 0x2E), 0);
        WriteWord(rider + 0x2E, 0x13);
        WriteWord(0x006C, 0x0FB8); WriteWord(0x006E, type); WriteWord(0x08C2, 0x200);
        cpu.X = other;
        Dkc2CoopSelectStateWord(&cpu, 0);
        ExpectU16("older mounted-loss snapshot releases stale context", cpu_read16(&cpu, 0, 0x006E), 0);
        ExpectU16("older repair preserves survivor state", cpu_read16(&cpu, 0, other + 0x2E), 0);
      }
    }
  }
}

static void TestWaterHeldOwnership(void) {
  CpuState cpu;
  const uint16_t types[] = {0x1DC, 0x1A8, 0x11C};
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t owner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t swimmer = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    for (unsigned type = 0; type < sizeof types / sizeof types[0]; ++type) {
      RescueSetup(&cpu);
      WriteWord(0xE9E, types[type]); WriteWord(0x0D7A, 0xE9E);
      WriteWord(owner + 0x2E, 0x0C); WriteWord(swimmer + 0x2E, 1);
      Dkc2CoopResetSession(); /* Reconstruct ownership without pickup history. */
      WriteWord(0x0064, swimmer);
      ExpectU16("water entry cannot drop teammate's grabbable", Dkc2CoopHeldObjectValue(&cpu, 0xE9E), 0);
      WriteWord(swimmer + 0x2E, 0x3D);
      ExpectU16("swimming cannot drop teammate's grabbable", Dkc2CoopHeldObjectValue(&cpu, 0xE9E), 0);
      ExpectU16("partner water entry retains shared held slot", cpu_read16(&cpu, 0, 0x0D7A), 0xE9E);
      ExpectU16("partner water entry retains carrier state", cpu_read16(&cpu, 0, owner + 0x2E), 0x0C);
      WriteWord(0x0064, owner);
      ExpectU16("carrier's own water entry retains native drop", Dkc2CoopHeldObjectValue(&cpu, 0xE9E), 0xE9E);
      WriteWord(0x0064, 0xEFC);
      ExpectU16("non-Kong cleanup keeps native global drop", Dkc2CoopHeldObjectValue(&cpu, 0xE9E), 0xE9E);
      Dkc2CoopSetMode(kDkc2CoopClassic); WriteWord(0x0064, swimmer);
      ExpectU16("classic water drop stays native", Dkc2CoopHeldObjectValue(&cpu, 0xE9E), 0xE9E);
    }
  }
}

static void TestAnimalOnlyTurns(void) {
  CpuState cpu;
  for (uint16_t animal = 0x190; animal <= 0x1A0; animal += 4) {
    for (unsigned who = 0; who < 2; ++who) {
      uint16_t owner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      RescueSetup(&cpu);
      WriteWord(0x0593, owner); WriteWord(0x0597, other);
      WriteWord(0x006E, animal); WriteWord(0x08C2, 0x4200);
      WriteWord(0x08A2, who ? 2 : 1); WriteWord(0x08A4, who);
      WriteWord(0x091B, 0x300);
      WriteWord(kDkc2CoopP1Held, 0x2180); WriteWord(kDkc2CoopP2Held, 0x2040);
      WriteWord(kDkc2CoopP1Pressed, 0x2800); WriteWord(kDkc2CoopP2Pressed, 0x2400);
      cpu.X = other; WriteWord(0x0064, other); WriteWord(other + 0x2E, 0x13);
      ExpectU16("animal-only input cannot spawn second Kong", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
      ExpectU16("animal-only partner stays hidden", cpu_read16(&cpu, 0, other + 0x30), 0);
      ExpectU16("animal-only dormant held input is suppressed", Dkc2CoopSelectHeldWord(&cpu, 0xFFFF), 0);
      ExpectU16("animal-only dormant pressed input is suppressed", Dkc2CoopSelectPressedWord(&cpu, 0xFFFF), 0);
      ExpectU16("animal-only input gate retains native actor", Dkc2CoopGateActiveValue(&cpu, owner), owner);
      ExpectU16("animal-only Select cannot rescue hidden partner", Dkc2CoopRescueFrame(&cpu, true, true), 0);
      cpu.X = owner; WriteWord(0x0064, owner);
      ExpectU16("animal initially uses native owner port", Dkc2CoopSelectHeldWord(&cpu, 0), who ? 0x40 : 0x180);
      for (unsigned hits = 1; hits <= 4; ++hits) {
        WriteWord(0x0A82, 0x23); WriteWord(0x0A84, 0xE9E);
        Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
        Dkc2CoopAnimalOnlyHit(&cpu);
        bool p2 = (who != 0) != ((hits & 1) != 0);
        ExpectU16("each accepted animal hit switches controller", cpu_read16(&cpu, 0, 0x08A2), p2 ? 2 : 1);
        ExpectU16("animal held input follows damage turn", Dkc2CoopSelectHeldWord(&cpu, 0), p2 ? 0x40 : 0x180);
        ExpectU16("animal pressed input follows damage turn", Dkc2CoopSelectPressedWord(&cpu, 0), p2 ? 0x400 : 0x800);
        ExpectU16("animal turn does not change native health", cpu_read16(&cpu, 0, 0x091B), 0x300);
        ExpectU16("animal turn does not consume native presence health", cpu_read16(&cpu, 0, 0x08C2), 0x4200);
        ExpectU16("animal turn retains physical actor", cpu_read16(&cpu, 0, 0x0593), owner);
        Dkc2CoopAnimalOnlyHit(&cpu);
        ExpectU16("accepted animal hit changes turns once", cpu_read16(&cpu, 0, 0x08A2), p2 ? 2 : 1);
      }
    }
  }
  for (unsigned blocked = 0; blocked < 5; ++blocked) {
    RescueSetup(&cpu); WriteWord(0x006E, 0x194); WriteWord(0x08C2, 0x4200); WriteWord(0x08A2, 1);
    WriteWord(0x0A82, 0x23); WriteWord(0x0A84, 0xE9E);
    Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
    if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
    if (blocked == 1) WriteWord(0x060D, 0);
    if (blocked == 2) WriteWord(0x006C, 0xE9E);
    if (blocked == 3) WriteWord(0x08C2, 0x4000);
    if (blocked == 4) WriteWord(0x006E, 0x192);
    Dkc2CoopAnimalOnlyHit(&cpu);
    ExpectU16("ordinary mounts/modes do not change animal turns", cpu_read16(&cpu, 0, 0x08A2), 1);
  }
  RescueSetup(&cpu); WriteWord(0x006E, 0x194); WriteWord(0x08C2, 0x4200); WriteWord(0x08A2, 1);
  WriteWord(0x0A82, 0x23); WriteWord(0x0A84, 0xE9E);
  Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
  Dkc2CoopResetSession();
  Dkc2CoopAnimalOnlyHit(&cpu);
  ExpectU16("snapshot reset discards pending animal hit", cpu_read16(&cpu, 0, 0x08A2), 1);
  Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
  WriteWord(0x0A82, 0x1B);
  Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
  Dkc2CoopAnimalOnlyHit(&cpu);
  ExpectU16("replacement reaction discards pending animal hit", cpu_read16(&cpu, 0, 0x08A2), 1);
}

static void TestMountedReturnArrival(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t rider = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    const uint16_t travel[] = {9, 0x0A, 0x0B, 0x11, 0x12, 0x30};
    for (unsigned i = 0; i < sizeof travel / sizeof travel[0]; ++i) {
      RescueSetup(&cpu); cpu.X = partner;
      WriteWord(0x0593, rider); WriteWord(0x0597, partner);
      WriteWord(0x006C, 0xE9E); WriteWord(0x006E, 0x190); WriteWord(0xE9E, 0x190);
      WriteWord(rider + 0x2E, travel[i]); WriteWord(rider + 0x1E, 0);
      WriteWord(partner + 0x2E, 0x22); WriteWord(partner + 0x30, 0);
      ExpectU16("dormant mounted arrival retains native following", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
      WriteWord(partner + 0x2E, 0x2A);
      ExpectU16("mounted arrival idle also waits for safe landing", Dkc2CoopSelectStateWord(&cpu, 0x2A), 0x2A);
      WriteWord(rider + 0x2E, 9); WriteWord(rider + 0x1E, 0x101);
      WriteWord(rider + 6, 500); WriteWord(rider + 10, 200);
      WriteWord(rider + 4, 0x1234); WriteWord(rider + 8, 0x5678);
      WriteWord(partner + 0x20, 0x300); WriteWord(partner + 0x24, 0x400);
      ExpectU16("mounted arrival releases control on safe ground", Dkc2CoopSelectStateWord(&cpu, 0x2A), 0);
      ExpectU16("returning partner lands at rider X", cpu_read16(&cpu, 0, partner + 6), 500);
      ExpectU16("returning partner lands at rider Y", cpu_read16(&cpu, 0, partner + 10), 200);
      ExpectU16("returning partner retains ground subpixels", cpu_read16(&cpu, 0, partner + 4), 0x1234);
      ExpectU16("returning partner regains collision", cpu_read16(&cpu, 0, partner + 0x30), 0x1E);
      ExpectU16("returning partner has no flight drift", cpu_read16(&cpu, 0, partner + 0x20), 0);
      ExpectU16("arrival does not dismount rider", cpu_read16(&cpu, 0, 0x006E), 0x190);
      ExpectU16("arrival preserves rider state", cpu_read16(&cpu, 0, rider + 0x2E), 9);
    }
    for (unsigned blocked = 0; blocked < 5; ++blocked) {
      RescueSetup(&cpu); cpu.X = partner;
      WriteWord(0x0593, rider); WriteWord(0x0597, partner);
      WriteWord(0x006C, 0xE9E); WriteWord(0x006E, 0x190); WriteWord(0xE9E, 0x190);
      WriteWord(rider + 0x2E, 9); WriteWord(rider + 0x1E, 0);
      WriteWord(partner + 0x30, 0); WriteWord(partner + 6, 123);
      if (blocked == 0) WriteWord(partner + 0x30, 0x1E);
      if (blocked == 1) WriteWord(0xE9E, 0);
      if (blocked == 2) WriteWord(0x006C, 0xE9F);
      if (blocked == 3) WriteWord(0x006E, 0x198); /* Bird sharing has its own policy. */
      if (blocked == 4) Dkc2CoopSetMode(kDkc2CoopClassic);
      ExpectU16("only dormant valid mounted arrival waits", Dkc2CoopSelectStateWord(&cpu, 0x22), blocked == 4 ? 0x22 : 0);
      ExpectU16("other contexts do not teleport partner", cpu_read16(&cpu, 0, partner + 6), 123);
    }
    RescueSetup(&cpu); WriteWord(0x0593, rider); WriteWord(0x0597, partner);
    WriteWord(0x006C, 0xE9E); WriteWord(0x006E, 0x190); WriteWord(0xE9E, 0x190);
    WriteWord(0x0A84, 0xEFC); WriteWord(0xEFC, 0x140);
    ExpectBool("mounted cannon keeps complete native setup", Dkc2CoopCannonIndependent(&cpu), false);
    ExpectU16("mounted cannon preserves rider ownership", cpu_read16(&cpu, 0, 0x0593), rider);
  }
}

static void TestBarrelGravity(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t leader = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    for (unsigned swapped = 0; swapped < 2; ++swapped) {
      uint16_t owner_work = swapped ? 0x16D8 : 0x16B2;
      uint16_t partner_work = swapped ? 0x16B2 : 0x16D8;
      RescueSetup(&cpu); cpu.X = partner;
      WriteWord(0x0593, leader); WriteWord(0x0597, partner);
      WriteWord(0x0595, owner_work); WriteWord(0x0599, partner_work);
      WriteWord(0x0A84, 0xE9E); WriteWord(0xE9E, 0x140);
      WriteWord(leader + 0x2E, 0x12); WriteWord(partner + 0x2E, 0x22);
      WriteWord(leader + 0x24, 0xFA00);
      WriteWord(owner_work + 8, 0x53); WriteWord(owner_work + 10, 0x723);
      WriteWord(partner_work + 8, 0x11); WriteWord(partner_work + 10, 0x100);
      ExpectU16("barrel passenger enters native shot state", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x12);
      ExpectU16("barrel passenger inherits shot gravity", cpu_read16(&cpu, 0, partner_work + 8), 0x53);
      ExpectU16("barrel passenger inherits terminal velocity", cpu_read16(&cpu, 0, partner_work + 10), 0x723);
      ExpectU16("barrel passenger keeps native launch impulse", cpu_read16(&cpu, 0, partner + 0x24), 0xFA00);
      ExpectU16("barrel leader physics unchanged", cpu_read16(&cpu, 0, owner_work + 10), 0x723);
    }
    for (unsigned blocked = 0; blocked < 6; ++blocked) {
      RescueSetup(&cpu); cpu.X = partner;
      WriteWord(0x0593, leader); WriteWord(0x0597, partner); WriteWord(0x0599, 0x16D8);
      WriteWord(partner + 0x54, 0x9000);
      WriteWord(0x9000, 0x53); WriteWord(0x9002, 0x723);
      WriteWord(0x16E0, 0x11); WriteWord(0x16E2, 0x100);
      if (blocked == 1) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (blocked == 2) WriteWord(partner + 0x1E, 0);
      if (blocked == 3) WriteWord(0x006E, 0x194);
      if (blocked == 4) WriteWord(partner + 0x54, 0);
      if (blocked == 5) WriteWord(0x0599, 0x9004);
      Dkc2CoopSelectStateWord(&cpu, 0);
      ExpectU16("grounded repair preserves native jump gravity setup", cpu_read16(&cpu, 0, 0x16E0), 0x11);
      ExpectU16("grounded old save restores own terminal speed", cpu_read16(&cpu, 0, 0x16E2), blocked ? 0x100 : 0x723);
    }
  }
}

static void TestRespawnPlacement(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t slot = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t target = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    for (unsigned timing = 0; timing < 3; ++timing) {
      RescueSetup(&cpu); cpu.X = slot;
      WriteWord(slot + 0x2E, 0x3E);
      WriteWord(slot + 0x42, timing == 0 ? 1 : timing == 1 ? 0 : 0xFFFF);
      WriteWord(slot + 0x48, 999); WriteWord(slot + 0x4A, 888);
      WriteWord(slot + 0x20, 0xF400); WriteWord(slot + 0x24, 0x100);
      WriteWord(slot + 0x26, 0x40);
      WriteWord(target + 6, 321); WriteWord(target + 10, 200);
      WriteWord(target + 4, 0x1234); WriteWord(target + 8, 0x5678);
      ExpectU16("respawn retains native arrival state", Dkc2CoopSelectStateWord(&cpu, 0x3E), 0x3E);
      ExpectU16("respawn refreshes current target X only at completion", cpu_read16(&cpu, 0, slot + 0x48), timing ? 321 : 999);
      ExpectU16("respawn refreshes current target Y only at completion", cpu_read16(&cpu, 0, slot + 0x4A), timing ? 200 : 888);
      ExpectU16("respawn stops sideways entrance momentum", cpu_read16(&cpu, 0, slot + 0x20), timing ? 0 : 0xF400);
      ExpectU16("respawn stops entrance acceleration", cpu_read16(&cpu, 0, slot + 0x26), timing ? 0 : 0x40);
      if (timing) {
        ExpectU16("respawn copies target X subpixels", cpu_read16(&cpu, 0, slot + 4), 0x1234);
        ExpectU16("respawn copies target Y subpixels", cpu_read16(&cpu, 0, slot + 8), 0x5678);
      }
      ExpectU16("respawn leaves target X unchanged", cpu_read16(&cpu, 0, target + 6), 321);
      ExpectU16("respawn leaves native timer unchanged", cpu_read16(&cpu, 0, slot + 0x42), timing == 0 ? 1 : timing == 1 ? 0 : 0xFFFF);
    }
    for (unsigned blocked = 0; blocked < 4; ++blocked) {
      RescueSetup(&cpu); cpu.X = slot; WriteWord(slot + 0x48, 999);
      if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (blocked == 1) WriteWord(0x060D, 0);
      if (blocked == 2) WriteWord(target + 0x30, 0);
      if (blocked == 3) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
      Dkc2CoopSelectStateWord(&cpu, 0x3E);
      ExpectU16("respawn without live co-op target stays native", cpu_read16(&cpu, 0, slot + 0x48), 999);
    }
  }
}

static void TestPitSurvivor(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    uint16_t fallen = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    for (unsigned leader = 0; leader < 2; ++leader) {
      RescueSetup(&cpu); WriteWord(0x0064, fallen);
      WriteWord(0x0593, leader ? fallen : other);
      WriteWord(0x0597, leader ? other : fallen);
      WriteWord(fallen + 0x10, 0x103); WriteWord(fallen + 0x1E, 1);
      WriteWord(0x17C0, 100);
      ExpectBool("thorns retain native damage animation", Dkc2CoopPitSurvivor(&cpu), false);
      ExpectU16("thorns target the touching Kong", cpu_read16(&cpu, 0, 0x0593), fallen);
      ExpectU16("thorns do not prematurely mark a loss", Dkc2CoopSaveLostMask(), 0);
      ExpectU16("thorns preserve the teammate", cpu_read16(&cpu, 0, other + 0x30), 0x1E);
    }
    RescueSetup(&cpu); WriteWord(0x0064, fallen);
    WriteWord(0x0593, fallen); WriteWord(0x0597, other);
    WriteWord(0x17C0, 100); WriteWord(other + 10, 200);
    WriteWord(fallen + 10, 450);
    ExpectBool("pit preserves live teammate", Dkc2CoopPitSurvivor(&cpu), true);
    ExpectU16("pit promotes survivor", cpu_read16(&cpu, 0, 0x0593), other);
    ExpectU16("pit preserves survivor position", cpu_read16(&cpu, 0, other + 10), 200);
    ExpectU16("pit removes fallen collision", cpu_read16(&cpu, 0, fallen + 0x30), 0);
    ExpectU16("pit records fallen player for save", Dkc2CoopSaveLostMask(), who ? 2 : 1);
    ExpectU16("pit leaves DK barrel rescue available", cpu_read16(&cpu, 0, 0x08C2) & 0x4000, 0);
    cpu.X = fallen;
    WriteWord(fallen + 0x2E, 0x13); WriteWord(who ? 0x0504 : 0x0502, 0x8000);
    ExpectU16("pit input cannot revive lost actor", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
    Dkc2CoopSelectStateWord(&cpu, 0x3E);
    ExpectU16("native DK barrel clears pit loss", Dkc2CoopSaveLostMask(), 0);
    for (unsigned leader = 0; leader < 2; ++leader) {
      RescueSetup(&cpu); WriteWord(0x0064, fallen); WriteWord(0x17C0, 100);
      WriteWord(0x0593, leader ? fallen : other); WriteWord(0x0597, leader ? other : fallen);
      WriteWord(other + 0x2E, 0x45); WriteWord(other + 0x4C, 0xE9E);
      WriteWord(other + 0x36, 0x2F); WriteWord(other + 0x3C, 0x8123);
      ExpectBool("pit preserves live pole climber with either leader/faller", Dkc2CoopPitSurvivor(&cpu), true);
      ExpectU16("pole survivor becomes leader", cpu_read16(&cpu, 0, 0x0593), other);
      ExpectU16("pole survivor retains native hanging state", cpu_read16(&cpu, 0, other + 0x2E), 0x45);
      ExpectU16("pole survivor retains its anchor", cpu_read16(&cpu, 0, other + 0x4C), 0xE9E);
      ExpectU16("pole survivor retains animation cursor", cpu_read16(&cpu, 0, other + 0x3C), 0x8123);
      ExpectU16("pole pit records only fallen Kong", Dkc2CoopSaveLostMask(), who ? 2 : 1);
      ExpectU16("pole pit removes fallen collision", cpu_read16(&cpu, 0, fallen + 0x30), 0);
    }
    const uint16_t honey_states[] = {0x41, 0x42, 0x43, 0x44, 0x7A};
    for (unsigned leader = 0; leader < 2; ++leader) {
      for (unsigned i = 0; i < sizeof honey_states / sizeof honey_states[0]; ++i) {
        RescueSetup(&cpu); WriteWord(0x0064, fallen); WriteWord(0x17C0, 100);
        WriteWord(0x0593, leader ? fallen : other); WriteWord(0x0597, leader ? other : fallen);
        WriteWord(other + 0x2E, honey_states[i]);
        WriteWord(other + 0x1E, honey_states[i] == 0x41 ? 2 : 1);
        WriteWord(other + 0x36, 0x2B); WriteWord(other + 0x3C, 0x8123);
        ExpectBool("pit recognizes either live honey survivor", Dkc2CoopPitSurvivor(&cpu), true);
        ExpectU16("honey survivor becomes leader", cpu_read16(&cpu, 0, 0x0593), other);
        ExpectU16("honey survivor preserves attachment/action", cpu_read16(&cpu, 0, other + 0x2E), honey_states[i]);
        ExpectU16("honey survivor preserves animation", cpu_read16(&cpu, 0, other + 0x3C), 0x8123);
      }
    }
    for (unsigned blocked = 0; blocked < 7; ++blocked) {
      RescueSetup(&cpu); WriteWord(0x0064, fallen); WriteWord(0x17C0, 100);
      WriteWord(other + 0x2E, 0x45);
      if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (blocked == 1) WriteWord(other + 0x30, 0);
      if (blocked == 2) WriteWord(other + 0x2E, 0x3B);
      if (blocked == 3) WriteWord(other + 0x1E, 0x80);
      if (blocked == 4) WriteWord(other + 10, 388);
      if (blocked == 5) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
      if (blocked == 6) WriteWord(0x0515, 1);
      ExpectBool("pit without eligible survivor retains native death", Dkc2CoopPitSurvivor(&cpu), false);
    }
  }
}

static void TestKleeverSharedHooks(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t first = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    const uint16_t hook = 0xEFC;
    WriteWord(0x00D3, 0x21);
    WriteWord(0x0064, hook); WriteWord(0x0A84, hook); WriteWord(0x0D82, hook);
    WriteWord(hook, 0x24); WriteWord(hook + 0x36, 0x2AB); WriteWord(hook + 0x4E, 0xE000);
    WriteWord(hook + 6, 1008); WriteWord(hook + 10, 536);
    WriteWord(first + 6, 1008); WriteWord(first + 10, 559); WriteWord(first + 0x2E, 0x3C);
    WriteWord(0x0593, first); WriteWord(0x0597, other);
    ExpectBool("boss hook keeps native occupied camera path", Dkc2CoopHookShared(&cpu), false);
    ExpectBool("boss hook scans second collider after camera", Dkc2CoopHookSharedAfterCamera(&cpu), true);
    ExpectU16("boss hook busy leader admits another grab", Dkc2CoopHookLeaderFlags(&cpu, 0x81), 1);
    cpu.X = first;
    ExpectU16("hanging boss player cannot mask new collider", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0);
    cpu.X = other;
    ExpectU16("other boss player retains hook collisions", Dkc2CoopGoalCollisionFlags(&cpu, 0x1E), 0x1E);
    ExpectU16("boss hook flight targets actual prop", Dkc2CoopCannonTargetValue(&cpu, first), hook);
    ExpectBool("boss follower grab remains independent", Dkc2CoopHookIndependent(&cpu), true);
    ExpectU16("boss touching follower owns accepted grab", cpu_read16(&cpu, 0, 0x0593), other);
    ExpectU16("boss grab preserves hanging partner X", cpu_read16(&cpu, 0, first + 6), 1008);
    WriteWord(other + 6, 1008); WriteWord(other + 10, 559); WriteWord(other + 0x2E, 0x3C);
    WriteWord(0x0064, other); WriteWord(hook + 0x2E, 0);
    Dkc2CoopHookRelease(&cpu);
    ExpectU16("boss release re-arms native occupied transition", cpu_read16(&cpu, 0, hook + 0x2E), 1);
    WriteWord(0x0064, hook); WriteWord(0x0D82, 0);
    ExpectBool("boss shared scan honors native release cooldown", Dkc2CoopHookSharedAfterCamera(&cpu), false);
    WriteWord(0x0D82, hook);
    WriteWord(hook + 0x36, 0x2AA);
    ExpectBool("other generic animation is not a boss hook", Dkc2CoopHookIndependent(&cpu), false);
    WriteWord(hook + 0x36, 0x2AB); WriteWord(0x00D3, 0x22);
    ExpectBool("other level generic actor is not a boss hook", Dkc2CoopHookIndependent(&cpu), false);
    WriteWord(0x00D3, 0x21); WriteWord(hook + 0x4E, 0xC000);
    ExpectBool("other script flags remain native", Dkc2CoopHookIndependent(&cpu), false);
    WriteWord(hook + 0x4E, 0xE000);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectBool("classic boss hook keeps native sharing", Dkc2CoopHookSharedAfterCamera(&cpu), false);
  }
}

static void TestScriptedIntro(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    for (uint16_t scripted = 0x74; scripted <= 0x75; ++scripted) {
      for (unsigned waiting = 0; waiting < 2; ++waiting) {
        RescueSetup(&cpu);
        uint16_t partner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t leader = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        uint16_t state = waiting ? 0x13 : 0x22;
        WriteWord(0x0593, leader);
        WriteWord(0x0597, partner);
        WriteWord(leader + 0x2E, scripted);
        WriteWord(partner + 0x2E, state);
        WriteWord(partner + 0x30, 0);
        WriteWord(partner + 0x24, 0);
        WriteWord(who ? kDkc2CoopP2Held : kDkc2CoopP1Held, 0xFFFF);
        if (waiting) WriteWord(0x08C2, 0x200); /* Native DK-barrel arrival is pending. */
        cpu.X = partner;
        ExpectU16("scripted intro retains native wait/follow despite input", Dkc2CoopSelectStateWord(&cpu, state), state);
        ExpectU16("intro does not activate waiting collisions", cpu_read16(&cpu, 0, partner + 0x30), 0);
        ExpectU16("intro leaves native vertical motion untouched", cpu_read16(&cpu, 0, partner + 0x24), 0);
        WriteWord(leader + 0x2E, 0);
        ExpectU16("intro completion resumes independent control", Dkc2CoopSelectStateWord(&cpu, state), 0);
        ExpectU16("resumed partner has normal collisions", cpu_read16(&cpu, 0, partner + 0x30), 0x1E);
        WriteWord(leader + 0x2E, scripted);
        WriteWord(partner + 0x2E, 0x13);
        Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
        ExpectU16("intro never revives a lost partner", Dkc2CoopSelectStateWord(&cpu, 0x13), 0x13);
        Dkc2CoopSetMode(kDkc2CoopClassic);
        ExpectU16("classic intro keeps native state", Dkc2CoopSelectStateWord(&cpu, state), state);
      }
    }
  }
}

static void TestBarrelAirControl(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t partner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t leader = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0593, leader);
    WriteWord(0x0597, partner);
    WriteWord(0x0A84, 0xE9E);
    WriteWord(0xE9E, 0x140);
    WriteWord(leader + 0x2E, 0x12);
    WriteWord(partner + 0x2E, 0x22);
    WriteWord(leader + 4, 0x1234);
    WriteWord(leader + 8, 0x5678);
    WriteWord(leader + 0x20, 0xFA00);
    WriteWord(leader + 0x24, 0xF800);
    WriteWord(leader + 0x26, 0xFE00);
    WriteWord(leader + 0x42, 18);
    WriteWord(leader + 0x44, 22);
    WriteWord(leader + 0x46, 0xFA00);
    WriteWord(leader + 0x4A, 0x1122);
    WriteWord(leader + 0x4C, 0x3344);
    WriteWord(leader + 0x4E, 1);
    WriteWord(partner + 0x30, 0);
    WriteWord(partner + 0x12, 0x2300);
    WriteWord(partner + 0x36, 0x40);
    cpu.X = partner;
    ExpectU16("both players use native barrel flight even with neutral input", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x12);
    const uint16_t offsets[] = {4, 6, 8, 10, 0x1C, 0x1E, 0x20, 0x24, 0x26, 0x30,
                               0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E};
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
      ExpectU16("partner inherits exact shot parameters and native locks",
                cpu_read16(&cpu, 0, partner + offsets[i]), cpu_read16(&cpu, 0, leader + offsets[i]));
    ExpectU16("shot retains own palette/attributes", cpu_read16(&cpu, 0, partner + 0x12), 0x2300);
    ExpectU16("shot leaves character animation selection to native code", cpu_read16(&cpu, 0, partner + 0x36), 0x40);
    ExpectU16("shot preserves leader selector", cpu_read16(&cpu, 0, 0x0593), leader);
    WriteWord(0x0064, partner);
    ExpectU16("passenger landing uses native idle transition", Dkc2CoopRecoveryFollowerValue(&cpu, partner), 0);
    WriteWord(partner + 0x2E, 0x22);
    WriteWord(leader + 0x48, 1);
    ExpectU16("scripted barrel exit keeps native flight", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
    WriteWord(leader + 0x48, 0);
    WriteWord(0xE9E, 0x13C);
    ExpectU16("transport transition keeps native flight", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
    WriteWord(0xE9E, 0x140);
    Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
    ExpectBool("lost player cannot inherit a shot", Dkc2CoopSelectStateWord(&cpu, 0x22) == 0x12, false);
    Dkc2CoopResetSession();
    Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic barrel flight remains native", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
  }
}

static void TestPoleOwnership(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t kong = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0064, kong); WriteWord(0x0090, 0xFF);
    WriteWord(kong + 6, 100); WriteWord(kong + 10, 200); WriteWord(kong + 0x20, 0x300);
    WriteWord(partner + 6, 300); WriteWord(partner + 10, 200);
    WriteWord(0x9000, (uint16_t)-40); WriteWord(0x9002, 60);
    for (uint16_t pole = 0xEFC; pole <= 0xF5A; pole += 0x5E) {
      WriteWord(pole, 0x2D8); WriteWord(pole + 6, pole == 0xEFC ? 100 : 300);
      WriteWord(pole + 10, 200); WriteWord(pole + 0x54, 0x9000);
      WriteWord(pole + 0x2E, 5);
    }
    ExpectU16("each player finds its own pole contact", Dkc2CoopPolePendingValue(&cpu, 0xF5A), 0xEFC);
    ExpectU16("native pending slot matches the current contact", cpu_read16(&cpu, 0, 0xBA2), 0xEFC);
    ExpectU16("unattached Kong must not inherit another anchor", Dkc2CoopPoleCurrentValue(&cpu, 0xF5A), 0);
    WriteWord(kong + 0x2E, 0x45);
    ExpectU16("standing partner cannot clear this climber's anchor", Dkc2CoopPoleCurrentValue(&cpu, 0), 0xEFC);
    ExpectU16("different pole does not redirect the climber", Dkc2CoopPoleCurrentValue(&cpu, 0xF5A), 0xEFC);
    ExpectU16("pole limits apply to this Kong", Dkc2CoopPolePlayerValue(&cpu, partner), kong);
    WriteWord(kong + 0x2E, 6);
    ExpectU16("pole jump suppresses immediate regrab", Dkc2CoopPoleCurrentValue(&cpu, 0), 0xEFC);
    WriteWord(kong + 6, 120);
    ExpectU16("native no-contact pending pass finds no pole", Dkc2CoopPolePendingValue(&cpu, 0), 0);
    ExpectU16("no-contact pass expires latch even without current-pole callback", cpu_read16(&cpu, 0, kong + 0x4C), 0);
    ExpectU16("leaving pole contact releases the jump latch", Dkc2CoopPoleCurrentValue(&cpu, 0), 0);
    ExpectU16("released latch is stored in guest state", cpu_read16(&cpu, 0, kong + 0x4C), 0);
    WriteWord(kong + 6, 100);
    ExpectU16("fresh approach can grab again", Dkc2CoopPoleCurrentValue(&cpu, 0xF5A), 0);
    ExpectU16("other movement retains native target", Dkc2CoopPolePlayerValue(&cpu, partner), partner);
    WriteWord(0x0A84, partner);
    ExpectU16("entry uses the queued Kong, not the last updated Kong", Dkc2CoopPoleReactionValue(&cpu, 0xEFC), 0xF5A);
    WriteWord(kong + 0x2E, 6);
    WriteWord(kong + 6, 104);
    ExpectU16("fast pole contact rejects native outer X boundary", Dkc2CoopPolePendingValue(&cpu, 0xEFC), 0);
    WriteWord(kong + 6, 103);
    ExpectU16("fast pole contact accepts inside native X boundary", Dkc2CoopPolePendingValue(&cpu, 0), 0xEFC);
    WriteWord(kong + 0x20, 0); WriteWord(kong + 6, 111);
    ExpectU16("slow native pole contact is wider", Dkc2CoopPolePendingValue(&cpu, 0), 0xEFC);
    WriteWord(kong + 6, 100); WriteWord(kong + 10, 160);
    ExpectU16("native top boundary is excluded", Dkc2CoopPolePendingValue(&cpu, 0), 0);
    WriteWord(kong + 10, 260);
    ExpectU16("native bottom boundary is included", Dkc2CoopPolePendingValue(&cpu, 0), 0xEFC);
    WriteWord(kong + 10, 261);
    ExpectU16("below the pole cannot grab", Dkc2CoopPolePendingValue(&cpu, 0xEFC), 0);
    WriteWord(kong + 10, 200); WriteWord(0xEFC, 0x14C);
    ExpectU16("other hanger families retain only the native leader contact", Dkc2CoopPolePendingValue(&cpu, 0xEFC),
              kong == cpu_read16(&cpu, 0, 0x0597) ? 0 : 0xEFC);
    WriteWord(0xEFC, 0x2D8); WriteWord(0xEFC + 0x54, 0);
    ExpectU16("uninitialized pole clipping is rejected", Dkc2CoopPolePendingValue(&cpu, 0), 0);
    WriteWord(0xEFC + 0x54, 0x9000);
    WriteWord(0xEFC + 0x2E, 0);
    ExpectU16("uninitialized or hidden poles cannot be discovered", Dkc2CoopPolePendingValue(&cpu, 0), 0);
    WriteWord(0xEFC + 0x2E, 5);
    Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
    ExpectU16("lost player cannot grab", Dkc2CoopPolePendingValue(&cpu, 0), 0);
    Dkc2CoopLoadLostMask(&cpu, 3, true);
    WriteWord(0x0593, kong); WriteWord(0x0597, partner); WriteWord(0x08C2, 0);
    WriteWord(partner + 0x30, 0); WriteWord(partner + 0x2E, 0x13);
    WriteWord(kong + 0x2E, 0); cpu.X = kong;
    (void)Dkc2CoopSelectStateWord(&cpu, 0);
    ExpectU16("respawn permits pole grab while teammate stays dead", Dkc2CoopPolePendingValue(&cpu, 0), 0xEFC);
    ExpectU16("pole recovery keeps only the teammate lost", Dkc2CoopSaveLostMask(), who ? 1 : 2);
    Dkc2CoopResetSession(); Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic pending stays native", Dkc2CoopPolePendingValue(&cpu, 0x1234), 0x1234);
    ExpectU16("classic anchor stays native", Dkc2CoopPoleCurrentValue(&cpu, 0x1234), 0x1234);
  }
  ExpectU16("null pole policy stays native", Dkc2CoopPolePendingValue(NULL, 123), 123);
}

static void TestEnemyPush(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    for (unsigned blocked = 0; blocked < 11; ++blocked) {
      RescueSetup(&cpu);
      uint16_t pushed = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t active = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      WriteWord(0x0593, active); WriteWord(0x0597, pushed);
      WriteWord(0x006A, pushed); WriteWord(0x0A82, 0x1D); WriteWord(0x0A84, 0xEFC);
      uint16_t source = 0xEFC;
      if (blocked == 1) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (blocked == 2) Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
      if (blocked == 3) WriteWord(0x08C2, 0);
      if (blocked == 4) WriteWord(pushed + 0x30, 0);
      if (blocked == 5) WriteWord(0x0A84, 0xE9E);
      if (blocked == 6) source++;
      if (blocked == 7) source = active;
      if (blocked == 8) WriteWord(0x0A82, 0x17);
      ExpectU16("push preserves the native enemy source", Dkc2CoopRecordInteractionSource(&cpu, source), source);
      WriteWord(0x006A, active); /* later collision must not steal this push */
      if (blocked == 9) Dkc2CoopResetSession();
      if (blocked == 10) {
        WriteWord(0x0A82, 0x20);
        (void)Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
      }
      ExpectBool("only a live independent partner uses native push", Dkc2CoopPushIndependent(&cpu), !blocked);
      ExpectBool("accepted push keeps its collision owner", Dkc2CoopPushUsesFollower(&cpu), !blocked);
      ExpectBool("push ownership consumed once", Dkc2CoopPushUsesFollower(&cpu), false);
      if (!blocked) ExpectU16("push priority is unchanged", cpu_read16(&cpu, 0, 0x0A82), 0x1D);
    }
    RescueSetup(&cpu);
    WriteWord(0x0593, who ? kDkc2CoopSlotB : kDkc2CoopSlotA);
    WriteWord(0x0597, who ? kDkc2CoopSlotA : kDkc2CoopSlotB);
    WriteWord(0x006A, cpu_read16(&cpu, 0, 0x0593));
    WriteWord(0x0A82, 0x1E); WriteWord(0x0A84, 0xEFC);
    (void)Dkc2CoopRecordInteractionSource(&cpu, 0xEFC);
    ExpectBool("active player's normal push stays native", Dkc2CoopPushUsesFollower(&cpu), false);
  }
  ExpectBool("null push fails safe", Dkc2CoopPushIndependent(NULL), false);
}

static void TestHoneyOwnership(void) {
  CpuState cpu;
  for (unsigned leader = 0; leader < 2; ++leader) {
    for (unsigned terrain = 0; terrain < 4; ++terrain) {
      unsigned contact = terrain & 1;
      RescueSetup(&cpu);
      uint16_t active = leader ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t toucher = contact ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      WriteWord(0x0593, active);
      WriteWord(0x0597, leader ? kDkc2CoopSlotA : kDkc2CoopSlotB);
      WriteWord(0x0A82, (uint16_t)(7 + terrain / 2)); WriteWord(0x0A84, toucher);
      ExpectBool("honey reaction selects touching player work", Dkc2CoopRopeUsesFollower(&cpu), contact != leader);
      WriteWord(0x0064, toucher);
      WriteWord(0x0D7A, 0xE9E); WriteWord(0xE9E, 0x1DC);
      WriteWord(active + 0x2E, 0x0C);
      ExpectU16("honey does not inherit teammate held item",
                Dkc2CoopHeldObjectValue(&cpu, 0xE9E), contact == leader ? 0xE9E : 0);
      Dkc2CoopSetMode(kDkc2CoopClassic);
      ExpectBool("classic honey keeps native owner", Dkc2CoopRopeUsesFollower(&cpu), false);
    }
  }
}

static void TestGroundShake(void) {
  CpuState cpu;
  for (unsigned leader = 0; leader < 2; ++leader) {
    uint16_t active = leader ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t follower = leader ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    for (unsigned blocked = 0; blocked < 8; ++blocked) {
      RescueSetup(&cpu);
      WriteWord(0x0593, active); WriteWord(0x0597, follower);
      ExpectU16("shake prefers eligible native leader", Dkc2CoopQuakeCandidateValue(&cpu, active), active);
      ExpectBool("grounded leader uses native reaction", Dkc2CoopQuakeUsesFollower(&cpu), false);
      WriteWord(active + 0x1E, 0); WriteWord(active + 0x2E, 6);
      if (blocked == 1) WriteWord(follower + 0x1E, 0);
      if (blocked == 2) WriteWord(follower + 0x30, 0x9E);
      if (blocked == 3) WriteWord(follower + 0x30, 0);
      if (blocked == 4) Dkc2CoopLoadLostMask(&cpu, leader ? 1 : 2, true);
      if (blocked == 5) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (blocked == 6) WriteWord(0x006E, 0x194);
      if (blocked == 7) WriteWord(follower + 0x2E, 0x24);
      ExpectU16("jumping leader does not shield grounded partner",
                Dkc2CoopQuakeCandidateValue(&cpu, active), blocked ? active : follower);
      ExpectBool("shake initializes correct work area", Dkc2CoopQuakeUsesFollower(&cpu), !blocked);
      ExpectBool("airborne leader cannot restart quake", Dkc2CoopQuakeRepeat(&cpu), false);
      WriteWord(active + 0x2E, 0x63);
      ExpectBool("native initialization repeats only for eligible partner", Dkc2CoopQuakeRepeat(&cpu), !blocked);
      WriteWord(follower + 0x2E, 0x63);
      ExpectBool("both stunned completes without another repeat", Dkc2CoopQuakeRepeat(&cpu), false);
      WriteWord(0x0064, follower);
      ExpectU16("independent stun exits through native idle recovery",
                Dkc2CoopQuakeRecoveryValue(&cpu, follower), blocked == 5 ? follower : 0);
      ExpectBool("paired stun counts the native clock only once",
                 Dkc2CoopQuakeSkipTimer(&cpu), blocked != 5);
      WriteWord(0x0064, active);
      ExpectBool("active stun keeps the native clock", Dkc2CoopQuakeSkipTimer(&cpu), false);
      WriteWord(active + 0x2E, 6); WriteWord(0x0064, follower);
      ExpectBool("lone follower stun keeps the native clock", Dkc2CoopQuakeSkipTimer(&cpu), false);
      ExpectU16("quake selection does not swap control owners", cpu_read16(&cpu, 0, 0x0593), active);
    }
  }
}

static void TestSharedStomp(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    for (unsigned flight = 0; flight < 20; ++flight) {
      for (unsigned blocked = 0; blocked < 16; ++blocked) {
        RescueSetup(&cpu);
        uint16_t owner = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        uint16_t owner_work = who ? 0x16D8 : 0x16B2;
        uint16_t partner_work = who ? 0x16B2 : 0x16D8;
        WriteWord(0x0595, 0x16B2); WriteWord(0x0599, 0x16D8);
        for (uint16_t slot = kDkc2CoopSlotA; slot <= kDkc2CoopSlotB; slot += 0x5E) {
          WriteWord(slot + 6, 100); WriteWord(slot + 10, 200);
          WriteWord(slot + 0x1E, 0); WriteWord(slot + 0x2E, 6);
          WriteWord(slot + 0x24, 0x200);
        }
        WriteWord(partner + 6, 100 + DKC2_COOP_STOMP_HORIZONTAL_PX);
        WriteWord(owner + 0x2E, flight & 1 ? 0x12 : 6);
        WriteWord(partner + 0x2E, flight & 2 ? 0x12 : 6);
        if (flight >= 4) {
          WriteWord(0xEFC, 0x158); WriteWord(0xEFC + 0x30, 0x1000);
          WriteWord(owner + 0x42, 0xEFC); WriteWord(partner + 0x42, 0xEFC);
          WriteWord(owner + 0x2E, flight & 1 ? 0x68 : 0x67);
          WriteWord(partner + 0x2E, flight & 2 ? 0x68 : 0x67);
        }
        if (flight >= 8 && (flight < 12 || flight >= 16)) WriteWord(owner + 0x42, 0);
        if (flight >= 12) WriteWord(partner + 0x42, 0);
        WriteWord(partner + 10, 200 - DKC2_COOP_STOMP_VERTICAL_PX);
        WriteWord(partner + 0x20, 0x234);
        WriteWord(partner + 0x36, 0xAA);
        WriteWord(0xE9E, 0x220); WriteWord(0xEA4, 100); WriteWord(0xEA8, 220);
        if (blocked == 1) WriteWord(partner + 6, 101 + DKC2_COOP_STOMP_HORIZONTAL_PX);
        if (blocked == 2) WriteWord(partner + 10, 199 - DKC2_COOP_STOMP_VERTICAL_PX);
        if (blocked == 3) WriteWord(partner + 0x24, 0xFF00); /* rising */
        if (blocked == 4) WriteWord(partner + 0x2E, 0); /* grounded */
        if (blocked == 5) WriteWord(partner + 0x2E, 0x3C); /* hanging */
        if (blocked == 6) WriteWord(partner + 0x30, 0); /* absent */
        if (blocked == 7) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
        if (blocked == 8) WriteWord(0x006E, 0x194);
        if (blocked == 9) Dkc2CoopSetMode(kDkc2CoopClassic);
        if (blocked == 10) WriteWord(0x08C2, 0);
        if (blocked == 11) WriteWord(0xEA4, 1000); /* another enemy */
        /* Either/both Kongs may still be in native barrel flight at contact. */
        WriteWord(0x0A82, 0x1B); WriteWord(0x0A84, 0xE9E); WriteWord(0x006A, owner);
        (void)Dkc2CoopRecordInteractionSource(&cpu, 0xE9E);
        (void)Dkc2CoopBounceUsesFollower(&cpu);
        (void)Dkc2CoopTakeStompEvents();
        /* Native reaction has now initialized the actual stomp owner's bounce. */
        WriteWord(owner + 0x24, 0xFA00); WriteWord(owner + 0x2E, flight >= 4 ? 0x68 : 0x16);
        WriteWord(owner_work + 8, 0x28); WriteWord(owner_work + 10, 0x600);
        if (blocked == 12) Dkc2CoopResetSession();
        if (blocked == 13) WriteWord(0x08C2, 0);
        if (blocked == 14) WriteWord(partner + 0x2E, 0x24);
        if (blocked == 15) WriteWord(0x0599, 0xFFFF);
        uint16_t previous = cpu_read16(&cpu, 0, partner + 0x24);
        Dkc2CoopShareStomp(&cpu);
        ExpectU16("shared stomp eligibility and distance boundaries",
                  cpu_read16(&cpu, 0, partner + 0x24), blocked ? previous : 0xFA00);
        if (!blocked) {
          ExpectU16("partner uses bounce gravity", cpu_read16(&cpu, 0, partner_work + 8), 0x28);
          ExpectU16("partner uses bounce terminal speed", cpu_read16(&cpu, 0, partner_work + 10), 0x600);
          ExpectU16("partner enters its own bounce state", cpu_read16(&cpu, 0, partner + 0x2E), flight >= 4 ? 0x68 : 0x16);
          ExpectU16("partner keeps horizontal speed", cpu_read16(&cpu, 0, partner + 0x20), 0x234);
          ExpectU16("partner keeps own animation", cpu_read16(&cpu, 0, partner + 0x36), 0xAA);
          ExpectU16("shared bounce feedback goes to partner", Dkc2CoopTakeStompEvents(), who ? 1 : 2);
          WriteWord(partner + 0x24, 0x200); Dkc2CoopShareStomp(&cpu);
          ExpectU16("shared impulse consumed once", cpu_read16(&cpu, 0, partner + 0x24), 0x200);
        }
      }
    }
  }
  Dkc2CoopShareStomp(NULL);
}

static void TestSharedBarrelInput(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t leader = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x0593, leader);
    WriteWord(0x0597, partner);
    WriteWord(0x0064, leader);
    WriteWord(0x0A84, 0xF5A);
    WriteWord(0xF5A, 0x140);
    WriteWord(leader + 0x2E, 0x11);
    WriteWord(partner + 0x2E, 0x31);
    WriteWord(who ? kDkc2CoopP1Pressed : kDkc2CoopP2Pressed, 0xFFFF);
    WriteWord(who ? kDkc2CoopP2Pressed : kDkc2CoopP1Pressed, 0x0100);
    ExpectU16("either controller can fire the shared barrel", Dkc2CoopSelectPressedWord(&cpu, 0), 0x8100);
    WriteWord(who ? kDkc2CoopP1Held : kDkc2CoopP2Held, 0xA180);
    WriteWord(who ? kDkc2CoopP2Held : kDkc2CoopP1Held, 0x4080);
    ExpectU16("either occupant can aim without sharing other buttons", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4180);
    WriteWord(who ? kDkc2CoopP2Held : kDkc2CoopP1Held, 0x4680);
    ExpectU16("opposing horizontal aim cancels independently of vertical", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4480);
    WriteWord(who ? kDkc2CoopP1Held : kDkc2CoopP2Held, 0x0800);
    ExpectU16("opposing vertical aim cancels independently of horizontal", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4280);
    WriteWord(partner + 0x2E, 0x30);
    ExpectU16("partner cannot aim before arriving", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4680);
    WriteWord(partner + 0x2E, 0x31);
    WriteWord(0xF5A, 0x174);
    ExpectU16("other transport retains native aiming", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4680);
    WriteWord(0xF5A, 0x140);
    ExpectU16("manual barrel recruits both players", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4000), 0x4000);
    WriteWord(leader + 0x2E, 0);
    ExpectU16("partner buttons remain separate on foot", Dkc2CoopSelectPressedWord(&cpu, 0), 0x0100);
    ExpectU16("held input remains separate on foot", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4680);
    WriteWord(leader + 0x2E, 0x12);
    ExpectU16("partner cannot steer or alter flight actions", Dkc2CoopSelectPressedWord(&cpu, 0), 0x0100);
    ExpectU16("held air steering remains separate", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4680);
    WriteWord(leader + 0x2E, 0x11);
    Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
    ExpectU16("lost partner cannot fire", Dkc2CoopSelectPressedWord(&cpu, 0), 0x0100);
    ExpectU16("lost partner cannot aim", Dkc2CoopSelectHeldWord(&cpu, 0), 0x4680);
    ExpectBool("manual barrel does not recruit lost partner", Dkc2CoopCannonIndependent(&cpu), true);
    Dkc2CoopResetSession();
    WriteWord(0xF5A, 0x14C);
    ExpectU16("hanger cannot share shooting input", Dkc2CoopSelectPressedWord(&cpu, 0), 0x0100);
    Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic mode keeps native barrel controls", Dkc2CoopSelectPressedWord(&cpu, 0x1234), 0x1234);
    ExpectU16("classic mode keeps native aiming", Dkc2CoopSelectHeldWord(&cpu, 0x1234), 0x1234);
  }
}

static void TestSharedBird(void) {
  CpuState cpu;
  for (int who = 0; who < 2; ++who) {
    memset(&cpu, 0, sizeof cpu);
    memset(s_fake_wram, 0, sizeof s_fake_wram);
    Dkc2CoopSetMode(kDkc2CoopSimultaneous);
    Dkc2CoopResetSession();
    uint16_t rider = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t partner = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x060D, 1);
    WriteWord(0x0593, rider);
    WriteWord(0x0597, partner);
    WriteWord(0x006C, 0xEFC);
    WriteWord(0x006E, 0x198);
    WriteWord(0xEFC, 0x198);
    WriteWord(0x08C2, 0x4200);
    WriteWord(partner, who ? 0xE4 : 0xE8);
    WriteWord(partner + 0x30, 0x1E);
    WriteWord(rider + 0x2E, 0x10);
    ExpectU16("parrot carries both players", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4200), 0x4200);
    ExpectU16("parrot partner uses hanging state", cpu_read16(&cpu, 0, partner + 0x2E), 0x22);
    cpu.X = partner;
    WriteWord(who ? kDkc2CoopP1Held : kDkc2CoopP2Held, 0x8100);
    ExpectU16("partner input retains shared parrot", Dkc2CoopSelectStateWord(&cpu, 0x22), 0x22);
    Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
    WriteWord(0x0593, partner);
    WriteWord(0x0597, rider);
    WriteWord(0x08C2, 0x200);
    WriteWord(0x0A36, 7);
    ExpectU16("parrot survivor retains flight state", Dkc2CoopSelectStateWord(&cpu, 0x6F), 0x10);
    ExpectU16("parrot handoff unfreezes controls", cpu_read16(&cpu, 0, 0x0A36), 0);
    ExpectU16("lost parrot partner is not carried", Dkc2CoopAnimalFollowerFlags(&cpu, 0x4200), 0x200);
    WriteWord(0xEFC, 0);
    ExpectU16("stale parrot pointer retains ordinary handoff", Dkc2CoopSelectStateWord(&cpu, 0x6F), 6);
  }
}

static void TestHoneyRescue(void) {
  CpuState cpu;
  const uint16_t honey_states[] = {0x41, 0x42, 0x43, 0x44, 0x7A};
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned i = 0; i < sizeof honey_states / sizeof honey_states[0]; ++i) {
      for (unsigned wall = 0; wall < 2; ++wall) {
        RescueSetup(&cpu);
        uint16_t slot = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t target = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        uint16_t animation = (uint16_t)((wall ? 0x2B : 1) + (who ? 0xA3 : 0));
        WriteWord(0x0595, 0x16B2); WriteWord(0x0599, 0x16D8);
        WriteWord(who ? 0x0504 : 0x0502, 0x2000);
        WriteWord(slot + 0x2E, honey_states[i]);
        WriteWord(slot + 0x54, 0x9000); WriteWord(0x9000, 0x70); WriteWord(0x9002, 0x800);
        WriteWord(slot + 0x3E, 0x1234); WriteWord(slot + 0x12, 0x2345);
        WriteWord(target + 0x2E, wall ? 0x41 : 0);
        WriteWord(target + 0x1E, wall ? 2 : 1); WriteWord(target + 0x12, 0x7654);
        WriteWord(animation * 4, 0x8456); WriteWord(animation * 4 + 2, 0xB9);
        ExpectU16("honey teleport obeys compiled timer", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames - 1), 0);
        ExpectU16("either player can leave honey or share a wall", RescueTick(&cpu), (uint16_t)(1u << who));
        ExpectU16("honey teleport selects destination state", cpu_read16(&cpu, 0, slot + 0x2E), wall ? 0x41 : 0);
        ExpectU16("honey teleport uses own character animation", cpu_read16(&cpu, 0, slot + 0x36), animation);
        ExpectU16("honey teleport clears old animation callback", cpu_read16(&cpu, 0, slot + 0x3E), 0);
        ExpectU16("honey teleport restores own normal gravity", cpu_read16(&cpu, 0, (who ? 0x16D8 : 0x16B2) + 8), 0x70);
        ExpectU16("wall teleport keeps palette while matching facing", cpu_read16(&cpu, 0, slot + 0x12), wall ? 0x6345 : 0x2345);
        ExpectU16("wall teleport uses proven anchor", cpu_read16(&cpu, 0, slot + 6), wall ? (who ? 100 : 300) : (who ? 116 : 284));
        ExpectU16("honey teleport preserves target pose", cpu_read16(&cpu, 0, target + 0x2E), wall ? 0x41 : 0);
        ExpectU16("holding Select never repeats honey teleport", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
      }
    }
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
      RescueSetup(&cpu);
      uint16_t target = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      WriteWord(who ? 0x0504 : 0x0502, 0x2000);
      WriteWord(target + 0x2E, invalid == 0 ? 0x42 : 0x41);
      if (invalid == 1) WriteWord(target + 0x30, 0);
      if (invalid == 2) Dkc2CoopSetMode(kDkc2CoopClassic);
      ExpectU16("wall rescue rejects jumping dead and classic destinations", RescueFrames(&cpu, kDkc2CoopRescueHoldFrames), 0);
    }
  }
}

static void TestCenteredCamera(void) {
  CpuState cpu, original;
  const uint16_t states[] = {0, 9, 0x0A, 0x0B, 0x3C, 0x41, 0x45};
  for (unsigned selector = 0; selector < 2; ++selector) {
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i) {
      RescueSetup(&cpu); original = cpu;
      WriteWord(0x0A98, selector ? kDkc2CoopSlotB : kDkc2CoopSlotA);
      WriteWord(kDkc2CoopSlotA + 0x2E, states[i]);
      WriteWord(kDkc2CoopSlotA + 6, 100); WriteWord(kDkc2CoopSlotA + 4, 0x8000);
      WriteWord(kDkc2CoopSlotB + 6, 301); WriteWord(kDkc2CoopSlotB + 4, 0x8000);
      WriteWord(kDkc2CoopSlotA + 10, 100); WriteWord(kDkc2CoopSlotA + 8, 0x4000);
      WriteWord(kDkc2CoopSlotB + 10, 401); WriteWord(kDkc2CoopSlotB + 8, 0xC000);
      WriteWord(0xAEA, 0x1234);
      Dkc2CoopCameraCenter(&cpu);
      ExpectU16("centered camera X uses midpoint minus half viewport", cpu_read16(&cpu, 0, 0xADE), 73 * 256);
      ExpectU16("centered camera Y uses midpoint minus half viewport", cpu_read16(&cpu, 0, 0xAE2), 139 * 256);
      ExpectU16("camera centering preserves native profiles", cpu_read16(&cpu, 0, 0xAEA), 0x1234);
      ExpectBool("camera centering preserves CPU context", memcmp(&cpu, &original, sizeof cpu) == 0, true);
    }
  }
  for (unsigned blocked = 0; blocked < 6; ++blocked) {
    RescueSetup(&cpu); WriteWord(0xA98, kDkc2CoopSlotA);
    WriteWord(0xADE, 0x1234); WriteWord(0xAE2, 0x5678);
    if (blocked == 0) Dkc2CoopSetMode(kDkc2CoopClassic);
    if (blocked == 1) Dkc2CoopLoadLostMask(&cpu, 1, true);
    if (blocked == 2) WriteWord(kDkc2CoopSlotB + 0x30, 0);
    if (blocked == 3) WriteWord(kDkc2CoopSlotB + 0x2E, 0x74);
    if (blocked == 4) WriteWord(0xA98, 0xEFC);
    if (blocked == 5) WriteWord(0x24, 0);
    Dkc2CoopCameraCenter(&cpu);
    ExpectU16("native camera X retained outside eligible co-op", cpu_read16(&cpu, 0, 0xADE), 0x1234);
    ExpectU16("native camera Y retained outside eligible co-op", cpu_read16(&cpu, 0, 0xAE2), 0x5678);
  }
  RescueSetup(&cpu); WriteWord(0xA98, kDkc2CoopSlotA);
  WriteWord(kDkc2CoopSlotA + 6, 1); WriteWord(kDkc2CoopSlotB + 6, 2);
  WriteWord(kDkc2CoopSlotA + 10, 1); WriteWord(kDkc2CoopSlotB + 10, 2);
  Dkc2CoopCameraCenter(&cpu);
  ExpectU16("viewport center never underflows X", cpu_read16(&cpu, 0, 0xADE), 0);
  ExpectU16("viewport center never underflows Y", cpu_read16(&cpu, 0, 0xAE2), 0);
  Dkc2CoopCameraCenter(NULL);
}

static void TestThrowIntoHook(void) {
  CpuState cpu = {0};
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned variant = 0; variant < 9; ++variant) {
      memset(s_fake_wram, 0, sizeof s_fake_wram);
      Dkc2CoopResetSession(); Dkc2CoopSetMode(kDkc2CoopSimultaneous);
      uint16_t thrower = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t passenger = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      uint16_t waiting = variant == 1 ? 0x20 : 0x2C;
      WriteWord(0x060D, 1); WriteWord(0x0024, 0x8819); WriteWord(0x08C2, 0x4000);
      WriteWord(0x0593, passenger); WriteWord(0x0597, thrower);
      WriteWord(thrower, 0xE4); WriteWord(passenger, 0xE8);
      WriteWord(thrower + 0x30, 0x1E); WriteWord(passenger + 0x30, 0x1E);
      WriteWord(thrower + 0x2E, waiting); WriteWord(passenger + 0x2E, 0x3C);
      WriteWord(thrower + 6, 100); WriteWord(thrower + 10, 300);
      WriteWord(thrower + 0x24, 0xFE00);
      WriteWord(passenger + 6, 500); WriteWord(passenger + 10, 223);
      WriteWord(0xE9E, 0x14C); WriteWord(0xEA4, 500); WriteWord(0xEA8, 200);
      if (variant == 2) WriteWord(passenger + 0x2E, 0x2B);
      if (variant == 3) WriteWord(0xE9E, 0x140);
      if (variant == 4) WriteWord(passenger + 10, 222);
      if (variant == 5) WriteWord(0x0D7A, passenger);
      if (variant == 6) WriteWord(0x006E, 0x194);
      if (variant == 7) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
      if (variant == 8) Dkc2CoopSetMode(kDkc2CoopClassic);
      cpu.X = thrower;
      ExpectU16("hanger ends only an accepted independent throw wait",
                Dkc2CoopSelectStateWord(&cpu, waiting), variant < 2 ? 0 : waiting);
      ExpectU16("hanger wait release preserves thrower position", cpu_read16(&cpu, 0, thrower + 10), 300);
      ExpectU16("hanger wait release preserves thrower velocity", cpu_read16(&cpu, 0, thrower + 0x24), 0xFE00);
      ExpectU16("hanger wait release preserves passenger position", cpu_read16(&cpu, 0, passenger + 6), 500);
      ExpectU16("hanger wait release preserves passenger state", cpu_read16(&cpu, 0, passenger + 0x2E), variant == 2 ? 0x2B : 0x3C);
    }
  }
}

static void TestSharedKartLeader(void) {
  CpuState cpu = {0};
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t leader = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t peer = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    uint16_t work = who ? 0x16D8 : 0x16B2;
    WriteWord(0x595, 0x16B2); WriteWord(0x599, 0x16D8);
    WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
    WriteWord(0xE9E + 0x2E, 0x202); WriteWord(0x64, 0xE9E);
    WriteWord(0x6A, leader); WriteWord(0xE9E + 0x42, 0); WriteWord(0xE9E + 0x44, 0);
    ExpectU16("first touching player becomes front rider regardless of active port",
              Dkc2CoopCartBoardingLeaderValue(&cpu, kDkc2CoopSlotA), leader);
    WriteWord(0xE9E + 0x42, leader); WriteWord(0xE9E + 0x44, peer);
    WriteWord(leader + 0x42, 0xE9E); WriteWord(peer + 0x42, 0xE9E);
    WriteWord(leader + 0x2E, 0x5B); WriteWord(peer + 0x2E, 0x5C);
    WriteWord(0x6A, peer);
    ExpectU16("later boarder cannot replace front owner",
              Dkc2CoopCartBoardingLeaderValue(&cpu, peer), leader);
    WriteWord(0xA86, 0xE9E); WriteWord(0xA88, leader);
    ExpectBool("shared kart accepts first boarder", Dkc2CoopCartEntryBlocked(&cpu), 0);
    ExpectU16("first boarder owns native cart control", cpu_read16(&cpu, 0, 0x593), leader);
    /* Restore inactive leadership to check input remains tied to the seat. */
    if (who) { WriteWord(0x593, kDkc2CoopSlotA); WriteWord(0x597, kDkc2CoopSlotB);
      WriteWord(0x595, 0x16B2); WriteWord(0x599, 0x16D8); }

    WriteWord(0x2A, 0x1234); WriteWord(work + 0x22, 0x1000);
    WriteWord(who ? kDkc2CoopP1Pressed : kDkc2CoopP2Pressed, 0x8000);
    ExpectU16("first boarder owns shared kart even when inactive",
              Dkc2CoopCartLeaderWorkValue(&cpu, 0x7777), work);
    ExpectU16("passenger cannot queue vehicle jump", cpu_read16(&cpu, 0, work + 0x22), 0x1000);
    cpu.X = peer;
    ExpectU16("shared kart passenger cannot jump out", Dkc2CoopSelectStateWord(&cpu, 0x5C), 0x5C);
    WriteWord(who ? kDkc2CoopP2Pressed : kDkc2CoopP1Pressed, 0x8000);
    Dkc2CoopCartLeaderWorkValue(&cpu, 0x7777);
    ExpectU16("leader queues native vehicle jump", cpu_read16(&cpu, 0, work + 0x22), 0x1234);
    cpu.X = leader;
    ExpectU16("vehicle leader stays seated", Dkc2CoopSelectStateWord(&cpu, 0x5B), 0x5B);
    WriteWord(0xE9E + 0x2E, 0x201);
    ExpectU16("ordinary coaster keeps native policy", Dkc2CoopCartLeaderWorkValue(&cpu, 0x7777), 0x7777);
    WriteWord(0xE9E + 0x2E, 0x202); Dkc2CoopSetMode(kDkc2CoopClassic);
    ExpectU16("classic shared kart untouched", Dkc2CoopCartLeaderWorkValue(&cpu, 0x7777), 0x7777);
  }
}

static void TestIndependentCart(void) {
  CpuState cpu = {0};
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    WriteWord(0x17C0, 400); /* The fixture cart is inside the visible viewport. */
    uint16_t jumper = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0x595, 0x16B2); WriteWord(0x599, 0x16D8);
    WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
    WriteWord(0xE9E + 6, 500); WriteWord(0xE9E + 10, 600);
    WriteWord(0xE9E + 0x42, kDkc2CoopSlotA); WriteWord(0xE9E + 0x44, kDkc2CoopSlotB);
    for (unsigned i = 0; i < 2; ++i) {
      uint16_t slot = i ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      WriteWord(slot + 0x42, 0xE9E); WriteWord(slot + 0x2E, i ? 0x5C : 0x5B);
      WriteWord(slot + 0x54, 0x7000);
    }
    WriteWord(0x7002, 0x800); WriteWord(0x7004, 0x5A); WriteWord(0x7008, 0xF7C8);
    WriteWord(who ? kDkc2CoopP2Pressed : kDkc2CoopP1Pressed, 0x8000);
    WriteWord(0x2A, 0x1234);
    cpu.X = jumper;
    ExpectU16("each cart rider jumps on its own port", Dkc2CoopSelectStateWord(&cpu, who ? 0x5C : 0x5B), 0x67);
    ExpectU16("jump keeps partner seated", cpu_read16(&cpu, 0, other + 0x2E), who ? 0x5B : 0x5C);
    ExpectU16("jump uses own impulse", cpu_read16(&cpu, 0, jumper + 0x24), 0xF7C8);
    ExpectU16("jump records native track-gate timestamp", cpu_read16(&cpu, 0, (who ? 0x16D8 : 0x16B2) + 0x22), 0x1234);
    WriteWord(jumper + 10, 550); WriteWord(0x64, 0xE9E);
    ExpectBool("native cart placement is adapted", Dkc2CoopCartSeats(&cpu), 1);
    ExpectU16("cart never snaps airborne Kong down", cpu_read16(&cpu, 0, jumper + 10), 550);
    ExpectU16("partner remains on cart", cpu_read16(&cpu, 0, other + 10), 600);
    WriteWord(jumper + 10, 603); WriteWord(jumper + 0x24, 0x300);
    Dkc2CoopCartSeats(&cpu);
    ExpectU16("descending rider lands on its seat", cpu_read16(&cpu, 0, jumper + 10), 600);
    ExpectU16("landing restores own seat state", cpu_read16(&cpu, 0, jumper + 0x2E), who ? 0x5C : 0x5B);
    ExpectBool("riders use their own hitboxes", Dkc2CoopCartClip(&cpu), 1);
    WriteWord(0xA82, 0x23); WriteWord(0x6A, jumper); WriteWord(0xA84, 0xF5A);
    Dkc2CoopRecordInteractionSource(&cpu, 0xF5A);
    WriteWord(0x6A, other); /* A later check must not change accepted ownership. */
    Dkc2CoopCartHitOwner(&cpu);
    ExpectU16("coaster hurt belongs to accepted collider", cpu_read16(&cpu, 0, 0x593), jumper);
    WriteWord(0x64, jumper); WriteWord(jumper + 0x2E, 0x24); WriteWord(jumper + 0x30, 0);
    WriteWord(0xA36, 7); WriteWord(0xA38, 0x280);
    ExpectBool("native accepted damage preserves seated survivor", Dkc2CoopCartSurvivor(&cpu), 1);
    ExpectU16("survivor takes cart ownership", cpu_read16(&cpu, 0, 0xE9E + 0x42), other);
    ExpectU16("survivor becomes camera leader", cpu_read16(&cpu, 0, 0x593), other);
    ExpectU16("damage records only injured rider", Dkc2CoopSaveLostMask(), who ? 2 : 1);
    ExpectU16("handoff does not freeze the cart", cpu_read16(&cpu, 0, 0xA36), 0);
    ExpectU16("survivor receives native grace period", cpu_read16(&cpu, 0, 0x16B2 + (who ? 0 : 0x26) + 0x16), 0x70);
    WriteWord(0x64, other); WriteWord(other + 0x2E, 0x24); WriteWord(other + 0x30, 0);
    ExpectBool("last rider retains native death", Dkc2CoopCartSurvivor(&cpu), 0);
    Dkc2CoopSetMode(kDkc2CoopClassic); WriteWord(0x64, 0xE9E);
    ExpectBool("classic retains native cart", Dkc2CoopCartIndependent(&cpu), 0);
    ExpectBool("classic retains native seats", Dkc2CoopCartSeats(&cpu), 0);
  }
}

static void TestTerrainHurtOwner(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned variant = 0; variant < 10; ++variant) {
      RescueSetup(&cpu);
      uint16_t victim = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t peer = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      WriteWord(0x593, variant == 1 ? victim : peer);
      WriteWord(0x597, variant == 1 ? peer : victim);
      WriteWord(0xA82, variant == 3 ? 0x1D : 0x23); WriteWord(0xA84, victim);
      WriteWord(0x64, variant == 6 ? peer : victim);
      WriteWord(0x6A, peer); /* An unrelated collision record is stale. */
      if (variant == 4) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (variant == 5) WriteWord(victim + 0x30, 0);
      if (variant == 9) Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
      Dkc2CoopRecordInteractionSource(&cpu, victim);
      if (variant == 2) {
        WriteWord(0xA84, 0xE9E);
        Dkc2CoopRecordInteractionSource(&cpu, 0xE9E); /* Superseding reaction. */
      }
      if (variant == 7) WriteWord(0xA84, 0xE9E);
      if (variant == 8) Dkc2CoopResetSession();
      WriteWord(0x64, peer); WriteWord(0x6A, peer);
      Dkc2CoopCartHitOwner(&cpu);
      ExpectU16("accepted terrain hit selects actual victim despite later collider",
                cpu_read16(&cpu, 0, 0x593), variant < 2 ? victim : peer);
      if (variant == 0) {
        ExpectU16("terrain hit leaves survivor inactive for native handoff", cpu_read16(&cpu, 0, 0x597), peer);
        WriteWord(0x593, peer); WriteWord(0x597, victim);
        Dkc2CoopCartHitOwner(&cpu);
        ExpectU16("terrain damage ownership is consumed once", cpu_read16(&cpu, 0, 0x593), peer);
      }
    }
  }
}

static void TestCartScriptedExit(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned seat = 0; seat < 2; ++seat) {
      for (unsigned variant = 0; variant < 5; ++variant) {
        RescueSetup(&cpu);
        uint16_t kong = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
        WriteWord(0xE9E + 6, 900); WriteWord(0xE9E + 10, 600); WriteWord(0x17C0, 400);
        WriteWord(0xE9E + 0x42 + seat*2, kong); WriteWord(0x64, 0xE9E);
        WriteWord(kong + 0x42, variant == 3 ? 0 : 0xE9E);
        WriteWord(kong + 0x2E, 0x70); WriteWord(kong + 6, 800); WriteWord(kong + 10, 590);
        WriteWord(0xDAAD, 0xFFF2); WriteWord(0xDAAD+0x4C, 0xFFF2);
        WriteWord(0xDAD3, 3); WriteWord(0xDAD3+0x4C, 3);
        if (variant == 1) Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
        if (variant == 2) WriteWord(kong + 0x30, 0);
        if (variant == 4) Dkc2CoopSetMode(kDkc2CoopClassic);
        Dkc2CoopCartSeats(&cpu);
        ExpectU16("only live owned scripted exit rider follows cart", cpu_read16(&cpu, 0, kong+6),
                  variant == 0 ? (seat ? 886 : 900) : 800);
        ExpectU16("scripted exit keeps native seat height", cpu_read16(&cpu, 0, kong+10),
                  variant == 0 ? (seat ? 603 : 600) : 590);
        ExpectU16("cart placement preserves scripted exit action", cpu_read16(&cpu, 0, kong+0x2E), 0x70);
        if (variant == 0) {
          WriteWord(0xE9E+6, 950); Dkc2CoopCartSeats(&cpu);
          ExpectU16("exit rider continues across viewport", cpu_read16(&cpu, 0, kong+6), seat ? 936 : 950);
          cpu.X=kong; WriteWord(who ? kDkc2CoopP2Pressed : kDkc2CoopP1Pressed, 0x8000);
          ExpectU16("jump cannot cancel scripted cart exit", Dkc2CoopSelectStateWord(&cpu, 0x70), 0x70);
        }
      }
    }
  }
}

static void TestCartCannonCapture(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned follower = 0; follower < 2; ++follower) {
      for (unsigned peer_riding = 0; peer_riding < 2; ++peer_riding) {
        RescueSetup(&cpu);
        uint16_t kong = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t peer = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        uint16_t state = follower ? 0x31 : 0x11;
        WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
        WriteWord(0xE9E + 0x42, kong); WriteWord(0xE9E + 0x44, peer);
        WriteWord(0x64, 0xE9E); WriteWord(0xA86, 0xE9E); WriteWord(0xA88, kong);
        WriteWord(0xD5E, 0xE9E); WriteWord(kong + 0x42, 0xE9E);
        WriteWord(kong + 0x2E, state); WriteWord(kong + 0x30, 0x10);
        WriteWord(kong + 6, 500); WriteWord(kong + 10, 300);
        WriteWord(peer + 0x42, peer_riding ? 0xE9E : 0);
        WriteWord(peer + 0x2E, peer_riding ? 0x5C : 0x13);
        cpu.X = kong;
        ExpectU16("falling cart cannot collide with a captured cannon rider", Dkc2CoopGoalCollisionFlags(&cpu, 0x10), 0);
        ExpectBool("queued cart entry cannot reclaim a cannon occupant", Dkc2CoopCartEntryBlocked(&cpu), 1);
        ExpectU16("cannon capture retains its native action", Dkc2CoopSelectStateWord(&cpu, state), state);
        ExpectU16("cannon capture releases stale cart seat", cpu_read16(&cpu, 0, 0xE9E + 0x42), 0);
        ExpectU16("cannon capture leaves other cart seat alone", cpu_read16(&cpu, 0, 0xE9E + 0x44), peer);
        ExpectU16("cannon capture releases own old cart pointer", cpu_read16(&cpu, 0, kong + 0x42), 0);
        ExpectU16("only a real other rider retains global cart ownership", cpu_read16(&cpu, 0, 0xD5E), peer_riding ? 0xE9E : 0);
        ExpectU16("cart release preserves cannon X", cpu_read16(&cpu, 0, kong + 6), 500);
        ExpectU16("cart release preserves cannon Y", cpu_read16(&cpu, 0, kong + 10), 300);
        WriteWord(kong + 0x2E, 0x12);
        ExpectU16("shot flight retains native cart collision eligibility", Dkc2CoopGoalCollisionFlags(&cpu, 0x10), 0x10);
        ExpectBool("shot may board its destination cart", Dkc2CoopCartEntryBlocked(&cpu), 0);
        WriteWord(kong + 0x2E, state); WriteWord(0xE9E, 0x140);
        ExpectU16("other source collisions remain native", Dkc2CoopGoalCollisionFlags(&cpu, 0x10), 0x10);
        WriteWord(0xE9E, 0x158); Dkc2CoopSetMode(kDkc2CoopClassic);
        ExpectU16("classic cannon/cart collision remains native", Dkc2CoopGoalCollisionFlags(&cpu, 0x10), 0x10);
        ExpectBool("classic cart entry remains native", Dkc2CoopCartEntryBlocked(&cpu), 0);
      }
    }
  }
}

static void TestCartPartnerLanding(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned seat = 0; seat < 2; ++seat) {
      for (unsigned variant = 0; variant < 12; ++variant) {
        RescueSetup(&cpu);
        uint16_t kong = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
        uint16_t peer = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
        WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
        WriteWord(0xE9E + 6, 800); WriteWord(0xE9E + 10, 600);
        WriteWord(0xE9E + 0x2E, 0x101);
        WriteWord(0xE9E + 0x42 + (1-seat)*2, peer);
        WriteWord(peer + 0x42, 0xE9E); WriteWord(peer + 0x2E, seat ? 0x5B : 0x5C);
        /* Synthetic ROM seat offsets: both character indices use -14 X. */
        WriteWord(0xDAAD, 0xFFF2); WriteWord(0xDAAD + 0x4C, 0xFFF2);
        WriteWord(kong + 0x42, 0); WriteWord(kong + 0x2E, 0x67);
        WriteWord(kong + 6, (uint16_t)(800 - (seat ? 14 : 0) - 32));
        WriteWord(kong + 10, 624); WriteWord(kong + 0x24, 0x400);
        if (variant == 1) WriteWord(kong + 10, 568); /* upper edge */
        if (variant == 2) WriteWord(kong + 0x24, 0xFF00); /* rising */
        if (variant == 3) WriteWord(kong + 10, 625); /* too low */
        if (variant == 4) WriteWord(kong + 6, (uint16_t)(800 - (seat ? 14 : 0) - 33));
        if (variant == 5) WriteWord(kong + 0x42, 0xE9E); /* own-seat jump */
        if (variant == 6) Dkc2CoopLoadLostMask(&cpu, who ? 2 : 1, true);
        if (variant == 7) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
        if (variant == 8) WriteWord(peer + 0x2E, 0x67); /* no accepted landing */
        if (variant == 9) WriteWord(0xE9E + 0x42 + seat*2, 0xEFC); /* occupied */
        if (variant == 10) Dkc2CoopSetMode(kDkc2CoopClassic);
        if (variant == 11) WriteWord(kong + 0x2E, 0x5C); /* non-jumper */
        uint16_t original = variant == 11 ? 0x5C : 0x67;
        cpu.X = kong;
        ExpectU16("only a live descending nearby cart jumper can join its peer",
                  Dkc2CoopSelectStateWord(&cpu, original), variant < 2 ? (seat ? 0x5C : 0x5B) : original);
        if (variant < 2) {
          ExpectU16("partner landing fills only empty seat", cpu_read16(&cpu, 0, 0xE9E + 0x42 + seat*2), kong);
          ExpectU16("partner landing preserves native accepted rider", cpu_read16(&cpu, 0, 0xE9E + 0x42 + (1-seat)*2), peer);
          ExpectU16("partner landing uses own seat offset", cpu_read16(&cpu, 0, kong + 6), seat ? 786 : 800);
          ExpectU16("partner landing ends falling", cpu_read16(&cpu, 0, kong + 0x24), 0);
          ExpectU16("front arrival starts native cart movement", cpu_read16(&cpu, 0, 0xE9E + 0x2E), seat ? 0x101 : 0x201);
        }
      }
    }
  }
}

static void TestCartPitAndStomp(void) {
  CpuState cpu;
  for (unsigned who = 0; who < 2; ++who) {
    for (unsigned variant = 0; variant < 7; ++variant) {
      RescueSetup(&cpu);
      uint16_t fallen = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
      uint16_t survivor = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
      WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
      WriteWord(0xE9E + 0x20, 0x480);
      WriteWord(0xA84, 0xE9E); WriteWord(0xD5E, 0xE9E); WriteWord(0x17C0, 100);
      WriteWord(fallen + 0x42, 0xE9E); WriteWord(fallen + 0x2E, 0x5B);
      WriteWord(fallen + 10, 400); WriteWord(fallen + 0x1E, 0);
      WriteWord(survivor + 0x42, 0xE9E); WriteWord(survivor + 0x2E, variant == 1 ? 0x68 : 0x67);
      WriteWord(survivor + 10, 250); WriteWord(survivor + 0x1E, 0);
      WriteWord(survivor + 0x24, 0xFF00); WriteWord(0x6A, survivor);
      ExpectU16("airborne cart contact uses ordinary enemy reaction", Dkc2CoopCartStompValue(&cpu, 0xE9E), 0);
      WriteWord(0x6A, fallen);
      ExpectU16("seated contact keeps native cart reaction", Dkc2CoopCartStompValue(&cpu, 0xE9E), 0xE9E);
      if (variant == 2) WriteWord(survivor + 0x42, 0); /* already detached */
      if (variant == 3) WriteWord(survivor + 10, 400); /* both below cutoff */
      if (variant == 4) Dkc2CoopLoadLostMask(&cpu, who ? 1 : 2, true);
      if (variant == 5) Dkc2CoopSetMode(kDkc2CoopClassic);
      if (variant == 6) WriteWord(0xE9E, 0x140);
      ExpectBool("falling cart preserves only a valid survivor", Dkc2CoopCartPit(&cpu), variant < 3);
      if (variant < 3) {
        ExpectU16("falling rider stays lost", cpu_read16(&cpu, 0, fallen + 0x2E), 0x13);
        ExpectU16("falling rider cannot collide", cpu_read16(&cpu, 0, fallen + 0x30), 0);
        ExpectU16("pit records only fallen rider", Dkc2CoopSaveLostMask(), who ? 2 : 1);
        ExpectU16("pit promotes surviving controller", cpu_read16(&cpu, 0, 0x593), survivor);
        ExpectU16("survivor retains altitude", cpu_read16(&cpu, 0, survivor + 10), 250);
        ExpectU16("survivor retains bounce/jump impulse", cpu_read16(&cpu, 0, survivor + 0x24), 0xFF00);
        ExpectU16("survivor detaches from sinking cart", cpu_read16(&cpu, 0, survivor + 0x42), 0);
        ExpectU16("old cart no longer drives collisions", cpu_read16(&cpu, 0, 0xD5E), 0);
        if (variant != 2)
          ExpectU16("survivor keeps cart momentum", cpu_read16(&cpu, 0, survivor + 0x20), 0x480);
        cpu.X = fallen; WriteWord(fallen + 0x2E, 0x5C);
        ExpectU16("lost rider cannot retain a stale seated pose", Dkc2CoopSelectStateWord(&cpu, 0x5C), 0x13);
      }
    }
  }
  RescueSetup(&cpu);
  WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
  WriteWord(0xEFC, 0x158); WriteWord(0xEFC + 0x30, 0x1000);
  WriteWord(0xE9E + 0x42, kDkc2CoopSlotA);
  WriteWord(0xE9E + 10, 900); WriteWord(0x64, 0xE9E);
  WriteWord(kDkc2CoopSlotA + 0x42, 0xEFC);
  WriteWord(kDkc2CoopSlotA + 0x2E, 0x5B);
  WriteWord(kDkc2CoopSlotA + 10, 600);
  Dkc2CoopCartSeats(&cpu);
  ExpectU16("old cart cannot pull rider away from new cart", cpu_read16(&cpu, 0, kDkc2CoopSlotA + 10), 600);
  ExpectU16("old cart drops stale rider reference", cpu_read16(&cpu, 0, 0xE9E + 0x42), 0);
  for (unsigned who = 0; who < 2; ++who) {
    RescueSetup(&cpu);
    uint16_t jumper = who ? kDkc2CoopSlotB : kDkc2CoopSlotA;
    uint16_t other = who ? kDkc2CoopSlotA : kDkc2CoopSlotB;
    WriteWord(0xE9E, 0x158); WriteWord(0xE9E + 0x30, 0x1000);
    WriteWord(0xE9E + 0x42, kDkc2CoopSlotA); WriteWord(0xE9E + 0x44, kDkc2CoopSlotB);
    WriteWord(0xE9E + 0x20, 0x480); WriteWord(0x64, jumper);
    WriteWord(0x595, 0x16B2); WriteWord(0x599, 0x16D8);
    WriteWord(0x16B2 + 0x0E, 0x1234); WriteWord(0x16D8 + 0x0E, 0x4321);
    WriteWord(jumper + 0x42, 0xE9E); WriteWord(other + 0x42, 0xE9E);
    WriteWord(jumper + 0x2E, 0x67); WriteWord(other + 0x2E, 0x5B);
    WriteWord(jumper + 0x24, 0xFE00);
    ExpectU16("airborne correction uses own work record", Dkc2CoopCartWorkValue(&cpu, 0x16B2), who ? 0x16D8 : 0x16B2);
    ExpectBool("each rider departs the old cart independently", Dkc2CoopCartDetach(&cpu), 1);
    ExpectU16("departure removes only own seat", cpu_read16(&cpu, 0, 0xE9E + 0x42 + who * 2), 0);
    ExpectU16("departure retains teammate seat", cpu_read16(&cpu, 0, 0xE9E + 0x42 + (1 - who) * 2), other);
    ExpectU16("departure inherits forward momentum", cpu_read16(&cpu, 0, jumper + 0x20), 0x480);
    WriteWord(0x593, jumper);
    ExpectBool("detached survivor keeps its own collision box", Dkc2CoopCartClip(&cpu), 1);
    WriteWord(0x593, kDkc2CoopSlotA);
    ExpectU16("departure retains vertical jump", cpu_read16(&cpu, 0, jumper + 0x24), 0xFE00);
    ExpectU16("departure does not detach teammate", cpu_read16(&cpu, 0, other + 0x42), 0xE9E);
    ExpectU16("departure clears own old cart correction", cpu_read16(&cpu, 0, (who ? 0x16D8 : 0x16B2) + 0x0E), 0);
    ExpectU16("departure preserves teammate movement context", cpu_read16(&cpu, 0, (who ? 0x16B2 : 0x16D8) + 0x0E), who ? 0x1234 : 0x4321);
    WriteWord(0xA86, 0xE9E); WriteWord(0xA88, jumper);
    WriteWord(0xE9E + 10, 600); WriteWord(jumper + 10, 550);
    ExpectBool("rising rider cannot be auto-recruited", Dkc2CoopCartEntryBlocked(&cpu), 1);
    WriteWord(jumper + 0x24, 0x200); WriteWord(jumper + 10, 595);
    ExpectBool("new cart accepts descending contact", Dkc2CoopCartEntryBlocked(&cpu), 0);
    WriteWord(jumper + 10, 565);
    ExpectBool("native foot hitbox can accept contact above the seat", Dkc2CoopCartEntryBlocked(&cpu), 0);
    ExpectU16("accepted new cart contact reaches its seat", cpu_read16(&cpu, 0, jumper + 10), 600);
    ExpectU16("accepted new cart contact ends falling", cpu_read16(&cpu, 0, jumper + 0x24), 0);
    WriteWord(jumper + 0x42, 0xE9E);
    ExpectBool("native follower reunion cannot cancel independent arc", Dkc2CoopCartEntryBlocked(&cpu), 1);
    WriteWord(jumper + 0x42, 0x140); WriteWord(jumper + 0x2E, 0x12);
    ExpectBool("initial barrel arrival retains native recruitment", Dkc2CoopCartEntryBlocked(&cpu), 0);
  }
}

int main(void) {
  TestTerrainHurtOwner();
  TestCartScriptedExit();
  TestCartCannonCapture();
  TestCartPartnerLanding();
  TestCartPitAndStomp();
  TestSharedKartLeader();
  TestIndependentCart();
  TestKleeverSharedHooks();
  TestScriptedIntro();
  TestBarrelAirControl();
  TestSharedBarrelInput();
  TestSharedBird();
  TestMountedPitCleanup();
  TestMountedHookAndCamera();
  TestPitSurvivor();
  TestRespawnPlacement();
  TestBarrelGravity();
  TestMountedReturnArrival();
  TestAnimalOnlyTurns();
  TestWaterHeldOwnership();
  TestModeAccessors();
  TestModeFromName();
  TestSimultaneousActive();
  TestGateActive();
  TestSelectWords();
  TestFollowerNeedsControl();
  TestWrappersAgainstFakeWram();
  TestStompOwnership();
  TestSharedStomp();
  TestGroundShake();
  TestHoneyOwnership();
  TestEnemyPush();
  TestPoleOwnership();
  TestRecovery();
  TestHeldObjectOwnership();
  TestHurtLifecycle();
  TestAnimalsAndHandoff();
  TestWideMovementBounds();
  TestBananaCollectionPass();
  TestTeamCarry();
  TestThrowIntoHook();
  TestRopeOwnership();
  TestRopeTransitionReload();
  TestSelectRescue();
  TestHoneyRescue();
  TestGoalOwnership();
  TestCameraMidpoint();
  TestCenteredCamera();
  TestCatGrabOwnership();
  TestSeparateHookContacts();
  TestThrownHookContact();
  TestBonusArrivalAndHooks();
  if (s_failures > 0) {
    fprintf(stderr, "%d co-op test(s) failed\n", s_failures);
    return EXIT_FAILURE;
  }
  printf("compiled co-op tuning: Select=%d ms (%d ticks), stomp=%dx%d pixels\n",
         DKC2_COOP_SELECT_HOLD_MS, kDkc2CoopRescueHoldFrames,
         DKC2_COOP_STOMP_HORIZONTAL_PX, DKC2_COOP_STOMP_VERTICAL_PX);
  puts("co-op policy tests passed");
  return EXIT_SUCCESS;
}
