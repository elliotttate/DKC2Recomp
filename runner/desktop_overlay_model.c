#include "desktop_overlay_model.h"

static bool IsAssistAction(uint32_t action) {
  return (action &
          (kDkc2OverlayActionSaveState | kDkc2OverlayActionLoadState)) != 0;
}

void Dkc2DesktopOverlayModelInit(Dkc2DesktopOverlayModel *model,
                                 bool assist_tools) {
  if (!model) return;
  model->pending_actions = 0;
  model->selected_slot = 0;
  model->capture_player = 0;
  model->capture_index = 0;
  model->binding_capture = kDkc2OverlayCaptureNone;
  model->open = false;
  model->assist_tools = assist_tools;
  model->pad_capture_armed = false;
}

void Dkc2DesktopOverlayModelSetOpen(Dkc2DesktopOverlayModel *model,
                                    bool open) {
  if (!model) return;
  model->open = open;
  if (!open) Dkc2DesktopOverlayModelCancelBindingCapture(model);
}

void Dkc2DesktopOverlayModelToggle(Dkc2DesktopOverlayModel *model) {
  if (!model) return;
  model->open = !model->open;
  if (!model->open) Dkc2DesktopOverlayModelCancelBindingCapture(model);
}

void Dkc2DesktopOverlayModelSetAssistTools(Dkc2DesktopOverlayModel *model,
                                           bool enabled) {
  if (!model) return;
  model->assist_tools = enabled;
}

void Dkc2DesktopOverlayModelSetSlot(Dkc2DesktopOverlayModel *model, int slot) {
  if (!model) return;
  if (slot < 0) slot = 0;
  if (slot > 4) slot = 4;
  model->selected_slot = slot;
}

void Dkc2DesktopOverlayModelShiftSlot(Dkc2DesktopOverlayModel *model,
                                      int delta) {
  if (!model || delta == 0) return;
  int slot = (model->selected_slot + delta) % 5;
  if (slot < 0) slot += 5;
  model->selected_slot = slot;
}

bool Dkc2DesktopOverlayModelRequest(Dkc2DesktopOverlayModel *model,
                                    uint32_t action) {
  if (!model || action == kDkc2OverlayActionNone) return false;
  if (IsAssistAction(action) && !model->assist_tools) return false;
  model->pending_actions |= action;
  if (action & kDkc2OverlayActionResume) {
    model->open = false;
    Dkc2DesktopOverlayModelCancelBindingCapture(model);
  }
  return true;
}

uint32_t Dkc2DesktopOverlayModelTakeActions(Dkc2DesktopOverlayModel *model) {
  if (!model) return 0;
  uint32_t actions = model->pending_actions;
  model->pending_actions = 0;
  return actions;
}

bool Dkc2DesktopOverlayModelBeginBindingCapture(
    Dkc2DesktopOverlayModel *model, Dkc2OverlayBindingCapture capture,
    int player, int index) {
  if (!model || !model->open || capture <= kDkc2OverlayCaptureNone ||
      capture > kDkc2OverlayCaptureAssistPad)
    return false;
  bool player_capture = capture == kDkc2OverlayCapturePlayerKey ||
                        capture == kDkc2OverlayCapturePlayerPad;
  int maximum = player_capture ? 12 : 4;
  if ((player_capture && (player < 0 || player >= 2)) ||
      index < 0 || index >= maximum)
    return false;
  model->binding_capture = capture;
  model->capture_player = player_capture ? player : 0;
  model->capture_index = index;
  model->pad_capture_armed = false;
  return true;
}

void Dkc2DesktopOverlayModelCancelBindingCapture(
    Dkc2DesktopOverlayModel *model) {
  if (!model) return;
  model->binding_capture = kDkc2OverlayCaptureNone;
  model->capture_player = 0;
  model->capture_index = 0;
  model->pad_capture_armed = false;
}

bool Dkc2DesktopOverlayModelBindingCaptureIsPad(
    const Dkc2DesktopOverlayModel *model) {
  return model &&
         (model->binding_capture == kDkc2OverlayCapturePlayerPad ||
          model->binding_capture == kDkc2OverlayCaptureAssistPad);
}

bool Dkc2DesktopOverlayModelArmPadCapture(
    Dkc2DesktopOverlayModel *model, bool gamepad_neutral) {
  if (!Dkc2DesktopOverlayModelBindingCaptureIsPad(model)) return false;
  if (!model->pad_capture_armed && gamepad_neutral)
    model->pad_capture_armed = true;
  return model->pad_capture_armed;
}
