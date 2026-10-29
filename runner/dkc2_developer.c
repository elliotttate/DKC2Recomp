#include "dkc2_developer.h"
#include "../config/developer.cfg"

#include <string.h>

int Dkc2DeveloperBeatLevelKey(void) {
  return DKC2_DEVELOPER_ENABLED ? DKC2_DEVELOPER_BEAT_LEVEL_KEY : 0;
}

Dkc2DeveloperResult Dkc2DeveloperCommand(CpuState *cpu, const char *command) {
  if (!DKC2_DEVELOPER_ENABLED) return kDkc2DeveloperDisabled;
  if (!command || strcmp(command, "beat_level") != 0)
    return kDkc2DeveloperUnknownCommand;
  if (!cpu || cpu_read16(cpu, 0, 0x0020) != 0x8608 ||
      cpu_read16(cpu, 0, 0x0024) != 0x8819 ||
      cpu_read16(cpu, 0, 0x0515) > 3 ||
      (cpu_read16(cpu, 0, 0x059D) != 0 &&
       !(cpu_read16(cpu, 0, 0x0515) == 1 && cpu_read16(cpu, 0, 0x059D) == 1)) ||
      (cpu_read16(cpu, 0, 0x08C2) & 2))
    return kDkc2DeveloperUnavailable;
  uint16 active = cpu_read16(cpu, 0, 0x0593);
  uint16 inactive = cpu_read16(cpu, 0, 0x0597);
  uint16 level = cpu_read16(cpu, 0, 0x08A8);
  uint16 kind = cpu_read16(cpu, 0, 0x0515);
  uint16 room = cpu_read16(cpu, 0, 0x00D3);
  if ((active != 0xDE2 && active != 0xE40) ||
      (inactive != 0xDE2 && inactive != 0xE40) || active == inactive ||
      level >= 0x100 || ((kind == 1 || kind == 3) && room >= 0x100) ||
      !(cpu_read16(cpu, 0, active + 0x30) & 0x10))
    return kDkc2DeveloperUnavailable;
  /* Native set_current_level_as_cleared ($BB:8158/$8169): use the ROM's
   * completion-bit table and current map-level index, not the room ID. */
  uint16 mask = cpu_read16(cpu, 0xBB, (uint16)(0x817F + 2 * (level & 15)));
  if (!mask) return kDkc2DeveloperUnavailable;
  uint16 address = (uint16)(0x59F2 + 2 * (level >> 4));
  if (kind != 1)
    cpu_write16(cpu, 0x7E, address, (uint16)(cpu_read16(cpu, 0x7E, address) | mask));
  if (kind == 1 || kind == 3) {
    /* Native reward collection ($BB:8125, $BE:B9FA): bonus/boss records
     * are keyed by room, not the parent map node. Replays do not mint coins.
     * The native Kremcoin HUD limit is 75 ($BE:B88A/$B8A7). */
    uint16 reward_mask = cpu_read16(cpu, 0xBB, (uint16)(0x817F + 2 * (room & 15)));
    uint16 reward_address = (uint16)(0x59B2 + 2 * (room >> 4));
    uint16 collected = cpu_read16(cpu, 0x7E, reward_address);
    if (!(collected & reward_mask)) {
      uint16 coins = cpu_read16(cpu, 0, 0x08CC);
      if (coins < 75) cpu_write16(cpu, 0, 0x08CC, (uint16)(coins + 1));
      cpu_write16(cpu, 0x7E, reward_address, (uint16)(collected | reward_mask));
    }
    cpu_write16(cpu, 0, 0x08C4, (uint16)(cpu_read16(cpu, 0, 0x08C4) | 2));
    /* Boss skip exits to the map, rather than the type-3 parent-room
     * restoration used by an ordinary sublevel departure. */
    if (kind == 3) cpu_write16(cpu, 0, 0x0515, 0);
  }
  /* Native set_level_end_exit_state/fade ($B8:A5CA/$A5DA and $80:8C03).
   * Let the cartridge perform its normal fade, map exit and progression. */
  cpu_write16(cpu, 0, inactive + 0x2E, 0x13);
  cpu_write16(cpu, 0, active + 0x2E, 0x34);
  cpu_write16(cpu, 0, 0x0513, 0);
  cpu_write16(cpu, 0, 0x0512, 0x830F);
  cpu_write16(cpu, 0, 0x08C2, (uint16)(cpu_read16(cpu, 0, 0x08C2) | 2));
  return kDkc2DeveloperExecuted;
}
