#ifndef DKC2_DESKTOP_LAUNCHER_H
#define DKC2_DESKTOP_LAUNCHER_H

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
/* Upscaler choice (kDkc2Upscaler*), remembered with the launcher settings;
 * the Reconstruct experiment's mode (0..3) and strength (0..100). */
int Dkc2LauncherUpscaler(void);
void Dkc2LauncherSetUpscaler(int upscaler);
int Dkc2LauncherReconstructMode(void);
void Dkc2LauncherSetReconstructMode(int mode);
int Dkc2LauncherReconstructStrength(void);
void Dkc2LauncherSetReconstructStrength(int percent);

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
