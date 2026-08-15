#include "host_report.h"
#include "spc_player.h"
#include "types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

bool g_new_ppu = true;

static void HeadlessSpcInitialize(SpcPlayer *player) { (void)player; }

static void HeadlessSpcUpload(SpcPlayer *player, const uint8_t *data) {
  (void)player;
  (void)data;
}

static SpcPlayer g_headless_spc_player = {
    .initialize = HeadlessSpcInitialize,
    .upload = HeadlessSpcUpload,
};

SpcPlayer *g_spc_player = &g_headless_spc_player;

void NORETURN Die(const char *error) {
#ifdef DKC2_REAL_HOST_REPORT
  host_report_fatal(error ? error : "unknown error");
#endif
  fprintf(stderr, "fatal: %s\n", error ? error : "unknown error");
  exit(EXIT_FAILURE);
}

void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

#ifndef DKC2_REAL_HOST_REPORT
void host_report_init(const char *game_name, const char *build_version) {
  (void)game_name;
  (void)build_version;
}

void host_report_set_output_directory(const char *directory) {
  (void)directory;
}

void host_report_breadcrumb(const char *format, ...) {
  (void)format;
}

void host_report_fatal(const char *message) {
  if (message) fprintf(stderr, "fatal: %s\n", message);
}

int host_report_has_fatal(void) { return 0; }
void host_report_dump_json(FILE *stream) { (void)stream; }
const char *host_report_write_minidump(void *info) {
  (void)info;
  return NULL;
}
const char *host_report_preserve_crash_copy(const char *path) {
  (void)path;
  return NULL;
}
void host_report_crash_test_tick(void) {}
#endif
