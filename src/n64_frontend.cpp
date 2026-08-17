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

namespace {

static joypad_inputs_t g_pad {};
static bool g_z_prev = false;
static bool g_high_gear = false;
static char g_last_message[192] = "Cannonball64 booting...";
static bool g_shutdown_requested = false;

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
    if (!std::strcmp(key, "cannonball_video_fps"))               return "Smooth (60)";
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

static inline uint16_t rgb565_to_rgba5551(uint16_t p)
{
    // RGB565:    RRRRRGGG GGGBBBBB
    // RGBA5551:  RRRRRGGG GGBBBBBA
    return static_cast<uint16_t>(
        (p & 0xF800u) |
        (p & 0x07C0u) |
        ((p & 0x001Fu) << 1) |
        0x0001u
    );
}

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0)
        return;

    surface_t* fb = display_get();
    if (!fb)
        return;

    // The N64 target is 320x240. Native Cannonball with widescreen/hires off
    // is 320x224, so this normally becomes an 8-line top/bottom border.
    const unsigned out_w = fb->width;
    const unsigned out_h = fb->height;

    uint16_t* dst_base = static_cast<uint16_t*>(fb->buffer);
    const unsigned dst_stride = fb->stride / sizeof(uint16_t);

    // Opaque black.
    for (unsigned y = 0; y < out_h; y++)
    {
        uint16_t* row = dst_base + y * dst_stride;
        for (unsigned x = 0; x < out_w; x++)
            row[x] = 0x0001;
    }

    unsigned draw_w = std::min(width, out_w);
    unsigned draw_h = std::min(height, out_h);

    // If a future core option unexpectedly produces >320 pixels, downscale
    // safely instead of overrunning the framebuffer.
    if (width > out_w)
        draw_w = out_w;
    if (height > out_h)
        draw_h = out_h;

    const unsigned xoff = (out_w - draw_w) / 2;
    const unsigned yoff = (out_h - draw_h) / 2;

    const auto* src_bytes = static_cast<const uint8_t*>(data);

    for (unsigned y = 0; y < draw_h; y++)
    {
        const unsigned sy = (height == draw_h) ? y : (y * height / draw_h);
        const auto* src = reinterpret_cast<const uint16_t*>(src_bytes + sy * pitch);
        uint16_t* dst = dst_base + (y + yoff) * dst_stride + xoff;

        for (unsigned x = 0; x < draw_w; x++)
        {
            const unsigned sx = (width == draw_w) ? x : (x * width / draw_w);
            dst[x] = rgb565_to_rgba5551(src[sx]);
        }
    }

    display_show(fb);
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
    int v = static_cast<int>(g_pad.stick_x) * 384;
    v = std::max(-32767, std::min(32767, v));
    return static_cast<int16_t>(v);
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
        retro_run();

    retro_unload_game();
    retro_deinit();
    audio_close();
    display_close();
    joypad_close();

    while (true) {}
    return 0;
}
