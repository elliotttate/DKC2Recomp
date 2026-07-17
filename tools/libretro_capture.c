#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "dkc2/hash.h"
#include "libretro.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CAPTURE_WIDTH = 256,
    CAPTURE_HEIGHT = 224,
    CAPTURE_BYTES_PER_PIXEL = 4
};

static HMODULE core_module;
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t frame_bgrx[CAPTURE_WIDTH * CAPTURE_HEIGHT *
                          CAPTURE_BYTES_PER_PIXEL];
static unsigned source_width;
static unsigned source_height;
static unsigned long video_callbacks;
static unsigned long video_active_frames;
static unsigned long video_blank_frames;
static unsigned long video_consecutive_blank_frames;
static unsigned long video_max_consecutive_blank_frames;
static unsigned long audio_active_host_frames;
static unsigned long audio_silent_host_frames;
static unsigned long long audio_frames;
static unsigned long long audio_nonzero_samples;
static unsigned long long audio_clipped_samples;
static unsigned long long audio_zero_frames;
static unsigned long long audio_consecutive_zero_frames;
static unsigned long long audio_max_consecutive_zero_frames;
static unsigned audio_peak;
static unsigned audio_max_delta;
static int previous_audio_samples[2];
static bool audio_sample_history_initialized;
static unsigned audio_sample_channel;
static uint64_t audio_fnv1a = UINT64_C(14695981039346656037);
static FILE *audio_pcm;
static bool audio_pcm_failed;

static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static unsigned (*p_retro_api_version)(void);
static void (*p_retro_get_system_info)(struct retro_system_info *info);
static void (*p_retro_get_system_av_info)(struct retro_system_av_info *info);
static void (*p_retro_set_environment)(retro_environment_t callback);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t callback);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t callback);
static void (*p_retro_set_audio_sample_batch)(
    retro_audio_sample_batch_t callback);
static void (*p_retro_set_input_poll)(retro_input_poll_t callback);
static void (*p_retro_set_input_state)(retro_input_state_t callback);
static bool (*p_retro_load_game)(const struct retro_game_info *game);
static void (*p_retro_unload_game)(void);
static void (*p_retro_run)(void);
static void *(*p_retro_get_memory_data)(unsigned id);
static size_t (*p_retro_get_memory_size)(unsigned id);
static size_t (*p_retro_serialize_size)(void);
static bool (*p_retro_serialize)(void *data, size_t size);

static bool bind_symbols(void) {
#define BIND(symbol)                                                     \
    do {                                                                 \
        FARPROC address = GetProcAddress(core_module, #symbol);          \
        _Static_assert(sizeof p_##symbol == sizeof address,              \
                       "Windows function pointer size mismatch");       \
        memcpy(&p_##symbol, &address, sizeof address);                    \
        if (p_##symbol == NULL) {                                        \
            fprintf(stderr, "libretro core is missing %s\n", #symbol);       \
            return false;                                                \
        }                                                                \
    } while (0)
    BIND(retro_init);
    BIND(retro_deinit);
    BIND(retro_api_version);
    BIND(retro_get_system_info);
    BIND(retro_get_system_av_info);
    BIND(retro_set_environment);
    BIND(retro_set_video_refresh);
    BIND(retro_set_audio_sample);
    BIND(retro_set_audio_sample_batch);
    BIND(retro_set_input_poll);
    BIND(retro_set_input_state);
    BIND(retro_load_game);
    BIND(retro_unload_game);
    BIND(retro_run);
    BIND(retro_get_memory_data);
    BIND(retro_get_memory_size);
    BIND(retro_serialize_size);
    BIND(retro_serialize);
#undef BIND
    return true;
}

static bool environment_callback(unsigned command, void *data) {
    static const char current_directory[] = ".";
    switch (command) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            pixel_format = *(const enum retro_pixel_format *)data;
            return pixel_format == RETRO_PIXEL_FORMAT_0RGB1555 ||
                   pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ||
                   pixel_format == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data = current_directory;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        default:
            return false;
    }
}

static uint8_t expand5(unsigned value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(unsigned value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static void video_callback(const void *data, unsigned width, unsigned height,
                           size_t pitch) {
    if (data == NULL || data == (const void *)(intptr_t)-1) {
        return;
    }
    source_width = width;
    source_height = height;
    video_callbacks++;
    memset(frame_bgrx, 0, sizeof frame_bgrx);

    unsigned rows = height < CAPTURE_HEIGHT ? height : CAPTURE_HEIGHT;
    for (unsigned y = 0; y < rows; y++) {
        const uint8_t *source = (const uint8_t *)data + (size_t)y * pitch;
        uint8_t *target = frame_bgrx +
                          (size_t)y * CAPTURE_WIDTH * CAPTURE_BYTES_PER_PIXEL;
        for (unsigned x = 0; x < CAPTURE_WIDTH && width != 0; x++) {
            unsigned source_x = width > CAPTURE_WIDTH ? x * width /
                                                        CAPTURE_WIDTH : x;
            if (source_x >= width) source_x = width - 1;
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
                blue = source[source_x * 4];
                green = source[source_x * 4 + 1];
                red = source[source_x * 4 + 2];
            } else {
                unsigned packed = source[source_x * 2] |
                                  ((unsigned)source[source_x * 2 + 1] << 8);
                if (pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
                    red = expand5((packed >> 11) & 0x1f);
                    green = expand6((packed >> 5) & 0x3f);
                    blue = expand5(packed & 0x1f);
                } else {
                    red = expand5((packed >> 10) & 0x1f);
                    green = expand5((packed >> 5) & 0x1f);
                    blue = expand5(packed & 0x1f);
                }
            }
            target[x * 4] = blue;
            target[x * 4 + 1] = green;
            target[x * 4 + 2] = red;
        }
    }
}

static void observe_sample(int16_t sample) {
    int value = sample;
    unsigned magnitude = (unsigned)(value < 0 ? -value : value);
    if (magnitude != 0) {
        audio_nonzero_samples++;
        if (magnitude > audio_peak) audio_peak = magnitude;
    }
    if (magnitude >= 32760u) audio_clipped_samples++;
    if (audio_sample_history_initialized) {
        int delta = value - previous_audio_samples[audio_sample_channel];
        unsigned delta_magnitude = (unsigned)(delta < 0 ? -delta : delta);
        if (delta_magnitude > audio_max_delta)
            audio_max_delta = delta_magnitude;
    }
    previous_audio_samples[audio_sample_channel] = value;
    if (audio_sample_channel == 1) audio_sample_history_initialized = true;
    audio_sample_channel ^= 1u;
    audio_fnv1a ^= (uint8_t)(sample & 0xff);
    audio_fnv1a *= UINT64_C(1099511628211);
    audio_fnv1a ^= (uint8_t)(((uint16_t)sample >> 8) & 0xff);
    audio_fnv1a *= UINT64_C(1099511628211);
}

static void observe_stereo_frame(int16_t left, int16_t right) {
    if (left == 0 && right == 0) {
        audio_zero_frames++;
        audio_consecutive_zero_frames++;
        if (audio_consecutive_zero_frames >
            audio_max_consecutive_zero_frames) {
            audio_max_consecutive_zero_frames =
                audio_consecutive_zero_frames;
        }
    } else {
        audio_consecutive_zero_frames = 0;
    }
}

static void audio_sample_callback(int16_t left, int16_t right) {
    int16_t pair[2] = { left, right };
    observe_sample(left);
    observe_sample(right);
    observe_stereo_frame(left, right);
    if (audio_pcm != NULL &&
        fwrite(pair, sizeof pair[0], 2, audio_pcm) != 2) {
        audio_pcm_failed = true;
    }
    audio_frames++;
}

static size_t audio_batch_callback(const int16_t *data, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        observe_sample(data[i * 2]);
        observe_sample(data[i * 2 + 1]);
        observe_stereo_frame(data[i * 2], data[i * 2 + 1]);
    }
    if (audio_pcm != NULL &&
        fwrite(data, sizeof data[0], frames * 2, audio_pcm) != frames * 2) {
        audio_pcm_failed = true;
    }
    audio_frames += frames;
    return frames;
}

static void input_poll_callback(void) {
}

static int16_t input_state_callback(unsigned port, unsigned device,
                                    unsigned index, unsigned id) {
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) return NULL;
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    long length = ftell(stream);
    if (length <= 0 || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)length);
    if (data == NULL || fread(data, 1, (size_t)length, stream) !=
                            (size_t)length) {
        free(data);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *size_out = (size_t)length;
    return data;
}

static bool write_ppm(const char *path) {
    FILE *stream = fopen(path, "wb");
    if (stream == NULL) return false;
    bool ok = fprintf(stream, "P6\n%d %d\n255\n", CAPTURE_WIDTH,
                      CAPTURE_HEIGHT) > 0;
    for (unsigned y = 0; ok && y < CAPTURE_HEIGHT; y++) {
        const uint8_t *row = frame_bgrx +
                             (size_t)y * CAPTURE_WIDTH *
                                 CAPTURE_BYTES_PER_PIXEL;
        for (unsigned x = 0; ok && x < CAPTURE_WIDTH; x++) {
            uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
            ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
        }
    }
    if (fclose(stream) != 0) ok = false;
    return ok;
}

static bool write_file(const char *path, const void *data, size_t size) {
    FILE *stream = fopen(path, "wb");
    if (stream == NULL) return false;
    bool ok = fwrite(data, 1, size, stream) == size;
    if (fclose(stream) != 0) ok = false;
    return ok;
}

static uint16_t read_wram16(const uint8_t *wram, size_t address) {
    return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1] << 8));
}

int main(int argc, char **argv) {
    static const char expected_sha256[] =
        "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633";
    if (argc != 5) {
        fprintf(stderr,
                "usage: dkc2_libretro_capture <core.dll> <rom.sfc> "
                "<frames> <private.ppm>\n");
        return 2;
    }
    char *frame_end = NULL;
    unsigned long frames = strtoul(argv[3], &frame_end, 10);
    if (frame_end == argv[3] || *frame_end != '\0' || frames == 0 ||
        frames > 1000000) {
        fprintf(stderr, "frames must be between 1 and 1000000\n");
        return 2;
    }

    size_t rom_size = 0;
    uint8_t *rom = read_file(argv[2], &rom_size);
    if (rom == NULL) {
        fprintf(stderr, "unable to read ROM: %s\n", argv[2]);
        return 2;
    }
    size_t skip = rom_size % 1024 == 512 ? 512 : 0;
    char rom_sha256[65];
    dkc2_sha256_hex(rom + skip, rom_size - skip, rom_sha256);
    if (rom_size - skip != 0x400000 ||
        strcmp(rom_sha256, expected_sha256) != 0) {
        fprintf(stderr, "unsupported ROM (size=%zu sha256=%s)\n",
                rom_size - skip, rom_sha256);
        free(rom);
        return 3;
    }

    core_module = LoadLibraryA(argv[1]);
    if (core_module == NULL || !bind_symbols()) {
        fprintf(stderr, "unable to load libretro core: %s (error=%lu)\n",
                argv[1], (unsigned long)GetLastError());
        if (core_module != NULL) FreeLibrary(core_module);
        free(rom);
        return 4;
    }
    if (p_retro_api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "unsupported libretro API version\n");
        FreeLibrary(core_module);
        free(rom);
        return 4;
    }

    p_retro_set_environment(environment_callback);
    p_retro_set_video_refresh(video_callback);
    p_retro_set_audio_sample(audio_sample_callback);
    p_retro_set_audio_sample_batch(audio_batch_callback);
    p_retro_set_input_poll(input_poll_callback);
    p_retro_set_input_state(input_state_callback);
    p_retro_init();

    struct retro_system_info system_info;
    memset(&system_info, 0, sizeof system_info);
    p_retro_get_system_info(&system_info);
    struct retro_game_info game = {
        argv[2], rom + skip, rom_size - skip, NULL
    };
    if (!p_retro_load_game(&game)) {
        fprintf(stderr, "libretro core rejected the verified ROM\n");
        p_retro_deinit();
        FreeLibrary(core_module);
        free(rom);
        return 5;
    }
    struct retro_system_av_info av_info;
    memset(&av_info, 0, sizeof av_info);
    p_retro_get_system_av_info(&av_info);
    const char *audio_pcm_path = getenv("DKC2_AUDIO_PCM");
    if (audio_pcm_path != NULL && *audio_pcm_path != '\0') {
        audio_pcm = fopen(audio_pcm_path, "wb");
        if (audio_pcm == NULL) {
            fprintf(stderr, "unable to open private reference audio: %s\n",
                    audio_pcm_path);
            p_retro_unload_game();
            p_retro_deinit();
            FreeLibrary(core_module);
            free(rom);
            return 8;
        }
    }

    const uint8_t *wram =
        (const uint8_t *)p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t wram_size = p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    const char *state_trace_text = getenv("DKC2_STATE_TRACE");
    bool state_trace = state_trace_text != NULL && *state_trace_text != '\0' &&
                       *state_trace_text != '0';
    bool state_initialized = false;
    uint16_t previous_game_mode = 0;
    uint16_t previous_demo_status = 0;
    uint16_t previous_demo_sequence = 0;
    uint16_t previous_level = 0;
    uint16_t previous_game_sub_mode = 0;
    unsigned title_entries = 0;
    unsigned demo_starts = 0;
    unsigned demo_ends = 0;
    unsigned attract_cycles = 0;
    unsigned attract_sequence_errors = 0;
    size_t state_events = 0;
    unsigned long long previous_audio_nonzero_samples = 0;
    for (unsigned long frame = 0; frame < frames; frame++) {
        p_retro_run();
        bool video_active = false;
        for (size_t i = 0; i < sizeof frame_bgrx; i++) {
            if (frame_bgrx[i] != 0) {
                video_active = true;
                break;
            }
        }
        if (video_active) {
            video_active_frames++;
            video_consecutive_blank_frames = 0;
        } else {
            video_blank_frames++;
            video_consecutive_blank_frames++;
            if (video_consecutive_blank_frames >
                video_max_consecutive_blank_frames) {
                video_max_consecutive_blank_frames =
                    video_consecutive_blank_frames;
            }
        }
        if (audio_nonzero_samples != previous_audio_nonzero_samples)
            audio_active_host_frames++;
        else
            audio_silent_host_frames++;
        previous_audio_nonzero_samples = audio_nonzero_samples;
        if (wram == NULL || wram_size < 0x0610) continue;
        uint16_t game_mode = read_wram16(wram, 0x24);
        uint16_t demo_status = read_wram16(wram, 0x05fb);
        uint16_t demo_sequence = read_wram16(wram, 0x0605);
        uint16_t level = read_wram16(wram, 0x00d3);
        uint16_t game_sub_mode = read_wram16(wram, 0x0096);
        /* Snes9x power-on WRAM uses $55 until the game clears it. Do not
         * mistake that initialization pattern for an attract transition. */
        if (!state_initialized && game_mode == 0x5555) continue;
        bool state_changed = !state_initialized ||
                             game_mode != previous_game_mode ||
                             demo_status != previous_demo_status ||
                             demo_sequence != previous_demo_sequence ||
                             level != previous_level ||
                             game_sub_mode != previous_game_sub_mode;
        if (state_initialized) {
            if (game_mode != previous_game_mode && game_mode == 0xb397)
                title_entries++;
            if (previous_demo_status == 0 && demo_status != 0 &&
                game_mode == 0x87e1) {
                static const uint16_t expected_levels[3] = {
                    0x000c, 0x000f, 0x0013
                };
                demo_starts++;
                unsigned expected = (demo_starts - 1) % 3;
                if (demo_sequence != expected + 1 ||
                    level != expected_levels[expected])
                    attract_sequence_errors++;
            }
            if (previous_demo_status != 0 && demo_status == 0 &&
                previous_game_mode == 0x8819)
                demo_ends++;
            if (previous_demo_sequence == 3 && demo_sequence == 0 &&
                demo_status == 0)
                attract_cycles++;
        }
        if (state_changed) {
            state_events++;
            if (state_trace) {
                fprintf(stderr,
                        "reference_state_event frame=%lu game_mode=$%04x "
                        "game_sub_mode=$%04x demo_status=$%04x "
                        "demo_sequence=$%04x demo_index=$%04x "
                        "demo_timer=$%04x level=$%04x active_frame=$%04x "
                        "continuation=$%04x\n",
                        frame + 1, game_mode, game_sub_mode, demo_status,
                        demo_sequence, read_wram16(wram, 0x05fd),
                        read_wram16(wram, 0x05ff), level,
                        read_wram16(wram, 0x002a),
                        read_wram16(wram, 0x0020));
            }
        }
        previous_game_mode = game_mode;
        previous_demo_status = demo_status;
        previous_demo_sequence = demo_sequence;
        previous_level = level;
        previous_game_sub_mode = game_sub_mode;
        state_initialized = true;
    }
    if (audio_pcm != NULL && fclose(audio_pcm) != 0)
        audio_pcm_failed = true;
    audio_pcm = NULL;
    if (audio_pcm_failed) {
        fprintf(stderr, "unable to write private reference audio: %s\n",
                audio_pcm_path);
        p_retro_unload_game();
        p_retro_deinit();
        FreeLibrary(core_module);
        free(rom);
        return 8;
    }
    if (video_callbacks == 0 || !write_ppm(argv[4])) {
        fprintf(stderr, "reference core did not produce a writable frame\n");
        p_retro_unload_game();
        p_retro_deinit();
        FreeLibrary(core_module);
        free(rom);
        return 6;
    }

    printf("core=%s version=%s fps=%.9f sample_rate=%.3f\n",
           system_info.library_name ? system_info.library_name : "unknown",
           system_info.library_version ? system_info.library_version :
                                         "unknown",
           av_info.timing.fps, av_info.timing.sample_rate);
    printf("result=completed frames=%lu video_callbacks=%lu "
           "source=%ux%u output=%s\n",
           frames, video_callbacks, source_width, source_height, argv[4]);
    printf("audio_frames=%llu audio_nonzero_samples=%llu "
           "audio_zero_frames=%llu max_consecutive_audio_zero_frames=%llu "
           "audio_clipped_samples=%llu audio_peak=%u audio_max_delta=%u "
           "audio_fnv1a=%016llx\n",
           audio_frames, audio_nonzero_samples, audio_zero_frames,
           audio_max_consecutive_zero_frames, audio_clipped_samples,
           audio_peak, audio_max_delta,
           (unsigned long long)audio_fnv1a);
    if (audio_pcm_path != NULL && *audio_pcm_path != '\0')
        printf("audio_output=%s\n", audio_pcm_path);
    printf("run_stats video_active_frames=%lu blank_frames=%lu "
           "max_consecutive_blank_frames=%lu audio_active_frames=%lu "
           "audio_silent_frames=%lu\n",
           video_active_frames, video_blank_frames,
           video_max_consecutive_blank_frames, audio_active_host_frames,
           audio_silent_host_frames);
    if (wram != NULL && wram_size >= 0x2c) {
        printf("wram_size=%zu continuation=$%04x dispatcher=$%04x "
               "intro_state=$%04x\n",
               wram_size, (unsigned)(wram[0x20] | (wram[0x21] << 8)),
               (unsigned)(wram[0x24] | (wram[0x25] << 8)),
               (unsigned)(wram[0x2a] | (wram[0x2b] << 8)));
    }
    printf("state_stats events=%zu title_entries=%u demo_starts=%u "
           "demo_ends=%u attract_cycles=%u sequence_errors=%u\n",
           state_events, title_entries, demo_starts, demo_ends,
           attract_cycles, attract_sequence_errors);
    const void *video_ram = p_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
    size_t video_ram_size = p_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM);
    if (video_ram != NULL && video_ram_size != 0) {
        char video_ram_sha256[65];
        dkc2_sha256_hex(video_ram, video_ram_size, video_ram_sha256);
        printf("video_ram_size=%zu video_ram_sha256=%s\n", video_ram_size,
               video_ram_sha256);
    }
    const char *state_output = getenv("DKC2_STATE_OUTPUT");
    if (state_output != NULL && *state_output != '\0') {
        size_t state_size = p_retro_serialize_size();
        void *state = malloc(state_size);
        if (state_size == 0 || state == NULL ||
            !p_retro_serialize(state, state_size) ||
            !write_file(state_output, state, state_size)) {
            fprintf(stderr, "unable to write private reference state: %s\n",
                    state_output);
            free(state);
            p_retro_unload_game();
            p_retro_deinit();
            FreeLibrary(core_module);
            free(rom);
            return 7;
        }
        printf("state_size=%zu state_output=%s\n", state_size, state_output);
        free(state);
    }

    p_retro_unload_game();
    p_retro_deinit();
    FreeLibrary(core_module);
    free(rom);
    return 0;
}
