#!/usr/bin/env python3
"""Apply source-owned DKC2 simultaneous co-op adaptations to private generated C.

The patch rewrites the Kong action input gate so that, in "2 PLAYER TEAM"
mode with the simultaneous policy selected, both Kongs read their own
controller:

* The gate compare (LDX current_sprite / CPX $0593 / BEQ) wraps its $0593
  read with Dkc2CoopGateActiveValue, which substitutes current_sprite for
  the active-Kong address so both Kong slots take the input path. With the
  classic policy (or outside TEAM mode) the cartridge value is returned
  and behavior is byte-for-byte stock.
* The input path (LDA $050E/$0510 -> STA $0981/$0983) wraps its reads with
  Dkc2CoopSelectHeldWord/Dkc2CoopSelectPressedWord, which substitute the
  per-controller words ($0502/$0504 held, $0506/$0508 pressed) keyed by
  the Kong slot, matching the cartridge's own slot-to-controller binding
  from set_active_kong ($80:883B).

* The state read in the named Kong dispatcher wraps $2E,x with
  Dkc2CoopSelectStateWord, admitting the waiting second player and preventing
  the normal follower-history state from taking control back after landing.
* Normal follower clipping uses the existing separate hitbox path. Accepted
  stomp reactions remember the colliding Kong and select its bounce handler.
* The normal dim-follower palette offset is suppressed, leaving special
  status palettes on their existing branches.

Gate anchors are instruction-shape based, so the adapter tolerates
revision-dependent PC drift between game revisions. The active-word loads
are located first (their $0983/$0981 store context is unique across the
tree); the gate compare is then the $0593 read above them in the same unit
fed by the current_sprite load and taken by the equality branch. Other
$0593 compares in the tree share the local context but not that position.
Every adaptation is verified by exactly-one-match counts and is idempotent
across regeneration runs.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re


INCLUDE = '#include "dkc2_coop.h"'

CURRENT_SPRITE_READ = re.compile(
    r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0064\)\)")
X16_READ = re.compile(r"uint16 \w+ = cpu_read_x16\(cpu\);")
COMPARE_READ = re.compile(
    r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0593\)\)")
COMPARE_BRANCH = re.compile(r"if \(cpu->_flag_Z == 1\)")
PRESSED_READ = re.compile(
    r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0510\)\)")
HELD_READ = re.compile(
    r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x050e\)\)")
PRESSED_STORE = re.compile(r"cpu_write16\(cpu, [^,]*, \(uint16\)\(0x0983\),")
HELD_STORE = re.compile(r"cpu_write16\(cpu, [^,]*, \(uint16\)\(0x0981\),")
STATE_READ = re.compile(
    r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x002e \+ cpu->X\)\)")
STATE_HANDLER = re.compile(
    r"RecompReturn kong_state_handler_M0X0\(CpuState \*cpu\) \{.*?^\}",
    re.MULTILINE | re.DOTALL)

CONTEXT_WINDOW = 800
GATE_MAX_DISTANCE_FROM_PRESSED = 8192
CLIPPING_ENTRY = "RecompReturn get_active_kong_clipping_M0X0(CpuState *cpu) {"
FOLLOWER_CLIPPING_ENTRY = "RecompReturn CODE_BCFB2C_M0X0(CpuState *cpu) {"
CLIPPING_REDIRECT = """
  /* The existing inactive-Kong clipping path keeps the two hitboxes separate. */
  if (Dkc2CoopUseFollowerClipping(cpu))
    return CODE_BCFB2C_M0X0(cpu);
"""
INTERACTION_HANDLER = re.compile(
    r"RecompReturn set_player_interaction_M0X0\(CpuState \*cpu\) \{.*?^\}",
    re.MULTILINE | re.DOTALL)
BOUNCE_HANDLER = re.compile(
    r"RecompReturn player_interaction_1B_M0X0\(CpuState \*cpu\) \{.*?^\}",
    re.MULTILINE | re.DOTALL)
BOUNCE_CALL = "work_on_active_kong_M0X0(cpu)"
PUSH_ENTRY = "RecompReturn player_interaction_1D_M0X0(CpuState *cpu) {"
PUSH_EARLY = """
  /* A controlled partner receives native knockback, not AI regroup/pickup. */
  if (Dkc2CoopPushIndependent(cpu)) {
    extern RecompReturn CODE_B88936_M0X0(CpuState *cpu);
    return CODE_B88936_M0X0(cpu);
  }
"""
PUSH_REDIRECT = ("(Dkc2CoopPushUsesFollower(cpu) ? "
                 "work_on_inactive_kong_M0X0(cpu) : " + BOUNCE_CALL + ")")
SHARED_STOMP_TRACE = "cpu_trace_block(cpu, 0xB887F4);"
SHARED_STOMP_HOOK = "\n    Dkc2CoopShareStomp(cpu);"
ANIMAL_HIT_TRACE = "cpu_trace_block(cpu, 0xB88DCB);"
ANIMAL_HIT_HOOK = "\n    Dkc2CoopAnimalOnlyHit(cpu);"
BOUNCE_REDIRECT = ("(Dkc2CoopBounceUsesFollower(cpu) ? "
                   "work_on_inactive_kong_M0X0(cpu) : " + BOUNCE_CALL + ")")
QUAKE_REDIRECT = ("(Dkc2CoopQuakeUsesFollower(cpu) ? work_on_inactive_kong_M0X0(cpu) : " + BOUNCE_CALL + ")")
QUAKE_TIMER_TRACE = "cpu_trace_block(cpu, 0xB8AFAA);"
QUAKE_TIMER_SKIP = "\n    if (Dkc2CoopQuakeSkipTimer(cpu)) goto L_AFAD_M0X0;"
QUAKE_TRACE = "cpu_trace_block(cpu, 0xB88150);"
QUAKE_REPEAT = "\n    if (Dkc2CoopQuakeRepeat(cpu)) goto L_811E_M0X0;"
ROPE_FUNCTIONS = ("player_interaction_11", "player_interaction_12", "player_interaction_0A", "player_interaction_07", "player_interaction_08")
ROPE_REDIRECT = ("(Dkc2CoopRopeUsesFollower(cpu) ? "
                 "work_on_inactive_kong_M0X0(cpu) : " + BOUNCE_CALL + ")")
PALETTE_HANDLER = re.compile(
    r"RecompReturn CODE_BB8B66_M0X0\(CpuState \*cpu\) \{.*?^\}",
    re.MULTILINE | re.DOTALL)
PALETTE_OFFSET = re.compile(r"\b0x1e\b")
RECOVERY_HANDLER = re.compile(
    r"RecompReturn CODE_B9D705_M0X0\(CpuState \*cpu\) \{.*?^\}",
    re.MULTILINE | re.DOTALL)
FOLLOWER_READ = re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0597\)\)")
HELD_READ_WORD = re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d7a\)\)")
OWNER_READ = re.compile(r"cpu_read16\(cpu, (?:cpu->DB|0x00), \(uint16\)\(0x0593\)\)")
PICKUP_X_READ = re.compile(r"cpu_read_x16\(cpu\)")
OWNER_FUNCTIONS = {"update_held_sprite_position": 1, "animation_command_89": 1,
                   "CODE_B9D3E7": 1, "animation_command_8B": 1, "animation_command_8C": 1,
                   "CODE_B8D4AE": 5}  # release terrain sweep from the thrower's position

ANIMAL_TYPE_READ = re.compile(r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x006e\)\)")
# Kong movement/reaction and animation entry points, including emitted tail paths.
# Crate spawning keeps the global mounted-animal check.
ANIMAL_TYPE_FUNCTIONS = {
    "CODE_B8830E": 1,
    "CODE_B88936": 2,
    "CODE_B88F14": 1,
    "CODE_B88F39": 4,
    "CODE_B890E1": 1,
    "CODE_B893DF": 1,
    "CODE_B8958F": 1,
    "CODE_B898C2": 1,
    "CODE_B89AE0": 1,
    "CODE_B89B49": 1,
    "CODE_B8A98E": 1,
    "CODE_B8B5C3": 1,
    "CODE_B9D705": 1,
    "CODE_B9DEEF": 1,
    "CODE_B9DF51": 1,
    "CODE_B9DFB6": 1,
    "CODE_B9E003": 2,
    "CODE_B9E021": 1,
    "CODE_B9E0A1": 1,
    "CODE_B9E0D0": 2,
    "CODE_B9E11A": 1,
    "CODE_B9E252": 1,
    "animal_special_action": 3,
    "bank_B8_981E": 1,
    "bank_B8_B4ED": 1,
    "check_if_animal_or_holding_sprite": 1,
    "coop_take_control_start_action": 1,
    "coop_take_control_y_action": 1,
    "dismount_animal_action": 1,
    "get_player_x_move_speed": 2,
    "get_x_acceleration": 1,
    "handle_player_slope_state_and_anim": 1,
    "kong_state_09": 1,
    "kong_state_0A": 1,
    "kong_state_12": 2,
    "kong_state_16": 1,
    "land_animal_attack_action": 1,
    "player_interaction_09": 1,
    "player_interaction_0C": 1,
    "player_interaction_0E": 1,
    "player_interaction_18": 4,
    "player_interaction_19": 4,
    "player_interaction_1B": 1,
    "player_interaction_23": 2,
    "player_interaction_27": 1,
    "set_anim_handle_animal_and_dixie": 1,
    "set_player_jumping_gravity": 1,
    "set_player_normal_gravity": 1,
    "set_player_terminal_velocity": 1,
    "set_player_terminal_velocity_down": 1,
    "set_swimming_state": 1,
    "shoot_web_platform_l_action": 1,
    "shoot_web_platform_r_action": 1,
    "slow_rattly_if_on_ground": 1,
    "start_player_falling": 1,
    "start_player_jumping": 1,
}

POLICY_READS = [
    ("camera_handler", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ae8\)\)"), 1, "Dkc2CoopCatCameraValue"),
    ("spin_cat_o9_tails_with_kong", OWNER_READ, 1, "Dkc2CoopCatOwnerValue"),
    ("CODE_BED7F9", OWNER_READ, 1, "Dkc2CoopCartBoardingLeaderValue"),
    ("CODE_BEDB9C", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0595\)\)"), 1, "Dkc2CoopCartLeaderWorkValue"),
    ("CODE_B8B107", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0595\)\)"), 1, "Dkc2CoopCartWorkValue"),
    ("CODE_BEBE8E", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d5e\)\)"), 1, "Dkc2CoopCartStompValue"),
    ("player_interaction_07", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9DFE5", FOLLOWER_READ, 1, "Dkc2CoopQuakeRecoveryValue"),
    ("bank_B6_819C", OWNER_READ, 1, "Dkc2CoopQuakeCandidateValue"),
    ("CODE_B8B4F7", OWNER_READ, 1, "Dkc2CoopPolePlayerValue"),
    ("CODE_B8B644", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba2\)\)"), 1, "Dkc2CoopPolePendingValue"),
    ("CODE_B8B644", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba0\)\)"), 1, "Dkc2CoopPoleCurrentValue"),
    ("kong_state_45", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba0\)\)"), 1, "Dkc2CoopPoleCurrentValue"),
    ("CODE_B8B4CF", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba0\)\)"), 1, "Dkc2CoopPoleCurrentValue"),
    ("player_interaction_0A", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0a86\)\)"), 1, "Dkc2CoopPoleReactionValue"),
    ("player_interaction_0A", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba2\)\)"), 2, "Dkc2CoopPoleReactionValue"),
    ("player_interaction_0A", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0ba0\)\)"), 1, "Dkc2CoopPoleReactionValue"),
    ("CODE_B88936", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("drop_sprite_if_holding", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("drop_follower_kong_if_teamed_up", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B39986", ANIMAL_TYPE_READ, 1, "Dkc2CoopHookAnimalValue"),
    ("CODE_B39986", re.compile(r"(?:(?<= = )|(?<=Dkc2CoopHookCallbackValue\(cpu, ))0x98bd\b"), 1, "Dkc2CoopHookCallbackValue"),
    ("handle_player_pit_death", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x17c0\)\)"), 1, "Dkc2CoopPitCameraY"),
    # These decisions concern the current Kong, not the shared item slot.
    # Otherwise a partner's jump enters a carrying state and can release
    # the owner's item through its own run/pick input.
    ("CODE_B9D705", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("start_player_jumping", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("start_player_falling", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("handle_player_slope_state_and_anim", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("check_if_animal_or_holding_sprite", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("set_stuck_on_honey_floor_animation", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    # Animation callbacks independently choose jump/landing/carry states and
    # movement speed. Their tail paths must use the same actor-local view.
    ("CODE_B9DEEF", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9DF51", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9E003", HELD_READ_WORD, 2, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9E021", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9E0A1", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9E0D0", HELD_READ_WORD, 2, "Dkc2CoopHeldObjectValue"),
    ("CODE_B9E11A", HELD_READ_WORD, 1, "Dkc2CoopHeldObjectValue"),
    # Single/double-rope hanging, direction, speed and junction completion
    # callbacks normally skip the AI follower. Independent climbers use them.
    ("CODE_B9DAB7", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DAE0", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DB19", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DB45", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    # Swim speed/facing also skips the follower before updating its sprite.
    ("CODE_B9DB6C", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DD61", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DD7C", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DD8E", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DD9C", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DDB7", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DDC9", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DDE8", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9DE17", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B9E013", FOLLOWER_READ, 1, "Dkc2CoopRopeAnimationFollowerValue"),
    ("CODE_B39A5F", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0xb89620[^;\n]*?\)\)\)"), 1, "Dkc2CoopHookLeaderFlags"),
    ("CODE_B8A592", FOLLOWER_READ, 1, "Dkc2CoopCannonTargetValue"),
    ("CODE_B5E560", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0x0005[^;\n]*?\)\)\)"), 1, "Dkc2CoopCameraXFraction"),
    ("CODE_B5E560", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0x0009[^;\n]*?\)\)\)"), 1, "Dkc2CoopCameraYFraction"),
    ("CODE_B3EF84", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x08c2\)\)"), 1, "Dkc2CoopAnimalFollowerFlags"),
    ("CODE_B5E560", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0x0006[^;\n]*?\)\)\)"), 3, "Dkc2CoopCameraXValue"),
    ("CODE_B5E560", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0x000a[^;\n]*?\)\)\)"), 3, "Dkc2CoopCameraYValue"),
    ("level_goal_main_idle_state", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0593\)\)"), 1, "Dkc2CoopGoalCandidateValue"),
    ("level_goal_main_idle_state", CURRENT_SPRITE_READ, 2, "Dkc2CoopGoalAcceptedValue"),
    ("check_for_collision", re.compile(r"cpu_read16\(cpu, 0x00, \(uint16\)\(cpu->D \+ 0x0030 \+ cpu->X\)\)"), 1, "Dkc2CoopGoalCollisionFlags"),
    ("team_up_action", FOLLOWER_READ, 2, "Dkc2CoopTeamPartnerValue"),
    ("team_up_action", re.compile(r"cpu_read16\(cpu, \(uint8\)\([^;\n]*?0x002e[^;\n]*?\)\)\)"), 1, "Dkc2CoopTeamStateValue"),
    ("bank_B5_F776", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d44\)\)"), 1, "Dkc2CoopBananaSecondWidth"),
    ("prevent_sprite_from_leaving_level_x", re.compile(r"(?:(?<= = )|(?<=Dkc2CoopScreenLeftValue\(cpu, ))0x10\b"), 1, "Dkc2CoopScreenLeftValue"),
    ("prevent_sprite_from_leaving_level_x", re.compile(r"(?:(?<= = )|(?<=Dkc2CoopScreenSpanValue\(cpu, ))0xe0\b"), 1, "Dkc2CoopScreenSpanValue"),
    ("kong_state_27", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d66\)\)"), 1, "Dkc2CoopHandoffXValue"),
    ("kong_state_27", re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d6a\)\)"), 1, "Dkc2CoopHandoffYValue"),
    ("handle_animal_mounting", OWNER_READ, 1, "Dkc2CoopMountCandidateValue"),
    ("get_state_death_and_mounting_flags", OWNER_READ, 1, "Dkc2CoopMountCandidateValue"),
    ("snap_follower_kong_to_animal",
     re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x08c2\)\)"),
     1, "Dkc2CoopAnimalFollowerFlags"),
] + [(name, ANIMAL_TYPE_READ, count, "Dkc2CoopAnimalTypeValue")
     for name, count in ANIMAL_TYPE_FUNCTIONS.items()]
POLICY_READS += [
    (name, re.compile(r"cpu_read16\(cpu, cpu->DB, \(uint16\)\(0x0d5e\)\)"), count,
     "Dkc2CoopCartStompValue")
    for name, count in (("CODE_BEBF63", 1), ("CODE_BEC02A", 1),
                        ("CODE_BEC085", 1), ("CODE_BEC169", 3),
                        ("CODE_BEC1D6", 1), ("CODE_BEC21D", 1),
                        ("CODE_BEC267", 1), ("CODE_BEC2C0", 1),
                        ("CODE_BEC359", 1), ("CODE_BEC410", 1),
                        ("CODE_BEC452", 1))
]
MOUNT_ENTRY = "RecompReturn player_interaction_17_M0X0(CpuState *cpu) {"
MOUNT_PREPARE = "\n  Dkc2CoopPrepareAnimalMount(cpu);\n"
MOUNT_COLLISION_CALL = "check_active_kong_collision_M0X0(cpu)"
MOUNT_COLLISION_REDIRECT = ("(Dkc2CoopUseBothMountColliders(cpu) ? "
                            "check_inactive_kong_collision_M0X0(cpu) : " + MOUNT_COLLISION_CALL + ")")
HANDOFF_TRACE = "cpu_trace_block(cpu, 0xB8A16F);"
HANDOFF_SKIP = "\n    if (Dkc2CoopHandoffInPlace(cpu)) goto L_A1AB_M0X0;"
BANANA_TRACE = "cpu_trace_block(cpu, 0xB5F794);"
BANANA_REPEAT = "\n    if (Dkc2CoopBananaNextPass(cpu)) goto L_F789_M0X0;"

CANNON_TRACE = "cpu_trace_block(cpu, 0xB3EFD9);"
CANNON_SKIP = "\n    if (Dkc2CoopCannonIndependent(cpu)) goto L_F01F_M0X0;"

HOOK_TRACE = "cpu_trace_block(cpu, 0xB398E0);"
CAMERA_CENTER_TRACE = "cpu_trace_block(cpu, 0xB5E549);"
CAMERA_CENTER = "\n    Dkc2CoopCameraCenter(cpu);"
CART_HOOKS = (
    ("player_interaction_14", "B892DF", "92E9", "Dkc2CoopCatIndependent"),
    ("CODE_B3F207", "B3F1B0", "F1CF", "Dkc2CoopCatIndependent"),
    ("player_interaction_13", "B8929C", "92AB", "Dkc2CoopCatIndependent"),
    ("spin_cat_o9_tails_with_kong", "B3CEB3", "CEDA", "Dkc2CoopCatIndependent"),
    ("CODE_BED9FD", "BED9F2", "DA30", "Dkc2CoopCartSeats"),
    ("CODE_BEDBE2", "BEDBD7", "DC1F", "Dkc2CoopCartIndependent"),
    ("player_interaction_23", "B88D30", "8D45", "Dkc2CoopCartSurvivor"),
    ("player_interaction_21", "B88A6F", "8A5D", "Dkc2CoopCartSurvivor"),
    ("get_active_kong_clipping", "BCFB1D", "FB1C", "Dkc2CoopCartClip"),
    ("player_interaction_29", "B88347", "83A2", "Dkc2CoopCartPit"),
    ("CODE_B8B107", "B8B05F", "B067", "Dkc2CoopCartDetach"),
    ("CODE_BED7F9", "BED88B", "D8AA", "Dkc2CoopCartIndependent"),
    ("player_interaction_15", "B881DB", "8213", "Dkc2CoopCartEntryBlocked"),
)
CAT_RELEASE_OWNER_TRACE = "cpu_trace_block(cpu, 0xB892B3);"
CAT_RELEASE_OWNER_HOOK = "\n    Dkc2CoopCatReleaseOwner(cpu);"
CAT_OWNER_TRACE = "cpu_trace_block(cpu, 0xB89285);"
CAT_OWNER_HOOK = "\n    Dkc2CoopCatCaptureOwner(cpu);"
CART_OWNER_TRACE = "cpu_trace_block(cpu, 0xB88C36);"
CART_OWNER_HOOK = "\n    Dkc2CoopCartHitOwner(cpu);"
HOOK_RELEASE_TRACE = "cpu_trace_block(cpu, 0xB8C621);"
HOOK_RELEASE = "\n    Dkc2CoopHookRelease(cpu);"
HOOK_OCCUPIED_TRACE = "cpu_trace_block(cpu, 0xB6E2CD);"
HOOK_OCCUPIED_SCAN = """
    if (Dkc2CoopHookSharedAfterCamera(cpu)) {
      extern RecompReturn CODE_B39982_M0X0(CpuState *cpu);
      cpu_write8(cpu, 0, cpu->S, 0xB6); cpu->S = (uint16)(cpu->S - 1);
      cpu_write8(cpu, 0, cpu->S, 0xE2); cpu->S = (uint16)(cpu->S - 1);
      cpu_write8(cpu, 0, cpu->S, 0xCC); cpu->S = (uint16)(cpu->S - 1);
      cpu->host_return_valid = 3;
      uint8 _hook_pb = cpu->PB;
      cpu->PB = 0xB3;
      RecompReturn _hook_result;
      switch (((cpu->m_flag & 1) << 1) | (cpu->x_flag & 1)) {
        case 0: _hook_result = CODE_B39982_M0X0(cpu); break;
        default: _hook_result = interp_tier_run_call_frame(cpu, 0xb39968u, 0xb6e2cdu, 3, NULL); break;
      }
      cpu->PB = _hook_pb;
      if (_hook_result != RECOMP_RETURN_NORMAL) {
        RecompStackPop();
        return (RecompReturn)((int)_hook_result - 1);
      }
    }"""
HOOK_FRAMED_TRACE = "cpu_trace_block(cpu, 0xB3986B);"
HOOK_FRAMED_SCAN = HOOK_OCCUPIED_SCAN.replace(
    "cpu->S, 0xB6", "cpu->S, 0xB3").replace(
    "cpu->S, 0xE2", "cpu->S, 0x98").replace(
    "cpu->S, 0xCC", "cpu->S, 0x6A").replace("0xb6e2cdu", "0xb3986bu")
PIT_TRACES = ("cpu_trace_block(cpu, 0xB8B481);", "cpu_trace_block(cpu, 0xB8B499);")
PIT_SKIP = "\n    if (Dkc2CoopPitSurvivor(cpu)) goto L_B4CA_M0X0;"
HOOK_SKIP = "\n    if (Dkc2CoopHookIndependent(cpu)) goto L_990B_M0X0;"
HOOK_CALL = "hook_main_state_01_M0X0(cpu)"
HOOK_REDIRECT = ("(Dkc2CoopHookShared(cpu) ? hook_main_state_00_M0X0(cpu) : " + HOOK_CALL + ")")
HOOK_OWNER_CALL = "work_on_active_kong_global_M0X0(cpu)"
HOOK_OWNER_REDIRECT = ("(Dkc2CoopHookUsesFollower(cpu) ? work_on_inactive_kong_global_M0X0(cpu) : " + HOOK_OWNER_CALL + ")")



def require_action_calls(sources):
    targets = named_candidates(sources, "handle_player_pit_death", re.compile(r"L_B4CA_M0X0:"))
    if len(targets) != 1:
        raise ValueError("expected one inactive Kong pit completion")
    for trace in PIT_TRACES:
        anchors = named_candidates(sources, "handle_player_pit_death", re.compile(re.escape(trace)))
        if len(anchors) != 1 or anchors[0][0] != targets[0][0]:
            raise ValueError("expected one accepted pit block " + trace)
    count = sum(s.count("Dkc2CoopPitSurvivor(") for s in sources.values())
    anchored = sum(s.count(trace + PIT_SKIP) for s in sources.values() for trace in PIT_TRACES)
    if count != anchored:
        raise ValueError("out of place pit survivor adaptation")
    centers = named_candidates(sources, "camera_handler", re.compile(re.escape(CAMERA_CENTER_TRACE)))
    if len(centers) != 1:
        raise ValueError("expected one camera bounds/streaming continuation")
    count = sum(s.count("Dkc2CoopCameraCenter(") for s in sources.values())
    if count and (count != 1 or CAMERA_CENTER_TRACE + CAMERA_CENTER not in sources[centers[0][0]]):
        raise ValueError("out of place camera centering adaptation")
    releases = named_candidates(sources, "jump_off_hook_action", re.compile(re.escape(HOOK_RELEASE_TRACE)))
    occupied = named_candidates(sources, "CODE_B6E21D", re.compile(re.escape(HOOK_OCCUPIED_TRACE)))
    if len(occupied) != 1:
        raise ValueError("expected one hook occupied-camera continuation")
    framed = named_candidates(sources, "hook_main_state_01", re.compile(re.escape(HOOK_FRAMED_TRACE)))
    if len(framed) != 1:
        raise ValueError("expected one ordinary hook camera continuation")
    count = sum(s.count("Dkc2CoopHookSharedAfterCamera(") for s in sources.values())
    anchored = sum(HOOK_OCCUPIED_TRACE + HOOK_OCCUPIED_SCAN in sources[path] for path, _, _ in occupied)
    anchored += sum(HOOK_FRAMED_TRACE + HOOK_FRAMED_SCAN in sources[path] for path, _, _ in framed)
    if count != anchored:
        raise ValueError("out of place hook occupied-camera adaptation")
    if len(releases) != 1:
        raise ValueError("expected one accepted hook jump block")
    count = sum(s.count("Dkc2CoopHookRelease(") for s in sources.values())
    if count and (count != 1 or HOOK_RELEASE_TRACE + HOOK_RELEASE not in sources[releases[0][0]]):
        raise ValueError("out of place hook release adaptation")
    repeats = named_candidates(sources, "bank_B5_F776", re.compile(re.escape(BANANA_TRACE)))
    targets = named_candidates(sources, "bank_B5_F776", re.compile(r"L_F789_M0X0:"))
    if len(repeats) != 1 or len(targets) != 1 or repeats[0][0] != targets[0][0]:
        raise ValueError("expected one banana group continuation and collision label")
    count = sum(s.count("Dkc2CoopBananaNextPass(") for s in sources.values())
    if count and (count != 1 or BANANA_TRACE + BANANA_REPEAT not in sources[repeats[0][0]]):
        raise ValueError("out of place banana group adaptation")
    handoffs = named_candidates(sources, "kong_state_27", re.compile(re.escape(HANDOFF_TRACE)))
    targets = named_candidates(sources, "kong_state_27", re.compile(r"L_A1AB_M0X0:"))
    if len(handoffs) != 1 or len(targets) != 1 or handoffs[0][0] != targets[0][0]:
        raise ValueError("expected one handoff movement entry and completion label")
    count = sum(s.count("Dkc2CoopHandoffInPlace(") for s in sources.values())
    if count and (count != 1 or HANDOFF_TRACE + HANDOFF_SKIP not in sources[handoffs[0][0]]):
        raise ValueError("out of place handoff movement adaptation")
    cannon = named_candidates(sources, "CODE_B3F069", re.compile(re.escape(CANNON_TRACE)))
    targets = named_candidates(sources, "CODE_B3F069", re.compile(r"L_F01F_M0X0:"))
    if len(cannon) != 1 or len(targets) != 1 or cannon[0][0] != targets[0][0]:
        raise ValueError("expected cannon follower entry and active-owner continuation")
    count = sum(s.count("Dkc2CoopCannonIndependent(") for s in sources.values())
    if count and (count != 1 or CANNON_TRACE + CANNON_SKIP not in sources[cannon[0][0]]):
        raise ValueError("out of place cannon follower adaptation")
    hooks = named_candidates(sources, "CODE_B398D7", re.compile(re.escape(HOOK_TRACE)))
    targets = named_candidates(sources, "CODE_B398D7", re.compile(r"L_990B_M0X0:"))
    if len(hooks) != 1 or len(targets) != 1 or hooks[0][0] != targets[0][0]:
        raise ValueError("expected one hook follower block and owner continuation")
    count = sum(s.count("Dkc2CoopHookIndependent(") for s in sources.values())
    if count and (count != 1 or HOOK_TRACE + HOOK_SKIP not in sources[hooks[0][0]]):
        raise ValueError("out of place hook follower adaptation")
    calls = named_candidates(sources, "CODE_B3992E", re.compile(re.escape(HOOK_OWNER_CALL)))
    if len(calls) != 1:
        raise ValueError("expected one hook owner selection")
    path, pos, length = calls[0]
    prefix = HOOK_OWNER_REDIRECT[:-len(HOOK_OWNER_CALL)-1]
    anchored = sources[path][pos-len(prefix):pos+length+1] == HOOK_OWNER_REDIRECT
    if sum(s.count("Dkc2CoopHookUsesFollower(") for s in sources.values()) != int(anchored):
        raise ValueError("out of place mounted hook owner call")
    calls = named_candidates(sources, "hook_main", re.compile(re.escape(HOOK_CALL)))
    if len(calls) != 1:
        raise ValueError("expected one occupied-hook state call")
    path, pos, length = calls[0]
    prefix = HOOK_REDIRECT[:-len(HOOK_CALL)-1]
    anchored = sources[path][pos-len(prefix):pos+length+1] == HOOK_REDIRECT
    if sum(s.count("Dkc2CoopHookShared(") for s in sources.values()) != int(anchored):
        raise ValueError("out of place shared-hook collision call")
    anchored_calls = 0
    for name in ("handle_animal_mounting", "level_goal_main_idle_state"):
        calls = named_candidates(sources, name, re.compile(re.escape(MOUNT_COLLISION_CALL)))
        if len(calls) != 1:
            raise ValueError(f"expected one mounting/goal collision call in {name}")
        path, pos, length = calls[0]
        prefix = MOUNT_COLLISION_REDIRECT[:-len(MOUNT_COLLISION_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] == MOUNT_COLLISION_REDIRECT:
            anchored_calls += 1
    count = sum(s.count("Dkc2CoopUseBothMountColliders(") for s in sources.values())
    if count != anchored_calls:
        raise ValueError("out of place mounting/goal collision call")
    helpers = {row[3] for row in POLICY_READS}
    anchored = dict.fromkeys(helpers, 0)
    for name, pattern, expected, helper in POLICY_READS:
        candidates = named_candidates(sources, name, pattern)
        if len(candidates) != expected:
            raise ValueError(f"expected {expected} policy operand(s) in {name}; found {len(candidates)}")
        prefix = f"{helper}(cpu, "
        for path, pos, length in candidates:
            if sources[path][pos-len(prefix):pos] == prefix and sources[path][pos+length:pos+length+1] == ")":
                anchored[helper] += 1
    for helper in helpers:
        if sum(s.count(helper + "(") for s in sources.values()) != anchored[helper]:
            raise ValueError(f"out of place {helper} adaptation")
    entries = [p for p, s in sources.items() for _ in range(s.count(MOUNT_ENTRY))]
    if len(entries) != 1:
        raise ValueError("expected one accepted animal mount entry")
    count = sum(s.count("Dkc2CoopPrepareAnimalMount(") for s in sources.values())
    if count and (count != 1 or MOUNT_ENTRY + MOUNT_PREPARE not in sources[entries[0]]):
        raise ValueError("out of place animal mount preparation")


def named_candidates(sources, name, pattern):
    handler_re = re.compile(r"RecompReturn " + re.escape(name) +
                            r"_M0X0\(CpuState \*cpu\) \{.*?^\}",
                            re.MULTILINE | re.DOTALL)
    return [(path, handler.start() + match.start(), len(match.group()))
            for path, source in sources.items()
            for handler in handler_re.finditer(source)
            for match in pattern.finditer(handler.group())]


def pickup_candidates(sources):
    return [candidate for candidate in named_candidates(
                sources, "update_object_pickup", PICKUP_X_READ)
            if re.search(r"cpu_write16\(cpu, cpu->DB, \(uint16\)\(0x0d7a\),",
                         sources[candidate[0]][candidate[1]:candidate[1] + 160])]


def require_owner_calls(sources):
    # One owner read in each named animation/attachment function, rather than
    # replacing unrelated active-Kong accesses throughout the generated tree.
    total = sum(s.count("Dkc2CoopHeldOwnerValue(") for s in sources.values())
    wrapped = 0
    for name, expected in OWNER_FUNCTIONS.items():
        candidates = named_candidates(sources, name, OWNER_READ)
        if len(candidates) != expected:
            raise ValueError(f"expected exactly {expected} held owner reads in {name}")
        for path, pos, length in candidates:
            prefix = "Dkc2CoopHeldOwnerValue(cpu, "
            if sources[path][pos-len(prefix):pos] == prefix:
                if sources[path][pos+length:pos+length+1] != ")":
                    raise ValueError("misplaced held owner wrapper")
                wrapped += 1
    if total != wrapped:
        raise ValueError("ambiguous existing held owner adaptation")


def recovery_candidates(sources):
    handlers = [(path, handler) for path, source in sources.items()
                for handler in RECOVERY_HANDLER.finditer(source)]
    if len(handlers) != 1:
        raise ValueError("expected exactly one Kong recovery handler")
    path, handler = handlers[0]
    reads = list(FOLLOWER_READ.finditer(handler.group()))
    # Only the entry CPX distinguishes follower recovery. The later CMP
    # distinguishes a carried Kong from a carried object and stays stock.
    if len(reads) != 2:
        raise ValueError("expected two follower reads in Kong recovery")
    match = reads[0]
    return [(path, handler.start() + match.start(), len(match.group()))]


def palette_candidates(sources):
    return [(path, handler.start() + match.start(), len(match.group()))
            for path, source in sources.items()
            for handler in PALETTE_HANDLER.finditer(source)
            for match in PALETTE_OFFSET.finditer(handler.group())]


def interaction_candidates(sources):
    return [(path, handler.start() + match.start(), len(match.group()))
            for path, source in sources.items()
            for handler in INTERACTION_HANDLER.finditer(source)
            for match in CURRENT_SPRITE_READ.finditer(handler.group())]


def bounce_candidate(sources):
    candidates = [(path, handler) for path, source in sources.items()
                  for handler in BOUNCE_HANDLER.finditer(source)]
    if len(candidates) != 1 or candidates[0][1].group().count(BOUNCE_CALL) != 1:
        raise ValueError("expected exactly one stomp reaction active-Kong call")
    path, handler = candidates[0]
    redirects = sum(s.count("Dkc2CoopBounceUsesFollower(") for s in sources.values())
    if redirects and (redirects != 1 or BOUNCE_REDIRECT not in handler.group()):
        raise ValueError("ambiguous existing stomp reaction adaptation")
    return path, handler, redirects


def require_quake_calls(sources):
    timers = named_candidates(sources, "kong_state_63", re.compile(re.escape(QUAKE_TIMER_TRACE)))
    exits = named_candidates(sources, "kong_state_63", re.compile("L_AFAD_M0X0:"))
    if len(timers) != 1 or len(exits) != 1:
        raise ValueError("expected one quake reaction timer and exit")
    anchored_timer = QUAKE_TIMER_TRACE + QUAKE_TIMER_SKIP in sources[timers[0][0]]
    if sum(s.count("Dkc2CoopQuakeSkipTimer(") for s in sources.values()) != int(anchored_timer):
        raise ValueError("out of place quake timer adaptation")
    calls = named_candidates(sources, "player_interaction_0B", re.compile(re.escape(BOUNCE_CALL)))
    traces = named_candidates(sources, "player_interaction_0B", re.compile(re.escape(QUAKE_TRACE)))
    targets = named_candidates(sources, "player_interaction_0B", re.compile("L_811E_M0X0:"))
    if len(calls) != 1 or len(traces) != 1 or len(targets) != 1:
        raise ValueError("expected one quake reaction call, completion and restart")
    path, pos, length = calls[0]
    adapted = sources[path][pos-len(QUAKE_REDIRECT)+length+1:pos+length+1] == QUAKE_REDIRECT
    if sum(s.count("Dkc2CoopQuakeUsesFollower(") for s in sources.values()) != int(adapted):
        raise ValueError("out of place quake owner adaptation")
    anchored = QUAKE_TRACE + QUAKE_REPEAT in sources[traces[0][0]]
    if sum(s.count("Dkc2CoopQuakeRepeat(") for s in sources.values()) != int(anchored):
        raise ValueError("out of place quake repeat adaptation")


def require_rope_calls(sources):
    anchored = 0
    for name in ROPE_FUNCTIONS:
        calls = named_candidates(sources, name, re.compile(re.escape(BOUNCE_CALL)))
        if len(calls) != 1:
            raise ValueError(f"expected one rope reaction active-Kong call in {name}")
        path, pos, length = calls[0]
        prefix = ROPE_REDIRECT[:-len(BOUNCE_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] == ROPE_REDIRECT:
            anchored += 1
    if sum(s.count("Dkc2CoopRopeUsesFollower(") for s in sources.values()) != anchored:
        raise ValueError("out of place rope reaction adaptation")


def generated_units(generated_dir: Path) -> list[Path]:
    return sorted(generated_dir.glob("*.c"))


def qualifies(text: str, position: int, requirements) -> bool:
    """True when every (pattern, pre_window, post_window) requirement is
    met at `position`; a zero window skips that side."""
    for pattern, pre_window, post_window in requirements:
        pre = text[max(0, position - pre_window):position]
        post = text[position:position + post_window]
        if (pre_window and not pattern.search(pre)) or \
                (post_window and not pattern.search(post)):
            return False
    return True


def find_candidates(
        sources: dict[Path, str], read_pattern: re.Pattern[str],
        alternative_requirements: list) -> list[tuple[Path, int, int]]:
    """Every read position satisfying any single requirement set."""
    candidates: list[tuple[Path, int, int]] = []
    for path, text in sources.items():
        start = 0
        while True:
            match = read_pattern.search(text, start)
            if not match:
                break
            position = match.start()
            if any(qualifies(text, position, requirements)
                   for requirements in alternative_requirements):
                candidates.append((path, position, match.end() - position))
            start = match.end()
    return candidates


def require_single(candidates: list[tuple[Path, int, int]], what: str,
                   helper: str, sources: dict[Path, str]) -> bool:
    """Fail closed unless exactly one candidate exists. Returns True when
    the wrap must be applied; False when the one existing helper call is
    already anchored at that candidate (idempotency)."""
    already = [(path, match.start()) for path, text in sources.items()
               for match in re.finditer(r"\b" + helper + r"\(", text)]
    if already:
        if len(already) != 1 or len(candidates) != 1:
            raise ValueError(f"ambiguous existing {helper} adaptation")
        path, position, length = candidates[0]
        prefix = f"{helper}(cpu, "
        if already[0] != (path, position - len(prefix)) or \
                sources[path][position + length:position + length + 1] != ")":
            raise ValueError(f"existing {helper} call is out of place")
        return False
    if len(candidates) != 1:
        raise ValueError(
            f"expected exactly one {what} candidate across generated "
            f"sources; found {len(candidates)}")
    return True


def wrap_candidate(sources: dict[Path, str],
                   candidate: tuple[Path, int, int], helper: str) -> None:
    path, position, length = candidate
    text = sources[path]
    wrapped = f"{helper}(cpu, {text[position:position + length]})"
    sources[path] = text[:position] + wrapped + text[position + length:]


def state_candidates(sources: dict[Path, str]) -> list[tuple[Path, int, int]]:
    return [(path, handler.start() + match.start(), len(match.group()))
            for path, source in sources.items()
            for handler in STATE_HANDLER.finditer(source)
            for match in STATE_READ.finditer(handler.group())]


def apply_overrides(generated_dir: Path) -> list[Path]:
    paths = generated_units(generated_dir)
    if not paths:
        raise ValueError("no generated C units found")
    sources = {path: path.read_text(encoding="utf-8") for path in paths}
    originals = sources.copy()
    # Normalize the earlier framed-hook scan's short return frame. The scan
    # entry is a native long-call wrapper even when called within bank $B3.
    legacy_framed_scan = HOOK_FRAMED_SCAN.replace(
        "cpu_write8(cpu, 0, cpu->S, 0xB3); cpu->S = (uint16)(cpu->S - 1);\n", "").replace(
        "cpu->host_return_valid = 3", "cpu->host_return_valid = 2").replace(
        "0xb3986bu, 3", "0xb3986bu, 2")
    for path in sources:
        sources[path] = sources[path].replace(legacy_framed_scan, HOOK_FRAMED_SCAN)

    release_anchors = named_candidates(sources, "player_interaction_14", re.compile(re.escape(CAT_RELEASE_OWNER_TRACE)))
    if len(release_anchors) != 1:
        raise ValueError("expected one cat release ownership continuation")
    release_path, release_pos, release_length = release_anchors[0]
    release_count = sum(text.count("Dkc2CoopCatReleaseOwner(") for text in sources.values())
    if release_count and (release_count != 1 or CAT_RELEASE_OWNER_TRACE + CAT_RELEASE_OWNER_HOOK not in sources[release_path]):
        raise ValueError("out of place cat release ownership adaptation")
    if not release_count:
        sources[release_path] = sources[release_path][:release_pos+release_length] + CAT_RELEASE_OWNER_HOOK + sources[release_path][release_pos+release_length:]
    cat_anchors = named_candidates(sources, "player_interaction_13", re.compile(re.escape(CAT_OWNER_TRACE)))
    if len(cat_anchors) != 1:
        raise ValueError("expected one cat capture ownership continuation")
    cat_path, cat_pos, cat_length = cat_anchors[0]
    cat_count = sum(text.count("Dkc2CoopCatCaptureOwner(") for text in sources.values())
    if cat_count and (cat_count != 1 or CAT_OWNER_TRACE + CAT_OWNER_HOOK not in sources[cat_path]):
        raise ValueError("out of place cat capture ownership adaptation")
    if not cat_count:
        sources[cat_path] = sources[cat_path][:cat_pos+cat_length] + CAT_OWNER_HOOK + sources[cat_path][cat_pos+cat_length:]
    anchors = named_candidates(sources, "player_interaction_23", re.compile(re.escape(CART_OWNER_TRACE)))
    if len(anchors) != 1:
        raise ValueError("expected one coaster damage ownership continuation")
    path, pos, length = anchors[0]
    count = sum(s.count("Dkc2CoopCartHitOwner(") for s in sources.values())
    if count and (count != 1 or CART_OWNER_TRACE + CART_OWNER_HOOK not in sources[path]):
        raise ValueError("out of place coaster damage ownership adaptation")
    if not count:
        sources[path] = sources[path][:pos+length] + CART_OWNER_HOOK + sources[path][pos+length:]
    for function, pc, target, helper in CART_HOOKS:
        trace = f"cpu_trace_block(cpu, 0x{pc});"
        hook = f"\n    if ({helper}(cpu)) goto L_{target}_M0X0;"
        anchors = named_candidates(sources, function, re.compile(re.escape(trace)))
        labels = named_candidates(sources, function, re.compile(f"L_{target}_M0X0:"))
        if len(anchors) != 1 or len(labels) != 1 or anchors[0][0] != labels[0][0]:
            raise ValueError("expected one coaster continuation for " + helper)
        path, pos, length = anchors[0]
        occurrences = named_candidates(sources, function, re.compile(re.escape(helper + "(")))
        count = len(occurrences)
        if count and (count != 1 or trace + hook not in sources[path]):
            raise ValueError("out of place coaster adaptation for " + helper)
        if not count:
            sources[path] = sources[path][:pos+length] + hook + sources[path][pos+length:]
    # Upgrade the earlier follower-flight adaptation: mounted hook contact now
    # uses the native direct-attachment callback, whose work selection is below.
    for path, pos, length in named_candidates(sources, "CODE_B398D7", re.compile(re.escape(HOOK_OWNER_REDIRECT))):
        sources[path] = sources[path][:pos] + HOOK_OWNER_CALL + sources[path][pos+length:]
    require_action_calls(sources)
    require_owner_calls(sources)
    require_single(pickup_candidates(sources), "accepted object pickup",
                   "Dkc2CoopRecordPickupValue", sources)
    bounce_candidate(sources)
    pushes = named_candidates(sources, "CODE_B88936", re.compile(re.escape(BOUNCE_CALL)))
    push_entries = [path for path, source in sources.items() if PUSH_ENTRY in source]
    if len(pushes) != 1 or len(push_entries) != 1:
        raise ValueError("expected native follower and active enemy push handlers")
    for helper, expected, path in (("Dkc2CoopPushIndependent(", PUSH_ENTRY + PUSH_EARLY, push_entries[0]),
                                    ("Dkc2CoopPushUsesFollower(", PUSH_REDIRECT, pushes[0][0])):
        count = sum(s.count(helper) for s in sources.values())
        if count and (count != 1 or expected not in sources[path]):
            raise ValueError("out of place native enemy push adaptation")
    stomp_ends = named_candidates(sources, "player_interaction_1B", re.compile(re.escape(SHARED_STOMP_TRACE)))
    if len(stomp_ends) != 1:
        raise ValueError("expected exactly one completed on-foot stomp continuation")
    stomp_count = sum(s.count("Dkc2CoopShareStomp(") for s in sources.values())
    if stomp_count and (stomp_count != 1 or SHARED_STOMP_TRACE + SHARED_STOMP_HOOK not in sources[stomp_ends[0][0]]):
        raise ValueError("out of place shared stomp adaptation")
    require_rope_calls(sources)
    require_quake_calls(sources)
    animal_hits = named_candidates(sources, "player_interaction_23", re.compile(re.escape(ANIMAL_HIT_TRACE)))
    if len(animal_hits) != 1:
        raise ValueError("expected exactly one surviving animal hit continuation")
    hit_count = sum(s.count("Dkc2CoopAnimalOnlyHit(") for s in sources.values())
    if hit_count and (hit_count != 1 or ANIMAL_HIT_TRACE + ANIMAL_HIT_HOOK not in sources[animal_hits[0][0]]):
        raise ValueError("out of place animal hit adaptation")
    require_single(recovery_candidates(sources), "Kong recovery follower compare",
                   "Dkc2CoopRecoveryFollowerValue", sources)
    require_single(palette_candidates(sources), "follower palette offset",
                   "Dkc2CoopFollowerPaletteOffset", sources)
    require_single(interaction_candidates(sources), "interaction source read",
                   "Dkc2CoopRecordInteractionSource", sources)
    clipping = [path for path, source in sources.items()
                for _ in range(source.count(CLIPPING_ENTRY))]
    if len(clipping) != 1 or sum(s.count(FOLLOWER_CLIPPING_ENTRY)
                                 for s in sources.values()) != 1:
        raise ValueError("expected exactly one active and inactive clipping entry")
    clipping_path = clipping[0]
    redirects = sum(s.count("Dkc2CoopUseFollowerClipping(") for s in sources.values())
    if redirects and (redirects != 1 or CLIPPING_ENTRY + CLIPPING_REDIRECT
                      not in sources[clipping_path]):
        raise ValueError("ambiguous existing follower clipping adaptation")
    state = state_candidates(sources)
    require_single(state, "Kong state dispatch read",
                   "Dkc2CoopSelectStateWord", sources)

    # Input path first: the active pressed/held word loads feeding the
    # $0983/$0981 stores are gate-unique across the tree.
    pressed = find_candidates(
        sources, PRESSED_READ, [[(PRESSED_STORE, 0, CONTEXT_WINDOW)]])
    require_single(pressed, "active pressed word load",
                   "Dkc2CoopSelectPressedWord", sources)
    held = find_candidates(
        sources, HELD_READ, [[(HELD_STORE, 0, CONTEXT_WINDOW)]])
    require_single(held, "active held word load",
                   "Dkc2CoopSelectHeldWord", sources)

    # Gate compare: the $0593 read fed by current_sprite and taken by the
    # equality branch into the input path. Other $0593 compares share the
    # local context, so the discriminator is position: the real gate sits
    # above the pressed-word load it admits into (same unit, bounded
    # distance). The fall-through CLC path (screen fades) is untouched.
    pressed_path, pressed_pos, _ = pressed[0]
    gate = [candidate for candidate in find_candidates(
                sources, COMPARE_READ,
                [[(CURRENT_SPRITE_READ, CONTEXT_WINDOW, 0),
                  (COMPARE_BRANCH, 0, CONTEXT_WINDOW)],
                 [(X16_READ, CONTEXT_WINDOW, 0),
                  (COMPARE_BRANCH, 0, CONTEXT_WINDOW)]])
            if candidate[0] == pressed_path and
            candidate[1] < pressed_pos and
            pressed_pos - candidate[1] < GATE_MAX_DISTANCE_FROM_PRESSED]
    require_single(gate, "gate compare", "Dkc2CoopGateActiveValue", sources)

    # Wrap the gate first, then re-find the word loads: inserted text
    # shifts every later position in the unit. Each wrap is skipped when
    # the helper is already anchored there (idempotent regeneration).
    if require_single(gate, "gate compare", "Dkc2CoopGateActiveValue",
                      sources):
        wrap_candidate(sources, gate[0], "Dkc2CoopGateActiveValue")
    pressed = find_candidates(
        sources, PRESSED_READ, [[(PRESSED_STORE, 0, CONTEXT_WINDOW)]])
    if require_single(pressed, "active pressed word load",
                      "Dkc2CoopSelectPressedWord", sources):
        wrap_candidate(sources, pressed[0], "Dkc2CoopSelectPressedWord")
    held = find_candidates(
        sources, HELD_READ, [[(HELD_STORE, 0, CONTEXT_WINDOW)]])
    if require_single(held, "active held word load",
                      "Dkc2CoopSelectHeldWord", sources):
        wrap_candidate(sources, held[0], "Dkc2CoopSelectHeldWord")
    state = state_candidates(sources)
    if require_single(state, "Kong state dispatch read",
                      "Dkc2CoopSelectStateWord", sources):
        wrap_candidate(sources, state[0], "Dkc2CoopSelectStateWord")
    if not redirects:
        sources[clipping_path] = sources[clipping_path].replace(
            CLIPPING_ENTRY, CLIPPING_ENTRY + CLIPPING_REDIRECT, 1)
    interaction = interaction_candidates(sources)
    if require_single(interaction, "interaction source read",
                      "Dkc2CoopRecordInteractionSource", sources):
        wrap_candidate(sources, interaction[0], "Dkc2CoopRecordInteractionSource")
    bounce_path, handler, bounce_redirects = bounce_candidate(sources)
    for path in push_entries:
        if PUSH_ENTRY + PUSH_EARLY not in sources[path]:
            sources[path] = sources[path].replace(PUSH_ENTRY, PUSH_ENTRY + PUSH_EARLY, 1)
    for path, pos, length in named_candidates(sources, "CODE_B88936", re.compile(re.escape(BOUNCE_CALL))):
        prefix = PUSH_REDIRECT[:-len(BOUNCE_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] != PUSH_REDIRECT:
            sources[path] = sources[path][:pos] + PUSH_REDIRECT + sources[path][pos+length:]
        declaration = "RecompReturn work_on_inactive_kong_M0X0(CpuState *cpu);"
        if declaration not in sources[path]:
            sources[path] = sources[path].replace('#include "funcs.h"', '#include "funcs.h"\n' + declaration, 1)
    bounce_path, handler, bounce_redirects = bounce_candidate(sources)
    if not bounce_redirects:
        source = sources[bounce_path]
        sources[bounce_path] = (source[:handler.start()] +
            handler.group().replace(BOUNCE_CALL, BOUNCE_REDIRECT, 1) +
            source[handler.end():])
    for path, pos, length in named_candidates(sources, "kong_state_63", re.compile(re.escape(QUAKE_TIMER_TRACE))):
        if QUAKE_TIMER_TRACE + QUAKE_TIMER_SKIP not in sources[path]:
            sources[path] = sources[path][:pos+length] + QUAKE_TIMER_SKIP + sources[path][pos+length:]
    for path, pos, length in named_candidates(sources, "player_interaction_0B", re.compile(re.escape(BOUNCE_CALL))):
        prefix = QUAKE_REDIRECT[:-len(BOUNCE_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] != QUAKE_REDIRECT:
            sources[path] = sources[path][:pos] + QUAKE_REDIRECT + sources[path][pos+length:]
        declaration = "RecompReturn work_on_inactive_kong_M0X0(CpuState *cpu);"
        if declaration not in sources[path]:
            sources[path] = sources[path].replace('#include "funcs.h"', '#include "funcs.h"\n' + declaration, 1)
    for path, pos, length in named_candidates(sources, "player_interaction_0B", re.compile(re.escape(QUAKE_TRACE))):
        if QUAKE_TRACE + QUAKE_REPEAT not in sources[path]:
            sources[path] = sources[path][:pos+length] + QUAKE_REPEAT + sources[path][pos+length:]
    for name in ROPE_FUNCTIONS:
        path, pos, length = named_candidates(sources, name, re.compile(re.escape(BOUNCE_CALL)))[0]
        prefix = ROPE_REDIRECT[:-len(BOUNCE_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] != ROPE_REDIRECT:
            sources[path] = sources[path][:pos] + ROPE_REDIRECT + sources[path][pos+length:]
        declaration = "RecompReturn work_on_inactive_kong_M0X0(CpuState *cpu);"
        if declaration not in sources[path]:
            sources[path] = sources[path].replace('#include "funcs.h"', '#include "funcs.h"\n' + declaration, 1)
    for path, pos, length in named_candidates(sources, "player_interaction_1B", re.compile(re.escape(SHARED_STOMP_TRACE))):
        if SHARED_STOMP_TRACE + SHARED_STOMP_HOOK not in sources[path]:
            sources[path] = sources[path][:pos+length] + SHARED_STOMP_HOOK + sources[path][pos+length:]
        if '#include "dkc2_coop_assist.h"' not in sources[path]:
            sources[path] = sources[path].replace('#include "funcs.h"', '#include "funcs.h"\n#include "dkc2_coop_assist.h"', 1)
    for path, pos, length in named_candidates(sources, "player_interaction_23", re.compile(re.escape(ANIMAL_HIT_TRACE))):
        if ANIMAL_HIT_TRACE + ANIMAL_HIT_HOOK not in sources[path]:
            sources[path] = sources[path][:pos+length] + ANIMAL_HIT_HOOK + sources[path][pos+length:]
        if '#include "dkc2_coop_assist.h"' not in sources[path]:
            sources[path] = sources[path].replace('#include "funcs.h"', '#include "funcs.h"\n#include "dkc2_coop_assist.h"', 1)
    palette = palette_candidates(sources)
    if require_single(palette, "follower palette offset",
                      "Dkc2CoopFollowerPaletteOffset", sources):
        wrap_candidate(sources, palette[0], "Dkc2CoopFollowerPaletteOffset")
    recovery = recovery_candidates(sources)
    if require_single(recovery, "Kong recovery follower compare",
                      "Dkc2CoopRecoveryFollowerValue", sources):
        wrap_candidate(sources, recovery[0], "Dkc2CoopRecoveryFollowerValue")
    pickup = pickup_candidates(sources)
    if require_single(pickup, "accepted object pickup", "Dkc2CoopRecordPickupValue", sources):
        wrap_candidate(sources, pickup[0], "Dkc2CoopRecordPickupValue")
    for name in OWNER_FUNCTIONS:
        # Right-to-left leaves earlier offsets valid when a function has
        # several branches that read the owner.
        for candidate in reversed(named_candidates(sources, name, OWNER_READ)):
            path, pos, _ = candidate
            prefix = "Dkc2CoopHeldOwnerValue(cpu, "
            if sources[path][pos-len(prefix):pos] != prefix:
                wrap_candidate(sources, candidate, "Dkc2CoopHeldOwnerValue")

    for name, pattern, _, helper in POLICY_READS:
        for candidate in reversed(named_candidates(sources, name, pattern)):
            path, pos, _ = candidate
            prefix = f"{helper}(cpu, "
            if sources[path][pos-len(prefix):pos] != prefix:
                wrap_candidate(sources, candidate, helper)
    for path, source in sources.items():
        if MOUNT_ENTRY in source and MOUNT_ENTRY + MOUNT_PREPARE not in source:
            sources[path] = source.replace(MOUNT_ENTRY, MOUNT_ENTRY + MOUNT_PREPARE, 1)
    for path, pos, length in named_candidates(sources, "kong_state_27", re.compile(re.escape(HANDOFF_TRACE))):
        if HANDOFF_TRACE + HANDOFF_SKIP not in sources[path]:
            sources[path] = sources[path][:pos+length] + HANDOFF_SKIP + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "bank_B5_F776", re.compile(re.escape(BANANA_TRACE))):
        if BANANA_TRACE + BANANA_REPEAT not in sources[path]:
            sources[path] = sources[path][:pos+length] + BANANA_REPEAT + sources[path][pos+length:]

    for name in ("handle_animal_mounting", "level_goal_main_idle_state"):
        for path, pos, length in named_candidates(sources, name, re.compile(re.escape(MOUNT_COLLISION_CALL))):
            prefix = MOUNT_COLLISION_REDIRECT[:-len(MOUNT_COLLISION_CALL)-1]
            if sources[path][pos-len(prefix):pos+length+1] != MOUNT_COLLISION_REDIRECT:
                sources[path] = sources[path][:pos] + MOUNT_COLLISION_REDIRECT + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "CODE_B3F069", re.compile(re.escape(CANNON_TRACE))):
        if CANNON_TRACE + CANNON_SKIP not in sources[path]:
            sources[path] = sources[path][:pos+length] + CANNON_SKIP + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "CODE_B398D7", re.compile(re.escape(HOOK_TRACE))):
        if HOOK_TRACE + HOOK_SKIP not in sources[path]:
            sources[path] = sources[path][:pos+length] + HOOK_SKIP + sources[path][pos+length:]
    for path, pos, length in named_candidates(sources, "CODE_B3992E", re.compile(re.escape(HOOK_OWNER_CALL))):
        prefix = HOOK_OWNER_REDIRECT[:-len(HOOK_OWNER_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] != HOOK_OWNER_REDIRECT:
            sources[path] = sources[path][:pos] + HOOK_OWNER_REDIRECT + sources[path][pos+length:]
    for path, pos, length in named_candidates(sources, "hook_main", re.compile(re.escape(HOOK_CALL))):
        prefix = HOOK_REDIRECT[:-len(HOOK_CALL)-1]
        if sources[path][pos-len(prefix):pos+length+1] != HOOK_REDIRECT:
            sources[path] = sources[path][:pos] + HOOK_REDIRECT + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "camera_handler", re.compile(re.escape(CAMERA_CENTER_TRACE))):
        if CAMERA_CENTER_TRACE + CAMERA_CENTER not in sources[path]:
            sources[path] = sources[path][:pos+length] + CAMERA_CENTER + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "jump_off_hook_action", re.compile(re.escape(HOOK_RELEASE_TRACE))):
        if HOOK_RELEASE_TRACE + HOOK_RELEASE not in sources[path]:
            sources[path] = sources[path][:pos+length] + HOOK_RELEASE + sources[path][pos+length:]

    for path, pos, length in named_candidates(sources, "hook_main_state_01", re.compile(re.escape(HOOK_FRAMED_TRACE))):
        if HOOK_FRAMED_TRACE + HOOK_FRAMED_SCAN not in sources[path]:
            sources[path] = sources[path][:pos+length] + HOOK_FRAMED_SCAN + sources[path][pos+length:]
    for path, pos, length in named_candidates(sources, "CODE_B6E21D", re.compile(re.escape(HOOK_OCCUPIED_TRACE))):
        if HOOK_OCCUPIED_TRACE + HOOK_OCCUPIED_SCAN not in sources[path]:
            sources[path] = sources[path][:pos+length] + HOOK_OCCUPIED_SCAN + sources[path][pos+length:]

    for trace in PIT_TRACES:
        for path, pos, length in named_candidates(sources, "handle_player_pit_death", re.compile(re.escape(trace))):
            if trace + PIT_SKIP not in sources[path]:
                sources[path] = sources[path][:pos+length] + PIT_SKIP + sources[path][pos+length:]

    changed: list[Path] = []
    for path in paths:
        text = sources[path]
        if "Dkc2Coop" not in text:
            continue
        if '#include "dkc2_coop.h"' not in text:
            marker = '#include "funcs.h"'
            if text.count(marker) != 1:
                raise ValueError(
                    f"generated unit {path.name} has an unexpected funcs.h "
                    f"include")
            text = text.replace(marker, marker + "\n" + INCLUDE, 1)
            sources[path] = text
        if any(helper in text for helper in ("Dkc2CoopCat", "Dkc2CoopCart", "Dkc2CoopQuake", "Dkc2CoopGoal", "Dkc2CoopCamera", "Dkc2CoopCannon", "Dkc2CoopHook", "Dkc2CoopPit", "Dkc2CoopPush", "Dkc2CoopPole")) and '#include "dkc2_coop_assist.h"' not in text:
            sources[path] = sources[path].replace(INCLUDE, INCLUDE + '\n#include "dkc2_coop_assist.h"', 1)
        changed.append(path)

    for path in changed:
        if sources[path] != originals[path]:
            path.write_text(sources[path], encoding="utf-8", newline="\n")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", required=True, type=Path)
    args = parser.parse_args()
    generated_dir = args.generated_dir.expanduser().resolve(strict=True)
    changed = apply_overrides(generated_dir)
    for path in changed:
        print(f"Applied DKC2 co-op overrides: {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
