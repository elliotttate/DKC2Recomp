#include "dkc2/ppu_render.h"

#include <stdlib.h>
#include <string.h>

enum {
    DKC2_PPU_REGISTER_BASE = 0x2000,
    DKC2_PPU_VRAM_MASK = 0xFFFF,
    DKC2_PPU_CGRAM_MASK = 0x01FF,
    DKC2_PPU_BACKDROP_LAYER = 5,
    DKC2_PPU_OBJECT_RANGE_OVER = 0x01,
    DKC2_PPU_OBJECT_TIME_OVER = 0x02
};

typedef struct dkc2_ppu_pixel {
    uint16_t color;
    uint8_t priority;
    uint8_t layer;
    bool opaque;
} dkc2_ppu_pixel;

static uint8_t ppu_register(const dkc2_ppu_render_source *source,
                            uint16_t address) {
    return source->registers[
        (size_t)(address - DKC2_PPU_REGISTER_BASE)];
}

static uint16_t read_word_wrapped(const uint8_t *memory,
                                  size_t mask,
                                  size_t address) {
    uint16_t low = memory[address & mask];
    uint16_t high = memory[(address + 1U) & mask];
    return (uint16_t)(low | (high << 8));
}

static uint16_t palette_color(const dkc2_ppu_render_source *source,
                              unsigned index) {
    return (uint16_t)(read_word_wrapped(source->cgram,
                                       DKC2_PPU_CGRAM_MASK,
                                       (size_t)(index & 0xFFU) * 2U) &
                      UINT16_C(0x7FFF));
}

static unsigned background_depth(unsigned mode, unsigned background) {
    static const uint8_t depths[8][4] = {
        {2, 2, 2, 2},
        {4, 4, 2, 0},
        {0, 0, 0, 0},
        {8, 4, 0, 0},
        {0, 0, 0, 0},
        {4, 2, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
    };
    return depths[mode & 7U][background & 3U];
}

static uint8_t background_priority(unsigned mode,
                                   unsigned background,
                                   bool high,
                                   uint8_t bgmode) {
    static const uint8_t low[8][4] = {
        {11, 10, 3, 2},
        {11, 10, 3, 0},
        {0, 0, 0, 0},
        {7, 3, 0, 0},
        {0, 0, 0, 0},
        {7, 3, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
    };
    static const uint8_t high_priority[8][4] = {
        {15, 14, 7, 6},
        {15, 14, 7, 0},
        {0, 0, 0, 0},
        {15, 11, 0, 0},
        {0, 0, 0, 0},
        {15, 11, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
    };
    uint8_t value = high ? high_priority[mode][background]
                         : low[mode][background];

    if (mode == 1U && background == 2U && high &&
        (bgmode & UINT8_C(0x0F)) == UINT8_C(0x09)) {
        value = 17;
    }
    return value;
}

static size_t tilemap_address(const dkc2_ppu_render_source *source,
                              unsigned background,
                              unsigned tile_x,
                              unsigned tile_y) {
    uint8_t screen = ppu_register(
        source,
        (uint16_t)(UINT16_C(0x2107) + background));
    unsigned size = screen & 3U;
    unsigned screen_x = (size & 1U) != 0 ? (tile_x >> 5) & 1U : 0;
    unsigned screen_y = (size & 2U) != 0 ? (tile_y >> 5) & 1U : 0;
    unsigned screens_per_row = (size & 1U) != 0 ? 2U : 1U;
    unsigned screen_index = screen_y * screens_per_row + screen_x;
    size_t base = (size_t)(screen & UINT8_C(0x7C)) << 9;
    size_t entry = ((size_t)(tile_y & 31U) * 32U) +
                   (tile_x & 31U);
    return (base + (size_t)screen_index * 2048U + entry * 2U) &
           DKC2_PPU_VRAM_MASK;
}

static size_t tile_data_base(const dkc2_ppu_render_source *source,
                             unsigned background) {
    uint8_t bases = ppu_register(
        source,
        (uint16_t)(UINT16_C(0x210B) + background / 2U));
    unsigned nibble = (background & 1U) != 0
                          ? (bases >> 4) & 7U
                          : bases & 7U;
    return (size_t)nibble << 13;
}

static unsigned decode_tile_pixel(const dkc2_ppu_render_source *source,
                                  size_t base,
                                  unsigned tile,
                                  unsigned bits_per_pixel,
                                  unsigned x,
                                  unsigned y) {
    size_t tile_address = base +
                          (size_t)(tile & 0x3FFU) *
                              bits_per_pixel * 8U;
    unsigned bit = 7U - (x & 7U);
    unsigned pixel = 0;
    unsigned plane;

    for (plane = 0; plane < bits_per_pixel; ++plane) {
        size_t plane_address = tile_address +
                               (size_t)(plane / 2U) * 16U +
                               (size_t)(y & 7U) * 2U +
                               (plane & 1U);
        pixel |= ((source->vram[plane_address & DKC2_PPU_VRAM_MASK] >>
                   bit) &
                  1U)
                 << plane;
    }
    return pixel;
}

static dkc2_ppu_pixel sample_background(
    const dkc2_ppu_render_source *source,
    unsigned mode,
    unsigned background,
    unsigned x,
    unsigned y) {
    dkc2_ppu_pixel result = {0, 0, 0, false};
    uint8_t bgmode = ppu_register(source, UINT16_C(0x2105));
    unsigned depth = background_depth(mode, background);
    unsigned tile_size_x;
    unsigned tile_size_y;
    unsigned world_x;
    unsigned world_y;
    unsigned tile_x;
    unsigned tile_y;
    unsigned local_x;
    unsigned local_y;
    uint16_t entry;
    unsigned tile;
    unsigned palette;
    unsigned pixel;
    unsigned palette_index;
    bool hflip;
    bool vflip;

    if (depth == 0) {
        return result;
    }
    tile_size_y =
        (bgmode & (uint8_t)(UINT8_C(0x10) << background)) != 0
            ? 16U
            : 8U;
    tile_size_x = mode == 5U ? 8U : tile_size_y;
    world_x = (x + (source->bg_hofs[background] & UINT16_C(0x03FF))) &
              0x7FFU;
    world_y = (y + (source->bg_vofs[background] & UINT16_C(0x03FF))) &
              0x7FFU;
    tile_x = world_x / tile_size_x;
    tile_y = world_y / tile_size_y;
    local_x = world_x % tile_size_x;
    local_y = world_y % tile_size_y;
    entry = read_word_wrapped(source->vram,
                              DKC2_PPU_VRAM_MASK,
                              tilemap_address(source,
                                              background,
                                              tile_x,
                                              tile_y));
    tile = entry & UINT16_C(0x03FF);
    palette = (entry >> 10) & 7U;
    hflip = (entry & UINT16_C(0x4000)) != 0;
    vflip = (entry & UINT16_C(0x8000)) != 0;
    if (hflip) {
        local_x = tile_size_x - 1U - local_x;
    }
    if (vflip) {
        local_y = tile_size_y - 1U - local_y;
    }
    if (tile_size_x == 16U || tile_size_y == 16U) {
        tile = (tile + local_x / 8U + (local_y / 8U) * 16U) &
               0x3FFU;
    }
    pixel = decode_tile_pixel(source,
                              tile_data_base(source, background),
                              tile,
                              depth,
                              local_x,
                              local_y);
    if (pixel == 0) {
        return result;
    }
    if (mode == 0U) {
        palette_index = background * 32U + palette * 4U + pixel;
    } else if (depth == 8U) {
        palette_index = pixel;
    } else {
        palette_index = palette * (1U << depth) + pixel;
    }
    result.color = palette_color(source, palette_index);
    result.priority = background_priority(mode,
                                          background,
                                          (entry & UINT16_C(0x2000)) != 0,
                                          bgmode);
    result.layer = (uint8_t)background;
    result.opaque = true;
    return result;
}

static void object_dimensions(uint8_t selection,
                              bool large,
                              unsigned *width,
                              unsigned *height) {
    static const uint8_t small_width[8] = {8, 8, 8, 16, 16, 32, 16, 16};
    static const uint8_t small_height[8] = {8, 8, 8, 16, 16, 32, 32, 32};
    static const uint8_t large_width[8] = {16, 32, 64, 32, 64, 64, 32, 32};
    static const uint8_t large_height[8] = {16, 32, 64, 32, 64, 64, 64, 32};
    unsigned index = selection & 7U;

    *width = large ? large_width[index] : small_width[index];
    *height = large ? large_height[index] : small_height[index];
}

typedef struct dkc2_ppu_selected_object {
    unsigned sprite;
    unsigned first_visible_tile;
    unsigned tile_count;
} dkc2_ppu_selected_object;

static uint8_t render_objects(const dkc2_ppu_render_source *source,
                              unsigned y,
                              dkc2_ppu_pixel *pixels) {
    uint8_t selection = ppu_register(source, UINT16_C(0x2101));
    size_t name_base = (size_t)(selection & UINT8_C(3)) << 15;
    size_t name_select = (size_t)((selection >> 3) & UINT8_C(3)) << 14;
    dkc2_ppu_selected_object selected[32];
    unsigned selected_count = 0;
    unsigned tiles_remaining = 34;
    uint8_t overflow = 0;
    int order;

    memset(pixels, 0, DKC2_PPU_FRAME_WIDTH * sizeof(*pixels));
    for (order = 0; order < 128; ++order) {
        unsigned sprite = (source->first_sprite + (unsigned)order) & 127U;
        size_t base = (size_t)sprite * 4U;
        uint8_t high = source->oam[512U + sprite / 4U];
        unsigned high_shift = (sprite & 3U) * 2U;
        bool x_high = ((high >> high_shift) & 1U) != 0;
        bool large = ((high >> (high_shift + 1U)) & 1U) != 0;
        unsigned width;
        unsigned height;
        int sprite_x = source->oam[base] | (x_high ? 0x100 : 0);
        unsigned sprite_y = source->oam[base + 1U];
        unsigned line = (y - sprite_y) & 0xFFU;
        int visible_left;
        int visible_right;
        unsigned visible_tiles;
        unsigned first_visible_tile;

        object_dimensions(selection >> 5, large, &width, &height);
        if (sprite_x >= 256) {
            sprite_x -= 512;
        }
        if (line >= height || sprite_x <= -(int)width || sprite_x >= 256) {
            continue;
        }
        if (selected_count == 32U) {
            overflow |= DKC2_PPU_OBJECT_RANGE_OVER;
            continue;
        }
        visible_left = sprite_x < 0 ? 0 : sprite_x;
        visible_right = sprite_x + (int)width > 256
                            ? 256
                            : sprite_x + (int)width;
        visible_tiles = (unsigned)(visible_right - visible_left + 7) / 8U;
        first_visible_tile = (unsigned)(visible_left - sprite_x) / 8U;
        selected[selected_count].sprite = sprite;
        selected[selected_count].first_visible_tile = first_visible_tile;
        if (visible_tiles > tiles_remaining) {
            overflow |= DKC2_PPU_OBJECT_TIME_OVER;
            selected[selected_count].tile_count = tiles_remaining;
            tiles_remaining = 0;
        } else {
            selected[selected_count].tile_count = visible_tiles;
            tiles_remaining -= visible_tiles;
        }
        ++selected_count;
    }

    for (order = (int)selected_count - 1; order >= 0; --order) {
        dkc2_ppu_selected_object *object = &selected[order];
        unsigned sprite = object->sprite;
        size_t base = (size_t)sprite * 4U;
        uint8_t high = source->oam[512U + sprite / 4U];
        unsigned high_shift = (sprite & 3U) * 2U;
        bool x_high = ((high >> high_shift) & 1U) != 0;
        bool large = ((high >> (high_shift + 1U)) & 1U) != 0;
        unsigned width;
        unsigned height;
        int sprite_x = source->oam[base] | (x_high ? 0x100 : 0);
        unsigned sprite_y = source->oam[base + 1U];
        unsigned line = (y - sprite_y) & 0xFFU;
        uint8_t attributes = source->oam[base + 3U];
        unsigned screen_offset;

        object_dimensions(selection >> 5, large, &width, &height);
        if (sprite_x >= 256) {
            sprite_x -= 512;
        }
        for (screen_offset = 0; screen_offset < width; ++screen_offset) {
            int screen_x = sprite_x + (int)screen_offset;
            unsigned local_x = screen_offset;
            unsigned local_y = line;
            unsigned tile;
            unsigned pixel;
            unsigned palette;
            dkc2_ppu_pixel candidate;
            unsigned screen_tile = screen_offset / 8U;

            if (screen_x < 0 || screen_x >= 256) {
                continue;
            }
            if (screen_tile < object->first_visible_tile ||
                screen_tile >= object->first_visible_tile +
                                   object->tile_count) {
                continue;
            }
            if ((attributes & UINT8_C(0x40)) != 0) {
                local_x = width - 1U - local_x;
            }
            if ((attributes & UINT8_C(0x80)) != 0) {
                local_y = height - 1U - local_y;
            }
            tile = source->oam[base + 2U] |
                   ((unsigned)(attributes & UINT8_C(1)) << 8);
            tile = (tile + local_x / 8U + (local_y / 8U) * 16U) &
                   0x1FFU;
            pixel = decode_tile_pixel(source,
                                      name_base +
                                          ((tile & 0x100U) != 0
                                               ? name_select
                                               : 0),
                                      tile & 0xFFU,
                                      4,
                                      local_x,
                                      local_y);
            if (pixel == 0) {
                continue;
            }
            palette = (attributes >> 1) & 7U;
            candidate.color = palette_color(
                source,
                128U + palette * 16U + pixel);
            candidate.priority = (uint8_t)(
                4U + ((attributes >> 4) & 3U) * 4U);
            candidate.layer = 4;
            candidate.opaque = true;
            if (!pixels[screen_x].opaque ||
                candidate.priority >= pixels[screen_x].priority) {
                pixels[screen_x] = candidate;
            }
        }
    }
    return overflow;
}

static dkc2_ppu_pixel render_screen_pixel(
    const dkc2_ppu_render_source *source,
    unsigned mode,
    uint8_t screen_mask,
    const dkc2_ppu_pixel *objects,
    unsigned background_x,
    unsigned object_x,
    unsigned y) {
    dkc2_ppu_pixel result;
    unsigned background;

    result.color = palette_color(source, 0);
    result.priority = 0;
    result.layer = DKC2_PPU_BACKDROP_LAYER;
    result.opaque = true;
    for (background = 0; background < 4U; ++background) {
        dkc2_ppu_pixel candidate;
        if ((screen_mask & (uint8_t)(UINT8_C(1) << background)) == 0) {
            continue;
        }
        candidate = sample_background(source,
                                      mode,
                                      background,
                                      background_x,
                                      y);
        if (candidate.opaque && candidate.priority > result.priority) {
            result = candidate;
        }
    }
    if ((screen_mask & UINT8_C(0x10)) != 0) {
        dkc2_ppu_pixel candidate = objects[object_x];
        if (candidate.opaque && candidate.priority > result.priority) {
            result = candidate;
        }
    }
    return result;
}

static uint16_t fixed_color(const dkc2_ppu_render_source *source) {
    return (uint16_t)((source->fixed_color_red & UINT8_C(0x1F)) |
                      ((uint16_t)(source->fixed_color_green &
                                  UINT8_C(0x1F))
                       << 5) |
                      ((uint16_t)(source->fixed_color_blue &
                                  UINT8_C(0x1F))
                       << 10));
}

static unsigned color_component(uint16_t color, unsigned shift) {
    return (color >> shift) & UINT16_C(0x1F);
}

static uint16_t apply_color_math(const dkc2_ppu_render_source *source,
                                 dkc2_ppu_pixel main,
                                 dkc2_ppu_pixel sub) {
    uint8_t control = ppu_register(source, UINT16_C(0x2131));
    uint16_t other;
    unsigned red;
    unsigned green;
    unsigned blue;
    bool subtract;
    bool half;

    if ((control & (uint8_t)(UINT8_C(1) << main.layer)) == 0) {
        return main.color;
    }
    other = (ppu_register(source, UINT16_C(0x2130)) & UINT8_C(2)) != 0
                ? sub.color
                : fixed_color(source);
    subtract = (control & UINT8_C(0x80)) != 0;
    half = (control & UINT8_C(0x40)) != 0;
    if (subtract) {
        unsigned main_red = color_component(main.color, 0);
        unsigned main_green = color_component(main.color, 5);
        unsigned main_blue = color_component(main.color, 10);
        unsigned other_red = color_component(other, 0);
        unsigned other_green = color_component(other, 5);
        unsigned other_blue = color_component(other, 10);
        red = main_red > other_red ? main_red - other_red : 0;
        green = main_green > other_green
                    ? main_green - other_green
                    : 0;
        blue = main_blue > other_blue ? main_blue - other_blue : 0;
    } else {
        red = color_component(main.color, 0) + color_component(other, 0);
        green = color_component(main.color, 5) +
                color_component(other, 5);
        blue = color_component(main.color, 10) +
               color_component(other, 10);
        if (red > 31U) {
            red = 31U;
        }
        if (green > 31U) {
            green = 31U;
        }
        if (blue > 31U) {
            blue = 31U;
        }
    }
    if (half) {
        red >>= 1;
        green >>= 1;
        blue >>= 1;
    }
    return (uint16_t)(red | (green << 5) | (blue << 10));
}

static uint8_t expand_brightness(unsigned component, unsigned brightness) {
    unsigned scaled;
    if (brightness == 0) {
        return 0;
    }
    scaled = (component * brightness + 7U) / 15U;
    return (uint8_t)((scaled << 3) | (scaled >> 2));
}

static void store_rgb(uint8_t *destination,
                      uint16_t color,
                      unsigned brightness) {
    destination[0] = expand_brightness(color_component(color, 0),
                                       brightness);
    destination[1] = expand_brightness(color_component(color, 5),
                                       brightness);
    destination[2] = expand_brightness(color_component(color, 10),
                                       brightness);
}

static uint32_t scanline_limitations(
    const dkc2_ppu_render_source *source,
    unsigned mode) {
    uint32_t limitations = 0;
    uint8_t mosaic = ppu_register(source, UINT16_C(0x2106));
    uint16_t address;

    if (mode != 0U && mode != 1U && mode != 3U && mode != 5U) {
        limitations |= DKC2_PPU_LIMIT_UNSUPPORTED_MODE;
    }
    for (address = UINT16_C(0x2123);
         address <= UINT16_C(0x212B);
         ++address) {
        if (ppu_register(source, address) != 0) {
            limitations |= DKC2_PPU_LIMIT_WINDOWS;
            break;
        }
    }
    if (ppu_register(source, UINT16_C(0x212E)) != 0 ||
        ppu_register(source, UINT16_C(0x212F)) != 0) {
        limitations |= DKC2_PPU_LIMIT_WINDOWS;
    }
    if ((ppu_register(source, UINT16_C(0x2130)) & UINT8_C(1)) != 0) {
        limitations |= DKC2_PPU_LIMIT_DIRECT_COLOR;
    }
    if ((mosaic & UINT8_C(0x0F)) != 0 &&
        (mosaic & UINT8_C(0xF0)) != 0) {
        limitations |= DKC2_PPU_LIMIT_MOSAIC;
    }
    if (mode == 6U ||
        (ppu_register(source, UINT16_C(0x2133)) & UINT8_C(8)) != 0) {
        limitations |= DKC2_PPU_LIMIT_HIRES;
    }
    if ((ppu_register(source, UINT16_C(0x2133)) & UINT8_C(0x40)) != 0) {
        limitations |= DKC2_PPU_LIMIT_EXTBG;
    }
    if ((ppu_register(source, UINT16_C(0x2133)) & UINT8_C(3)) != 0) {
        limitations |= DKC2_PPU_LIMIT_INTERLACE;
    }
    return limitations;
}

bool dkc2_ppu_renderer_init(dkc2_ppu_renderer *renderer) {
    if (renderer == NULL) {
        return false;
    }
    memset(renderer, 0, sizeof(*renderer));
    renderer->working_rgb = (uint8_t *)calloc(DKC2_PPU_RGB_SIZE, 1);
    renderer->frame_rgb = (uint8_t *)calloc(DKC2_PPU_RGB_SIZE, 1);
    if (renderer->working_rgb == NULL || renderer->frame_rgb == NULL) {
        dkc2_ppu_renderer_free(renderer);
        return false;
    }
    return true;
}

void dkc2_ppu_renderer_free(dkc2_ppu_renderer *renderer) {
    if (renderer == NULL) {
        return;
    }
    free(renderer->working_rgb);
    free(renderer->frame_rgb);
    memset(renderer, 0, sizeof(*renderer));
}

bool dkc2_ppu_render_scanline(dkc2_ppu_renderer *renderer,
                              const dkc2_ppu_render_source *source,
                              unsigned line) {
    uint8_t display;
    uint8_t main_mask;
    uint8_t sub_mask;
    unsigned mode;
    uint8_t *destination;
    uint32_t limitations;
    dkc2_ppu_pixel objects[DKC2_PPU_FRAME_WIDTH];
    uint8_t object_overflow = 0;
    unsigned x;

    if (renderer == NULL || source == NULL || source->registers == NULL ||
        source->vram == NULL || source->cgram == NULL ||
        source->oam == NULL || source->bg_hofs == NULL ||
        source->bg_vofs == NULL || renderer->working_rgb == NULL ||
        line >= DKC2_PPU_FRAME_HEIGHT) {
        return false;
    }
    display = ppu_register(source, UINT16_C(0x2100));
    mode = ppu_register(source, UINT16_C(0x2105)) & UINT8_C(7);
    main_mask = ppu_register(source, UINT16_C(0x212C)) & UINT8_C(0x1F);
    sub_mask = ppu_register(source, UINT16_C(0x212D)) & UINT8_C(0x1F);
    destination = renderer->working_rgb +
                  (size_t)line * DKC2_PPU_FRAME_WIDTH * 3U;
    ++renderer->scanlines;
    ++renderer->mode_scanlines[mode];
    renderer->working_mode_mask |= (uint8_t)(UINT8_C(1) << mode);
    if ((display & UINT8_C(0x80)) != 0) {
        memset(destination, 0, DKC2_PPU_FRAME_WIDTH * 3U);
        ++renderer->forced_blank_scanlines;
        return true;
    }
    if (((main_mask | sub_mask) & UINT8_C(0x10)) != 0) {
        object_overflow = render_objects(source, line, objects);
    } else {
        memset(objects, 0, sizeof(objects));
    }
    limitations = scanline_limitations(source, mode);
    if ((object_overflow & DKC2_PPU_OBJECT_RANGE_OVER) != 0) {
        ++renderer->object_range_over_scanlines;
    }
    if ((object_overflow & DKC2_PPU_OBJECT_TIME_OVER) != 0) {
        ++renderer->object_time_over_scanlines;
    }
    renderer->limitations_seen |= limitations;
    renderer->working_limitations |= limitations;
    if (limitations != 0) {
        ++renderer->unsupported_scanlines;
        ++renderer->working_limited_scanlines;
    }
    for (x = 0; x < DKC2_PPU_FRAME_WIDTH; ++x) {
        bool hires = mode == 5U;
        unsigned background_x = hires ? x : x / 2U;
        unsigned object_x = x / 2U;
        if (!hires && (x & 1U) != 0) {
            memcpy(destination + (size_t)x * 3U,
                   destination + (size_t)(x - 1U) * 3U,
                   3);
            continue;
        }
        dkc2_ppu_pixel main = render_screen_pixel(source,
                                                  mode,
                                                  main_mask,
                                                  objects,
                                                  background_x,
                                                  object_x,
                                                  line);
        dkc2_ppu_pixel sub = render_screen_pixel(source,
                                                 mode,
                                                 sub_mask,
                                                 objects,
                                                 background_x,
                                                 object_x,
                                                 line);
        uint16_t color = apply_color_math(source, main, sub);
        store_rgb(destination + (size_t)x * 3U,
                  color,
                  display & UINT8_C(0x0F));
    }
    return true;
}

bool dkc2_ppu_finish_frame(dkc2_ppu_renderer *renderer) {
    if (renderer == NULL || renderer->working_rgb == NULL ||
        renderer->frame_rgb == NULL) {
        return false;
    }
    memcpy(renderer->frame_rgb,
           renderer->working_rgb,
           DKC2_PPU_RGB_SIZE);
    renderer->frame_mode_mask = renderer->working_mode_mask;
    renderer->frame_limitations = renderer->working_limitations;
    renderer->frame_limited_scanlines =
        renderer->working_limited_scanlines;
    renderer->working_mode_mask = 0;
    renderer->working_limitations = 0;
    renderer->working_limited_scanlines = 0;
    ++renderer->completed_frames;
    return true;
}

const uint8_t *dkc2_ppu_frame_rgb(const dkc2_ppu_renderer *renderer) {
    return renderer != NULL ? renderer->frame_rgb : NULL;
}
