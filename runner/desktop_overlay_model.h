#ifndef DKC2_DESKTOP_OVERLAY_MODEL_H
#define DKC2_DESKTOP_OVERLAY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kDkc2OverlayActionNone = 0,
  kDkc2OverlayActionResume = 1u << 0,
  kDkc2OverlayActionQuit = 1u << 1,
  kDkc2OverlayActionSaveState = 1u << 2,
  kDkc2OverlayActionLoadState = 1u << 3,
};

typedef enum Dkc2OverlayBindingCapture {
  kDkc2OverlayCaptureNone = 0,
  kDkc2OverlayCapturePlayerKey,
  kDkc2OverlayCapturePlayerPad,
  kDkc2OverlayCaptureAssistKey,
  kDkc2OverlayCaptureAssistPad,
} Dkc2OverlayBindingCapture;

typedef struct Dkc2DesktopOverlayModel {
  uint32_t pending_actions;
  int selected_slot;
  int capture_player;
  int capture_index;
  Dkc2OverlayBindingCapture binding_capture;
  bool open;
  bool assist_tools;
  bool pad_capture_armed;
} Dkc2DesktopOverlayModel;

void Dkc2DesktopOverlayModelInit(Dkc2DesktopOverlayModel *model,
                                 bool assist_tools);
void Dkc2DesktopOverlayModelSetOpen(Dkc2DesktopOverlayModel *model,
                                    bool open);
void Dkc2DesktopOverlayModelToggle(Dkc2DesktopOverlayModel *model);
void Dkc2DesktopOverlayModelSetAssistTools(Dkc2DesktopOverlayModel *model,
                                           bool enabled);
void Dkc2DesktopOverlayModelSetSlot(Dkc2DesktopOverlayModel *model, int slot);
void Dkc2DesktopOverlayModelShiftSlot(Dkc2DesktopOverlayModel *model,
                                      int delta);
bool Dkc2DesktopOverlayModelRequest(Dkc2DesktopOverlayModel *model,
                                    uint32_t action);
uint32_t Dkc2DesktopOverlayModelTakeActions(Dkc2DesktopOverlayModel *model);
bool Dkc2DesktopOverlayModelBeginBindingCapture(
    Dkc2DesktopOverlayModel *model, Dkc2OverlayBindingCapture capture,
    int player, int index);
void Dkc2DesktopOverlayModelCancelBindingCapture(
    Dkc2DesktopOverlayModel *model);
bool Dkc2DesktopOverlayModelBindingCaptureIsPad(
    const Dkc2DesktopOverlayModel *model);
bool Dkc2DesktopOverlayModelArmPadCapture(
    Dkc2DesktopOverlayModel *model, bool gamepad_neutral);

/* Escape leaves fullscreen before it is offered to the closed pause menu.
 * Once windowed, or while the menu is already open, Escape retains its
 * normal overlay/capture behavior. */
bool Dkc2DesktopEscapeExitsFullscreen(bool fullscreen, bool overlay_open);

#ifdef __cplusplus
}
#endif

#endif
