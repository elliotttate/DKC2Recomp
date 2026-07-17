#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "desktop_present.h"

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
                          &source_info, 2, 2)) {
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
                          &source_info, 2, 2)) {
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

  Dkc2DesktopPresenterDestroy(&presenter);
  SelectObject(target, original);
  DeleteObject(target_bitmap);
  DeleteDC(target);
  if (failures) return 1;
  puts("desktop presenter tests passed");
  return 0;
}
