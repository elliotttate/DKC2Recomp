#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "diagnostics.h"

#include "host_report.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define DKC2_MKDIR(path) _mkdir(path)
#define DKC2_GETPID() ((unsigned long)GetCurrentProcessId())
#else
#include <fcntl.h>
#include <unistd.h>
#define DKC2_MKDIR(path) mkdir(path, 0755)
#define DKC2_GETPID() ((unsigned long)getpid())
#endif

enum { kPathCapacity = 1024 };

typedef struct Dkc2DiagnosticState {
  char root[kPathCapacity];
  char host[64];
  char outcome[64];
  char backend[96];
  char screen_filter[32];
  uint64_t frame;
  uint32_t resume_pc;
  bool audio_available;
  bool initialized;
  bool shutdown;
  bool bundle_written;
} Dkc2DiagnosticState;

static Dkc2DiagnosticState s_diagnostics;
#ifndef _WIN32
static char s_signal_marker[kPathCapacity];
#endif

static bool EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static bool EnsureDirectory(const char *path) {
  if (DKC2_MKDIR(path) == 0) return true;
  return errno == EEXIST;
}

static bool JoinPath(char *output, size_t capacity, const char *directory,
                     const char *leaf) {
  if (!output || capacity == 0 || !directory || !leaf) return false;
  int written = snprintf(output, capacity, "%s/%s", directory, leaf);
  return written > 0 && (size_t)written < capacity;
}

static void JsonEscape(const char *source, char *output, size_t capacity) {
  if (!output || capacity == 0) return;
  size_t used = 0;
  if (!source) source = "";
  while (*source && used + 7 < capacity) {
    unsigned char value = (unsigned char)*source++;
    if (value == '\\' || value == '"') {
      output[used++] = '\\';
      output[used++] = (char)value;
    } else if (value == '\n') {
      output[used++] = '\\';
      output[used++] = 'n';
    } else if (value == '\r') {
      output[used++] = '\\';
      output[used++] = 'r';
    } else if (value < 0x20) {
      int count = snprintf(output + used, capacity - used, "\\u%04x", value);
      if (count < 0) break;
      used += (size_t)count;
    } else {
      output[used++] = (char)value;
    }
  }
  output[used] = '\0';
}

static void UtcStamp(char *output, size_t capacity) {
  time_t now = time(NULL);
  struct tm value;
#ifdef _WIN32
  if (gmtime_s(&value, &now) != 0) {
#else
  if (!gmtime_r(&now, &value)) {
#endif
    (void)snprintf(output, capacity, "unknown");
    return;
  }
  (void)strftime(output, capacity, "%Y%m%d_%H%M%S", &value);
}

static bool CopyDiagnosticFile(const char *source, const char *destination) {
  FILE *input = fopen(source, "rb");
  if (!input) return false;
  FILE *output = fopen(destination, "wb");
  if (!output) {
    (void)fclose(input);
    return false;
  }
  bool ok = true;
  unsigned char buffer[16384];
  size_t size = 0;
  while ((size = fread(buffer, 1, sizeof buffer, input)) != 0) {
    if (fwrite(buffer, 1, size, output) != size) {
      ok = false;
      break;
    }
  }
  if (ferror(input)) ok = false;
  if (fclose(input) != 0) ok = false;
  if (fclose(output) != 0) ok = false;
  if (!ok) (void)remove(destination);
  return ok;
}

static bool WriteReportAt(const char *path, const char *reason) {
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  char host[128];
  char outcome[128];
  char backend[192];
  char filter[64];
  char report_reason[192];
  JsonEscape(s_diagnostics.host, host, sizeof host);
  JsonEscape(s_diagnostics.outcome, outcome, sizeof outcome);
  JsonEscape(s_diagnostics.backend, backend, sizeof backend);
  JsonEscape(s_diagnostics.screen_filter, filter, sizeof filter);
  JsonEscape(reason ? reason : "rolling_report", report_reason,
             sizeof report_reason);
  (void)fprintf(file, "{\n  \"schema\": \"dkc2-diagnostic-v1\",\n");
  host_report_dump_json(file);
  (void)fprintf(
      file,
      "  \"dkc2\": {\"host\": \"%s\", \"outcome\": \"%s\", "
      "\"reason\": \"%s\", \"frame\": %llu, \"resume_pc\": "
      "\"%06x\", \"backend\": \"%s\", \"screen_filter\": \"%s\", "
      "\"audio_available\": %s},\n"
      "  \"privacy\": {\"rom_bytes\": false, \"rom_path\": false, "
      "\"save_data\": false, \"save_states\": false, "
      "\"generated_game_code\": false, \"screenshots\": false, "
      "\"audio_capture\": false}\n}\n",
      host, outcome, report_reason,
      (unsigned long long)s_diagnostics.frame,
      (unsigned)s_diagnostics.resume_pc, backend, filter,
      s_diagnostics.audio_available ? "true" : "false");
  bool ok = !ferror(file);
  if (fclose(file) != 0) ok = false;
  return ok;
}

static void CopyOptionalArtifact(const char *bundle, const char *source,
                                 const char *name) {
  char destination[kPathCapacity];
  if (JoinPath(destination, sizeof destination, bundle, name))
    (void)CopyDiagnosticFile(source, destination);
}

static bool WriteBundleReadme(const char *bundle, const char *reason) {
  char path[kPathCapacity];
  if (!JoinPath(path, sizeof path, bundle, "README.txt")) return false;
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  (void)fprintf(
      file,
      "DKC2Recomp automatic diagnostic bundle\n"
      "Reason: %s\n\n"
      "Attach this complete folder when reporting the problem. It may contain "
      "absolute paths for loaded program modules and basic system information.\n\n"
      "Privacy allowlist: this bundle never copies rom.cfg, any ROM, generated "
      "game code, SRAM, save states, screenshots, or audio captures. The only "
      "optional user files are launcher.cfg and performance.log.\n",
      reason ? reason : "unknown");
  bool ok = !ferror(file);
  if (fclose(file) != 0) ok = false;
  return ok;
}

static bool CreateBundle(const char *reason, const char *extra_artifact,
                         const char *extra_name) {
  if (!s_diagnostics.initialized || s_diagnostics.bundle_written) return false;
  char stamp[32];
  char name[128];
  char bundle[kPathCapacity];
  UtcStamp(stamp, sizeof stamp);
  (void)snprintf(name, sizeof name, "diagnostic_bundle_%s_%lu", stamp,
                 DKC2_GETPID());
  if (!JoinPath(bundle, sizeof bundle, s_diagnostics.root, name) ||
      !EnsureDirectory(bundle))
    return false;
  char report[kPathCapacity];
  if (!JoinPath(report, sizeof report, bundle, "report.json") ||
      !WriteReportAt(report, reason))
    return false;
  (void)WriteBundleReadme(bundle, reason);
  CopyOptionalArtifact(bundle, "launcher.cfg", "launcher.cfg");
  CopyOptionalArtifact(bundle, "performance.log", "performance.log");
  if (extra_artifact && extra_artifact[0] && extra_name && extra_name[0])
    CopyOptionalArtifact(bundle, extra_artifact, extra_name);
  s_diagnostics.bundle_written = true;
  host_report_breadcrumb("diagnostic bundle: %s", bundle);
  return true;
}

static void WriteRollingReport(const char *reason) {
  char path[kPathCapacity];
  char temporary[kPathCapacity];
  if (!JoinPath(path, sizeof path, s_diagnostics.root,
                "last_run_report.json") ||
      !JoinPath(temporary, sizeof temporary, s_diagnostics.root,
                "last_run_report.tmp"))
    return;
  if (!WriteReportAt(temporary, reason)) return;
  (void)remove(path);
  if (rename(temporary, path) != 0) (void)remove(temporary);
}

#ifdef _WIN32
static LONG WINAPI Dkc2UnhandledException(EXCEPTION_POINTERS *information) {
  const char *dump = host_report_write_minidump(information);
  host_report_fatal("unhandled Windows exception");
  (void)snprintf(s_diagnostics.outcome, sizeof s_diagnostics.outcome,
                 "crashed");
  WriteRollingReport("unhandled_windows_exception");
  (void)CreateBundle("unhandled_windows_exception", dump,
                     "crash_minidump.dmp");
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void Dkc2SignalHandler(int signal_number) {
  int descriptor = open(s_signal_marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor >= 0) {
    static const char message[] =
        "DKC2Recomp terminated by a fatal POSIX signal. A support bundle will "
        "be completed on the next launch.\n";
    (void)write(descriptor, message, sizeof message - 1);
    (void)close(descriptor);
  }
  _exit(128 + signal_number);
}

static void InstallSignalHandler(int signal_number) {
  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_handler = Dkc2SignalHandler;
  (void)sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESETHAND;
  (void)sigaction(signal_number, &action, NULL);
}
#endif

static void Dkc2DiagnosticsAtExit(void) {
  if (!s_diagnostics.initialized || s_diagnostics.shutdown) return;
  Dkc2DiagnosticsShutdown(host_report_has_fatal() ? "fatal_exit" : "early_exit");
}

bool Dkc2DiagnosticsInit(const char *host_name, const char *build_version) {
  if (s_diagnostics.initialized) return true;
  memset(&s_diagnostics, 0, sizeof s_diagnostics);
  const char *override = getenv("DKC2_DIAGNOSTICS_DIR");
  const char *root = override && override[0] ? override : "diagnostics";
  if (snprintf(s_diagnostics.root, sizeof s_diagnostics.root, "%s", root) <= 0 ||
      !EnsureDirectory(s_diagnostics.root))
    return false;
  (void)snprintf(s_diagnostics.host, sizeof s_diagnostics.host, "%s",
                 host_name ? host_name : "unknown");
  (void)snprintf(s_diagnostics.outcome, sizeof s_diagnostics.outcome,
                 "running");
  (void)snprintf(s_diagnostics.backend, sizeof s_diagnostics.backend,
                 "not_initialized");
  (void)snprintf(s_diagnostics.screen_filter,
                 sizeof s_diagnostics.screen_filter, "Raw");
  s_diagnostics.initialized = true;
  host_report_set_output_directory(s_diagnostics.root);
  host_report_init("DKC2Recomp", build_version ? build_version : "dev");
  host_report_breadcrumb("diagnostics initialized: host=%s", s_diagnostics.host);
#ifdef _WIN32
  (void)SetUnhandledExceptionFilter(Dkc2UnhandledException);
#else
  if (JoinPath(s_signal_marker, sizeof s_signal_marker, s_diagnostics.root,
               "pending_crash_signal.txt")) {
    FILE *pending = fopen(s_signal_marker, "rb");
    if (pending) {
      (void)fclose(pending);
      (void)CreateBundle("previous_posix_signal", s_signal_marker,
                         "crash_signal.txt");
      (void)remove(s_signal_marker);
      s_diagnostics.bundle_written = false;
    }
  }
  InstallSignalHandler(SIGSEGV);
  InstallSignalHandler(SIGILL);
  InstallSignalHandler(SIGFPE);
#ifdef SIGBUS
  InstallSignalHandler(SIGBUS);
#endif
#endif
  if (atexit(Dkc2DiagnosticsAtExit) != 0)
    host_report_breadcrumb("warning: diagnostics atexit registration failed");
  return true;
}

void Dkc2DiagnosticsSetPresentation(const char *backend,
                                    const char *screen_filter,
                                    bool audio_available) {
  if (!s_diagnostics.initialized) return;
  if (backend)
    (void)snprintf(s_diagnostics.backend, sizeof s_diagnostics.backend, "%s",
                   backend);
  if (screen_filter)
    (void)snprintf(s_diagnostics.screen_filter,
                   sizeof s_diagnostics.screen_filter, "%s", screen_filter);
  s_diagnostics.audio_available = audio_available;
  host_report_breadcrumb("presentation: backend=%s filter=%s audio=%s",
                         s_diagnostics.backend, s_diagnostics.screen_filter,
                         audio_available ? "yes" : "no");
}

void Dkc2DiagnosticsHeartbeat(uint64_t frame, uint32_t resume_pc) {
  if (!s_diagnostics.initialized) return;
  s_diagnostics.frame = frame;
  s_diagnostics.resume_pc = resume_pc;
  if (frame == 1 || frame % 600 == 0)
    host_report_breadcrumb("frame=%llu resume_pc=$%06x",
                           (unsigned long long)frame, (unsigned)resume_pc);
}

void Dkc2DiagnosticsFatal(const char *message) {
  if (!s_diagnostics.initialized) return;
  host_report_fatal(message ? message : "runtime failure");
  (void)snprintf(s_diagnostics.outcome, sizeof s_diagnostics.outcome,
                 "runtime_failure");
  WriteRollingReport("runtime_failure");
  (void)CreateBundle("runtime_failure", NULL, NULL);
}

void Dkc2DiagnosticsShutdown(const char *outcome) {
  if (!s_diagnostics.initialized || s_diagnostics.shutdown) return;
  s_diagnostics.shutdown = true;
  if (outcome && outcome[0])
    (void)snprintf(s_diagnostics.outcome, sizeof s_diagnostics.outcome, "%s",
                   outcome);
  host_report_breadcrumb("shutdown: outcome=%s", s_diagnostics.outcome);
  WriteRollingReport("process_exit");
  if (host_report_has_fatal() || EnvironmentEnabled("DKC2_DIAGNOSTIC_BUNDLE"))
    (void)CreateBundle(host_report_has_fatal() ? "fatal_exit" : "user_requested",
                       NULL, NULL);
}
