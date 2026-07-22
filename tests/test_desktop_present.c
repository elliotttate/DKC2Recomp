#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "desktop_filter.h"
#include "desktop_present.h"
#include "desktop_present_gl.h"
#include "desktop_viewport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void CheckPixel(const uint8_t *pixels, int pitch, int x, int y,
                       uint8_t blue, uint8_t green, uint8_t red,
                       const char *message) {
  const uint8_t *pixel = pixels + y * pitch + x * 4;
  if (pixel[0] != blue || pixel[1] != green || pixel[2] != red) {
    fprintf(stderr,
            "FAIL: %s at (%d,%d): got BGR=(%u,%u,%u), expected "
            "(%u,%u,%u)\n",
            message, x, y, pixel[0], pixel[1], pixel[2], blue, green, red);
    failures++;
  }
}

int main(void) {
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(12, 6, &viewport) ||
      viewport.x != 2 || viewport.y != 0 || viewport.width != 8 ||
      viewport.height != 6) {
    fprintf(stderr, "FAIL: shared 4:3 viewport calculation\n");
    failures++;
  }
  for (int filter = 0; filter < kDkc2ScreenFilterCount; filter++) {
    bool has_raw_name =
        strcmp(Dkc2DesktopScreenFilterName(filter), "Raw") == 0;
    if (!Dkc2DesktopScreenFilterValid(filter) ||
        has_raw_name != (filter == kDkc2ScreenRaw)) {
      fprintf(stderr, "FAIL: screen filter %d metadata\n", filter);
      failures++;
    }
  }
  if (Dkc2DesktopScreenFilterValid(-1) ||
      Dkc2DesktopScreenFilterValid(kDkc2ScreenFilterCount) ||
      strcmp(Dkc2DesktopScreenFilterName(99), "Raw") != 0) {
    fprintf(stderr, "FAIL: invalid screen filter fallback\n");
    failures++;
  }
  int parsed_filter = -1;
  if (!Dkc2DesktopScreenFilterFromName("crt", &parsed_filter) ||
      parsed_filter != kDkc2ScreenCrt ||
      Dkc2DesktopScreenFilterFromName("not-a-model", &parsed_filter)) {
    fprintf(stderr, "FAIL: screen-model environment parser\n");
    failures++;
  }

  const uint8_t source_pixels[8] = {
      0, 0, 0, 0x12, 0, 0, 255, 0x34,
  };
  uint8_t filtered_pixels[8] = {0};
  Dkc2DesktopColorFilter color_filter;
  if (!Dkc2DesktopColorFilterInit(&color_filter, kDkc2ScreenRaw) ||
      Dkc2DesktopColorFilterApply(&color_filter, source_pixels,
                                  filtered_pixels, 2) != source_pixels) {
    fprintf(stderr, "FAIL: Raw screen model is not a byte-exact bypass\n");
    failures++;
  }
  Dkc2DesktopColorFilterDestroy(&color_filter);

  static const uint8_t expected_models[3][8] = {
      {0x0d, 0x0d, 0x0d, 0x12, 0x0d, 0x27, 0xef, 0x34},
      {0x1d, 0x1d, 0x1d, 0x12, 0x1d, 0x2e, 0xed, 0x34},
      {0x07, 0x07, 0x07, 0x12, 0x0c, 0x2c, 0xff, 0x34},
  };
  for (int screen = kDkc2ScreenCrt; screen <= kDkc2ScreenTrinitron;
       screen++) {
    if (!Dkc2DesktopColorFilterInit(&color_filter, screen)) {
      fprintf(stderr, "FAIL: screen-color LUT %d initialization\n", screen);
      failures++;
    } else {
      const uint8_t *result = Dkc2DesktopColorFilterApply(
          &color_filter, source_pixels, filtered_pixels, 2);
      if (result != filtered_pixels ||
          memcmp(filtered_pixels, expected_models[screen - 1],
                 sizeof filtered_pixels) != 0) {
        fprintf(stderr, "FAIL: PSXRecomp screen-color model %d output\n",
                screen);
        failures++;
      }
    }
    Dkc2DesktopColorFilterDestroy(&color_filter);
  }
  if (Dkc2DesktopColorFilterInit(&color_filter, 99)) {
    fprintf(stderr, "FAIL: invalid screen model was accepted\n");
    failures++;
    Dkc2DesktopColorFilterDestroy(&color_filter);
  }

  BITMAPINFO target_info;
  memset(&target_info, 0, sizeof target_info);
  target_info.bmiHeader.biSize = sizeof target_info.bmiHeader;
  target_info.bmiHeader.biWidth = 12;
  target_info.bmiHeader.biHeight = -8;
  target_info.bmiHeader.biPlanes = 1;
  target_info.bmiHeader.biBitCount = 32;
  target_info.bmiHeader.biCompression = BI_RGB;

  HDC screen = GetDC(NULL);
  HDC target = CreateCompatibleDC(screen);
  void *target_pixels = NULL;
  HBITMAP target_bitmap = CreateDIBSection(
      screen, &target_info, DIB_RGB_COLORS, &target_pixels, NULL, 0);
  ReleaseDC(NULL, screen);
  if (!target || !target_bitmap || !target_pixels) {
    fprintf(stderr, "FAIL: unable to create synthetic GDI target\n");
    return 1;
  }
  HGDIOBJ original = SelectObject(target, target_bitmap);

  BITMAPINFO source_info;
  memset(&source_info, 0, sizeof source_info);
  source_info.bmiHeader.biSize = sizeof source_info.bmiHeader;
  source_info.bmiHeader.biWidth = 2;
  source_info.bmiHeader.biHeight = -2;
  source_info.bmiHeader.biPlanes = 1;
  source_info.bmiHeader.biBitCount = 32;
  source_info.bmiHeader.biCompression = BI_RGB;
  uint8_t red_source[2 * 2 * 4] = {
      0, 0, 255, 0, 0, 0, 255, 0,
      0, 0, 255, 0, 0, 0, 255, 0,
  };

  Dkc2DesktopPresenter presenter;
  memset(&presenter, 0, sizeof presenter);
  RECT tall_client = {0, 0, 8, 8};
  memset(target_pixels, 0x7f, 12 * 8 * 4);
  if (!Dkc2DesktopPresent(&presenter, target, &tall_client, red_source,
                          &source_info, 2, 2, false)) {
    fprintf(stderr, "FAIL: first off-screen presentation failed\n");
    failures++;
  } else {
    CheckPixel(target_pixels, 12 * 4, 3, 0, 0, 0, 0,
               "top letterbox is black");
    CheckPixel(target_pixels, 12 * 4, 3, 1, 0, 0, 255,
               "completed frame reaches target");
    CheckPixel(target_pixels, 12 * 4, 3, 7, 0, 0, 0,
               "bottom letterbox is black");
  }

  RECT wide_client = {0, 0, 12, 6};
  memset(target_pixels, 0x7f, 12 * 8 * 4);
  if (!Dkc2DesktopPresent(&presenter, target, &wide_client, red_source,
                          &source_info, 2, 2, false)) {
    fprintf(stderr, "FAIL: resized off-screen presentation failed\n");
    failures++;
  } else {
    CheckPixel(target_pixels, 12 * 4, 1, 2, 0, 0, 0,
               "left pillarbox is black");
    CheckPixel(target_pixels, 12 * 4, 2, 2, 0, 0, 255,
               "resized completed frame reaches target");
    CheckPixel(target_pixels, 12 * 4, 10, 2, 0, 0, 0,
               "right pillarbox is black");
  }
  if (!Dkc2DesktopPresent(&presenter, target, &wide_client, red_source,
                          &source_info, 2, 2, true)) {
    fprintf(stderr, "FAIL: GDI linear-filter presentation failed\n");
    failures++;
  }

  Dkc2DesktopPresenterDestroy(&presenter);
  SelectObject(target, original);
  DeleteObject(target_bitmap);
  DeleteDC(target);
  if (failures) return 1;
  puts("desktop presenter tests passed");
  return 0;
}
