#include "dkc2_developer.h"
#include "../config/developer.cfg"

#include <stdio.h>
#include <string.h>

static uint8 memory[0x10000], before[0x10000];
static int failures;
uint16 cpu_read16(CpuState *cpu, uint8 bank, uint16 address) {
  (void)cpu;
  if (bank == 0xBB && address >= 0x817F && address < 0x819F)
    return (uint16)(0x8000u >> ((address - 0x817F) / 2));
  return (uint16)(memory[address] | ((uint16)memory[(uint16)(address + 1)] << 8));
}
void cpu_write16(CpuState *cpu, uint8 bank, uint16 address, uint16 value) {
  (void)cpu; (void)bank;
  memory[address] = (uint8)value;
  memory[(uint16)(address + 1)] = (uint8)(value >> 8);
}
static void Check(int pass, const char *message) {
  if (!pass) { fprintf(stderr, "%s\n", message); failures++; }
}
static void Setup(CpuState *cpu, unsigned leader, unsigned level) {
  memset(cpu, 0, sizeof *cpu); memset(memory, 0, sizeof memory);
  cpu->A = 0x1234; cpu->S = 0x1F00; cpu->D = 0x1700; cpu->DB = 0xB8;
  cpu_write16(cpu, 0, 0x20, 0x8608);
  cpu_write16(cpu, 0, 0x24, 0x8819);
  cpu_write16(cpu, 0, 0x593, leader ? 0xE40 : 0xDE2);
  cpu_write16(cpu, 0, 0x597, leader ? 0xDE2 : 0xE40);
  cpu_write16(cpu, 0, 0x08A8, (uint16)level);
  cpu_write16(cpu, 0, 0x08C2, 0x4200);
  cpu_write16(cpu, 0, 0xDE2 + 0x30, 0x1E);
  cpu_write16(cpu, 0, 0xE40 + 0x30, 0x1E);
}
int main(void) {
  CpuState cpu, original;
  Check(Dkc2DeveloperBeatLevelKey() == (DKC2_DEVELOPER_ENABLED ? DKC2_DEVELOPER_BEAT_LEVEL_KEY : 0),
        "compiled enable/shortcut gate mismatch");
  for (unsigned leader = 0; leader < 2; ++leader) {
    for (unsigned level = 0; level < 256; ++level) {
      Setup(&cpu, leader, level); original = cpu;
      uint16 address = (uint16)(0x59F2 + 2 * (level >> 4));
      cpu_write16(&cpu, 0, address, 0x0001);
      memcpy(before, memory, sizeof memory);
      Dkc2DeveloperResult result = Dkc2DeveloperCommand(&cpu, "beat_level");
      if (!DKC2_DEVELOPER_ENABLED) {
        Check(result == kDkc2DeveloperDisabled && !memcmp(memory, before, sizeof memory),
              "disabled developer command changed guest memory");
      } else {
        Check(result == kDkc2DeveloperExecuted, "valid command was rejected");
        Check(cpu_read16(&cpu, 0, address) == (uint16)(1 | (0x8000u >> (level & 15))),
              "wrong map-level completion bit or lost previous progress");
        Check(cpu_read16(&cpu, 0, (uint16)((leader ? 0xE40 : 0xDE2) + 0x2E)) == 0x34 &&
              cpu_read16(&cpu, 0, (uint16)((leader ? 0xDE2 : 0xE40) + 0x2E)) == 0x13,
              "level exit did not use native leader/follower states");
        Check(cpu_read16(&cpu, 0, 0x512) == 0x830F && memory[0x514] == 0 &&
              cpu_read16(&cpu, 0, 0x8C2) == 0x4202, "native fade setup mismatch");
        memcpy(before, memory, sizeof memory);
        Check(Dkc2DeveloperCommand(&cpu, "beat_level") == kDkc2DeveloperUnavailable &&
              !memcmp(memory, before, sizeof memory), "repeated command changed an exiting level");
      }
      Check(!memcmp(&cpu, &original, sizeof cpu), "developer command changed CPU continuation");
    }
  }
  for (unsigned kind = 1; kind <= 3; kind += 2) {
    for (unsigned room = 0; room < 256; ++room) {
      for (unsigned replay = 0; replay < 2; ++replay) {
        Setup(&cpu, room & 1, 17); original = cpu;
        uint16 reward_address = (uint16)(0x59B2 + 2 * (room >> 4));
        uint16 reward_mask = (uint16)(0x8000u >> (room & 15));
        cpu_write16(&cpu, 0, 0x515, (uint16)kind);
        cpu_write16(&cpu, 0, 0xD3, (uint16)room);
        cpu_write16(&cpu, 0, 0x59D, kind == 1 ? 1 : 0);
        cpu_write16(&cpu, 0, 0x08CC, room == 255 ? 75 : 3);
        cpu_write16(&cpu, 0, reward_address, replay ? reward_mask : 0);
        memcpy(before, memory, sizeof memory);
        Dkc2DeveloperResult result = Dkc2DeveloperCommand(&cpu, "beat_level");
        if (!DKC2_DEVELOPER_ENABLED) {
          Check(result == kDkc2DeveloperDisabled && !memcmp(memory, before, sizeof memory),
                "disabled special-room command changed memory");
          continue;
        }
        Check(result == kDkc2DeveloperExecuted, "special-room command rejected");
        Check(cpu_read16(&cpu, 0, reward_address) == reward_mask,
              "special-room reward used wrong room index");
        Check(cpu_read16(&cpu, 0, 0x08CC) == (room == 255 ? 75 : replay ? 3 : 4),
              "special reward duplicated coins or exceeded native cap");
        Check(cpu_read16(&cpu, 0, 0x59F4) == (kind == 3 ? 0x4000 : 0),
              "bonus cleared its parent level or boss failed to clear");
        Check(cpu_read16(&cpu, 0, 0x08C4) == 2, "missing reward signal");
        Check(!memcmp(&cpu, &original, sizeof cpu), "special command changed CPU continuation");
      }
    }
  }
  for (unsigned guard = 0; guard < 12; ++guard) {
    Setup(&cpu, 0, 5);
    if (guard == 0) cpu_write16(&cpu, 0, 0x24, 0);
    if (guard == 1) cpu_write16(&cpu, 0, 0x515, 4);
    if (guard == 2) cpu_write16(&cpu, 0, 0x59D, 0xFFFF);
    if (guard == 3) cpu_write16(&cpu, 0, 0x8C2, 2);
    if (guard == 4) cpu_write16(&cpu, 0, 0x593, 0xE9E);
    if (guard == 5) cpu_write16(&cpu, 0, 0x597, 0xDE2);
    if (guard == 6) cpu_write16(&cpu, 0, 0x8A8, 256);
    if (guard == 7) cpu_write16(&cpu, 0, 0xDE2 + 0x30, 0);
    if (guard == 9) { cpu_write16(&cpu, 0, 0x515, 3); cpu_write16(&cpu, 0, 0xD3, 256); }
    if (guard == 10) cpu_write16(&cpu, 0, 0x515, 5);
    if (guard == 11) cpu_write16(&cpu, 0, 0x20, 0x8CC2);
    memcpy(before, memory, sizeof memory);
    Dkc2DeveloperResult result = Dkc2DeveloperCommand(guard == 8 ? NULL : &cpu, "beat_level");
    Check(result == (DKC2_DEVELOPER_ENABLED ? kDkc2DeveloperUnavailable : kDkc2DeveloperDisabled) &&
          !memcmp(memory, before, sizeof memory), "invalid context changed progress");
  }
  Check(Dkc2DeveloperCommand(&cpu, "invalid") ==
        (DKC2_DEVELOPER_ENABLED ? kDkc2DeveloperUnknownCommand : kDkc2DeveloperDisabled),
        "unknown developer command accepted");
  if (!failures) puts("developer command tests passed");
  return failures ? 1 : 0;
}
