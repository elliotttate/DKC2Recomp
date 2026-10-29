import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc2_coop_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_coop_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


# Synthetic generated C shaped like the v2 emitter's output for the Kong
# action input gate: one $0593 read temp feeding the compare, then the
# input path loading the active held/pressed words into $0981/$0983.
GATE_FIXTURE = """\
#include "funcs.h"
RecompReturn gate_M0X0(CpuState *cpu) {
L_B9D9_M0X0:
  cpu_trace_block(cpu, 0xB8B9D9);
  uint16 v_sprite = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0064));
  cpu_write_x_x(cpu, (uint16)(v_sprite));
  uint16 v_active = cpu_read16(cpu, cpu->DB, (uint16)(0x0593));
  uint32 _tc0_1 = (uint32)v_sprite - (uint32)v_active;
  cpu->_flag_C = (v_sprite >= v_active) ? 1 : 0;
  cpu->_flag_Z = ((_tc0_1 & 0xFFFF) == 0) ? 1 : 0;
  if (cpu->_flag_Z == 1) { cpu->cycles += 1; goto L_B9E0_M0X0; }
  goto L_B9DE_M0X0; /* fall-through */
L_B9DE_M0X0:
  cpu_trace_block(cpu, 0xB8B9DE);
  cpu->_flag_C = 0;
  return NORMAL;
L_B9E0_M0X0:
  cpu_trace_block(cpu, 0xB8B9E0);
  uint16 v_pressed = cpu_read16(cpu, cpu->DB, (uint16)(0x0510));
  cpu_write_a_m(cpu, (uint16)(v_pressed));
  uint16 v_stash = cpu_read_a16(cpu);
  cpu_write16(cpu, cpu->DB, (uint16)(0x0983), v_stash);
  uint16 v_held = cpu_read16(cpu, cpu->DB, (uint16)(0x050e));
  cpu_write_a_m(cpu, (uint16)(v_held));
  uint16 v_hold = cpu_read_a16(cpu);
  cpu_write16(cpu, cpu->DB, (uint16)(0x0981), v_hold);
L_B9EF_M0X0:
  cpu_trace_block(cpu, 0xB8B9EF);
  return NORMAL;
}
"""


# Same gate with X already holding current_sprite on entry (a revision
# may not repeat the LDX); only the x16 read and the branch remain as
# context, and the adapter must still find it.
GATE_FIXTURE_PRELOADED_X = """\
#include "funcs.h"
RecompReturn gate_preloaded_M0X0(CpuState *cpu) {
L_C000_M0X0:
  cpu_trace_block(cpu, 0xB8C000);
  uint16 v_sprite = cpu_read_x16(cpu);
  uint16 v_active = cpu_read16(cpu, cpu->DB, (uint16)(0x0593));
  uint32 _tc0_1 = (uint32)v_sprite - (uint32)v_active;
  cpu->_flag_C = (v_sprite >= v_active) ? 1 : 0;
  cpu->_flag_Z = ((_tc0_1 & 0xFFFF) == 0) ? 1 : 0;
  if (cpu->_flag_Z == 1) { cpu->cycles += 1; goto L_C010_M0X0; }
  return NORMAL;
L_C010_M0X0:
  cpu_trace_block(cpu, 0xB8C010);
  uint16 v_pressed = cpu_read16(cpu, cpu->DB, (uint16)(0x0510));
  cpu_write_a_m(cpu, (uint16)(v_pressed));
  uint16 v_stash = cpu_read_a16(cpu);
  cpu_write16(cpu, cpu->DB, (uint16)(0x0983), v_stash);
  uint16 v_held = cpu_read16(cpu, cpu->DB, (uint16)(0x050e));
  cpu_write_a_m(cpu, (uint16)(v_held));
  uint16 v_hold = cpu_read_a16(cpu);
  cpu_write16(cpu, cpu->DB, (uint16)(0x0981), v_hold);
  return NORMAL;
}
"""


STATE_FIXTURE = """
RecompReturn kong_state_handler_M0X0(CpuState *cpu) {
  uint16 state = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x002e + cpu->X));
  return NORMAL;
}
"""
CLIPPING_FIXTURE = """
RecompReturn CODE_BCFB2C_M0X0(CpuState *cpu) {
  return NORMAL;
}
RecompReturn get_active_kong_clipping_M0X0(CpuState *cpu) {
  return NORMAL;
}
"""
COMBAT_FIXTURE = """
RecompReturn CODE_B9D705_M0X0(CpuState *cpu) {
  uint16 follower = cpu_read16(cpu, cpu->DB, (uint16)(0x0597));
  uint16 carried_kong = cpu_read16(cpu, cpu->DB, (uint16)(0x0597));
  return NORMAL;
}
RecompReturn CODE_BB8B66_M0X0(CpuState *cpu) {
  uint16 offset = 0x1e;
  return NORMAL;
}
RecompReturn set_player_interaction_M0X0(CpuState *cpu) {
  uint16 source = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0064));
  cpu_write16(cpu, cpu->DB, (uint16)(0x0a84), source);
  return NORMAL;
}
RecompReturn player_interaction_1B_M0X0(CpuState *cpu) {
  RecompReturn result = work_on_active_kong_M0X0(cpu);
  cpu_trace_block(cpu, 0xB887F4);
  return result;
}
"""
COMBAT_FIXTURE += """
RecompReturn update_object_pickup_M0X0(CpuState *cpu) {
  uint16 held = cpu_read_x16(cpu);
  cpu_write16(cpu, cpu->DB, (uint16)(0x0d7a), held);
  return NORMAL;
}
""" + "".join(f"\nRecompReturn {name}_M0X0(CpuState *cpu) {{\n" +
                "".join(f"  uint16 owner{i} = cpu_read16(cpu, cpu->DB, (uint16)(0x0593));\n"
                        for i in range(count)) + "  return NORMAL;\n}\n"
                for name, count in MODULE.OWNER_FUNCTIONS.items())
# Synthetic reads for the bounded mounting and independent on-foot policy.
ANIMAL_FIXTURE = "\nRecompReturn player_interaction_17_M0X0(CpuState *cpu) {\n  return NORMAL;\n}\n"
for name, pattern, count, helper in MODULE.POLICY_READS:
    address = {"Dkc2CoopHeldObjectValue": "0d7a", "Dkc2CoopBananaSecondWidth": "0d44", "Dkc2CoopAnimalFollowerFlags": "08c2", "Dkc2CoopHandoffXValue": "0d66", "Dkc2CoopHandoffYValue": "0d6a"}.get(helper, "0593")
    read = ("cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x006e))" if helper == "Dkc2CoopAnimalTypeValue"
            else f"cpu_read16(cpu, cpu->DB, (uint16)(0x{address}))")
    read = {"Dkc2CoopScreenLeftValue": "0x10", "Dkc2CoopScreenSpanValue": "0xe0"}.get(helper, read)
    if helper == "Dkc2CoopHookCallbackValue":
        read = "0x98bd"
    if helper == "Dkc2CoopHookAnimalValue":
        read = "cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x006e))"
    if helper == "Dkc2CoopCatCameraValue":
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0ae8))"
    if helper == "Dkc2CoopPitCameraY":
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x17c0))"
    if helper == "Dkc2CoopCartStompValue":
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0d5e))"
    if helper in ("Dkc2CoopCartWorkValue", "Dkc2CoopCartLeaderWorkValue"):
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0595))"
    if helper == "Dkc2CoopHookLeaderFlags":
        read = "cpu_read16(cpu, (uint8)((((uint32)0xb89620 + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0xb89620 + (uint32)cpu->X)))"
    if helper in ("Dkc2CoopTeamPartnerValue", "Dkc2CoopRopeAnimationFollowerValue", "Dkc2CoopCannonTargetValue", "Dkc2CoopQuakeRecoveryValue"):
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0597))"
    if helper in ("Dkc2CoopCameraXValue", "Dkc2CoopCameraYValue", "Dkc2CoopCameraXFraction", "Dkc2CoopCameraYFraction"):
        offset = {"Dkc2CoopCameraXValue": "0006", "Dkc2CoopCameraYValue": "000a", "Dkc2CoopCameraXFraction": "0005", "Dkc2CoopCameraYFraction": "0009"}[helper]
        read = f"cpu_read16(cpu, (uint8)(((((uint32)cpu->DB << 16) + (uint32)0x{offset} + (uint32)cpu->Y)) >> 16), (uint16)((((uint32)cpu->DB << 16) + (uint32)0x{offset} + (uint32)cpu->Y)))"
    if helper == "Dkc2CoopGoalAcceptedValue":
        read = "cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0064))"
    if helper == "Dkc2CoopGoalCollisionFlags":
        read = "cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0030 + cpu->X))"
    if helper.startswith("Dkc2CoopPole"):
        address = next(a for a in ("0ba0", "0ba2", "0a86", "0593") if "0x" + a in pattern.pattern)
        read = f"cpu_read16(cpu, cpu->DB, (uint16)(0x{address}))"
    if helper == "Dkc2CoopTeamStateValue":
        read = "cpu_read16(cpu, (uint8)(((((uint32)cpu->DB << 16) + (uint32)0x002e + (uint32)cpu->Y)) >> 16), (uint16)((((uint32)cpu->DB << 16) + (uint32)0x002e + (uint32)cpu->Y)))"
    entry = f"RecompReturn {name}_M0X0(CpuState *cpu) {{"
    reads = "".join(f"  uint16 a{i} = {read};\n" for i in range(count))
    if entry in COMBAT_FIXTURE:
        COMBAT_FIXTURE = COMBAT_FIXTURE.replace(entry, entry + "\n" + reads)
    elif entry in ANIMAL_FIXTURE:
        ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace(entry, entry + "\n" + reads)
    else:
        ANIMAL_FIXTURE += "\n" + entry + "\n" + reads + "  return NORMAL;\n}\n"
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn handle_animal_mounting_M0X0(CpuState *cpu) {",
    "RecompReturn handle_animal_mounting_M0X0(CpuState *cpu) {\n  check_active_kong_collision_M0X0(cpu);")
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn CODE_B88936_M0X0(CpuState *cpu) {",
    "RecompReturn CODE_B88936_M0X0(CpuState *cpu) {\n  work_on_active_kong_M0X0(cpu);")
ANIMAL_FIXTURE += "\nRecompReturn player_interaction_1D_M0X0(CpuState *cpu) {\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn level_goal_main_idle_state_M0X0(CpuState *cpu) {",
    "RecompReturn level_goal_main_idle_state_M0X0(CpuState *cpu) {\n  check_active_kong_collision_M0X0(cpu);")
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn kong_state_27_M0X0(CpuState *cpu) {",
    "RecompReturn kong_state_27_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB8A16F);\nL_A1AB_M0X0:")
ANIMAL_FIXTURE += "\nRecompReturn CODE_B3F069_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB3EFD9);\nL_F01F_M0X0:\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn CODE_B398D7_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB398E0);\nL_990B_M0X0:\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn CODE_B3992E_M0X0(CpuState *cpu) {\n  work_on_active_kong_global_M0X0(cpu);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn hook_main_M0X0(CpuState *cpu) {\n  hook_main_state_01_M0X0(cpu);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn hook_main_state_01_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB3986B);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn CODE_B6E21D_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB6E2CD);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn camera_handler_M0X0(CpuState *cpu) {", "RecompReturn camera_handler_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB5E549);")
ANIMAL_FIXTURE += "\nRecompReturn jump_off_hook_action_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB8C621);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn handle_player_pit_death_M0X0(CpuState *cpu) {", "RecompReturn handle_player_pit_death_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB8B481);\n  cpu_trace_block(cpu, 0xB8B499);\nL_B4CA_M0X0:")
ANIMAL_FIXTURE += "\nRecompReturn player_interaction_0B_M0X0(CpuState *cpu) {\nL_811E_M0X0:\n  work_on_active_kong_M0X0(cpu);\n  cpu_trace_block(cpu, 0xB88150);\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE += "\nRecompReturn kong_state_63_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB8AFAA);\nL_AFAD_M0X0:\n  return NORMAL;\n}\n"
ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace("RecompReturn player_interaction_07_M0X0(CpuState *cpu) {", "RecompReturn player_interaction_07_M0X0(CpuState *cpu) {\n  work_on_active_kong_M0X0(cpu);")
COMBAT_FIXTURE += ANIMAL_FIXTURE
COMBAT_FIXTURE = COMBAT_FIXTURE.replace("RecompReturn player_interaction_23_M0X0(CpuState *cpu) {",
    "RecompReturn player_interaction_23_M0X0(CpuState *cpu) {\n  cpu_trace_block(cpu, 0xB88DCB);")
COMBAT_FIXTURE = COMBAT_FIXTURE.replace("RecompReturn bank_B5_F776_M0X0(CpuState *cpu) {",
    "RecompReturn bank_B5_F776_M0X0(CpuState *cpu) {\nL_F789_M0X0:\n  cpu_trace_block(cpu, 0xB5F794);")
COMBAT_FIXTURE += ''.join(f"""
RecompReturn {name}_M0X0(CpuState *cpu) {{
  work_on_active_kong_M0X0(cpu);
  return NORMAL;
}}
""" for name in MODULE.ROPE_FUNCTIONS if name != "player_interaction_07")
for function, pc, target, helper in MODULE.CART_HOOKS:
    entry = f"RecompReturn {function}_M0X0(CpuState *cpu) {{"
    body = f"\n  cpu_trace_block(cpu, 0x{pc});\nL_{target}_M0X0:\n"
    if entry in COMBAT_FIXTURE:
        COMBAT_FIXTURE = COMBAT_FIXTURE.replace(entry, entry + body)
    elif entry in CLIPPING_FIXTURE:
        CLIPPING_FIXTURE = CLIPPING_FIXTURE.replace(entry, entry + body)
    elif entry in ANIMAL_FIXTURE:
        ANIMAL_FIXTURE = ANIMAL_FIXTURE.replace(entry, entry + body)
    else:
        COMBAT_FIXTURE += entry + body + "  return NORMAL;\n}\n"
COMBAT_FIXTURE = COMBAT_FIXTURE.replace("RecompReturn player_interaction_23_M0X0(CpuState *cpu) {",
    "RecompReturn player_interaction_23_M0X0(CpuState *cpu) {\n  " + MODULE.CART_OWNER_TRACE)
COMBAT_FIXTURE = COMBAT_FIXTURE.replace("RecompReturn player_interaction_14_M0X0(CpuState *cpu) {",
    "RecompReturn player_interaction_14_M0X0(CpuState *cpu) {\n  " + MODULE.CAT_RELEASE_OWNER_TRACE)
COMBAT_FIXTURE = COMBAT_FIXTURE.replace("RecompReturn player_interaction_13_M0X0(CpuState *cpu) {",
    "RecompReturn player_interaction_13_M0X0(CpuState *cpu) {\n  " + MODULE.CAT_OWNER_TRACE)
GATE_FIXTURE += STATE_FIXTURE + CLIPPING_FIXTURE + COMBAT_FIXTURE
GATE_FIXTURE_PRELOADED_X += STATE_FIXTURE + CLIPPING_FIXTURE + COMBAT_FIXTURE


class ApplyCoopOverridesTests(unittest.TestCase):
    def test_coaster_hooks_fail_closed(self):
        for function, pc, target, helper in MODULE.CART_HOOKS:
            for old, new in ((f"0x{pc}", "0xFFFFFF"),
                             (f"L_{target}_M0X0:", "L_BAD_M0X0:")):
                with self.subTest(helper=helper, anchor=old), tempfile.TemporaryDirectory() as tmp:
                    unit = Path(tmp) / "bank_fixture.c"
                    unit.write_text(GATE_FIXTURE.replace(old, new), encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "coaster continuation"):
                        MODULE.apply_overrides(Path(tmp))

    def test_animal_hit_anchor_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace('0xB88DCB', '0xB88DCE')
            path = self.write_generated(directory, 'bank_b8.c', broken)
            with self.assertRaisesRegex(ValueError, 'surviving animal hit'):
                MODULE.apply_overrides(directory)
            self.assertEqual(path.read_text(encoding='utf-8'), broken)

    def test_rope_calls_fail_closed(self):
        for name in MODULE.ROPE_FUNCTIONS:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(name + '_M0X0', 'unrelated_M0X0')
                path = self.write_generated(directory, 'bank_b8.c', broken)
                with self.assertRaisesRegex(ValueError, 'policy operand' if name in ('player_interaction_0A', 'player_interaction_07') else 'rope reaction'):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding='utf-8'), broken)

    def write_generated(self, directory, name, text):
        path = directory / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_applies_expected_adaptations_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            path = self.write_generated(directory, "bank_b8.c", GATE_FIXTURE)
            changed = MODULE.apply_overrides(directory)
            self.assertEqual(changed, [path])
            adapted = path.read_text(encoding="utf-8")

            self.assertEqual(adapted.count('#include "dkc2_coop.h"'), 1)
            self.assertEqual(adapted.count("Dkc2CoopUseFollowerClipping(cpu)"), 1)
            self.assertEqual(adapted.count("Dkc2CoopBananaSecondWidth(cpu, "), 1)
            self.assertIn(MODULE.BANANA_TRACE + MODULE.BANANA_REPEAT, adapted)
            self.assertIn(MODULE.ANIMAL_HIT_TRACE + MODULE.ANIMAL_HIT_HOOK, adapted)
            self.assertEqual(adapted.count("Dkc2CoopRecordInteractionSource(cpu, "), 1)
            self.assertEqual(adapted.count("Dkc2CoopBounceUsesFollower(cpu)"), 1)
            self.assertEqual(adapted.count("Dkc2CoopRopeUsesFollower(cpu)"), 5)
            self.assertEqual(adapted.count("Dkc2CoopFollowerPaletteOffset(cpu, 0x1e)"), 1)
            self.assertEqual(adapted.count("Dkc2CoopRecoveryFollowerValue(cpu, "), 1)
            self.assertEqual(adapted.count("Dkc2CoopRecordPickupValue(cpu, "), 1)
            self.assertEqual(adapted.count("Dkc2CoopHeldObjectValue(cpu, "), 19)
            self.assertEqual(adapted.count("Dkc2CoopHeldOwnerValue(cpu, "), 10)
            self.assertEqual(adapted.count("Dkc2CoopTeamPartnerValue(cpu, "), 2)
            self.assertEqual(adapted.count("Dkc2CoopTeamStateValue(cpu, "), 1)
            self.assertEqual(
                adapted.count(
                    "Dkc2CoopGateActiveValue(cpu, cpu_read16(cpu, cpu->DB, "
                    "(uint16)(0x0593)))"),
                1)
            self.assertEqual(
                adapted.count(
                    "Dkc2CoopSelectPressedWord(cpu, cpu_read16(cpu, cpu->DB, "
                    "(uint16)(0x0510)))"),
                1)
            self.assertEqual(
                adapted.count(
                    "Dkc2CoopSelectHeldWord(cpu, cpu_read16(cpu, cpu->DB, "
                    "(uint16)(0x050e)))"),
                1)
            # The cartridge fall-through (fade) path is untouched.
            self.assertIn("goto L_B9DE_M0X0; /* fall-through */", adapted)

            again = MODULE.apply_overrides(directory)
            self.assertEqual(again, [path])
            self.assertEqual(path.read_text(encoding="utf-8"), adapted)

    def test_accepts_gate_with_preloaded_x(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            path = self.write_generated(
                directory, "bank_b8.c", GATE_FIXTURE_PRELOADED_X)
            changed = MODULE.apply_overrides(directory)
            self.assertEqual(changed, [path])
            adapted = path.read_text(encoding="utf-8")
            self.assertEqual(
                adapted.count(
                    "Dkc2CoopGateActiveValue(cpu, cpu_read16(cpu, cpu->DB, "
                    "(uint16)(0x0593)))"),
                1)

    def test_rejects_nested_existing_wrappers_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            path = self.write_generated(directory, "bank_b8.c", GATE_FIXTURE)
            MODULE.apply_overrides(directory)
            source = path.read_text(encoding="utf-8")
            read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0593))"
            helper = "Dkc2CoopGateActiveValue"
            broken = source.replace(f"{helper}(cpu, {read})",
                                    f"{helper}(cpu, {helper}(cpu, {read}))")
            path.write_text(broken, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "ambiguous existing"):
                MODULE.apply_overrides(directory)
            self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_state_dispatch_anchor(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.write_generated(directory, "bank_b8.c",
                                 GATE_FIXTURE.replace(STATE_FIXTURE, ""))
            with self.assertRaisesRegex(ValueError, "state dispatch"):
                MODULE.apply_overrides(directory)

    def test_requires_both_clipping_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.write_generated(directory, "bank_b8.c",
                                 GATE_FIXTURE.replace(CLIPPING_FIXTURE, ""))
            with self.assertRaisesRegex(ValueError, "clipping entry|coaster continuation"):
                MODULE.apply_overrides(directory)

    def test_requires_combat_anchors_without_writing(self):
        for missing in ("set_player_interaction", "player_interaction_1B", "CODE_BB8B66"):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(missing + "_M0X0", "unrelated_M0X0")
                path = self.write_generated(directory, "bank_b8.c", broken)
                with self.assertRaises(ValueError):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_boss_hook_camera_anchor_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace("cpu_trace_block(cpu, 0xB6E2CD);",
                                          "cpu_trace_block(cpu, 0xB6E2CE);")
            path = self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaisesRegex(ValueError, "occupied-camera"):
                MODULE.apply_overrides(directory)
            self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_camera_center_anchor_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace("cpu_trace_block(cpu, 0xB5E549);",
                                          "cpu_trace_block(cpu, 0xB5E54A);")
            path = self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaisesRegex(ValueError, "camera bounds/streaming"):
                MODULE.apply_overrides(directory)
            self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_completed_stomp_anchor_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace("cpu_trace_block(cpu, 0xB887F4);",
                                          "cpu_trace_block(cpu, 0xB887F5);")
            path = self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaisesRegex(ValueError, "completed on-foot stomp"):
                MODULE.apply_overrides(directory)
            self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_quake_anchors_fail_closed(self):
        for anchor in (MODULE.QUAKE_TRACE, "L_811E_M0X0:", MODULE.QUAKE_TIMER_TRACE, "L_AFAD_M0X0:"):
            with self.subTest(anchor=anchor), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(anchor, "/* missing quake anchor */")
                path = self.write_generated(directory, "bank_b8.c", broken)
                with self.assertRaisesRegex(ValueError, "quake reaction"):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_native_push_handlers_without_writing(self):
        for name in ("player_interaction_1D", "CODE_B88936"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(name + "_M0X0", "unrelated_M0X0")
                path = self.write_generated(directory, "bank_b8.c", broken)
                with self.assertRaises(ValueError):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_action_anchors_without_writing(self):
        for missing in ("CODE_B8B644", "kong_state_45", "CODE_B8B4CF", "CODE_B8B4F7", "handle_player_pit_death", "jump_off_hook_action", "CODE_B398D7", "hook_main", "CODE_B3F069", "CODE_B8A592", "CODE_B5E560", "handle_animal_mounting", "level_goal_main_idle_state", "check_for_collision", "player_interaction_17", "kong_state_27",
                        "start_player_jumping", "prevent_sprite_from_leaving_level_x", "bank_B5_F776", "bank_B6_819C", "player_interaction_0B",
                        "team_up_action"):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(missing + "_M0X0", "unrelated_M0X0")
                path = self.write_generated(directory, "bank_b8.c", broken)
                with self.assertRaises(ValueError):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_item_policy_reads_fail_closed(self):
        read = "cpu_read16(cpu, cpu->DB, (uint16)(0x0d7a))"
        for name, _, _, helper in MODULE.POLICY_READS:
            if helper != "Dkc2CoopHeldObjectValue":
                continue
            for replacement in (read.replace("0x0d7a", "0x1234"), f"{read} + {read}"):
                with self.subTest(name=name, replacement=replacement), tempfile.TemporaryDirectory() as tmp:
                    directory = Path(tmp)
                    start = GATE_FIXTURE.index(f"RecompReturn {name}_M0X0")
                    end = GATE_FIXTURE.index("\n}", start)
                    body = GATE_FIXTURE[start:end].replace(read, replacement)
                    broken = GATE_FIXTURE[:start] + body + GATE_FIXTURE[end:]
                    path = self.write_generated(directory, "bank_b8.c", broken)
                    with self.assertRaisesRegex(ValueError, "policy operand"):
                        MODULE.apply_overrides(directory)
                    self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_requires_banana_continuation_without_writing(self):
        for anchor in (MODULE.BANANA_TRACE, "L_F789_M0X0:"):
            with self.subTest(anchor=anchor), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                broken = GATE_FIXTURE.replace(anchor, "/* missing */")
                path = self.write_generated(directory, "bank_b8.c", broken)
                with self.assertRaises(ValueError):
                    MODULE.apply_overrides(directory)
                self.assertEqual(path.read_text(encoding="utf-8"), broken)

    def test_ignores_unrelated_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            path = self.write_generated(directory, "bank_b8.c", GATE_FIXTURE)
            other = self.write_generated(
                directory, "bank_b5.c",
                "#include \"funcs.h\"\n"
                "RecompReturn other_M0X0(CpuState *cpu) {\n"
                "  uint16 v = cpu_read16(cpu, cpu->DB, (uint16)(0x0593));\n"
                "  return NORMAL;\n"
                "}\n")
            changed = MODULE.apply_overrides(directory)
            self.assertEqual(changed, [path])
            self.assertIn("Dkc2CoopGateActiveValue",
                          path.read_text(encoding="utf-8"))
            self.assertNotIn("Dkc2Coop", other.read_text(encoding="utf-8"))

    def test_fails_closed_when_no_candidate_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.write_generated(directory, "other.c", "int other;\n")
            with self.assertRaises(ValueError):
                MODULE.apply_overrides(directory)

    def test_fails_closed_when_compare_read_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace(
                "uint16 v_active = cpu_read16(cpu, cpu->DB, (uint16)(0x0593));",
                "uint16 v_active = v_sprite;")
            self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaises(ValueError):
                MODULE.apply_overrides(directory)

    def test_fails_closed_without_current_sprite_context(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace(
                "uint16 v_sprite = cpu_read16(cpu, 0x00, "
                "(uint16)(cpu->D + 0x0064));",
                "uint16 v_sprite = 0x1234;")
            self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaises(ValueError):
                MODULE.apply_overrides(directory)

    def test_fails_closed_without_word_store_context(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            broken = GATE_FIXTURE.replace(
                "cpu_write16(cpu, cpu->DB, (uint16)(0x0981), v_hold);",
                "cpu_write16(cpu, cpu->DB, (uint16)(0x0980), v_hold);")
            self.write_generated(directory, "bank_b8.c", broken)
            with self.assertRaises(ValueError):
                MODULE.apply_overrides(directory)

    def test_fails_closed_on_ambiguous_gate_candidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.write_generated(directory, "bank_b8_a.c", GATE_FIXTURE)
            self.write_generated(directory, "bank_b8_b.c", GATE_FIXTURE)
            with self.assertRaises(ValueError):
                MODULE.apply_overrides(directory)


if __name__ == "__main__":
    unittest.main()
