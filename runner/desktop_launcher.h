#ifndef DKC2_DESKTOP_LAUNCHER_H
#define DKC2_DESKTOP_LAUNCHER_H

#include "desktop_crt.h"
#include "recomp_launcher.h"

#include <stdbool.h>
#include <stddef.h>

#define DKC2_PRODUCT_TITLE "DKC2 Recomp Alpha Pre-Release"

#ifdef __cplusplus
extern "C" {
#endif

/* Persisted widescreen edge policy (a Dkc2VideoEdgePolicy value). */
int Dkc2LauncherWidescreenEdge(void);
void Dkc2LauncherSetWidescreenEdge(int policy);
/* 2 PLAYER TEAM input policy (a Dkc2CoopMode value): simultaneous co-op
 * or the cartridge's classic alternating control. */
int Dkc2LauncherCoopMode(void);
void Dkc2LauncherSetCoopMode(int mode);
/* Upscaler choice (kDkc2Upscaler*), remembered with the launcher settings;
 * the Reconstruct experiment's mode (0..3) and strength (0..100). */
int Dkc2LauncherHaptics(void);
void Dkc2LauncherSetHaptics(int enabled);
/* Whether the map shows KONG letter badges (dkc2_kong_tracker.h); on by
 * default and persisted as KongBadges. Recording is unaffected. */
int Dkc2LauncherKongBadges(void);
void Dkc2LauncherSetKongBadges(int enabled);
/* Accessibility: hold Glimmer's light still instead of the white frame he
 * flashes on every turn (dkc2_glimmer_flash.h). Off by default and
 * persisted as ReduceGlimmerFlash. */
int Dkc2LauncherReduceGlimmerFlash(void);
void Dkc2LauncherSetReduceGlimmerFlash(int enabled);
/* Assist Tools / Cheats: while paused in a level, Left/Right picks an animal
 * buddy crate and B spawns it (dkc2_animal_crate_cheat.h). Off by default,
 * persisted as PauseCrateSpawn, and applied only with Assist Tools on. */
int Dkc2LauncherPauseCrateSpawn(void);
void Dkc2LauncherSetPauseCrateSpawn(int enabled);
int Dkc2LauncherUpscaler(void);
void Dkc2LauncherSetUpscaler(int upscaler);
int Dkc2LauncherReconstructMode(void);
void Dkc2LauncherSetReconstructMode(int mode);
int Dkc2LauncherReconstructStrength(void);
void Dkc2LauncherSetReconstructStrength(int percent);
int Dkc2LauncherReconstructSoftness(void);
void Dkc2LauncherSetReconstructSoftness(int percent);
int Dkc2LauncherReconstructShading(void);
void Dkc2LauncherSetReconstructShading(int percent);
/* Display choice (kDkc2Display*) and the CRT television settings it
 * draws with, remembered with the launcher settings. */
int Dkc2LauncherDisplay(void);
void Dkc2LauncherSetDisplay(int display);
bool Dkc2LauncherApplyCrtEnvironment(char *error, size_t capacity);
const Dkc2CrtSettings *Dkc2LauncherCrt(void);
void Dkc2LauncherSetCrt(const Dkc2CrtSettings *crt);

void Dkc2LauncherSettingsDefault(RecompLauncherCSettings *settings);
void Dkc2LauncherSettingsLoad(RecompLauncherCSettings *settings);
bool Dkc2LauncherSettingsSave(const RecompLauncherCSettings *settings);

bool Dkc2LauncherReadRomCache(char *path, size_t capacity);
bool Dkc2LauncherWriteRomCache(const char *path);
void Dkc2LauncherSetAssetsPath(const char *path);

/* Runs the shared recomp-ui launcher with host-specific renderer labels.
 * Pass no labels to hide the renderer selector for a single-backend host. */
int Dkc2LauncherRun(RecompLauncherCSettings *settings,
                    const char *initial_rom, char *selected_rom,
                    size_t selected_capacity,
                    const char *const *renderer_labels,
                    size_t renderer_count);

#ifdef __cplusplus
}
#endif

#endif
