#ifndef DKC2_COOP_ASSIST_H
#define DKC2_COOP_ASSIST_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu_state.h"
#include "../config/coop_tuning.cfg"

/* Configured hold rounded up at the supported NTSC console frame rate.
 * Call once per forward console frame with actual controller port Select
 * state, not cached guest input words; returns the moved player bit (1/2).
 * Pending holds reset with the co-op session, mode changes and snapshot load. */
enum { kDkc2CoopRescueHoldFrames = (DKC2_COOP_SELECT_HOLD_MS * 60 + 999) / 1000 + 1 };
uint8_t Dkc2CoopRescueFrame(CpuState *cpu, bool p1_select, bool p2_select);
/* Host overlay pause stops console frames, so cancel its pending charge
 * explicitly while retaining the one-shot latch of an already fired hold. */
void Dkc2CoopRescuePause(void);

/* Native goal eligibility/collision operands and accepted-contact ownership. */
uint16_t Dkc2CoopGoalCandidateValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopGoalCollisionFlags(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopGoalAcceptedValue(CpuState *cpu, uint16_t original);
void Dkc2CoopCameraCenter(CpuState *cpu);
bool Dkc2CoopCartSeats(CpuState *cpu);
bool Dkc2CoopCartIndependent(CpuState *cpu);
bool Dkc2CoopCartSurvivor(CpuState *cpu);
void Dkc2CoopCartHitOwner(CpuState *cpu);
bool Dkc2CoopCartClip(CpuState *cpu);
uint16_t Dkc2CoopCartStompValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopCartPit(CpuState *cpu);
bool Dkc2CoopCartDetach(CpuState *cpu);
uint16_t Dkc2CoopCartBoardingLeaderValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCartLeaderWorkValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCartWorkValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopCartEntryBlocked(CpuState *cpu);
uint16_t Dkc2CoopCameraXValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCameraYValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCameraXFraction(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCameraYFraction(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopHookLeaderFlags(CpuState *cpu, uint16_t original);
bool Dkc2CoopHookIndependent(CpuState *cpu);
bool Dkc2CoopHookShared(CpuState *cpu);
bool Dkc2CoopHookSharedAfterCamera(CpuState *cpu);
void Dkc2CoopHookRelease(CpuState *cpu);
bool Dkc2CoopPitSurvivor(CpuState *cpu);
uint16_t Dkc2CoopPitCameraY(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopHookAnimalValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopHookUsesFollower(CpuState *cpu);
uint16_t Dkc2CoopHookCallbackValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopCannonIndependent(CpuState *cpu);
uint16_t Dkc2CoopCannonTargetValue(CpuState *cpu, uint16_t original);
void Dkc2CoopShareStomp(CpuState *cpu);
void Dkc2CoopAnimalOnlyHit(CpuState *cpu);
/* Ground-shake candidate selection, per-Kong native initialization/recovery,
 * and one shared timer update when both independent Kongs are stunned. */
uint16_t Dkc2CoopQuakeCandidateValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopQuakeUsesFollower(CpuState *cpu);
bool Dkc2CoopQuakeRepeat(CpuState *cpu);
bool Dkc2CoopQuakeSkipTimer(CpuState *cpu);
uint16_t Dkc2CoopQuakeRecoveryValue(CpuState *cpu, uint16_t original);
bool Dkc2CoopPushIndependent(CpuState *cpu);
bool Dkc2CoopPushUsesFollower(CpuState *cpu);
uint16_t Dkc2CoopPolePendingValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopPoleCurrentValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopPoleReactionValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopPolePlayerValue(CpuState *cpu, uint16_t original);

void Dkc2CoopCatCaptureOwner(CpuState *cpu);
void Dkc2CoopCatReleaseOwner(CpuState *cpu);
bool Dkc2CoopCatIndependent(CpuState *cpu);
uint16_t Dkc2CoopCatOwnerValue(CpuState *cpu, uint16_t original);
uint16_t Dkc2CoopCatCameraValue(CpuState *cpu, uint16_t original);

#endif
