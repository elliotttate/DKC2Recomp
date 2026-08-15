#include "desktop_launcher.h"

#include "launcher_profile.h"

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef DKC2_RELEASE_VERSION
#define DKC2_RELEASE_VERSION "dev"
#endif

enum { kDkc2AudioRate = 32040 };
enum {
  kDkc2BindingUp = 0,
  kDkc2BindingDown,
  kDkc2BindingLeft,
  kDkc2BindingRight,
  kDkc2BindingA,
  kDkc2BindingB,
  kDkc2BindingX,
  kDkc2BindingY,
  kDkc2BindingL,
  kDkc2BindingR,
  kDkc2BindingStart,
  kDkc2BindingSelect,
  kDkc2BindingCount,
};
enum {
  kDkc2AssistRewind = 0,
  kDkc2AssistFastForward,
  kDkc2AssistSaveState,
  kDkc2AssistLoadState,
  kDkc2AssistCount,
};

static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

void Dkc2LauncherSettingsDefault(RecompLauncherCSettings *settings) {
  if (!settings) return;
  memset(settings, 0, sizeof *settings);
  settings->output_method = 1;
  settings->window_scale = 3;
  settings->linear_filter = 0;
  settings->renderer = 1;
  settings->texture_filter = 0;
  settings->screen_kind = 0;
  settings->widescreen = 0;
  settings->enable_audio = 1;
  settings->audio_freq = kDkc2AudioRate;
  settings->volume = 100;
  settings->player_src[0] = 1;
  settings->player_src[1] = 2;
  settings->deadzone[0] = 24;
  settings->deadzone[1] = 24;
  settings->assist_tools = 0;
  const int keyboard[kDkc2BindingCount] = {
      SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
      SDL_SCANCODE_RIGHT, SDL_SCANCODE_X, SDL_SCANCODE_Z,
      SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_Q, SDL_SCANCODE_W,
      SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT,
  };
  const int gamepad[kDkc2BindingCount] = {
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_Y),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_START),
      RECOMP_LAUNCHER_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK),
  };
  for (int player = 0; player < 2; player++) {
    memcpy(settings->player_key_bind[player], keyboard, sizeof keyboard);
    memcpy(settings->player_pad_bind[player], gamepad, sizeof gamepad);
  }
  settings->assist_key_bind[kDkc2AssistRewind] = SDL_SCANCODE_1;
  settings->assist_key_bind[kDkc2AssistFastForward] = SDL_SCANCODE_2;
  settings->assist_key_bind[kDkc2AssistSaveState] = SDL_SCANCODE_F5;
  settings->assist_key_bind[kDkc2AssistLoadState] = SDL_SCANCODE_F9;
  settings->assist_pad_bind[kDkc2AssistRewind] =
      RECOMP_LAUNCHER_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
  settings->assist_pad_bind[kDkc2AssistFastForward] =
      RECOMP_LAUNCHER_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
}

void Dkc2LauncherSettingsLoad(RecompLauncherCSettings *settings) {
  if (!settings) return;
  FILE *file = fopen("launcher.cfg", "r");
  if (!file) return;
  char line[128];
  while (fgets(line, sizeof line, file)) {
    char key[48];
    int value = 0;
    if (sscanf(line, "%47[^=]=%d", key, &value) != 2) continue;
    if (strcmp(key, "WindowScale") == 0)
      settings->window_scale = ClampInt(value, 1, 4);
    else if (strcmp(key, "Fullscreen") == 0)
      settings->fullscreen = ClampInt(value, 0, 2);
    else if (strcmp(key, "Renderer") == 0)
      settings->renderer = ClampInt(value, 0, 1);
    else if (strcmp(key, "TextureFilter") == 0)
      settings->texture_filter = ClampInt(value, 0, 1);
    else if (strcmp(key, "LinearFilter") == 0) {
      settings->linear_filter = value != 0;
      settings->texture_filter = value != 0;
    } else if (strcmp(key, "ScreenKind") == 0)
      settings->screen_kind = ClampInt(value, 0, 3);
    else if (strcmp(key, "Widescreen") == 0)
      settings->widescreen = value != 0;
    else if (strcmp(key, "EnableAudio") == 0)
      settings->enable_audio = value != 0;
    else if (strcmp(key, "AudioFrequency") == 0)
      settings->audio_freq = ClampInt(value, 8000, 192000);
    else if (strcmp(key, "Volume") == 0)
      settings->volume = ClampInt(value, 0, 100);
    else if (strcmp(key, "Player1Source") == 0)
      settings->player_src[0] = ClampInt(value, 0, 2);
    else if (strcmp(key, "Player2Source") == 0)
      settings->player_src[1] = ClampInt(value, 0, 2);
    else if (strcmp(key, "Player1Deadzone") == 0)
      settings->deadzone[0] = ClampInt(value, 0, 100);
    else if (strcmp(key, "Player2Deadzone") == 0)
      settings->deadzone[1] = ClampInt(value, 0, 100);
    else if (strcmp(key, "SkipLauncher") == 0)
      settings->skip_launcher = value != 0;
    else if (strcmp(key, "AssistTools") == 0)
      settings->assist_tools = value != 0;
    else {
      int player = 0;
      int binding = 0;
      if (sscanf(key, "Player%dKey%d", &player, &binding) == 2 &&
          player >= 1 && player <= 2 &&
          binding >= 0 && binding < RECOMP_LAUNCHER_MAX_BINDINGS) {
        settings->player_key_bind[player - 1][binding] =
            ClampInt(value, 0, 512);
      } else if (sscanf(key, "Player%dPad%d", &player, &binding) == 2 &&
                 player >= 1 && player <= 2 &&
                 binding >= 0 &&
                 binding < RECOMP_LAUNCHER_MAX_BINDINGS) {
        settings->player_pad_bind[player - 1][binding] =
            ClampInt(value, 0, 255);
      } else if (sscanf(key, "AssistKey%d", &binding) == 1 &&
                 binding >= 0 &&
                 binding < RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS) {
        settings->assist_key_bind[binding] = ClampInt(value, 0, 512);
      } else if (sscanf(key, "AssistPad%d", &binding) == 1 &&
                 binding >= 0 &&
                 binding < RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS) {
        settings->assist_pad_bind[binding] = ClampInt(value, 0, 255);
      }
    }
  }
  (void)fclose(file);
}

bool Dkc2LauncherSettingsSave(const RecompLauncherCSettings *settings) {
  if (!settings) return false;
  FILE *file = fopen("launcher.cfg", "w");
  if (!file) return false;
  bool ok = fprintf(file,
                    "WindowScale=%d\nFullscreen=%d\nRenderer=%d\n"
                    "TextureFilter=%d\nScreenKind=%d\nWidescreen=%d\n"
                    "EnableAudio=%d\n"
                    "AudioFrequency=%d\n"
                    "Volume=%d\nPlayer1Source=%d\nPlayer2Source=%d\n"
                    "Player1Deadzone=%d\nPlayer2Deadzone=%d\n"
                    "SkipLauncher=%d\nAssistTools=%d\n",
                    ClampInt(settings->window_scale, 1, 4),
                    ClampInt(settings->fullscreen, 0, 2),
                    ClampInt(settings->renderer, 0, 1),
                    ClampInt(settings->texture_filter, 0, 1),
                    ClampInt(settings->screen_kind, 0, 3),
                    settings->widescreen != 0,
                    settings->enable_audio != 0,
                    ClampInt(settings->audio_freq, 8000, 192000),
                    ClampInt(settings->volume, 0, 100),
                    ClampInt(settings->player_src[0], 0, 2),
                    ClampInt(settings->player_src[1], 0, 2),
                    ClampInt(settings->deadzone[0], 0, 100),
                    ClampInt(settings->deadzone[1], 0, 100),
                    settings->skip_launcher != 0,
                    settings->assist_tools != 0) > 0;
  for (int player = 0; ok && player < 2; player++) {
    for (int binding = 0; ok && binding < kDkc2BindingCount; binding++) {
      ok = fprintf(file, "Player%dKey%d=%d\nPlayer%dPad%d=%d\n",
                   player + 1, binding,
                   ClampInt(settings->player_key_bind[player][binding],
                            0, 512),
                   player + 1, binding,
                   ClampInt(settings->player_pad_bind[player][binding],
                            0, 255)) > 0;
    }
  }
  for (int action = 0; ok && action < kDkc2AssistCount; action++) {
    ok = fprintf(file, "AssistKey%d=%d\nAssistPad%d=%d\n",
                 action, ClampInt(settings->assist_key_bind[action], 0, 512),
                 action, ClampInt(settings->assist_pad_bind[action], 0, 255)) >
         0;
  }
  if (fclose(file) != 0) ok = false;
  return ok;
}

bool Dkc2LauncherReadRomCache(char *path, size_t capacity) {
  if (!path || capacity == 0) return false;
  path[0] = '\0';
  FILE *file = fopen("rom.cfg", "r");
  if (!file) return false;
  bool read = fgets(path, (int)capacity, file) != NULL;
  (void)fclose(file);
  if (!read) return false;
  path[strcspn(path, "\r\n")] = '\0';
  if (!path[0]) return false;
  file = fopen(path, "rb");
  if (!file) {
    path[0] = '\0';
    return false;
  }
  (void)fclose(file);
  return true;
}

bool Dkc2LauncherWriteRomCache(const char *path) {
  if (!path || !path[0]) return false;
  FILE *file = fopen("rom.cfg", "w");
  if (!file) return false;
  bool ok = fprintf(file, "%s\n", path) > 0;
  if (fclose(file) != 0) ok = false;
  return ok;
}

int Dkc2LauncherRun(RecompLauncherCSettings *settings,
                    const char *initial_rom, char *selected_rom,
                    size_t selected_capacity,
                    const char *const *renderer_labels,
                    size_t renderer_count) {
  static const uint8_t known_sha256[][32] = {{
      0x35, 0x42, 0x1a, 0x9a, 0xf9, 0xdd, 0x01, 0x1b,
      0x40, 0xb9, 0x1f, 0x79, 0x21, 0x92, 0xaf, 0x9f,
      0x99, 0xc9, 0x32, 0x01, 0xd8, 0xd3, 0x94, 0x02,
      0x6b, 0xdf, 0xb4, 0x2c, 0xbf, 0x2d, 0x86, 0x33,
  }};
  if (!settings || !selected_rom || selected_capacity == 0) return 1;
  RecompLauncherCGameInfo game;
  RecompLauncherCSettings defaults;
  memset(&game, 0, sizeof game);
  Dkc2LauncherSettingsDefault(&defaults);
  (void)launcher_profile_apply("snes", &game);
  game.name = "Donkey Kong Country 2: Diddy's Kong Quest";
  game.region = "USA v1.0";
  game.expected_crc = 0x006364DBu;
  game.has_expected_crc = 1;
  game.known_sha256 = known_sha256;
  game.num_known_sha256 = sizeof known_sha256 / sizeof known_sha256[0];
  game.widescreen_supported = 1;
  game.has_renderer = renderer_labels && renderer_count > 1;
  game.has_texture_filter = 1;
  game.has_screen_kind = 1;
  game.renderer_labels = renderer_labels;
  game.num_renderers = (int)renderer_count;
  game.num_players = 2;
  game.sram_path = "saves/save.srm";
  game.hide_rebind = 0;
  game.settings_bindings = 1;
  game.has_assist_tools = 1;
  game.assist_tools_note =
      "Enables rewind, fast-forward, and five file save-state slots. "
      "The game window discloses when this optional mode is active.";
  static const char *const assist_labels[kDkc2AssistCount] = {
      "Rewind", "Fast-forward", "Save state", "Load state",
  };
  game.assist_binding_labels = assist_labels;
  game.assist_binding_count = kDkc2AssistCount;
  game.credits_text =
      "Donkey Kong Country 2: Diddy's Kong Quest\n"
      "Original game developed by Rare and published by Nintendo.\n\n"
      "Native-port foundation\n"
      "DKC2Recomp, SNESrecomp, recomp-ui, and their contributors.\n\n"
      "The complete named original-game credits will be added from the "
      "curated list supplied by the project owner.";
  game.default_settings = &defaults;
  return recomp_launcher_run_window(
      DKC2_PRODUCT_TITLE, settings, &game, "assets",
      initial_rom && initial_rom[0] ? initial_rom : NULL, selected_rom,
      selected_capacity);
}
