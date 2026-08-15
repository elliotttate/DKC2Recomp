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
  defaults.rewind_depth = 50;
  defaults.rewind_interval = 15;
  defaults.player_src[0] = 1;
  defaults.player_src[1] = 2;
  defaults.deadzone[0] = 24;
  defaults.deadzone[1] = 24;
  defaults.assist_tools = 0;
  defaults.player_key_bind[0][4] = 27;
  defaults.player_pad_bind[0][4] = RECOMP_LAUNCHER_PAD_BUTTON(1);
  defaults.assist_key_bind[0] = 30;
  defaults.assist_pad_bind[0] = RECOMP_LAUNCHER_PAD_AXIS(4, 1);

  RecompLauncherCSettings changed = defaults;
  changed.window_scale = 1;
  changed.fullscreen = 2;
  changed.renderer = 0;
  changed.texture_filter = 1;
  changed.screen_kind = 3;
  changed.widescreen = 1;
  changed.enable_audio = 0;
  changed.volume = 15;
  changed.player_src[0] = 2;
  changed.player_src[1] = 0;
  changed.deadzone[0] = 50;
  changed.deadzone[1] = 5;
  changed.skip_launcher = 1;
  changed.assist_tools = 1;

  RecompLauncherCGameInfo game;
  memset(&game, 0, sizeof game);
  game.name = "Synthetic SNES Test";
  game.platform = "SUPER NINTENDO";
  game.num_players = 2;
  game.has_renderer = 1;
  game.has_texture_filter = 1;
  game.has_screen_kind = 1;
  game.widescreen_supported = 1;
  game.has_assist_tools = 1;
  game.settings_bindings = 1;
  static const char *const assist_labels[] = {
      "Rewind", "Fast-forward", "Save state", "Load state"};
  game.assist_binding_labels = assist_labels;
  game.assist_binding_count = 4;
  game.assist_tools_note = "Synthetic assist-tools explanation.";
  game.credits_text = "Synthetic credits.";
  game.default_settings = &defaults;

  LauncherModel model;
  launcher_model_init(&model, &changed, &game, "missing-test-rom.smc");
  if (Check(model.widescreen_supported && model.s.widescreen == 1,
            "widescreen capability or selected mode did not reach the model"))
    return 1;
  launcher_model_toggle_widescreen(&model);
  if (Check(model.s.widescreen == 0,
            "supported widescreen mode did not toggle"))
    return 1;
  launcher_model_toggle_widescreen(&model);
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
  if (Check(model.has_assist_tools &&
                strcmp(model.assist_tools_note,
                       "Synthetic assist-tools explanation.") == 0,
            "assist-tools capability did not reach the launcher model"))
    return 1;
  if (Check(strcmp(model.credits_text, "Synthetic credits.") == 0,
            "credits text did not reach the launcher model"))
    return 1;
  if (Check(strcmp(launcher_view_name(LNG_VIEW_ASSIST_TOOLS),
                   "Assist Tools") == 0 &&
                strcmp(launcher_view_name(LNG_VIEW_CREDITS),
                       "Credits") == 0,
            "new launcher views do not have stable names"))
    return 1;

  launcher_model_begin_capture(&model, 4);
  launcher_model_set_captured_key(&model, 44);
  launcher_model_cancel_capture(&model);
  if (Check(model.s.player_key_bind[0][4] == 44,
            "player keyboard capture did not update settings"))
    return 1;
  launcher_model_begin_pad_capture(&model, 4);
  launcher_model_set_captured_pad(&model,
                                  RECOMP_LAUNCHER_PAD_BUTTON(2));
  launcher_model_cancel_capture(&model);
  if (Check(model.s.player_pad_bind[0][4] ==
                RECOMP_LAUNCHER_PAD_BUTTON(2),
            "player gamepad capture did not update settings"))
    return 1;
  launcher_model_reset_player_bindings(&model, 0);
  if (Check(model.s.player_key_bind[0][4] == 27 &&
                model.s.player_pad_bind[0][4] ==
                    RECOMP_LAUNCHER_PAD_BUTTON(1),
            "player binding reset did not restore host defaults"))
    return 1;
  launcher_model_begin_assist_capture(&model, 0, false);
  launcher_model_set_captured_key(&model, 55);
  launcher_model_cancel_capture(&model);
  if (Check(model.s.assist_key_bind[0] == 55,
            "Assist keyboard capture did not update settings"))
    return 1;
  launcher_model_reset_assist_bindings(&model);
  if (Check(model.s.assist_key_bind[0] == 30 &&
                model.s.assist_pad_bind[0] ==
                    RECOMP_LAUNCHER_PAD_AXIS(4, 1),
            "Assist binding reset did not restore host defaults"))
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
