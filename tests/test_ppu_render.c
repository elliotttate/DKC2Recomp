#include "dkc2/ppu_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_REGISTER_BYTES = 0x4000,
    TEST_VRAM_BYTES = 64 * 1024,
    TEST_CGRAM_BYTES = 512,
    TEST_OAM_BYTES = 544
};

static void fail(const char *message) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static size_t register_index(uint16_t address) {
    return (size_t)(address - UINT16_C(0x2000));
}

static void write_word(uint8_t *memory, size_t address, uint16_t value) {
    memory[address] = (uint8_t)value;
    memory[address + 1U] = (uint8_t)(value >> 8);
}

static void write_palette(uint8_t *cgram,
                          unsigned index,
                          uint16_t color) {
    write_word(cgram, (size_t)index * 2U, color);
}

static void fill_tile_color_one(uint8_t *vram, size_t address) {
    unsigned row;
    for (row = 0; row < 8U; ++row) {
        vram[address + (size_t)row * 2U] = UINT8_C(0x80);
    }
}

static void expect_rgb(const uint8_t *pixel,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue,
                       const char *message) {
    if (pixel[0] != red || pixel[1] != green || pixel[2] != blue) {
        fail(message);
    }
}

int main(void) {
    uint8_t registers[TEST_REGISTER_BYTES];
    uint8_t vram[TEST_VRAM_BYTES];
    uint8_t cgram[TEST_CGRAM_BYTES];
    uint8_t oam[TEST_OAM_BYTES];
    uint16_t bg_hofs[4] = {0, 0, 0, 0};
    uint16_t bg_vofs[4] = {0, 0, 0, 0};
    dkc2_ppu_render_source source;
    dkc2_ppu_renderer renderer;
    const uint8_t *frame;

    memset(registers, 0, sizeof(registers));
    memset(vram, 0, sizeof(vram));
    memset(cgram, 0, sizeof(cgram));
    memset(oam, 0, sizeof(oam));
    memset(&source, 0, sizeof(source));
    source.registers = registers;
    source.vram = vram;
    source.cgram = cgram;
    source.oam = oam;
    source.bg_hofs = bg_hofs;
    source.bg_vofs = bg_vofs;
    if (!dkc2_ppu_renderer_init(&renderer)) {
        fail("cannot allocate PPU renderer fixture");
    }

    registers[register_index(UINT16_C(0x2100))] = UINT8_C(0x8F);
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("forced-blank scanline was rejected");
    }
    expect_rgb(renderer.working_rgb,
               0,
               0,
               0,
               "forced blank did not render black");

    registers[register_index(UINT16_C(0x2100))] = UINT8_C(0x0F);
    registers[register_index(UINT16_C(0x2105))] = UINT8_C(0x01);
    registers[register_index(UINT16_C(0x2107))] = UINT8_C(0x04);
    registers[register_index(UINT16_C(0x2108))] = UINT8_C(0x08);
    registers[register_index(UINT16_C(0x210B))] = UINT8_C(0x32);
    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x03);
    write_word(vram, 0x0800, UINT16_C(0x0400));
    write_word(vram, 0x1000, UINT16_C(0x2800));
    fill_tile_color_one(vram, 0x4000);
    fill_tile_color_one(vram, 0x6000);
    write_palette(cgram, 17, UINT16_C(0x001F));
    write_palette(cgram, 33, UINT16_C(0x03E0));
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("Mode-1 priority scanline was rejected");
    }
    expect_rgb(renderer.working_rgb,
               0,
               UINT8_MAX,
               0,
               "BG2 high priority did not cover BG1 low priority");

    write_word(vram, 0x0800, UINT16_C(0x2400));
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("Mode-1 high-priority scanline was rejected");
    }
    expect_rgb(renderer.working_rgb,
               UINT8_MAX,
               0,
               0,
               "BG1 high priority did not cover BG2 high priority");

    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x01);
    registers[register_index(UINT16_C(0x212D))] = UINT8_C(0x02);
    registers[register_index(UINT16_C(0x2130))] = UINT8_C(0x02);
    registers[register_index(UINT16_C(0x2131))] = UINT8_C(0x01);
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("subscreen color-math scanline was rejected");
    }
    expect_rgb(renderer.working_rgb,
               UINT8_MAX,
               UINT8_MAX,
               0,
               "subscreen addition did not combine red and green");

    registers[register_index(UINT16_C(0x2130))] = 0;
    registers[register_index(UINT16_C(0x2131))] = 0;
    registers[register_index(UINT16_C(0x212D))] = 0;
    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x01);
    write_word(vram, 0x0800, UINT16_C(0x6400));
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("flipped-tile scanline was rejected");
    }
    expect_rgb(renderer.working_rgb,
               0,
               0,
               0,
               "horizontal flip left the source pixel at x=0");
    expect_rgb(renderer.working_rgb + 14U * 3U,
               UINT8_MAX,
               0,
               0,
               "horizontal flip did not move the source pixel to x=7");

    {
        unsigned sprite;
        for (sprite = 0; sprite < 128U; ++sprite) {
            oam[sprite * 4U + 1U] = UINT8_C(0xF0);
        }
    }
    registers[register_index(UINT16_C(0x2101))] = UINT8_C(0x01);
    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x10);
    oam[0] = UINT8_C(2);
    oam[1] = 0;
    oam[2] = 0;
    oam[3] = UINT8_C(0x30);
    fill_tile_color_one(vram, 0x8000);
    write_palette(cgram, 129, UINT16_C(0x7C00));
    if (!dkc2_ppu_render_scanline(&renderer, &source, 0)) {
        fail("object scanline was rejected");
    }
    expect_rgb(renderer.working_rgb + 4U * 3U,
               0,
               0,
               UINT8_MAX,
               "4bpp object pixel did not render from OAM");

    registers[register_index(UINT16_C(0x2103))] = 0;
    registers[register_index(UINT16_C(0x2105))] = UINT8_C(0x05);
    registers[register_index(UINT16_C(0x2107))] = UINT8_C(0x05);
    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x01);
    write_word(vram, 0x0800, UINT16_C(0x2400));
    if (!dkc2_ppu_render_scanline(&renderer, &source, 2)) {
        fail("Mode-5 high-resolution scanline was rejected");
    }
    expect_rgb(renderer.working_rgb +
                   2U * DKC2_PPU_FRAME_WIDTH * 3U,
               UINT8_MAX,
               0,
               0,
               "Mode-5 high-resolution BG1 pixel was not rendered");

    registers[register_index(UINT16_C(0x2105))] = UINT8_C(0x01);
    registers[register_index(UINT16_C(0x2103))] = UINT8_C(0x80);
    registers[register_index(UINT16_C(0x212C))] = UINT8_C(0x10);
    {
        unsigned sprite;
        for (sprite = 0; sprite < 33U; ++sprite) {
            oam[sprite * 4U] = (uint8_t)(sprite * 2U);
            oam[sprite * 4U + 1U] = UINT8_C(1);
        }
    }
    if (!dkc2_ppu_render_scanline(&renderer, &source, 1) ||
        renderer.object_range_over_scanlines == 0) {
        fail("object range overflow was not reported explicitly");
    }

    if (!dkc2_ppu_finish_frame(&renderer)) {
        fail("completed framebuffer could not be published");
    }
    frame = dkc2_ppu_frame_rgb(&renderer);
    if (frame == NULL || renderer.completed_frames != 1 ||
        memcmp(frame, renderer.working_rgb, DKC2_PPU_RGB_SIZE) != 0 ||
        renderer.frame_mode_mask != UINT8_C(0x23) ||
        renderer.frame_limited_scanlines != 0 ||
        renderer.frame_limitations != 0 ||
        renderer.working_mode_mask != 0 ||
        renderer.working_limitations != 0) {
        fail("published framebuffer does not match rendered pixels");
    }

    dkc2_ppu_renderer_free(&renderer);
    (void)puts("headless PPU background and color-math tests passed");
    return EXIT_SUCCESS;
}
