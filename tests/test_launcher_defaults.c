#include "launcher_model.h"

#include <stdio.h>
#include <string.h>

/* The model's Zapper toggle bridge is unrelated to this test. Supplying the
 * no-op host seam keeps this synthetic settings test independent of bind-file
 * persistence. */
void launcher_binds_set_zapper(int mouse_enabled, int crosshair) {
  (void)mouse_enabled;
  (void)crosshair;
}

static int Check(int condition, const char *message) {
  if (condition) return 0;
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

int main(void) {
  RecompLauncherCSettings defaults;
  memset(&defaults, 0, sizeof defaults);
  defaults.output_method = 1;
  defaults.window_scale = 3;
  defaults.renderer = 1;
  defaults.enable_audio = 1;
  defaults.audio_freq = 32040;
  defaults.volume = 100;
  defaults.player_src[0] = 1;
  defaults.player_src[1] = 2;
  defaults.deadzone[0] = 24;
  defaults.deadzone[1] = 24;

  RecompLauncherCSettings changed = defaults;
  changed.window_scale = 1;
  changed.fullscreen = 2;
  changed.renderer = 0;
  changed.texture_filter = 1;
  changed.screen_kind = 3;
  changed.enable_audio = 0;
  changed.volume = 15;
  changed.player_src[0] = 2;
  changed.player_src[1] = 0;
  changed.deadzone[0] = 50;
  changed.deadzone[1] = 5;
  changed.skip_launcher = 1;

  RecompLauncherCGameInfo game;
  memset(&game, 0, sizeof game);
  game.name = "Synthetic SNES Test";
  game.platform = "SUPER NINTENDO";
  game.num_players = 2;
  game.has_renderer = 1;
  game.has_texture_filter = 1;
  game.has_screen_kind = 1;
  game.default_settings = &defaults;

  LauncherModel model;
  launcher_model_init(&model, &changed, &game, "missing-test-rom.smc");
  if (Check(launcher_model_can_restore_defaults(&model),
            "host defaults did not enable the restore action"))
    return 1;

  launcher_model_request_restore_defaults(&model);
  if (Check(model.defaults_modal_open,
            "restore request did not open confirmation"))
    return 1;
  launcher_model_cancel_restore_defaults(&model);
  if (Check(!model.defaults_modal_open && model.s.volume == 15,
            "cancel changed settings or left confirmation open"))
    return 1;

  launcher_model_request_restore_defaults(&model);
  launcher_model_restore_defaults(&model);
  if (Check(!model.defaults_modal_open,
            "confirmed restore left confirmation open"))
    return 1;
  if (Check(memcmp(&model.s, &defaults, sizeof defaults) == 0,
            "confirmed restore did not replace the complete settings value"))
    return 1;
  if (Check(strcmp(launcher_model_rom_path(&model),
                   "missing-test-rom.smc") == 0,
            "settings restore changed the selected ROM"))
    return 1;

  game.default_settings = NULL;
  launcher_model_init(&model, &changed, &game, NULL);
  launcher_model_request_restore_defaults(&model);
  launcher_model_restore_defaults(&model);
  if (Check(!launcher_model_can_restore_defaults(&model) &&
                !model.defaults_modal_open && model.s.volume == 15,
            "a host without defaults exposed or applied the action"))
    return 1;

  puts("launcher default restore tests passed");
  return 0;
}
