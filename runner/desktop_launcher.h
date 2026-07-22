#ifndef DKC2_DESKTOP_LAUNCHER_H
#define DKC2_DESKTOP_LAUNCHER_H

#include "recomp_launcher.h"

#include <stdbool.h>
#include <stddef.h>

void Dkc2LauncherSettingsDefault(RecompLauncherCSettings *settings);
void Dkc2LauncherSettingsLoad(RecompLauncherCSettings *settings);
bool Dkc2LauncherSettingsSave(const RecompLauncherCSettings *settings);

bool Dkc2LauncherReadRomCache(char *path, size_t capacity);
bool Dkc2LauncherWriteRomCache(const char *path);

/* Runs the shared recomp-ui launcher with host-specific renderer labels.
 * Pass no labels to hide the renderer selector for a single-backend host. */
int Dkc2LauncherRun(RecompLauncherCSettings *settings,
                    const char *initial_rom, char *selected_rom,
                    size_t selected_capacity,
                    const char *const *renderer_labels,
                    size_t renderer_count);

#endif
