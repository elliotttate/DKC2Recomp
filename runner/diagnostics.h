#ifndef DKC2_DIAGNOSTICS_H
#define DKC2_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize once after the process has anchored its working directory beside
 * the executable. Reports never include ROM bytes, ROM paths, saves, states,
 * generated code, screenshots, or captured audio. */
bool Dkc2DiagnosticsInit(const char *host_name, const char *build_version);

void Dkc2DiagnosticsSetPresentation(const char *backend,
                                    const char *screen_filter,
                                    bool audio_available);
void Dkc2DiagnosticsHeartbeat(uint64_t frame, uint32_t resume_pc);

/* A controlled runtime failure writes the rolling report and a timestamped
 * support bundle immediately. Die() failures are caught by the atexit path. */
void Dkc2DiagnosticsFatal(const char *message);

/* Write diagnostics/last_run_report.json. A bundle is also created when a
 * fatal was recorded or DKC2_DIAGNOSTIC_BUNDLE=1 was requested. */
void Dkc2DiagnosticsShutdown(const char *outcome);

#endif
