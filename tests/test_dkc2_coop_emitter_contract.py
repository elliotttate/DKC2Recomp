"""Contract test: the co-op generation adapter must adapt REAL v2 emitter
output, not only hand-written fixtures.

The v2 emitter is driven on a synthetic ROM whose function mirrors the
Kong action input gate shape (LDX dp / CPX abs / BEQ / CLC / RTS /
LDA abs / STA abs / LDA abs / STA abs / RTS). The emitted C is then run
through scripts/apply_dkc2_coop_overrides.py and the three adaptations
must land exactly once each. This pins the adapter to the emitter's real
C idioms (single read temps, cpu->DB absolute banks, flag-mirror
branches) so a generator change fails this source-only test instead of
the owner's private regeneration. The adapter anchors on
instruction-shape context rather than trace PCs, so no address remapping
is needed here.
"""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
SCRIPT = REPOSITORY / "scripts" / "apply_dkc2_coop_overrides.py"
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_coop_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

sys.path.insert(0, str(REPOSITORY / "snesrecomp" / "recompiler"))
try:
    from v2.emit_function import emit_function
except ImportError:  # pragma: no cover - submodule not checked out
    emit_function = None


def gate_rom():
    """Gate-shaped synthetic function at LoROM bank-0 $8000, M0X0."""
    rom = bytearray(0x8000)
    code = bytes([
        0xA6, 0x64,             # $8000 LDX $64        (current_sprite)
        0xEC, 0x93, 0x05,       # $8002 CPX $0593      (active Kong gate)
        0xF0, 0x04,             # $8005 BEQ $800B      (input path)
        0x18,                   # $8007 CLC            (no input)
        0x60,                   # $8008 RTS
        0xEA, 0xEA,             # $8009 NOP padding
        0xAD, 0x10, 0x05,       # $800B LDA $0510      (active pressed)
        0x8D, 0x83, 0x09,       # $800E STA $0983
        0xAD, 0x0E, 0x05,       # $8011 LDA $050E      (active held)
        0x8D, 0x81, 0x09,       # $8014 STA $0981
        0x60,                   # $8017 RTS
    ])
    rom[0:len(code)] = code
    return bytes(rom)


@unittest.skipIf(emit_function is None, "snesrecomp v2 emitter unavailable")
class CoopEmitterContractTests(unittest.TestCase):
    def test_adapter_applies_to_real_emitted_c(self):
        src = emit_function(gate_rom(), bank=0, start=0x8000,
                            entry_m=0, entry_x=0)
        # Sanity: the synthetic function really emits the gate idioms the
        # adapter anchors on before any adaptation is attempted.
        self.assertIn("uint16 _v1 = cpu_read16(cpu, 0x00, "
                      "(uint16)(cpu->D + 0x0064));", src)
        self.assertEqual(src.count("cpu_read16(cpu, cpu->DB, "
                                   "(uint16)(0x0593))"), 1)
        self.assertEqual(src.count("cpu_read16(cpu, cpu->DB, "
                                   "(uint16)(0x0510))"), 1)
        self.assertEqual(src.count("cpu_read16(cpu, cpu->DB, "
                                   "(uint16)(0x050e))"), 1)

        # Real generated units carry the funcs.h include the adapter
        # anchors its own include behind; emit_function returns the body
        # only, so provide the unit header here.
        state_rom = bytearray(0x8000)
        state_rom[:3] = bytes([0xB5, 0x2E, 0x60])  # LDA $2E,x / RTS
        state_src = emit_function(bytes(state_rom), bank=0, start=0x8000,
                                  entry_m=0, entry_x=0,
                                  func_name="kong_state_handler")
        return_rom = bytes([0x6B]) + bytes(0x7fff)
        clipping_src = "".join(emit_function(return_rom, bank=0, start=0x8000,
                               entry_m=0, entry_x=0, func_name=name)
                               for name in ("CODE_BCFB2C", "get_active_kong_clipping"))
        interaction_rom = bytearray(0x8000)
        interaction_rom[:6] = bytes([0xA5, 0x64, 0x8D, 0x84, 0x0A, 0x60])
        interaction_src = emit_function(bytes(interaction_rom), bank=0, start=0x8000,
                                        entry_m=0, entry_x=0,
                                        func_name="set_player_interaction")
        bounce_rom = bytearray(0x8000)
        bounce_rom[:4] = bytes([0x20, 0x92, 0x80, 0x60])
        bounce_rom[0x92] = 0x60
        bounce_src = emit_function(bytes(bounce_rom), bank=0, start=0x8000,
                                   entry_m=0, entry_x=0,
                                   func_name="player_interaction_1B")
        # Give the synthetic callee the same symbol alias as the game config.
        import re
        bounce_src, count = re.subn(r"\b\w+_M0X0\(cpu\)",
                                    "work_on_active_kong_M0X0(cpu)", bounce_src)
        self.assertEqual(count, 1)
        rope_src = ''.join(bounce_src.replace('player_interaction_1B', name)
                           for name in MODULE.ROPE_FUNCTIONS if name not in ("player_interaction_0A", "player_interaction_07"))
        palette_rom = bytes([0xA9, 0x1E, 0x00, 0x60]) + bytes(0x7FFC)
        palette_src = emit_function(palette_rom, bank=0, start=0x8000,
                                    entry_m=0, entry_x=0, func_name="CODE_BB8B66")
        recovery_rom = bytes([0xA6, 0x64, 0xEC, 0x97, 0x05,
                              0xCD, 0x97, 0x05, 0x60]) + bytes(0x7FF7)
        recovery_src = emit_function(recovery_rom, bank=0, start=0x8000,
                                     entry_m=0, entry_x=0, func_name="CODE_B9D705")
        pickup_rom = bytes([0x8E, 0x7A, 0x0D, 0x60]) + bytes(0x7FFC)
        pickup_src = emit_function(pickup_rom, bank=0, start=0x8000,
                                   entry_m=0, entry_x=0, func_name="update_object_pickup")
        owner_src = "".join(emit_function(bytes([0xAE, 0x93, 0x05]) * count +
                                        bytes([0x60]) + bytes(0x7FFF - 3 * count),
                                        bank=0, start=0x8000,
                                        entry_m=0, entry_x=0, func_name=name)
                             for name, count in MODULE.OWNER_FUNCTIONS.items())
        animal_src = emit_function(return_rom, bank=0, start=0x8000,
                                   entry_m=0, entry_x=0, func_name="player_interaction_17")
        animal_ops = {}
        for name, pattern, count, helper in MODULE.POLICY_READS:
            address = {"Dkc2CoopHeldObjectValue": [0x7A, 0x0D],
                       "Dkc2CoopBananaSecondWidth": [0x44, 0x0D],
                       "Dkc2CoopAnimalFollowerFlags": [0xC2, 0x08],
                       "Dkc2CoopHandoffXValue": [0x66, 0x0D],
                       "Dkc2CoopHandoffYValue": [0x6A, 0x0D]}.get(helper, [0x93, 0x05])
            op = [0xA5, 0x6E] if helper == "Dkc2CoopAnimalTypeValue" else [0xAD] + address
            op = {"Dkc2CoopCameraXFraction": [0xB9, 0x05, 0],
                  "Dkc2CoopCameraYFraction": [0xB9, 0x09, 0],
                  "Dkc2CoopCannonTargetValue": [0xAD, 0x97, 0x05],
                  "Dkc2CoopQuakeRecoveryValue": [0xAD, 0x97, 0x05],
                  "Dkc2CoopHookCallbackValue": [0xA9, 0xBD, 0x98],
                  "Dkc2CoopHookAnimalValue": [0xA5, 0x6E],
                  "Dkc2CoopPitCameraY": [0xAD, 0xC0, 0x17],
                  "Dkc2CoopCartStompValue": [0xAD, 0x5E, 0x0D],
                  "Dkc2CoopCatCameraValue": [0xAD, 0xE8, 0x0A],
                  "Dkc2CoopCartWorkValue": [0xAD, 0x95, 0x05],
                  "Dkc2CoopCartLeaderWorkValue": [0xAD, 0x95, 0x05],
                  "Dkc2CoopHookLeaderFlags": [0xBF, 0x20, 0x96, 0xB8],
                  "Dkc2CoopCameraXValue": [0xB9, 0x06, 0],
                  "Dkc2CoopCameraYValue": [0xB9, 0x0A, 0],
                  "Dkc2CoopGoalAcceptedValue": [0xA5, 0x64],
                  "Dkc2CoopGoalCollisionFlags": [0xB5, 0x30],
                  "Dkc2CoopScreenLeftValue": [0xA9, 0x10, 0],
                  "Dkc2CoopScreenSpanValue": [0xA9, 0xE0, 0],
                  "Dkc2CoopTeamPartnerValue": [0xAC, 0x97, 0x05],
                  "Dkc2CoopRopeAnimationFollowerValue": [0xAD, 0x97, 0x05],
                  "Dkc2CoopTeamStateValue": [0xB9, 0x2E, 0]}.get(helper, op)
            if helper.startswith("Dkc2CoopPole"):
                address = next(a for a in ("0ba0", "0ba2", "0a86", "0593") if "0x" + a in pattern.pattern)
                value = int(address, 16)
                op = [0xAD, value & 255, value >> 8]
            animal_ops.setdefault(name, []).extend(op * count)
        for name, ops in animal_ops.items():
            if name == "handle_player_pit_death":
                continue
            if name == "CODE_B9D705":
                recovery_rom = bytes(ops) + recovery_rom[:9] + bytes(0x8000-9-len(ops))
                recovery_src = emit_function(recovery_rom, bank=0, start=0x8000,
                                            entry_m=0, entry_x=0, func_name=name)
            elif name == "player_interaction_23":
                synthetic = bytearray(0x8000)
                end = 0x8000 + len(ops) + 3
                synthetic[:len(ops)+4] = bytes(ops + [0x4C, end & 255, end >> 8, 0x60])
                animal_src += emit_function(bytes(synthetic), bank=0, start=0x8000,
                                            entry_m=0, entry_x=0, func_name=name).replace(
                    f'cpu_trace_block(cpu, 0x{end:06X});', MODULE.ANIMAL_HIT_TRACE)
            elif name == "player_interaction_1B":
                synthetic = bytearray(0x8000)
                synthetic[:len(ops)+4] = bytes(ops + [0x20, 0x92, 0x80, 0x60])
                synthetic[0x92] = 0x60
                bounce_src = emit_function(bytes(synthetic), bank=0, start=0x8000,
                                          entry_m=0, entry_x=0, func_name=name)
                bounce_src = re.sub(r"\b\w+_M0X0\(cpu\)", "work_on_active_kong_M0X0(cpu)", bounce_src)
                bounce_src = bounce_src.replace(f'cpu_trace_block(cpu, 0x{0x8000+len(ops)+3:06X});',
                                               'cpu_trace_block(cpu, 0xB887F4);')
            elif name == "bank_B5_F776":
                # Synthetic group flow with the real continuation labels.
                synthetic = bytearray(0x200000)
                base = 0x1AF776
                synthetic[base:base+6] = bytes(ops + [0x4C, 0x89, 0xF7])
                synthetic[0x1AF789:0x1AF78C] = bytes([0x4C, 0x94, 0xF7])
                synthetic[0x1AF794] = 0x60
                animal_src += emit_function(bytes(synthetic), bank=0xB5, start=0xF776,
                                            entry_m=0, entry_x=0, func_name=name)
            elif name == "kong_state_27":
                # Real emitter labels for the two source-owned handoff anchors.
                synthetic = bytearray(0x200000)
                synthetic[0x1C216F:0x1C2172] = bytes([0x4C, 0xAB, 0xA1])
                synthetic[0x1C21AB:0x1C21AB+len(ops)+1] = bytes(ops + [0x60])
                animal_src += emit_function(bytes(synthetic), bank=0xB8, start=0xA16F,
                                            entry_m=0, entry_x=0, func_name=name)
            else:
                if name in ("handle_animal_mounting", "level_goal_main_idle_state", "CODE_B88936", "player_interaction_0A", "player_interaction_07"):
                    ops += [0x20, 0x92, 0x80]
                code = bytearray(bytes(ops + [0x60]) + bytes(0x7FFF-len(ops)))
                code[0x92] = 0x60
                src_animal = emit_function(bytes(code), bank=0, start=0x8000,
                                           entry_m=0, entry_x=0, func_name=name)
                if name in ("handle_animal_mounting", "level_goal_main_idle_state"):
                    src_animal = re.sub(r"\b\w+_M0X0\(cpu\)", "check_active_kong_collision_M0X0(cpu)", src_animal)
                if name in ("CODE_B88936", "player_interaction_0A", "player_interaction_07"):
                    src_animal = re.sub(r"\b\w+_M0X0\(cpu\)", "work_on_active_kong_M0X0(cpu)", src_animal)
                animal_src += src_animal
        animal_src += emit_function(bytes([0x60]) + bytes(0x7FFF), bank=0, start=0x8000,
                                    entry_m=0, entry_x=0, func_name="player_interaction_1D")
        synthetic = bytearray(0x200000)
        synthetic[0x19EFD9:0x19EFDC] = bytes([0x4C, 0x1F, 0xF0])
        synthetic[0x19F01F] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB3, start=0xEFD9,
                                    entry_m=0, entry_x=0, func_name="CODE_B3F069")
        synthetic = bytearray(0x200000)
        synthetic[0x1998E0:0x1998E3] = bytes([0x4C, 0x0B, 0x99])
        synthetic[0x19990B] = 0x60
        hook_entry = emit_function(bytes(synthetic), bank=0xB3, start=0x98E0,
                                    entry_m=0, entry_x=0, func_name="CODE_B398D7")
        animal_src += hook_entry
        owner_src_hook = emit_function(bounce_rom, bank=0, start=0x8000,
                                      entry_m=0, entry_x=0, func_name="CODE_B3992E")
        animal_src += re.sub(r"\b\w+_M0X0\(cpu\)", "work_on_active_kong_global_M0X0(cpu)", owner_src_hook)
        hook_src = emit_function(bounce_rom, bank=0, start=0x8000,
                                entry_m=0, entry_x=0, func_name="hook_main")
        hook_src = re.sub(r"\b\w+_M0X0\(cpu\)", "hook_main_state_01_M0X0(cpu)", hook_src)
        animal_src += hook_src
        synthetic = bytearray(0x200000)
        synthetic[0x19B86B] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB3, start=0x986B,
                                    entry_m=0, entry_x=0, func_name="hook_main_state_01")
        synthetic[0x1B62CD] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB6, start=0xE2CD,
                                    entry_m=0, entry_x=0, func_name="CODE_B6E21D")
        synthetic = bytearray(0x200000)
        camera_rom = bytearray(0x200000)
        camera_rom[0x1AE549] = 0x60
        animal_src = animal_src.replace("RecompReturn camera_handler_M0X0(CpuState *cpu) {",
            "RecompReturn camera_handler_M0X0(CpuState *cpu) {\n  " + MODULE.CAMERA_CENTER_TRACE)
        synthetic[0x1C4621] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB8, start=0xC621,
                                    entry_m=0, entry_x=0, func_name="jump_off_hook_action")
        synthetic = bytearray(0x200000)
        synthetic[0x1C3481:0x1C3487] = bytes([0xAD, 0xC0, 0x17, 0x4C, 0x99, 0xB4])
        synthetic[0x1C3499:0x1C349C] = bytes([0x4C, 0xCA, 0xB4])
        synthetic[0x1C34CA] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB8, start=0xB481,
                                    entry_m=0, entry_x=0, func_name="handle_player_pit_death")
        synthetic = bytearray(0x200000)
        synthetic[0x1C011E:0x1C0124] = bytes([0x20, 0x92, 0x82, 0x4C, 0x50, 0x81])
        synthetic[0x1C0292] = 0x60
        synthetic[0x1C0150] = 0x60
        quake_src = emit_function(bytes(synthetic), bank=0xB8, start=0x811E,
                                  entry_m=0, entry_x=0, func_name="player_interaction_0B")
        animal_src += re.sub(r"\b\w+_M0X0\(cpu\)", "work_on_active_kong_M0X0(cpu)", quake_src)
        synthetic = bytearray(0x200000)
        synthetic[0x1C2FAA:0x1C2FAD] = bytes([0x4C, 0xAD, 0xAF])
        synthetic[0x1C2FAD] = 0x60
        animal_src += emit_function(bytes(synthetic), bank=0xB8, start=0xAFAA,
                                    entry_m=0, entry_x=0, func_name="kong_state_63")
        for function, pc, target, helper in MODULE.CART_HOOKS:
            bank, start, end = int(pc[:2], 16), int(pc[2:], 16), int(target, 16)
            synthetic = bytearray(0x200000)
            offset = ((bank & 0x3F) << 15) + (start & 0x7FFF)
            synthetic[offset:offset+3] = bytes([0x4C, end & 255, end >> 8])
            synthetic[((bank & 0x3F) << 15) + (end & 0x7FFF)] = 0x60
            cart_src = emit_function(bytes(synthetic), bank=bank, start=start,
                                     entry_m=0, entry_x=0, func_name=function)
            if function == "CODE_B8B107":
                # Emit its work-selector operand and departure labels together.
                target_offset = ((bank & 0x3F) << 15) + (end & 0x7FFF)
                synthetic[target_offset:target_offset+4] = bytes([0xAD, 0x95, 0x05, 0x60])
                cart_src = emit_function(bytes(synthetic), bank=bank, start=start,
                                         entry_m=0, entry_x=0, func_name=function)
                animal_src = re.sub(r"RecompReturn CODE_B8B107_M0X0\(CpuState \*cpu\) \{.*?^\}",
                                    "", animal_src, flags=re.S | re.M)
            if function in ("player_interaction_23", "spin_cat_o9_tails_with_kong"):
                entry = f"RecompReturn {function}_M0X0(CpuState *cpu) {{"
                # Preserve its other synthetic animal operands and hit anchor.
                animal_src = animal_src.replace(entry, entry +
                    f"\n  cpu_trace_block(cpu, 0x{pc});\nL_{target}_M0X0:\n")
            elif function == "get_active_kong_clipping":
                # Keep the inactive entry declaration used by the redirect.
                clipping_src = emit_function(return_rom, bank=0, start=0x8000,
                    entry_m=0, entry_x=0, func_name="CODE_BCFB2C") + cart_src
            else:
                animal_src += cart_src
        animal_src = animal_src.replace("RecompReturn player_interaction_14_M0X0(CpuState *cpu) {",
            "RecompReturn player_interaction_14_M0X0(CpuState *cpu) {\n  " + MODULE.CAT_RELEASE_OWNER_TRACE)
        animal_src = animal_src.replace("RecompReturn player_interaction_13_M0X0(CpuState *cpu) {",
            "RecompReturn player_interaction_13_M0X0(CpuState *cpu) {\n  " + MODULE.CAT_OWNER_TRACE)
        animal_src = animal_src.replace("RecompReturn player_interaction_23_M0X0(CpuState *cpu) {",
            "RecompReturn player_interaction_23_M0X0(CpuState *cpu) {\n  " + MODULE.CART_OWNER_TRACE)
        unit_text = ('#include "funcs.h"\n' + src + state_src + clipping_src +
                     interaction_src + bounce_src + palette_src + recovery_src +
                     pickup_src + owner_src + animal_src + rope_src)
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            unit = directory / "bank_b8.c"
            unit.write_text(unit_text, encoding="utf-8")
            changed = MODULE.apply_overrides(directory)
            self.assertEqual(changed, [unit])
            adapted = unit.read_text(encoding="utf-8")

            # A second pass must be a no-op (idempotent regeneration).
            again = MODULE.apply_overrides(directory)
            self.assertEqual(again, [unit])
            self.assertEqual(unit.read_text(encoding="utf-8"), adapted)

        self.assertEqual(adapted.count('#include "dkc2_coop.h"'), 1)
        self.assertEqual(adapted.count("Dkc2CoopSelectStateWord(cpu, "), 1)
        self.assertEqual(adapted.count("Dkc2CoopUseFollowerClipping(cpu)"), 1)
        self.assertEqual(adapted.count("Dkc2CoopBananaSecondWidth(cpu, "), 1)
        self.assertIn(MODULE.QUAKE_TRACE + MODULE.QUAKE_REPEAT, adapted)
        self.assertIn(MODULE.QUAKE_REDIRECT, adapted)
        self.assertIn(MODULE.BANANA_TRACE + MODULE.BANANA_REPEAT, adapted)
        self.assertIn(MODULE.HOOK_RELEASE_TRACE + MODULE.HOOK_RELEASE, adapted)
        self.assertIn(MODULE.CAMERA_CENTER_TRACE + MODULE.CAMERA_CENTER, adapted)
        self.assertIn(MODULE.HOOK_OCCUPIED_TRACE + MODULE.HOOK_OCCUPIED_SCAN, adapted)
        self.assertIn(MODULE.HOOK_FRAMED_TRACE + MODULE.HOOK_FRAMED_SCAN, adapted)
        self.assertIn(MODULE.SHARED_STOMP_TRACE + MODULE.SHARED_STOMP_HOOK, adapted)
        self.assertIn(MODULE.PUSH_ENTRY + MODULE.PUSH_EARLY, adapted)
        self.assertEqual(adapted.count('Dkc2CoopPushUsesFollower(cpu)'), 1)
        self.assertEqual(adapted.count('Dkc2CoopShareStomp(cpu)'), 1)
        for trace in MODULE.PIT_TRACES:
            self.assertIn(trace + MODULE.PIT_SKIP, adapted)
        self.assertEqual(adapted.count("Dkc2CoopRecordInteractionSource(cpu, "), 1)
        self.assertEqual(adapted.count("Dkc2CoopBounceUsesFollower(cpu)"), 1)
        self.assertEqual(adapted.count("Dkc2CoopRopeUsesFollower(cpu)"), 5)
        self.assertEqual(adapted.count("Dkc2CoopFollowerPaletteOffset(cpu, 0x1e)"), 1)
        self.assertEqual(adapted.count("Dkc2CoopRecoveryFollowerValue(cpu, "), 1)
        self.assertEqual(adapted.count("Dkc2CoopRecordPickupValue(cpu, "), 1)
        self.assertEqual(adapted.count("Dkc2CoopHeldObjectValue(cpu, "), 19)
        self.assertEqual(adapted.count("Dkc2CoopHeldOwnerValue(cpu, "), 10)
        self.assertEqual(
            adapted.count("Dkc2CoopGateActiveValue(cpu, cpu_read16("
                          "cpu, cpu->DB, (uint16)(0x0593)))"),
            1)
        self.assertEqual(
            adapted.count("Dkc2CoopSelectPressedWord(cpu, cpu_read16("
                          "cpu, cpu->DB, (uint16)(0x0510)))"),
            1)
        self.assertEqual(
            adapted.count("Dkc2CoopSelectHeldWord(cpu, cpu_read16("
                          "cpu, cpu->DB, (uint16)(0x050e)))"),
            1)
        # The CLC fall-through path stays stock.
        self.assertIn("goto L_8007_M0X0; /* fall-through */", adapted)


if __name__ == "__main__":
    unittest.main()
