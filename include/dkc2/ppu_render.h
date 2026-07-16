#ifndef DKC2_PPU_RENDER_H
#define DKC2_PPU_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DKC2_PPU_FRAME_WIDTH ((size_t)512)
#define DKC2_PPU_FRAME_HEIGHT ((size_t)224)
#define DKC2_PPU_RGB_SIZE \
    (DKC2_PPU_FRAME_WIDTH * DKC2_PPU_FRAME_HEIGHT * (size_t)3)

enum {
    DKC2_PPU_LIMIT_UNSUPPORTED_MODE = 0x01,
    DKC2_PPU_LIMIT_OBJECTS = 0x02,
    DKC2_PPU_LIMIT_WINDOWS = 0x04,
    DKC2_PPU_LIMIT_DIRECT_COLOR = 0x08,
    DKC2_PPU_LIMIT_MOSAIC = 0x10,
    DKC2_PPU_LIMIT_HIRES = 0x20,
    DKC2_PPU_LIMIT_EXTBG = 0x40,
    DKC2_PPU_LIMIT_INTERLACE = 0x80
};

typedef struct dkc2_ppu_render_source {
    const uint8_t *registers;
    const uint8_t *vram;
    const uint8_t *cgram;
    const uint8_t *oam;
    const uint16_t *bg_hofs;
    const uint16_t *bg_vofs;
    uint16_t mode7_hofs;
    uint16_t mode7_vofs;
    int16_t mode7_a;
    int16_t mode7_b;
    int16_t mode7_c;
    int16_t mode7_d;
    int16_t mode7_x;
    int16_t mode7_y;
    uint8_t fixed_color_red;
    uint8_t fixed_color_green;
    uint8_t fixed_color_blue;
    uint8_t first_sprite;
} dkc2_ppu_render_source;

typedef struct dkc2_ppu_renderer {
    uint8_t *working_rgb;
    uint8_t *frame_rgb;
    uint64_t scanlines;
    uint64_t completed_frames;
    uint64_t mode_scanlines[8];
    uint64_t forced_blank_scanlines;
    uint64_t unsupported_scanlines;
    uint64_t object_range_over_scanlines;
    uint64_t object_time_over_scanlines;
    uint32_t limitations_seen;
    uint32_t working_limitations;
    uint32_t frame_limitations;
    uint16_t working_limited_scanlines;
    uint16_t frame_limited_scanlines;
    uint8_t working_mode_mask;
    uint8_t frame_mode_mask;
} dkc2_ppu_renderer;

bool dkc2_ppu_renderer_init(dkc2_ppu_renderer *renderer);
void dkc2_ppu_renderer_free(dkc2_ppu_renderer *renderer);
bool dkc2_ppu_render_scanline(dkc2_ppu_renderer *renderer,
                              const dkc2_ppu_render_source *source,
                              unsigned line);
bool dkc2_ppu_finish_frame(dkc2_ppu_renderer *renderer);
const uint8_t *dkc2_ppu_frame_rgb(const dkc2_ppu_renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
