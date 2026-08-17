/*
 * Cannonball64: libdragon frontend for the Cannonball Libretro core.
 *
 * The goal is to keep Cannonball's game/renderer/audio logic intact and
 * implement the smallest possible frontend for Nintendo 64.
 */
#include <libdragon.h>
#include <libretro.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include "video.h"

namespace {

static joypad_inputs_t g_pad {};
static bool g_z_prev = false;
static bool g_high_gear = false;
static char g_last_message[192] = "Cannonball64 booting...";
static bool g_shutdown_requested = false;

static uint64_t g_prof_frame_us = 0;
static uint64_t g_prof_video_submit_us = 0;
static uint32_t g_prof_frames = 0;

static uint16_t g_text_tlut[16] __attribute__((aligned(8)));

static const char* gSystemDir = "sd://";
static const char* kSaveDir   = "sd://cannonball";

static void set_message(const char* s)
{
    if (!s) return;
    std::snprintf(g_last_message, sizeof(g_last_message), "%s", s);
    debugf("[Cannonball64] %s\n", g_last_message);
}

static void n64_log(enum retro_log_level level, const char* fmt, ...)
{
    (void)level;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    debugf("%s", buf);
}

static bool n64_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
    (void)effect;
    if (port != 0 || !joypad_is_connected(JOYPAD_PORT_1))
        return false;

    if (!joypad_get_rumble_supported(JOYPAD_PORT_1))
        return false;

    joypad_set_rumble_active(JOYPAD_PORT_1, strength != 0);
    return true;
}

static const char* get_variable_value(const char* key)
{
    if (!key) return nullptr;

    // Keep the native 320x224 System 16 presentation for the first N64 port.
    if (!std::strcmp(key, "cannonball_menu_enabled"))            return "ON";
    if (!std::strcmp(key, "cannonball_menu_road_scroll_speed"))  return "50";
    if (!std::strcmp(key, "cannonball_video_fps"))               return "Low (30)";
    if (!std::strcmp(key, "cannonball_video_widescreen"))        return "OFF";
    if (!std::strcmp(key, "cannonball_video_hires"))             return "OFF";

    if (!std::strcmp(key, "cannonball_sound_enable"))            return "ON";
    if (!std::strcmp(key, "cannonball_sound_custom_wav_volume")) return "200";

    // Use the ordinary factory PCM ROM so the optional replacement ROM is
    // not required just to boot.
    if (!std::strcmp(key, "cannonball_sound_fix_samples"))       return "OFF";

    // Z toggles low/high gear in our input frontend.
    if (!std::strcmp(key, "cannonball_gear"))                    return "Manual 2 Buttons";
    if (!std::strcmp(key, "cannonball_analog"))                  return "ON";
    if (!std::strcmp(key, "cannonball_jap"))                     return "OFF";

    return nullptr;
}

static bool environment_cb(unsigned cmd, void* data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            if (!data) return false;
            *static_cast<const char**>(data) = gSystemDir;
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if (!data) return false;
            *static_cast<const char**>(data) = kSaveDir;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            if (!data) return false;
            auto fmt = *static_cast<enum retro_pixel_format*>(data);
            return fmt == RETRO_PIXEL_FORMAT_RGB565;
        }

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            if (!data) return false;
            auto* cb = static_cast<retro_log_callback*>(data);
            cb->log = n64_log;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        {
            if (!data) return false;
            auto* rumble = static_cast<retro_rumble_interface*>(data);
            rumble->set_rumble_state = n64_rumble;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            if (!data) return false;
            auto* var = static_cast<retro_variable*>(data);
            const char* value = get_variable_value(var->key);
            if (!value)
            {
                var->value = nullptr;
                return false;
            }
            var->value = value;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *static_cast<bool*>(data) = false;
            return true;

        // Deliberately report no bitmask support. This makes the core call
        // input_state_cb per button, which keeps our N64 mapping simple.
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *static_cast<bool*>(data) = true;
            return true;

        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
            if (data) *static_cast<unsigned*>(data) = 1;
            return true;

        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            if (!data) return false;
            auto* msg = static_cast<retro_message*>(data);
            set_message(msg->msg);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        {
            if (!data) return false;
            auto* msg = static_cast<retro_message_ext*>(data);
            set_message(msg->msg);
            return true;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN:
            g_shutdown_requested = true;
            return true;

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;

        // We intentionally don't present a full frontend option UI yet.
        // GET_VARIABLE above forces the N64-safe values we care about and the
        // core's own defaults cover everything else.
        default:
            return false;
    }
}



static inline uint16_t cb64_rgba5551(uint32_t c)
{
    return static_cast<uint16_t>(c & 0xFFFFu);
}

static void cb64_rdp_load_text_palette(unsigned palette)
{
    /*
     * System 16 text tiles are 3bpp, stored in Cannonball's 4bpp tile cache.
     * CI4 is therefore a direct hardware fit. Index 0 is transparent.
     */
    g_text_tlut[0] = 0;

    const unsigned base =
        TILEMAP_COLOUR_OFFSET + ((palette & 7u) << 3);

    for (unsigned i = 1; i < 8; i++)
        g_text_tlut[i] = cb64_rgba5551(video.rgb[base + i]);

    for (unsigned i = 8; i < 16; i++)
        g_text_tlut[i] = 0;

    data_cache_hit_writeback(g_text_tlut, sizeof(g_text_tlut));
    rdpq_tex_upload_tlut(g_text_tlut, 0, 16);
}

static void cb64_rdp_draw_text_layer(void)
{
    hwtiles* tiles = video.tile_layer;
    if (!tiles)
        return;

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);

    int current_palette = -1;
    uint16_t tile_index = 0;

    for (unsigned my = 0; my < 32; my++)
    {
        for (unsigned mx = 0; mx < 64; mx++, tile_index += 2)
        {
            uint16_t code =
                (static_cast<uint16_t>(tiles->text_ram[tile_index]) << 8) |
                 static_cast<uint16_t>(tiles->text_ram[tile_index + 1]);

            const unsigned priority = (code >> 15) & 1u;
            if (priority != 1u)
                continue;

            const unsigned palette = (code >> 9) & 7u;

            code &= 0x1FFu;
            code += static_cast<uint16_t>(tiles->tile_banks[0]) * 0x1000u;
            code &= (NUM_TILES - 1);

            if (code == 0)
                continue;

            int x = static_cast<int>(mx * 8) - 192;
            int y = static_cast<int>(my * 8);

            /*
             * Native Cannonball is 320x224, centered in the 320x240 VI mode.
             * rdpq_tex_blit handles the partially clipped edge tiles.
             */
            if (x <= -8 || x >= 320 || y <= -8 || y >= 224)
                continue;

            if (current_palette != static_cast<int>(palette))
            {
                cb64_rdp_load_text_palette(palette);
                current_palette = static_cast<int>(palette);
            }

            uint32_t* tile_pixels = tiles->tiles + (static_cast<unsigned>(code) << 3);

            /*
             * One tile is eight uint32 rows = 32 bytes = 8x8 CI4.
             * The converted Cannonball tile cache is already nibble-packed in
             * the exact left-to-right order the RDP consumes.
             */
            data_cache_hit_writeback(tile_pixels, 32);

            surface_t tile_surface =
                surface_make_linear(tile_pixels, FMT_CI4, 8, 8);

            rdpq_tex_blit(&tile_surface,
                          static_cast<float>(x),
                          static_cast<float>(y + 8),
                          nullptr);
        }
    }

    rdpq_mode_tlut(TLUT_NONE);
    rdpq_mode_alphacompare(0);
}

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch)
{
    const uint64_t submit_start = get_ticks_us();

    if (!data || width == 0 || height == 0)
        return;

    if (width != 320 || height != 224 || pitch != 640)
    {
        debugf("[CB64 RDP] unexpected core frame %ux%u pitch=%u\n",
               width, height, static_cast<unsigned>(pitch));
        return;
    }

    surface_t* fb = display_get();
    if (!fb)
        return;

    /*
     * Cannonball's CPU renderer now emits RGBA5551 directly.
     * Flush it once, then let RDP copy the 320x224 surface and compose the
     * hardware-rendered text/HUD on top.
     */
    data_cache_hit_writeback(data, pitch * height);

    surface_t source = surface_make(
        const_cast<void*>(data),
        FMT_RGBA16,
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        static_cast<uint16_t>(pitch)
    );

    rdpq_attach_clear(fb, nullptr);

    /* Fast RDP copy mode for the software-rendered base layers. */
    rdpq_set_mode_copy(false);
    rdpq_tex_blit(&source, 0.0f, 8.0f, nullptr);

    /*
     * First genuinely hardware-rendered Cannonball layer:
     * System 16 text/HUD tiles are uploaded as CI4 and palette-expanded by
     * RDP's TLUT hardware.
     */
    cb64_rdp_draw_text_layer();

    /*
     * Do not wait for RDP. Let it finish and flip the buffer asynchronously
     * while the R4300 starts the next game frame.
     */
    rdpq_detach_show();

    g_prof_video_submit_us += get_ticks_us() - submit_start;
}

static void audio_sample_cb(int16_t left, int16_t right)
{
    int16_t frame[2] = {left, right};
    audio_push(frame, 1, true);
}

static size_t audio_batch_cb(const int16_t* data, size_t frames)
{
    if (!data || !frames)
        return 0;

    // Blocking here intentionally lets AI act as one of our timing governors.
    // Cannonball emits exactly one video frame's worth of samples per run.
    return static_cast<size_t>(audio_push(data, static_cast<int>(frames), true));
}

static void input_poll_cb(void)
{
    joypad_poll();
    g_pad = joypad_get_inputs(JOYPAD_PORT_1);

    if (g_pad.btn.z && !g_z_prev)
        g_high_gear = !g_high_gear;

    g_z_prev = g_pad.btn.z;
}

static int16_t scaled_stick_x()
{
    int x = static_cast<int>(g_pad.stick_x);
    const int sign = x < 0 ? -1 : 1;
    int mag = x < 0 ? -x : x;

    constexpr int deadzone = 7;
    constexpr int full_scale = 80;
    constexpr int usable = full_scale - deadzone;

    if (mag <= deadzone)
        return 0;

    mag -= deadzone;
    if (mag > usable)
        mag = usable;

    /*
     * 70% linear / 30% cubic-ish response:
     * gentler around center without making the ends feel dead.
     */
    const int linear = (mag * 32767) / usable;
    const int curved =
        (linear * linear / 32767) * linear / 32767;
    const int shaped = (linear * 7 + curved * 3) / 10;

    return static_cast<int16_t>(sign * shaped);
}

static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
    if (port != 0)
        return 0;

    if (device == RETRO_DEVICE_ANALOG &&
        index == RETRO_DEVICE_INDEX_ANALOG_LEFT &&
        id == RETRO_DEVICE_ID_ANALOG_X)
    {
        return scaled_stick_x();
    }

    // Let Cannonball fall back to digital pedals.
    if (device == RETRO_DEVICE_ANALOG &&
        index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
    {
        return 0;
    }

    if (device != RETRO_DEVICE_JOYPAD)
        return 0;

    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    {
        uint16_t mask = 0;

        if (g_pad.btn.d_up)    mask |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (g_pad.btn.d_down)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (g_pad.btn.d_left)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (g_pad.btn.d_right) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;

        /* Cannonball: Libretro B = accelerator, Y = brake */
        if (g_pad.btn.a)       mask |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
        if (g_pad.btn.b)       mask |= 1u << RETRO_DEVICE_ID_JOYPAD_Y;

        /* Z toggles Cannonball's two gear buttons. */
        if (g_high_gear)
            mask |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
        else
            mask |= 1u << RETRO_DEVICE_ID_JOYPAD_X;

        if (g_pad.btn.start)   mask |= 1u << RETRO_DEVICE_ID_JOYPAD_START;

        /* C-Down = coin */
        if (g_pad.btn.c_down)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT;

        /* Either shoulder = viewpoint */
        if (g_pad.btn.l || g_pad.btn.r)
            mask |= 1u << RETRO_DEVICE_ID_JOYPAD_L;

        /* C-Up = Cannonball frontend menu */
        if (g_pad.btn.c_up)
            mask |= 1u << RETRO_DEVICE_ID_JOYPAD_R;

        return (int16_t)mask;
    }

    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_UP:     return g_pad.btn.d_up;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return g_pad.btn.d_down;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return g_pad.btn.d_left;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return g_pad.btn.d_right;

        // Cannonball's Libretro mapping:
        // B = accelerate, Y = brake.
        case RETRO_DEVICE_ID_JOYPAD_B:      return g_pad.btn.a;
        case RETRO_DEVICE_ID_JOYPAD_Y:      return g_pad.btn.b;

        // "Manual 2 Buttons" wants X=low and A=high. N64 Z toggles state.
        case RETRO_DEVICE_ID_JOYPAD_X:      return !g_high_gear;
        case RETRO_DEVICE_ID_JOYPAD_A:      return  g_high_gear;

        case RETRO_DEVICE_ID_JOYPAD_START:  return g_pad.btn.start;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return g_pad.btn.c_down;
        case RETRO_DEVICE_ID_JOYPAD_L:      return g_pad.btn.l || g_pad.btn.r;
        case RETRO_DEVICE_ID_JOYPAD_R:      return g_pad.btn.c_up;
        default:                            return 0;
    }
}

static void draw_error_screen(const char* title, const char* detail)
{
    surface_t* fb = display_get();
    if (!fb) return;

    graphics_fill_screen(fb, graphics_make_color(0, 0, 0, 255));
    graphics_set_color(
        graphics_make_color(255, 255, 255, 255),
        graphics_make_color(0, 0, 0, 255));

    graphics_draw_text(fb, 16, 20, "CANNONBALL 64 r2");
    graphics_draw_text(fb, 16, 42, title ? title : "ERROR");

    graphics_set_color(
        graphics_make_color(255, 190, 60, 255),
        graphics_make_color(0, 0, 0, 255));

    if (detail)
        graphics_draw_text(fb, 16, 70, detail);

    graphics_set_color(
        graphics_make_color(200, 200, 200, 255),
        graphics_make_color(0, 0, 0, 255));

    graphics_draw_text(fb, 16, 112, "Self-contained build required.");
    graphics_draw_text(fb, 16, 128, "Public artifact contains placeholders.");
    graphics_draw_text(fb, 16, 150, "Run the local Termux injector after");
    graphics_draw_text(fb, 16, 166, "the GitHub build succeeds.");
    graphics_draw_text(fb, 16, 194, "scripts/pack_local_roms.sh");

    display_show(fb);
}

static bool ensure_sd_dirs()
{
    // mkdir may return an error when the folder already exists; that is fine.
    mkdir("sd://cannonball", 0777);
    return true;
}

} // namespace

int main()
{
    debug_init_emulog();
    debug_init_usblog();

    joypad_init();
    display_init(
        RESOLUTION_320x240,
        DEPTH_16_BPP,
        3,
        GAMMA_NONE,
        FILTERS_RESAMPLE
    );

    rdpq_init();
    debugf("[CB64 RDP] RDPQ initialized\n");

    /*
     * The public build includes an official libdragon DragonFS image with
     * same-size placeholder ROM slots. n64tool adds it to the ROMPAK TOC,
     * so DFS_DEFAULT_LOCATION is the authoritative mount mechanism.
     */
    const int dfs_rc = dfs_init(DFS_DEFAULT_LOCATION);
    if (dfs_rc != DFS_ESUCCESS)
    {
        std::snprintf(g_last_message, sizeof(g_last_message),
                      "DragonFS mount failed: %s", dfs_strerror(dfs_rc));
        while (true)
            draw_error_screen("ROM FILESYSTEM FAILED", g_last_message);
    }

    gSystemDir = "rom:/";
    debugf("[Cannonball64] DragonFS mounted from ROMPAK TOC\n");

    /* Detect an unpatched public artifact explicitly. */
    FILE* rom_probe = fopen("rom:/cannonball/epr-10380b.133", "rb");
    if (!rom_probe)
    {
        while (true)
            draw_error_screen("ROM SLOT MISSING",
                              "Embedded epr-10380b.133 could not be opened");
    }

    char probe_sig[21] = {};
    fread(probe_sig, 1, 20, rom_probe);
    fclose(rom_probe);

    if (!std::strncmp(probe_sig, "CB64_ROM_PLACEHOLDER", 20))
    {
        while (true)
            draw_error_screen("LOCAL PACK REQUIRED",
                              "Run scripts/pack_local_roms.sh in Termux");
    }

    ensure_sd_dirs();

    debugf("[Cannonball64] r2 full-core frontend starting\n");

    retro_set_environment(environment_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();

    /*
     * Cannonball uses info->path to derive its ROM directory. Supplying a
     * synthetic path points it directly at the already-mounted DragonFS.
     */
    retro_game_info game_info {};
    game_info.path = "rom:/cannonball/cannonball64.content";

    if (!retro_load_game(&game_info))
    {
        debugf("[Cannonball64] retro_load_game failed: %s\n", g_last_message);
        while (true)
            draw_error_screen("CORE LOAD FAILED", g_last_message);
    }

    retro_system_av_info av {};
    retro_get_system_av_info(&av);

    int sample_rate = static_cast<int>(av.timing.sample_rate + 0.5);
    if (sample_rate <= 0)
        sample_rate = 44100;

    audio_init(sample_rate, 4);

    debugf("[Cannonball64] core loaded: %ux%u %.3f fps %.0f Hz\n",
           av.geometry.base_width,
           av.geometry.base_height,
           av.timing.fps,
           av.timing.sample_rate);

    while (!g_shutdown_requested)
    {
        const uint64_t frame_start = get_ticks_us();

        retro_run();

        g_prof_frame_us += get_ticks_us() - frame_start;
        g_prof_frames++;

        if (g_prof_frames >= 30)
        {
            const uint64_t avg_frame =
                g_prof_frame_us / g_prof_frames;
            const uint64_t avg_submit =
                g_prof_video_submit_us / g_prof_frames;

            const uint32_t fps10 =
                avg_frame
                    ? static_cast<uint32_t>(10000000ULL / avg_frame)
                    : 0;

            debugf(
                "[CB64 RDP PERF] fps=%u.%u frame=%llu us "
                "rdp_submit=%llu us\n",
                fps10 / 10,
                fps10 % 10,
                (unsigned long long)avg_frame,
                (unsigned long long)avg_submit
            );

            g_prof_frame_us = 0;
            g_prof_video_submit_us = 0;
            g_prof_frames = 0;
        }
    }

    retro_unload_game();
    retro_deinit();
    audio_close();
    rdpq_close();
    display_close();
    joypad_close();

    while (true) {}
    return 0;
}
