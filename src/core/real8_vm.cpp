#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "real8_vm.h"
#include "real8_compression.h"
#include "real8_bindings.h"
#include "real8_fonts.h"

#if !defined(__GBA__)
    #include "real8_shell.h"
    #include "real8_tools.h"
#endif

#include <lodePNG.h> 

#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <vector>

#if defined(__GBA__)
#define IWRAM_CODE __attribute__((section(".iwram"), long_call))
#else
#define IWRAM_CODE
#endif

#ifndef REAL8_GBA_IWRAM_INPUT
#define REAL8_GBA_IWRAM_INPUT 1
#endif

#if REAL8_GBA_IWRAM_INPUT
#define IWRAM_INPUT_CODE IWRAM_CODE
#else
#define IWRAM_INPUT_CODE
#endif

static const char* g_last_api_call = "none";
static const char* g_last_lua_phase = "none";
static char g_last_cart_path[512] = {0};
static int g_last_lua_line = 0;
static char g_last_lua_source[256] = {0};

namespace {
    static inline bool supports_bottom_screen(const Real8VM* vm);
    static inline uint8_t default_platform_target_for_host(const Real8VM* vm);
}

void real8_set_last_api_call(const char* name)
{
    g_last_api_call = name ? name : "none";
}

void real8_set_last_lua_phase(const char* name)
{
    g_last_lua_phase = name ? name : "none";
}

void real8_set_last_cart_path(const char* path)
{
    if (!path) {
        g_last_cart_path[0] = '\0';
        return;
    }
    std::strncpy(g_last_cart_path, path, sizeof(g_last_cart_path) - 1);
    g_last_cart_path[sizeof(g_last_cart_path) - 1] = '\0';
}

void real8_set_last_lua_line(int line, const char* source)
{
    g_last_lua_line = line;
    if (source && source[0]) {
        std::strncpy(g_last_lua_source, source, sizeof(g_last_lua_source) - 1);
        g_last_lua_source[sizeof(g_last_lua_source) - 1] = '\0';
    }
}

const char* real8_get_last_api_call()
{
    return g_last_api_call ? g_last_api_call : "none";
}

const char* real8_get_last_lua_phase()
{
    return g_last_lua_phase ? g_last_lua_phase : "none";
}

const char* real8_get_last_cart_path()
{
    return g_last_cart_path;
}

int real8_get_last_lua_line()
{
    return g_last_lua_line;
}

const char* real8_get_last_lua_source()
{
    return g_last_lua_source;
}

static bool ends_with_ci(const std::string& value, const char* suffix)
{
    size_t value_len = value.size();
    size_t suffix_len = strlen(suffix);
    if (value_len < suffix_len) return false;
    size_t offset = value_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i)
    {
        char a = (char)tolower((unsigned char)value[offset + i]);
        char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

static bool is_text_cart_path(const std::string& path)
{
    return ends_with_ci(path, ".p8");
}

#if defined(__GBA__)
#ifndef REAL8_GBA_IWRAM_MAPCHECK
#define REAL8_GBA_IWRAM_MAPCHECK 1
#endif
#ifndef REAL8_GBA_IWRAM_DIRTYRECT
#define REAL8_GBA_IWRAM_DIRTYRECT 1
#endif
#if REAL8_GBA_IWRAM_MAPCHECK
#define IWRAM_MAPCHECK_CODE IWRAM_CODE
#else
#define IWRAM_MAPCHECK_CODE
#endif
#if REAL8_GBA_IWRAM_DIRTYRECT
#define IWRAM_DIRTYRECT_CODE IWRAM_CODE
#else
#define IWRAM_DIRTYRECT_CODE
#endif
#else
#define IWRAM_MAPCHECK_CODE
#define IWRAM_DIRTYRECT_CODE
#endif

// --------------------------------------------------------------------------
// STATIC HELPERS & PALETTE
// --------------------------------------------------------------------------

static uint8_t find_closest_p8_color(uint8_t r, uint8_t g, uint8_t b);

// PALETTE_RGB removed from here. It is now in Real8Gfx.

static uint8_t find_closest_p8_color(uint8_t r, uint8_t g, uint8_t b)
{
    int min_dist = 1000000;
    uint8_t best_idx = 0;

    // Use Real8Gfx::PALETTE_RGB
    for (int i = 0; i < 16; i++) {
        int dr = r - Real8Gfx::PALETTE_RGB[i][0];
        int dg = g - Real8Gfx::PALETTE_RGB[i][1];
        int db = b - Real8Gfx::PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = i; }
    }
    // Extended 16-31
    for (int i = 16; i < 32; i++) {
        int dr = r - Real8Gfx::PALETTE_RGB[i][0];
        int dg = g - Real8Gfx::PALETTE_RGB[i][1];
        int db = b - Real8Gfx::PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = 128 + (i - 16); }
    }
    return best_idx;
}

namespace {
#if defined(REAL8_GBA_ENABLE_AUDIO) && REAL8_GBA_ENABLE_AUDIO
    constexpr bool kGbaAudioDisabledDefault = false;
#else
    constexpr bool kGbaAudioDisabledDefault = true;
#endif

    const int kGbaInitHookCount = 1000;
    const int kGbaInitHookLimit = 4000;

    int g_gbaInitHookTicks = 0;
    int g_gbaInitHookLimit = 0;
    bool g_gbaInitHookActive = false;
    lua_Hook g_gbaInitPrevHook = nullptr;
    int g_gbaInitPrevMask = 0;
    int g_gbaInitPrevCount = 0;

    void gbaInitTimeoutHook(lua_State* L, lua_Debug* ar) {
        (void)ar;
        if (!g_gbaInitHookActive) return;
        if (++g_gbaInitHookTicks >= g_gbaInitHookLimit) {
            g_gbaInitHookActive = false;
            luaL_error(L, "GBA _init timeout");
        }
    }

    class GbaInitHookGuard {
    public:
        GbaInitHookGuard(lua_State* L, int count, int limit) : L_(L) {
            g_gbaInitHookTicks = 0;
            g_gbaInitHookLimit = limit;
            g_gbaInitHookActive = true;
            g_gbaInitPrevHook = lua_gethook(L_);
            g_gbaInitPrevMask = lua_gethookmask(L_);
            g_gbaInitPrevCount = lua_gethookcount(L_);
            lua_sethook(L_, gbaInitTimeoutHook, LUA_MASKCOUNT, count);
        }

        ~GbaInitHookGuard() {
            g_gbaInitHookActive = false;
            lua_sethook(L_, g_gbaInitPrevHook, g_gbaInitPrevMask, g_gbaInitPrevCount);
        }

    private:
        lua_State* L_;
    };

}

static void IWRAM_INPUT_CODE update_gba_input(Real8VM* vm) {
    if (!vm || !vm->host) return;

    uint32_t* states = vm->btn_states;
    vm->last_btn_states[0] = states[0];
    const uint32_t state = vm->host->getPlayerInput(0);
    states[0] = state;

    uint8_t* counters = vm->btn_counters[0];
    for (int b = 0; b < 6; ++b) {
        counters[b] = (state & (1u << b)) ? (uint8_t)(counters[b] + 1u) : 0u;
    }

    vm->host->consumeLatchedInput();
    vm->btn_state = state;

    if (vm->ram) {
        vm->ram[0x5F30] = (uint8_t)(state & 0xFF);
        vm->ram[0x5F34] = (uint8_t)((state >> 8) & 0xFF);
    }
}
// --------------------------------------------------------------------------
// ERROR HANDLING (Using Debugger)
// --------------------------------------------------------------------------

void Real8VM::setLastError(const char* title, const char* fmt, ...) {
    hasLastError = true;

    snprintf(lastErrorTitle, sizeof(lastErrorTitle), "%s", title ? title : "VM ERROR");

    va_list args;
    va_start(args, fmt);
    vsnprintf(lastErrorDetail, sizeof(lastErrorDetail), fmt ? fmt : "", args);
    va_end(args);

    // Also log it
    if (host) host->log("[VM] %s: %s", lastErrorTitle, lastErrorDetail);
}

static int traceback(lua_State *L) {
    lua_getglobal(L, "__pico8_vm_ptr");
    Real8VM *vm = (Real8VM *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *msg = lua_tostring(L, 1);
    
    // Filter internal HALT signals
    if (msg && strstr(msg, "HALT")) {
        lua_pushstring(L, msg);
        return 1;
    }

    // Standard stack trace
    luaL_traceback(L, L, msg, 1);
    
#if !defined(__GBA__)
    if (vm && vm->host && vm->host->isConsoleOpen()) {
        vm->host->log("[LUA ERROR] %s", msg);

        // Delegate to Debugger Class
        vm->debug.paused = true;
        vm->debug.step_mode = false;

        lua_Debug ar;
        if (lua_getstack(L, 1, &ar)) {
            lua_getinfo(L, "l", &ar); 
            vm->debug.printSourceContext(ar.currentline, 7);
            vm->host->log("[DEBUG] Paused on Error at Line %d.", ar.currentline);
        } else {
            vm->host->log("[DEBUG] Paused on Error (Unknown Line).");
        }

        vm->show_frame();

        // Blocking Wait for Debugger
        while (vm->debug.paused) {
            vm->host->waitForDebugEvent();
        }
    }
#endif
    
    return 1;
}

// --------------------------------------------------------------------------
// LIFECYCLE
// --------------------------------------------------------------------------

Real8VM::Real8VM(IReal8Host *h) : host(h), gpu(this)
#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
, debug(this)
#endif
{
    L = nullptr;
    ram = nullptr;
    rom = nullptr;
    fb = nullptr;

    isLibretroPlatform = (host && strcmp(host->getPlatform(), "Libretro") == 0);
    isGbaPlatform = (host && strcmp(host->getPlatform(), "GBA") == 0);
    skipDirtyRect = isLibretroPlatform;

    dirty_x0 = fb_w; dirty_y0 = fb_h;
    dirty_x1 = 0; dirty_y1 = 0;
    
#if REAL8_HAS_LIBRETRO_BUFFERS
    if (!isGbaPlatform) {
        memset(screen_buffer, 0, sizeof(screen_buffer));
        updatePaletteLUT();
    }
#endif

    gpu.init();
    initDefaultPalette();
    
    volume_music = 7;
    volume_sfx = 10;
    
    #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    audio.init(this);
    #endif
    crt_filter = false;
    showSkin = false;
    
    std::string fileUrl = host->getRepoUrlFromFile();
    if (!fileUrl.empty()) {
        currentRepoUrl = fileUrl;
    } else {
        currentRepoUrl = IReal8Host::DEFAULT_GAMES_REPOSITORY;
    }

#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    init_wavetables();
#endif

#if !defined(__GBA__)
    Real8Tools::LoadSettings(this, host);
#endif

}

Real8VM::~Real8VM()
{
    clearLuaRefs();
    if (L) { lua_close(L); L = nullptr; }
#if defined(__GBA__)
    fb = nullptr;
    depth_fb = nullptr;
    stereo_layers = nullptr;
#else
    if (fb) {
        if (fb_is_linear && host) {
            host->freeLinearFramebuffer(fb);
        } else {
            P8_FREE(fb);
        }
        fb = nullptr;
    }
    if (fb_bottom) {
        P8_FREE(fb_bottom);
        fb_bottom = nullptr;
    }
    if (depth_fb) { P8_FREE(depth_fb); depth_fb = nullptr; }
    if (stereo_layers) { P8_FREE(stereo_layers); stereo_layers = nullptr; }
#endif
    if (ram) { P8_FREE(ram); ram = nullptr; }
    if (rom && rom_owned) { P8_FREE(rom); }
    rom = nullptr;
}

void Real8VM::setRomView(const uint8_t* data, size_t size, bool readOnly)
{
    if (rom && rom_owned) {
        P8_FREE(rom);
    }
    rom = const_cast<uint8_t*>(data);
    rom_size = size;
    rom_readonly = readOnly;
    rom_owned = false;
}

bool Real8VM::ensureWritableRom()
{
    if (rom && !rom_readonly) return true;

    uint8_t* new_rom = (uint8_t*)calloc(1, 0x8000);
    if (!new_rom) return false;

    if (rom && rom_size > 0) {
        size_t copy = (rom_size < 0x8000) ? rom_size : 0x8000;
        memcpy(new_rom, rom, copy);
    }

    if (rom && rom_owned) {
        P8_FREE(rom);
    }

    rom = new_rom;
    rom_size = 0x8000;
    rom_readonly = false;
    rom_owned = true;
    return true;
}

bool Real8VM::initMemory()
{
    // 1. Allocate Master RAM (32KB)
    bool newRam = false;
    if (!ram) {
        ram = (uint8_t *)calloc(1, 0x8000); 
        if (!ram) return false;
        newRam = true;
    }
    if (newRam) {
        ram[0x5F81] = 3; // default stereo mode = host default
        ram[Real8VM::PLATFORM_TARGET_ADDR] = default_platform_target_for_host(this);
        if (host && std::strcmp(host->getPlatform(), "3DS") == 0) {
            ram[0x5FE1] = 1;
        }
    }
    if (ram && ram[Real8VM::PLATFORM_TARGET_ADDR] > Real8VM::PLATFORM_TARGET_SWITCH) {
        ram[Real8VM::PLATFORM_TARGET_ADDR] = default_platform_target_for_host(this);
    }

    if (!rom) {
        if (!rom_readonly) {
            rom = (uint8_t *)calloc(1, 0x8000);
            if (!rom) return false;
            rom_size = 0x8000;
            rom_owned = true;
            rom_readonly = false;
        }
    } else if (rom_size == 0 && !rom_readonly) {
        rom_size = 0x8000;
    }

    // 2. Setup Aliases
    gfx = (uint8_t (*)[128])(ram + 0x0000);
    map_data = (uint8_t (*)[128])(ram + 0x2000);
    sprite_flags = ram + 0x3000;
    music_ram = ram + 0x3100;
    sfx_ram = ram + 0x3200;
    user_data = ram + 0x4300;
    screen_ram = ram + 0x6000;

    // 3. Framebuffer + stereo buffers (dynamic resolution)
    r8_flags = ram ? ram[0x5FE0] : 0;
    r8_vmode_req = ram ? ram[0x5FE1] : 0;
    applyVideoMode(r8_vmode_req, /*force=*/true);
    bottom_vmode_req = ram ? ram[BOTTOM_VMODE_REQ_ADDR] : 0;
    if (supports_bottom_screen(this) && bottom_vmode_req == 0) {
        bottom_vmode_req = BOTTOM_VMODE_DEFAULT;
    }
    applyBottomVideoMode(bottom_vmode_req, /*force=*/true);
    if (ram) {
        ram[Real8VM::BOTTOM_GPIO_ADDR] = (uint8_t)(ram[Real8VM::BOTTOM_GPIO_ADDR] & 0x03);
        applyBottomScreenFlags(ram[Real8VM::BOTTOM_GPIO_ADDR]);
    }
    return fb != nullptr;
}

namespace {
    static inline uint8_t clamp_mode_u8(uint8_t v) {
        return (v > 3) ? 3 : v;
    }

    enum PlatformTarget : uint8_t {
        kTargetWindows = Real8VM::PLATFORM_TARGET_WINDOWS,
        kTargetGba = Real8VM::PLATFORM_TARGET_GBA,
        kTarget3ds = Real8VM::PLATFORM_TARGET_3DS,
        kTargetSwitch = Real8VM::PLATFORM_TARGET_SWITCH
    };

    static inline uint8_t clamp_platform_target(uint8_t v) {
        return (v > kTargetSwitch) ? kTargetWindows : v;
    }

    static inline uint8_t default_platform_target_for_host(const Real8VM* vm) {
        if (!vm || !vm->host) return kTargetWindows;
        const char* platform = vm->host->getPlatform();
        if (!platform) return kTargetWindows;
        if (std::strcmp(platform, "GBA") == 0) return kTargetGba;
        if (std::strcmp(platform, "3DS") == 0) return kTarget3ds;
        if (std::strcmp(platform, "Switch") == 0) return kTargetSwitch;
        return kTargetWindows;
    }

    static inline uint8_t effective_platform_target(const Real8VM* vm) {
        uint8_t target = kTargetWindows;
        if (vm && vm->ram) {
            target = clamp_platform_target(vm->ram[Real8VM::PLATFORM_TARGET_ADDR]);
        }
        if (!vm || !vm->host) return target;
        const char* platform = vm->host->getPlatform();
        if (!platform) return target;
        if (std::strcmp(platform, "GBA") == 0) return kTargetGba;
        if (std::strcmp(platform, "3DS") == 0) return kTarget3ds;
        if (std::strcmp(platform, "Switch") == 0) return kTargetSwitch;
        return target;
    }

    static inline uint8_t clamp_mode_for_target(uint8_t target, uint8_t mode) {
        if (target == kTargetWindows) return 0;
        if (target == kTargetGba && mode > 1) return 1;
        return mode;
    }

    static inline void mode_to_size_for_target(uint8_t target, bool bottom, uint8_t mode, int& out_w, int& out_h) {
        switch (target) {
            case kTargetGba:
                if (mode == 1) { out_w = 240; out_h = 160; }
                else { out_w = 128; out_h = 128; }
                break;
            case kTarget3ds:
                if (mode == 2) {
                    if (bottom) { out_w = 160; out_h = 120; }
                    else { out_w = 200; out_h = 120; }
                } else if (mode == 3) {
                    if (bottom) { out_w = 320; out_h = 240; }
                    else { out_w = 400; out_h = 240; }
                } else {
                    out_w = 128; out_h = 128;
                }
                break;
            case kTargetSwitch:
                switch (mode) {
                    case 1: out_w = 256; out_h = 144; break;
                    case 2: out_w = 640; out_h = 640; break;
                    case 3: out_w = 1280; out_h = 720; break;
                    default: out_w = 128; out_h = 128; break;
                }
                break;
            case kTargetWindows:
            default:
                out_w = 128; out_h = 128;
                break;
        }
    }

    static inline bool supports_bottom_screen(const Real8VM* vm) {
        if (!vm || !vm->host) return false;
        const char* platform = vm->host->getPlatform();
        return platform && (std::strcmp(platform, "3DS") == 0 || std::strcmp(platform, "Windows") == 0);
    }
}

void Real8VM::applyVideoMode(uint8_t requested_mode, bool force)
{
    const uint8_t prev_req = r8_vmode_req;
    const uint8_t prev_cur = r8_vmode_cur;
    const int prev_w = fb_w;
    const int prev_h = fb_h;

    uint8_t req = clamp_mode_u8(requested_mode);
    const uint8_t target = effective_platform_target(this);
    uint8_t cur = clamp_mode_for_target(target, req);

    int new_w = fb_w;
    int new_h = fb_h;
    mode_to_size_for_target(target, /*bottom=*/false, cur, new_w, new_h);

    const bool size_changed = (new_w != prev_w || new_h != prev_h);
    const bool mode_changed = (prev_req != req || prev_cur != cur);
    const bool need_realloc = (size_changed || !fb);
    const bool need_clear = force || mode_changed || size_changed;

    r8_vmode_req = req;
    r8_vmode_cur = cur;
    if (ram) {
        ram[0x5FE0] = r8_flags;
        ram[0x5FE1] = r8_vmode_req;
        ram[0x5FE2] = r8_vmode_cur;
    }

    fb_w = new_w;
    fb_h = new_h;

    if (need_realloc) {
        if (fb) {
            if (fb_is_linear && host) {
                host->freeLinearFramebuffer(fb);
            } else {
                P8_FREE(fb);
            }
            fb = nullptr;
        }
        fb_is_linear = false;

        const size_t fb_bytes = (size_t)fb_w * (size_t)fb_h;
        void* linear = host ? host->allocLinearFramebuffer(fb_bytes, 0x80) : nullptr;
        if (linear) {
            fb = (uint8_t*)linear;
            fb_is_linear = true;
        } else {
            fb = (uint8_t*)calloc(fb_bytes, 1);
            fb_is_linear = false;
        }

#if !defined(__GBA__)
        if (depth_fb) { P8_FREE(depth_fb); depth_fb = nullptr; }
        if (stereo_layers) { P8_FREE(stereo_layers); stereo_layers = nullptr; }
#else
        depth_fb = nullptr;
        stereo_layers = nullptr;
#endif
    }

#if !defined(__GBA__)
    if (!depth_fb && fb) {
        const size_t fb_bytes = (size_t)fb_w * (size_t)fb_h;
        depth_fb = (uint8_t*)calloc(fb_bytes, 1);
        if (depth_fb) {
            std::memset(depth_fb, (uint8_t)STEREO_BUCKET_BIAS, fb_bytes);
        }
    }
    if (!stereo_layers && fb) {
        const size_t layer_rows = (size_t)fb_h * (size_t)STEREO_LAYER_COUNT;
        stereo_layers = (uint8_t*)calloc(layer_rows * (size_t)fb_w, 1);
        if (stereo_layers) {
            std::memset(stereo_layers, 0xFF, layer_rows * (size_t)fb_w);
            for (int y = 0; y < fb_h; ++y) {
                std::memset(stereo_layer_row(STEREO_BUCKET_BIAS, y), 0, (size_t)fb_w);
            }
        }
    }
#endif

    if (need_clear && fb) {
        const size_t fb_bytes = (size_t)fb_w * (size_t)fb_h;
        std::memset(fb, 0, fb_bytes);
#if !defined(__GBA__)
        if (depth_fb) std::memset(depth_fb, (uint8_t)STEREO_BUCKET_BIAS, fb_bytes);
        if (stereo_layers) {
            const size_t layer_rows = (size_t)fb_h * (size_t)STEREO_LAYER_COUNT;
            std::memset(stereo_layers, 0xFF, layer_rows * (size_t)fb_w);
            for (int y = 0; y < fb_h; ++y) {
                std::memset(stereo_layer_row(STEREO_BUCKET_BIAS, y), 0, (size_t)fb_w);
            }
        }
#endif
    }

    if (need_clear) {
        dirty_x0 = 0;
        dirty_y0 = 0;
        dirty_x1 = fb_w - 1;
        dirty_y1 = fb_h - 1;
    }

    gpu.clip(0, 0, fb_w, fb_h);

    if (need_realloc && host) {
        host->onFramebufferResize(fb_w, fb_h);
    }

    if (size_changed || need_realloc) {
        applyBottomScreenFlags(bottom_screen_flags);
    }
}

void Real8VM::applyBottomVideoMode(uint8_t requested_mode, bool force)
{
    const uint8_t prev_req = bottom_vmode_req;
    const uint8_t prev_cur = bottom_vmode_cur;
    const int prev_w = bottom_fb_w;
    const int prev_h = bottom_fb_h;

    uint8_t req = clamp_mode_u8(requested_mode);
    const uint8_t target = effective_platform_target(this);
    uint8_t cur = clamp_mode_for_target(target, req);

    int new_w = bottom_fb_w;
    int new_h = bottom_fb_h;
    mode_to_size_for_target(target, /*bottom=*/true, cur, new_w, new_h);

    const bool size_changed = (new_w != prev_w || new_h != prev_h);
    const bool mode_changed = (prev_req != req || prev_cur != cur);
    const bool need_realloc = size_changed || !fb_bottom;
    const bool need_clear = force || mode_changed || size_changed;

    bottom_vmode_req = req;
    bottom_vmode_cur = cur;
    if (ram) {
        ram[BOTTOM_VMODE_REQ_ADDR] = bottom_vmode_req;
        ram[BOTTOM_VMODE_CUR_ADDR] = bottom_vmode_cur;
    }

    bottom_fb_w = new_w;
    bottom_fb_h = new_h;

    if (!supports_bottom_screen(this)) {
        return;
    }

    if (bottom_screen_enabled || draw_target_bottom || fb_bottom) {
        if (need_realloc) {
            if (fb_bottom) {
                P8_FREE(fb_bottom);
                fb_bottom = nullptr;
            }
            if (bottom_fb_w > 0 && bottom_fb_h > 0) {
                const size_t fb_bytes = (size_t)bottom_fb_w * (size_t)bottom_fb_h;
                fb_bottom = (uint8_t*)calloc(fb_bytes, 1);
            }
        }

        if (!fb_bottom) {
            bottom_screen_enabled = false;
            draw_target_bottom = false;
            return;
        }

        if (need_clear) {
            const size_t fb_bytes = (size_t)bottom_fb_w * (size_t)bottom_fb_h;
            std::memset(fb_bottom, 0, fb_bytes);
        }

        if (draw_target_bottom) {
            gpu.clip(0, 0, draw_w(), draw_h());
        }

        if (bottom_screen_enabled) {
            bottom_dirty = true;
        }
    }
}

void Real8VM::applyBottomScreenFlags(uint8_t flags)
{
    const uint8_t clamped = (uint8_t)(flags & 0x03);
    bottom_screen_flags = clamped;
    if (ram) ram[BOTTOM_GPIO_ADDR] = clamped;

    if (!supports_bottom_screen(this)) {
        bottom_screen_enabled = false;
        draw_target_bottom = false;
        return;
    }

    const bool prev_draw_target = draw_target_bottom;
    bottom_screen_enabled = (clamped & BOTTOM_FLAG_ENABLE) != 0;
    draw_target_bottom = (clamped & BOTTOM_FLAG_DRAW) != 0;

    if (bottom_screen_enabled || draw_target_bottom || fb_bottom) {
        if (!fb_bottom) {
            if (bottom_fb_w <= 0 || bottom_fb_h <= 0) {
                bottom_fb_w = BOTTOM_FIXED_W;
                bottom_fb_h = BOTTOM_FIXED_H;
            }
            if (bottom_fb_w > 0 && bottom_fb_h > 0) {
                const size_t fb_bytes = (size_t)bottom_fb_w * (size_t)bottom_fb_h;
                fb_bottom = (uint8_t*)calloc(fb_bytes, 1);
            }
        }
    }

    if (!fb_bottom) {
        bottom_screen_enabled = false;
        draw_target_bottom = false;
        return;
    }

    if (prev_draw_target != draw_target_bottom) {
        gpu.clip(0, 0, draw_w(), draw_h());
    }

    if (bottom_screen_enabled) {
        bottom_dirty = true;
    }
}


void Real8VM::clearDepthBuffer(uint8_t bucket)
{
#if !defined(__GBA__)
    if (!depth_fb) return;
    int8_t b = (int8_t)bucket;
    if (b < STEREO_BUCKET_MIN) b = STEREO_BUCKET_MIN;
    if (b > STEREO_BUCKET_MAX) b = STEREO_BUCKET_MAX;
    const uint8_t layer_idx = (uint8_t)(b + STEREO_BUCKET_BIAS);
    std::memset(depth_fb, layer_idx, (size_t)fb_w * (size_t)fb_h);
#else
    (void)bucket;
#endif
}

void Real8VM::rebootVM()
{
    const bool isGba = isGbaPlatform;
    auto gbaLog = [&](const char* msg) {
        if (isGba && host) host->log("%s", msg);
    };

    bootSplashActive = false;
    bootSplashEndMs = 0;

    host->log("[VM] Rebooting...");
    gbaLog("[BOOT] REBOOT BEGIN");

    if (!next_cart_path.empty()) {
        if (currentCartPath.empty()) currentCartPath = next_cart_path;
        std::string sourcePath = currentCartPath.empty() ? next_cart_path : currentCartPath;
        size_t lastSlash = sourcePath.find_last_of("/\\");
        currentGameId = (lastSlash == std::string::npos) ? sourcePath : sourcePath.substr(lastSlash + 1);
    }
    
    targetFPS = 30;
    debugFPS = 0;
    displayFPS = 0;
    app_fps_last_ms = 0;
    app_fps_counter = 0;
    display_fps_last_ms = 0;
    display_fps_counter = 0;
    patchModActive = false;
    
    // Reset Lua
    clearLuaRefs();
    if (L) { lua_close(L); L = nullptr; }
    gbaLog("[BOOT] REBOOT LUA CLOSED");
    gbaLog("[BOOT] REBOOT LUA NEWSTATE");
    L = luaL_newstate();
    if (L) {
        gbaLog("[BOOT] REBOOT LUA NEWSTATE OK");
    } else {
        gbaLog("[BOOT] REBOOT LUA NEWSTATE FAIL");
    }
    
    reset_requested = false;
    next_cart_path = "";

    // Run lib loading + API registration in protected calls.
    if (L) {
        gbaLog("[BOOT] REBOOT LUA OPENLIBS");
        lua_pushcfunction(L, [](lua_State* L_) -> int {
            luaL_openlibs(L_);
            return 0;
        });
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (host) host->log("[VM] ERROR: luaL_openlibs failed: %s", err ? err : "(no message)");
            lua_pop(L, 1);
            lua_close(L);
            L = nullptr;
            gbaLog("[BOOT] REBOOT LUA OPENLIBS FAIL");
        } else {
            gbaLog("[BOOT] REBOOT LUA OPENLIBS OK");
        }
    }

    if (L) {
        gbaLog("[BOOT] REBOOT LUA REG");
        lua_pushlightuserdata(L, (void*)this);
        lua_setglobal(L, "__pico8_vm_ptr");
#if !defined(__GBA__)
        if (!isGba) {
            lua_sethook(L, Real8Debugger::luaHook, LUA_MASKLINE, 0);
        }
#endif

        lua_pushcfunction(L, [](lua_State* L_) -> int {
            register_pico8_api(L_);
            return 0;
        });
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (host) host->log("[VM] ERROR: register_pico8_api failed: %s", err ? err : "(no message)");
            lua_pop(L, 1);
            lua_close(L);
            L = nullptr;
            gbaLog("[BOOT] REBOOT LUA REG FAIL");
        } else {
            gbaLog("[BOOT] REBOOT LUA REG OK");
        }
    }

    if (!L) {
        if (host) host->log("[ERROR] Failed to recreate Lua state!");
        // Continue resetting non-Lua state so the shell can show an error instead of exploding.
    }

    // Reset Core State
    cartDataId = "";
    memset(cart_data_ram, 0, sizeof(cart_data_ram));
    
    if (ram) memset(ram, 0, 0x8000);
    if (ram) ram[0x5F81] = 3; // default stereo mode = host default
    if (ram) ram[Real8VM::PLATFORM_TARGET_ADDR] = default_platform_target_for_host(this);
    if (rom && !rom_readonly) memset(rom, 0, 0x8000);
    memset(custom_font, 0, 0x800);
    clear_menu_items();
    r8_flags = 0;
    const uint8_t default_mode = (host && std::strcmp(host->getPlatform(), "3DS") == 0) ? 1 : 0;
    applyVideoMode(default_mode, /*force=*/true);
    bottom_vmode_req = supports_bottom_screen(this) ? BOTTOM_VMODE_DEFAULT : 0;
    applyBottomVideoMode(bottom_vmode_req, /*force=*/true);
    gbaLog("[BOOT] REBOOT CORE OK");

    // Reset Hardware
    gpu.reset();

    hwState.distort = 0;
    hwState.spriteSheetMemMapping = 0x00;
    hwState.screenDataMemMapping = 0x60;
    hwState.mapMemMapping = 0x20;
    hwState.widthOfTheMap = 128;
    if (ram) {
        ram[0x5F54] = hwState.spriteSheetMemMapping;
        ram[0x5F55] = hwState.screenDataMemMapping;
        ram[0x5F56] = hwState.mapMemMapping;
        ram[0x5F57] = hwState.widthOfTheMap;
    }
    gbaLog("[BOOT] REBOOT HW OK");
    
    resetInputState();
    gbaLog("[BOOT] REBOOT INPUT OK");

    #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    // Reset Audio
    audio.music_pattern = -1;
    for(int i=0; i<4; i++) {
        audio.channels[i].sfx_id = -1;
        audio.channels[i].phi = 0;
        audio.channels[i].current_vol = 0;
        audio.channels[i].lfsr = 0x7FFF;
        audio.channels[i].noise_sample = 0;
        audio.channels[i].tick_counter = 0;
    }
    gbaLog("[BOOT] REBOOT AUDIO OK");
    #endif
}

void Real8VM::forceExit()
{
#if !defined(__GBA__)
    if (debug.paused) debug.forceExit();
#endif
    saveCartData();
    #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    for (int i = 0; i < 4; i++) {
        audio.channels[i].sfx_id = -1;
        audio.channels[i].current_vol = 0;
    }
    #endif
    gpu.pal_reset();
    gpu.fillp(0);
    gpu.draw_mask = 0;
    if (ram) ram[0x5F5E] = 0;
    gpu.camera(0, 0);
    gpu.clip(0, 0, fb_w, fb_h);
    
    host->deleteFile("/cache.p8.png");
    resetInputState();
    currentGameId = ""; 
}

void Real8VM::resetInputState()
{
    memset(btn_states, 0, sizeof(btn_states));
    memset(last_btn_states, 0, sizeof(last_btn_states));
    memset(btn_counters, 0, sizeof(btn_counters));
    btn_mask = 0;
    btn_state = 0;
    last_btn_state = 0;
    mouse_x = 0;
    mouse_y = 0;
    mouse_buttons = 0;
    mouse_rel_x = 0;
    mouse_rel_y = 0;
    mouse_last_x = 0;
    mouse_last_y = 0;
    mouse_wheel_event = 0;
    key_pressed_this_frame = false;
    key_queue.clear();
    has_key_input = false;
    if (host) host->clearInputState();
}

// --------------------------------------------------------------------------
// CORE LOOP (runFrame)
// --------------------------------------------------------------------------

void Real8VM::runFrame()
{
    const bool isLibretro = isLibretroPlatform;
    const bool isGba = isGbaPlatform;
    if (ram) {
        ram[0x5FE2] = r8_vmode_cur;
        ram[BOTTOM_VMODE_CUR_ADDR] = bottom_vmode_cur;
    }
#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    const bool gbaAudioDisabled = isGba && kGbaAudioDisabledDefault;
#endif

#if !defined(__GBA__)
    // Debugger Paused?
    if (debug.paused && !debug.step_mode) {
        show_frame(); 
#if defined(__GBA__)
    #if REAL8_GBA_ENABLE_AUDIO
        audio.update(host);
    #endif
#else
        if (!gbaAudioDisabled) {
            audio.update(host);
        }
#endif
        return; 
    }
#endif

    // --------------------------------------------------------------------------
    // FRAME TIMING & SKIPPING
    // --------------------------------------------------------------------------
    // We determine early if this frame should actually execute Lua logic.
    static int tick_counter = 0;
    tick_counter++;
    bool is60FPS = (targetFPS == 60);
    const bool hostTicksAt30 = (host_tick_hz <= 30);
    bool shouldRunLua = is60FPS || (tick_counter % 2 == 0);
    if (!is60FPS && hostTicksAt30) {
        shouldRunLua = true;
    }

    if (skip_update_logic) {
        skip_update_logic = false; 
        shouldRunLua = false;
    }

    unsigned long now_ms = 0;
    bool splashActive = false;
    if (bootSplashActive && host) {
        now_ms = host->getMillis();
        if (now_ms < bootSplashEndMs) {
            splashActive = true;
            shouldRunLua = true;
        } else {
            bootSplashActive = false;
        }
    }

    // If we are skipping this frame (30fps simulation), we only maintain audio
    // on platforms that require it. We do NOT process input counters to prevent
    // desync with Lua logic.
    if (!shouldRunLua) {
#if defined(__GBA__)
    #if REAL8_GBA_ENABLE_AUDIO
        audio.update(host);
    #endif
#else
    #if REAL8_HAS_LIBRETRO_BUFFERS
        if (isLibretro) {
            // FIX: Calculate samples based on actual rate (22050 / 60 = 367.5)
            int samples_needed = (int)(AudioEngine::SAMPLE_RATE / 60.0f) + 1; 
            if (samples_needed > 2048) samples_needed = 2048;
            audio.generateSamples(static_audio_buffer, samples_needed);
            if (host) host->pushAudio(static_audio_buffer, samples_needed);
        } else if (!gbaAudioDisabled) {
            audio.update(host);
        }
    #else
        if (!gbaAudioDisabled) {
            audio.update(host);
        }
    #endif
#endif
#if REAL8_HAS_LIBRETRO_BUFFERS
        if (!isGba) {
            frame_is_dirty = false; // MARK FRAME AS CLEAN
        }
#endif
        return;
    }

#if REAL8_HAS_LIBRETRO_BUFFERS
    if (!isGba) {
        frame_is_dirty = true; // MARK FRAME AS DIRTY
    }
#endif

    gpu.beginFrame();
    if (splashActive) {
        const unsigned long splash_end = bootSplashEndMs;
        const unsigned long total_duration = 1500;
        const unsigned long text_duration = 1000;
        const unsigned long fade_ms = 200;
        const unsigned long splash_start = (splash_end >= total_duration) ? (splash_end - total_duration) : 0;
        const unsigned long elapsed = (now_ms > splash_start) ? (now_ms - splash_start) : 0;
        float alpha = 1.0f;
        if (elapsed >= text_duration) {
            alpha = 0.0f;
        } else if (fade_ms > 0 && text_duration > fade_ms * 2) {
            if (elapsed < fade_ms) {
                alpha = (float)elapsed / (float)fade_ms;
            } else if (elapsed > text_duration - fade_ms) {
                alpha = (float)(text_duration - elapsed) / (float)fade_ms;
            } else {
                alpha = 1.0f;
            }
        }
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        Real8Gfx::GfxState gfx_state;
        gpu.saveState(gfx_state);
        const char* msg = "Powered by REAL8";
        int screenW = draw_w();
        int screenH = draw_h();
        if (screenW <= 0) screenW = 128;
        if (screenH <= 0) screenH = 128;
        const int fontWidth = 5;
        const int fontHeight = 6;
        int x = (screenW / 2) - ((int)std::strlen(msg) * fontWidth / 2);
        int y = (screenH / 2) - (fontHeight / 2);
        static const uint8_t kFadeColors[] = {0, 5, 6, 7};
        const int colorCount = (int)(sizeof(kFadeColors) / sizeof(kFadeColors[0]));
        int colorIdx = (int)std::floor(alpha * (float)(colorCount - 1) + 0.5f);
        if (colorIdx < 0) colorIdx = 0;
        if (colorIdx >= colorCount) colorIdx = colorCount - 1;
        const uint8_t textColor = kFadeColors[colorIdx];

        gpu.setMenuFont(true);
        gpu.camera(0, 0);
        gpu.clip(0, 0, screenW, screenH);
        gpu.draw_mask = 0;
        gpu.fillp(0);
        gpu.rectfill(0, 0, screenW - 1, screenH - 1, 0);
        if (textColor != 0) {
            gpu.pprint(msg, (int)std::strlen(msg), x, y, textColor);
        }
        mark_draw_dirty_rect(0, 0, screenW - 1, screenH - 1);
        gpu.restoreState(gfx_state);
        gpu.setMenuFont(false);
        mouse_wheel_event = 0;
        return;
    }

    // --------------------------------------------------------------------------
    // INPUT PROCESSING (Synchronized with Logic Frame)
    // --------------------------------------------------------------------------

    if (isGba) {
        update_gba_input(this);
    } else {
        const int maxPlayers = 8;
        if (host) {
            // 1. Poll Events: 
            // Windows needs explicit polling. Libretro handles it externally.
            if (!isLibretro) {
                host->pollInput();
            }

            // 2. Fetch State:
            for (int i = 0; i < maxPlayers; i++) {
                last_btn_states[i] = btn_states[i]; // Track history
                btn_states[i] = host->getPlayerInput(i);
            }
        }

        // Update Internal Counters (btnp support)
        // This MUST happen here so counters increment only once per logic frame.
        for (int p = 0; p < maxPlayers; p++) {
            uint32_t state = btn_states[p];
            
            for (int b = 0; b < 6; b++) {
                if (state & (1 << b)) {
                    // Button is held
                    btn_counters[p][b]++;
                } else {
                    // Button is released
                    btn_counters[p][b] = 0;
                }
            }
        }
        if (host) {
            host->consumeLatchedInput();
        }

        // Update Legacy/Primary Player State for memory mapping
        btn_state = btn_states[0];

        // Update RAM Mapping
        if (ram) {
            MouseState ms = host->getMouseState();
            int mx = (ms.x < 0) ? 0 : (ms.x > 127) ? 127 : ms.x;
            int my = (ms.y < 0) ? 0 : (ms.y > 127) ? 127 : ms.y;

            mouse_rel_x = mx - mouse_last_x;
            mouse_rel_y = my - mouse_last_y;
            mouse_last_x = mx;
            mouse_last_y = my;
            mouse_x = mx;
            mouse_y = my;
            mouse_buttons = ms.btn;

            ram[0x5F30] = (uint8_t)(btn_state & 0xFF);
            ram[0x5F34] = (uint8_t)((btn_state >> 8) & 0xFF);
        }
    }

    // --------------------------------------------------------------------------
    // FPS MONITORING (App FPS)
    // --------------------------------------------------------------------------
    if (host) {
        unsigned long now = host->getMillis();
        if (app_fps_last_ms == 0) app_fps_last_ms = now;

        app_fps_counter++;

        if (now - app_fps_last_ms >= 1000) {
            debugFPS = app_fps_counter;
            app_fps_counter = 0;
            app_fps_last_ms = now;
        }
    }

    // --------------------------------------------------------------------------
    // LUA EXECUTION
    // --------------------------------------------------------------------------
#if defined(__GBA__) && REAL8_GBA_FAST_LUA
    const int errHandler = 0;
#else
    lua_pushcfunction(L, traceback);
    const int errHandler = lua_gettop(L);
#endif

    auto run_protected = [&](int nargs = 0) -> bool {
        int result = lua_pcall(L, nargs, 0, errHandler);
        if (result != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            if (reset_requested || (err && strstr(err, "HALT"))) {
                lua_pop(L, 1);
                return false;
            }
            host->log("!!! LUA RUNTIME ERROR !!!\n%s", err);
            if (!host->isConsoleOpen()) {
                exit_requested = true; 
            }
            lua_pop(L, 1);
            return false;
        }
        return true;
    };

    // --------------------------------------------------------------------------
    // Apply persistent patch.lua values before cart logic
    // --------------------------------------------------------------------------
    if (patchModActive) {
        lua_getglobal(L, "__real8_patch_apply");
        if (lua_isfunction(L, -1)) {
            int result = lua_pcall(L, 0, 0, errHandler);
            if (result != LUA_OK) {
                if (host) host->log("[MODS] patch.lua error: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
                patchModActive = false;
            }
        } else {
            lua_pop(L, 1);
            patchModActive = false;
        }
    }

    // _update / _update60
    if (lua_ref_update60 != LUA_NOREF) {
        real8_set_last_lua_phase("_update60");
        lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref_update60);
        if (!run_protected(0)) return;
    } else if (lua_ref_update != LUA_NOREF) {
        real8_set_last_lua_phase("_update");
        lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref_update);
        if (!run_protected(0)) return;
    }

    // Debug Logs
#if !defined(__GBA__)
    static int debug_log_timer = 0;
    if (showStats && ++debug_log_timer > 60) { 
        debug_log_timer = 0;
        if (!isGba) {
            host->log("[GFX] CAM:%d,%d CLIP:%d,%d PEN:%d MASK:%02X FPS:%d",
                      gpu.cam_x, gpu.cam_y, gpu.clip_x, gpu.clip_y, gpu.getPen(), gpu.draw_mask, debugFPS);
        }
    }
#endif

    // _draw
    if (lua_ref_draw != LUA_NOREF) {
        REAL8_PROFILE_BEGIN(this, kProfileDraw);
        real8_set_last_lua_phase("_draw");
        lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref_draw);
        run_protected(0);
        REAL8_PROFILE_END(this, kProfileDraw);
    }

#if !(defined(__GBA__) && REAL8_GBA_FAST_LUA)
    lua_pop(L, 1); // Pop traceback
#endif

    real8_set_last_lua_phase("idle");

    // --------------------------------------------------------------------------
    // 5. OVERLAYS & AUDIO UPDATE
    // --------------------------------------------------------------------------
    
    // FPS Overlay
    if (showStats && L) {
        lua_getglobal(L, "__p8_sys_overlay");
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, debugFPS);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1); 
            // Manual Drawing Backup (Simplified)
            int bk_cx = gpu.cam_x, bk_cy = gpu.cam_y;
            uint8_t bk_pen = gpu.getPen();
            gpu.camera(0, 0); gpu.clip(0, 0, fb_w, fb_h);
            int y0 = fb_h - 7;
            gpu.rectfill(0, y0, 32, fb_h - 1, 0); 
            char fpsText[16];
            snprintf(fpsText, sizeof(fpsText), "FPS:%d", debugFPS);
            gpu.pprint(fpsText, (int)strlen(fpsText), 1, y0 + 1, 11);
            gpu.camera(bk_cx, bk_cy); gpu.setPen(bk_pen);
        }
    }
    renderProfileOverlay();

    // Update Audio (Normal Path)
#if defined(__GBA__)
    #if REAL8_GBA_ENABLE_AUDIO
    audio.update(host);
    #endif
#else
    if (!gbaAudioDisabled) {
    #if REAL8_HAS_LIBRETRO_BUFFERS
        if (isLibretro) {
            int samples_needed = (int)(AudioEngine::SAMPLE_RATE / 60.0f) + 1; 
            if (samples_needed > 2048) samples_needed = 2048;
            audio.generateSamples(static_audio_buffer, samples_needed);
            if (host) host->pushAudio(static_audio_buffer, samples_needed);
        } else {
            audio.update(host);
        }
    #else
        audio.update(host);
    #endif
    }
#endif

    mouse_wheel_event = 0;
}

bool Real8VM::loadGame(const GameData& game)
{
    const bool isGba = isGbaPlatform;
    auto gbaLog = [&](const char* msg) {
        if (isGba && host) host->log("%s", msg);
    };

    if (!currentCartPath.empty())
        real8_set_last_cart_path(currentCartPath.c_str());

    gbaLog("[BOOT] loadGame");
    clearLastError();
    rebootVM();
    gbaLog("[BOOT] reboot ok");

    if (!L) {
        setLastError("VM INIT", "Failed to create Lua state (OOM or init failure)");
        return false;
    }
    gbaLog("[BOOT] lua ok");

    if (ram) {
        memcpy(ram + 0x0000, game.gfx, 0x2000);
        memcpy(ram + 0x2000, game.map, 0x1000);
        memcpy(ram + 0x3000, game.sprite_flags, 0x100);
        memcpy(ram + 0x3100, game.music, 0x100);
        memcpy(ram + 0x3200, game.sfx, 0x1100);
        if (rom) memcpy(rom, ram, 0x8000);
        gpu.pal_reset(); 
    }
    gbaLog("[BOOT] cart ok");

#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    if (host) host->pushAudio(nullptr, 0);
    gbaLog("[BOOT] audio ok");
#endif

#if !defined(__GBA__) 
    auto applyMods = [&]() {
        if (!host) return;
        std::string modCartPath;
        if (!currentCartPath.empty()) modCartPath = currentCartPath;
        else if (!game.cart_id.empty()) modCartPath = game.cart_id;
        else modCartPath = currentGameId;
        Real8Tools::ApplyMods(this, host, modCartPath);
    };
#endif

    // Install traceback handler for better error messages
    lua_pushcfunction(L, traceback);
    int errHandler = lua_gettop(L);

    const bool useGbaInitWatchdog = isGba;
    auto pcallWithInitWatchdog = [&](int nargs) -> int {
        if (!useGbaInitWatchdog) {
            return lua_pcall(L, nargs, 0, errHandler);
        }
        GbaInitHookGuard guard(L, kGbaInitHookCount, kGbaInitHookLimit);
        return lua_pcall(L, nargs, 0, errHandler);
    };

    const char* lua_src = nullptr;
    size_t lua_len = 0;

    // Cached for Export LUA on desktop hosts
    loadedLuaSource.clear();
    if (game.lua_code_ptr && game.lua_code_size > 0) {
        lua_src = game.lua_code_ptr;
        lua_len = game.lua_code_size;
    }
#if !defined(__GBA__)
    else {
        lua_src = game.lua_code.c_str();
        lua_len = game.lua_code.size();
    }
#endif

    if (useGbaInitWatchdog && host) {
        host->log("[BOOT] Lua bytes: %lu", (unsigned long)lua_len);
    }

    std::string normalized_lua;
    if (lua_len > 0) {
#if !defined(__GBA__)
        if (!currentCartPath.empty() && is_text_cart_path(currentCartPath))
        {
            size_t old_len = lua_len;
            std::string src_copy(lua_src, lua_len);
            normalized_lua = p8_normalize_lua_strings(src_copy);
            if (normalized_lua.size() != old_len || memcmp(normalized_lua.data(), lua_src, old_len) != 0)
            {
                if (host) host->log("[BOOT] UTF-8 string normalization applied (%lu -> %lu bytes)",
                                    (unsigned long)old_len, (unsigned long)normalized_lua.size());
                lua_src = normalized_lua.c_str();
                lua_len = normalized_lua.size();
            }
        }
#endif
#if !defined(__GBA__)
        loadedLuaSource.assign(lua_src, lua_len);
#endif

        if (!useGbaInitWatchdog) {
#if !defined(__GBA__)
            debug.setSource(game.lua_code);
#endif
        }

        if (useGbaInitWatchdog && host) host->log("[BOOT] Lua load");
        if (luaL_loadbuffer(L, lua_src, lua_len, "cart") != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            setLastError("LUA PARSE", "%s", err ? err : "(unknown parse error)");
            lua_pop(L, 2); // error + traceback
            return false;
        }
        if (useGbaInitWatchdog && host) host->log("[BOOT] Lua load ok");

        // pcall with traceback
        if (useGbaInitWatchdog && host) host->log("[BOOT] Lua run");
        if (pcallWithInitWatchdog(0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (err && strstr(err, "HALT")) { lua_pop(L, 2); return true; }
            setLastError("LUA RUNTIME", "%s", err ? err : "(unknown runtime error)");
            lua_pop(L, 2); // error + traceback
            return false;
        }
        if (useGbaInitWatchdog && host) host->log("[BOOT] Lua run ok");

    #if !defined(__GBA__)
        if (useGbaInitWatchdog && host) host->log("[BOOT] Mods");
        applyMods();
        if (useGbaInitWatchdog && host) host->log("[BOOT] Mods ok");
    #endif

        // Ensure native px9 bindings override any Lua implementations.
        register_px9_bindings(L);

        // _init (make it fatal so you see it immediately)
        cacheLuaRefs();
        if (lua_ref_init != LUA_NOREF) {
            real8_set_last_lua_phase("_init");
            lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref_init);
            if (useGbaInitWatchdog && host) host->log("[BOOT] _init");
            if (pcallWithInitWatchdog(0) != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                setLastError("_INIT ERROR", "%s", err ? err : "(unknown _init error)");
                lua_pop(L, 2); // error + traceback
                return false;
            }
            if (useGbaInitWatchdog && host) host->log("[BOOT] _init ok");

            // ------------------------------------------------------------------
#if !defined(__GBA__)
            // Stereo handshake (GPIO 0x5FF0): if cart pokes 0xA0 during _init,
            // automatically enable stereoscopic output.
            // ------------------------------------------------------------------
            if (!stereoscopic && ram) {
                const uint8_t v = ram[STEREO_GPIO_ADDR];
                if ((v & 0xF0) == 0xA0) {
                    stereoscopic = true;
                    if (host) host->log("[VM] Stereo handshake detected (0x%02X @ 0x%04X). Enabling stereoscopic.", v, (unsigned)STEREO_GPIO_ADDR);
                }
            }
#endif
        }
        cacheLuaRefs();
    } else {
        #if !defined(__GBA__)
        applyMods();
        #endif
        cacheLuaRefs();
    }

    lua_pop(L, 1); // pop traceback handler

    real8_set_last_lua_phase("idle");
    detectCartFPS();
    if (useGbaInitWatchdog && host) host->log("[BOOT] fps ok");
    mark_dirty_rect(0, 0, 128, 128);
    if (useGbaInitWatchdog && host) host->log("[BOOT] loadGame ok");
    if (host) {
        bootSplashActive = true;
        bootSplashEndMs = host->getMillis() + 1500;
    } else {
        bootSplashActive = false;
        bootSplashEndMs = 0;
    }
    return true;
}

static inline int p8_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void Real8VM::clearLuaRefs()
{
    if (!L) {
        lua_ref_update = LUA_NOREF;
        lua_ref_update60 = LUA_NOREF;
        lua_ref_draw = LUA_NOREF;
        lua_ref_init = LUA_NOREF;
        return;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_update);
    luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_update60);
    luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_draw);
    luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_init);
    lua_ref_update = LUA_NOREF;
    lua_ref_update60 = LUA_NOREF;
    lua_ref_draw = LUA_NOREF;
    lua_ref_init = LUA_NOREF;
}

void Real8VM::cacheLuaRefs()
{
    if (!L) return;
    clearLuaRefs();

    lua_getglobal(L, "_update60");
    if (lua_isfunction(L, -1)) {
        lua_ref_update60 = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    lua_getglobal(L, "_update");
    if (lua_isfunction(L, -1)) {
        lua_ref_update = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    lua_getglobal(L, "_draw");
    if (lua_isfunction(L, -1)) {
        lua_ref_draw = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    lua_getglobal(L, "_init");
    if (lua_isfunction(L, -1)) {
        lua_ref_init = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
}

void Real8VM::detectCartFPS()
{
    if (!L) return;
    targetFPS = (lua_ref_update60 != LUA_NOREF) ? 60 : 30;
}

// --------------------------------------------------------------------------
// MEMORY & PIXEL ACCESS
// --------------------------------------------------------------------------

void IWRAM_DIRTYRECT_CODE Real8VM::mark_dirty_rect(int x0, int y0, int x1, int y1)
{
    if (x1 < 0 || y1 < 0 || x0 >= fb_w || y0 >= fb_h) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb_w) x1 = fb_w - 1;
    if (y1 >= fb_h) y1 = fb_h - 1;
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}


// --------------------------------------------------------------------------
// GRAPHICS PRIMITIVES
// --------------------------------------------------------------------------

void Real8VM::initDefaultPalette()
{
    gpu.pal_reset();
}

// Clipping Helper
const int INSIDE = 0; const int LEFT = 1; const int RIGHT = 2; const int BOTTOM = 4; const int TOP = 8;
int computeOutCode(int x, int y, int xmin, int ymin, int xmax, int ymax) {
    int code = INSIDE;
    if (x < xmin) code |= LEFT; else if (x > xmax) code |= RIGHT;
    if (y < ymin) code |= TOP; else if (y > ymax) code |= BOTTOM;
    return code;
}

bool IWRAM_MAPCHECK_CODE Real8VM::map_check_flag(int x, int y, int w, int h, int flag)
{
    // Simplified bounds check logic, ignoring strict Lua table parsing here for speed/simplicity in VM
    int i0 = std::max(0, x / 8); int i1 = std::min(15, (x + w - 1) / 8);
    int j0 = std::max(0, y / 8); int j1 = std::min(15, (y + h - 1) / 8);
    for (int i = i0; i <= i1; ++i) {
        for (int j = j0; j <= j1; ++j) {
            // Need reference map logic? Assuming map_check is used in game world context
            // Default to 0,0 offset for now if not passed
             uint8_t tile = gpu.mget(i, j); // Simple mget call
             if (sprite_flags[tile] & (1 << flag)) return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// VM INTERFACE
// --------------------------------------------------------------------------

// New Helper: Pre-calculate RGBA colors once (or when palette changes)
#if REAL8_HAS_LIBRETRO_BUFFERS
void Real8VM::updatePaletteLUT() {
    for(int i=0; i<32; i++) {
        const uint8_t* c = Real8Gfx::PALETTE_RGB[i];
        // Libretro usually expects 0x00RRGGBB (XRGB8888)
        // Ensure this matches RETRO_PIXEL_FORMAT_XRGB8888
        palette_lut[i] = (c[0] << 16) | (c[1] << 8) | c[2];
    }
}
#endif

void Real8VM::show_frame()
{
    if (host) {
        unsigned long now = host->getMillis();
        if (display_fps_last_ms == 0) display_fps_last_ms = now;

        display_fps_counter++;

        if (now - display_fps_last_ms >= 1000) {
            displayFPS = display_fps_counter;
            display_fps_counter = 0;
            display_fps_last_ms = now;
        }
    }
    // --------------------------------------------------------
    // LIBRETRO OPTIMIZED PATH
    // --------------------------------------------------------
#if REAL8_HAS_LIBRETRO_BUFFERS
    if (isLibretroPlatform) {
        
        // If the frame didn't update (30fps skip), 
        // the screen_buffer is already valid from the previous call.
        // We can skip the conversion entirely.
        if (!frame_is_dirty) {
            return; // Libretro frontend will reuse the previous buffer or we push the old one
        }
        
        uint8_t draw_map[16]; 
        const uint8_t* screen_pal_ram = ram ? (ram + 0x5F10) : gpu.screen_palette; // You may need to make gpu.screen_palette public or add a getter

        for(int i=0; i<16; i++) {
            uint8_t col = screen_pal_ram[i];
            
            // Handle Extended Palette Mapping: 128-143 maps to internal indices 16-31
            if (col >= 128 && col <= 143) {
                draw_map[i] = 16 + (col - 128);
            } else {
                draw_map[i] = col & 0x1F; // Clamp to 0-31
            }
        }

        
#if !defined(__GBA__)
// 2. Convert FB -> 32-bit XRGB (or stereo anaglyph when enabled)
const bool stereo_active = (stereoscopic && !isShellUI && stereo_layers != nullptr);
if (!stereo_active) {
    const int copy_w = std::min(fb_w, 128);
    const int copy_h = std::min(fb_h, 128);
    std::memset(screen_buffer, 0, sizeof(screen_buffer));
    for (int y = 0; y < copy_h; y++) {
        const uint8_t* src_row = fb_row(y);
        uint32_t* dest_row = screen_buffer + (y * 128);
        for (int x = 0; x < copy_w; x++) {
            dest_row[x] = palette_lut[draw_map[src_row[x] & 0x0F]];
        }
    }
    return; // Libretro frontend handles the actual flip
}

// Stereo: forward-map each depth bucket layer into left/right eye destinations.
// This avoids "black hole" artifacts caused by disocclusion when shifting a single flattened framebuffer.
// Left eye -> RED, Right eye -> CYAN (G+B).
// Bucket-to-pixel mapping: each depth bucket step = 1 pixel of disparity (bucket 7 -> 7px).
static uint8_t z_left[128 * 128];
static uint8_t z_right[128 * 128];
const int copy_w = std::min(fb_w, 128);
const int copy_h = std::min(fb_h, 128);
std::memset(screen_buffer, 0, 128u * 128u * sizeof(uint32_t));
std::memset(z_left, 0, sizeof(z_left));
std::memset(z_right, 0, sizeof(z_right));

// NOTE: With signed buckets (-7..+7), we want both negative and positive buckets
// to be visible and participate in occlusion. Using the raw layer index as a Z
// value would cause bucket 0 (layer bias) to overwrite all negative buckets.
// Instead, use |bucket| as the per-pixel Z, so bucket 0 is the far background
// plane and increasing magnitude (regardless of sign) comes closer.
for (int li = 0; li < STEREO_LAYER_COUNT; ++li) {
    const int bucket = li - STEREO_BUCKET_BIAS;
    const int shift = bucket; // 1px per bucket step (signed)
    const uint8_t zval = (uint8_t)(bucket < 0 ? -bucket : bucket); // |bucket| in 0..7
    for (int y = 0; y < copy_h; ++y) {
        const uint8_t* src_row = stereo_layer_row(li, y);
        for (int x = 0; x < copy_w; ++x) {
            const uint8_t src_idx = src_row[x];
            if (src_idx == 0xFF) continue;
            const uint32_t rgb = palette_lut[draw_map[src_idx & 0x0F]];
            const uint8_t r = (rgb >> 16) & 0xFF;
            const uint8_t g = (rgb >> 8) & 0xFF;
            const uint8_t bb = rgb & 0xFF;
            const uint8_t ylum = (uint8_t)((77u * r + 150u * g + 29u * bb) >> 8);

            const int lx = x + shift;
            if ((unsigned)lx < 128u) {
                const int i = y * 128 + lx;
                if (zval >= z_left[i]) {
                    z_left[i] = zval;
                    const uint32_t cur = screen_buffer[i];
                    screen_buffer[i] = (cur & 0x0000FFFFu) | ((uint32_t)ylum << 16);
                }
            }

            const int rx = x - shift;
            if ((unsigned)rx < 128u) {
                const int i = y * 128 + rx;
                if (zval >= z_right[i]) {
                    z_right[i] = zval;
                    const uint32_t cur = screen_buffer[i];
                    screen_buffer[i] = (cur & 0x00FF0000u) | ((uint32_t)ylum << 8) | (uint32_t)ylum;
                }
            }
        }
    }
}

return; // Libretro frontend handles the actual flip
#else
// 2. Convert FB -> 32-bit XRGB (stereo disabled on GBA)
    const int copy_w = std::min(fb_w, 128);
    const int copy_h = std::min(fb_h, 128);
    std::memset(screen_buffer, 0, sizeof(screen_buffer));
    for (int y = 0; y < copy_h; y++) {
        const uint8_t* src_row = fb_row(y);
        uint32_t* dest_row = screen_buffer + (y * 128);
        for (int x = 0; x < copy_w; x++) {
            dest_row[x] = palette_lut[draw_map[src_row[x] & 0x0F]];
        }
    }
    return; // Libretro frontend handles the actual flip

#endif

    }
#endif

    // --------------------------------------------------------
    // STANDALONE / OTHER PATH (Existing Logic)
    // --------------------------------------------------------

    if (!host) return;

    if (ram && ram[BOTTOM_GPIO_ADDR] != bottom_screen_flags) {
        applyBottomScreenFlags(ram[BOTTOM_GPIO_ADDR]);
    }

    // If nothing drew since last present, don't re-upload/re-render.
    const bool bottom_active = supports_bottom_screen(this) && bottom_screen_enabled && fb_bottom &&
                               (!alt_fb || draw_target_bottom);
    const bool bottom_needs_present = bottom_active && bottom_dirty;
    if (dirty_x1 < 0 || dirty_y1 < 0) {
        if (!bottom_needs_present) return;
    }

    const uint8_t* top_buffer = alt_fb ? alt_fb : fb;
    const int top_w = alt_fb ? alt_fb_w : fb_w;
    const int top_h = alt_fb ? alt_fb_h : fb_h;
    const uint8_t* bottom_buffer = bottom_active ? fb_bottom : fb;
    const int bottom_w = bottom_active ? bottom_fb_w : fb_w;
    const int bottom_h = bottom_active ? bottom_fb_h : fb_h;
    const bool use_dual_present = (alt_fb != nullptr) || bottom_active;

    uint8_t final_palette[16];
    uint8_t *palette_map = nullptr;
    if (ram) {
        palette_map = ram + 0x5F10;
    } else {
        gpu.get_screen_palette(final_palette);
        palette_map = final_palette;
    }

    FramePresentDecision present_decision = FramePresentDecision::Present;
    if (host) {
        present_decision = host->decideFramePresent();
        if (present_decision == FramePresentDecision::Skip) return;
    }

    
#if !defined(__GBA__)
    uint8_t st_flags = 0;
    uint8_t st_mode = 3;
    int8_t st_depth = 0;
    int8_t st_conv = 0;
    if (ram) {
        st_flags = ram[0x5F80];
        st_mode = ram[0x5F81];
        st_depth = (int8_t)ram[0x5F82];
        st_conv = (int8_t)ram[0x5F83];
    }
    bool stereo_requested = false;
    if (st_mode == 3) {
        stereo_requested = stereoscopic;
    } else if (st_mode == 1 && (st_flags & 0x01)) {
        stereo_requested = true;
    }
    const bool stereo_active = (stereo_requested && !isShellUI && stereo_layers != nullptr);
    const bool swap_eyes = (st_flags & 0x02) != 0;

    int stereoPxPerLevel = 1;
    int convPxPerLevel = 1;
    if (host) {
        const char* platform = host->getPlatform();
        if (platform && std::strcmp(platform, "Switch") == 0) {
            stereoPxPerLevel = 2;
            convPxPerLevel = 2;
        }
    }

    int depthLevel = st_depth;
    if (st_mode == 3 && depthLevel == 0) depthLevel = 1;
    const int convPx = st_conv * convPxPerLevel;
    const int maxShift = std::abs(STEREO_BUCKET_MAX * depthLevel * stereoPxPerLevel) + std::abs(convPx);

    // 3DS host supports native stereoscopic output (separate left/right views).
    // When running on 3DS, let the host present from vm->stereo_layers directly
    // instead of building an anaglyph image here.
    if (stereo_active && host && std::strcmp(host->getPlatform(), "3DS") == 0) {
        const int kStereoMaxShift = maxShift;
        int sx0 = dirty_x0 - kStereoMaxShift; if (sx0 < 0) sx0 = 0;
        int sy0 = dirty_y0; if (sy0 < 0) sy0 = 0;
        int sx1 = dirty_x1 + kStereoMaxShift; if (sx1 > (fb_w - 1)) sx1 = fb_w - 1;
        int sy1 = dirty_y1; if (sy1 > (fb_h - 1)) sy1 = fb_h - 1;

        if (use_dual_present) {
            host->flipScreens(top_buffer, top_w, top_h, bottom_buffer, bottom_w, bottom_h, palette_map);
        } else {
            host->flipScreenDirty(fb, fb_w, fb_h, palette_map, sx0, sy0, sx1, sy1);
        }

        if (present_decision == FramePresentDecision::Present) {
            dirty_x0 = fb_w; dirty_y0 = fb_h; dirty_x1 = -1; dirty_y1 = -1;
            if (bottom_active) bottom_dirty = false;
        }
        return;
    }

    if (!stereo_active) {
        if (use_dual_present) {
            host->flipScreens(top_buffer, top_w, top_h, bottom_buffer, bottom_w, bottom_h, palette_map);
        } else {
            host->flipScreenDirty(fb, fb_w, fb_h, palette_map, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
        }
    } else {
        // Build a true-color anaglyph frame (0x00RRGGBB per pixel).
        // Bucket-to-pixel mapping: each depth bucket step = 1 pixel of disparity.
        const int kStereoMaxShift = maxShift;
        static std::vector<uint32_t> stereo_xrgb;
        static std::vector<uint8_t> z_left;
        static std::vector<uint8_t> z_right;
        const size_t stereo_pixels = (size_t)fb_w * (size_t)fb_h;
        if (stereo_xrgb.size() != stereo_pixels) {
            stereo_xrgb.assign(stereo_pixels, 0);
            z_left.assign(stereo_pixels, 0);
            z_right.assign(stereo_pixels, 0);
        } else {
            std::fill(stereo_xrgb.begin(), stereo_xrgb.end(), 0);
            std::fill(z_left.begin(), z_left.end(), 0);
            std::fill(z_right.begin(), z_right.end(), 0);
        }

        // Build draw_map from screen palette (0x5F10) for 0..15 -> 0..31.
        uint8_t draw_map[16];
        for (int i = 0; i < 16; ++i) {
            uint8_t col = palette_map[i];
            if (col >= 128 && col <= 143) draw_map[i] = 16 + (col - 128);
            else draw_map[i] = col & 0x1F;
        }

        // Use |bucket| as Z for occlusion so negative buckets are not overwritten by bucket 0.
        for (int li = 0; li < STEREO_LAYER_COUNT; ++li) {
            const int bucket = li - STEREO_BUCKET_BIAS;
            int shift = (bucket * depthLevel * stereoPxPerLevel) + convPx;
            if (swap_eyes) shift = -shift;
            const uint8_t zval = (uint8_t)(bucket < 0 ? -bucket : bucket); // |bucket| in 0..7
            for (int y = 0; y < fb_h; ++y) {
                const uint8_t* src_row = stereo_layer_row(li, y);
                for (int x = 0; x < fb_w; ++x) {
                    const uint8_t src_idx = src_row[x];
                    if (src_idx == 0xFF) continue;
                    const uint8_t pal32 = draw_map[src_idx & 0x0F];
                    const uint8_t* pc = Real8Gfx::PALETTE_RGB[pal32];
                    const uint8_t r = pc[0], g = pc[1], bb = pc[2];
                    const uint8_t ylum = (uint8_t)((77u * r + 150u * g + 29u * bb) >> 8);

                    const int lx = x + shift;
                    if ((unsigned)lx < (unsigned)fb_w) {
                        const size_t i = (size_t)y * (size_t)fb_w + (size_t)lx;
                        if (zval >= z_left[i]) {
                            z_left[i] = zval;
                            stereo_xrgb[i] = (stereo_xrgb[i] & 0x0000FFFFu) | ((uint32_t)ylum << 16);
                        }
                    }

                    const int rx = x - shift;
                    if ((unsigned)rx < (unsigned)fb_w) {
                        const size_t i = (size_t)y * (size_t)fb_w + (size_t)rx;
                        if (zval >= z_right[i]) {
                            z_right[i] = zval;
                            stereo_xrgb[i] = (stereo_xrgb[i] & 0x00FF0000u) | ((uint32_t)ylum << 8) | (uint32_t)ylum;
                        }
                    }
                }
            }
        }

        // Expand dirty rect to include disparity shifts.
        int sx0 = dirty_x0 - kStereoMaxShift; if (sx0 < 0) sx0 = 0;
        int sy0 = dirty_y0; if (sy0 < 0) sy0 = 0;
        int sx1 = dirty_x1 + kStereoMaxShift; if (sx1 > (fb_w - 1)) sx1 = fb_w - 1;
        int sy1 = dirty_y1; if (sy1 > (fb_h - 1)) sy1 = fb_h - 1;

        // Try true-color host present first (best quality).
        bool presented = false;
        presented = host->flipScreenRGBADirty(stereo_xrgb.data(), fb_w, fb_h, sx0, sy0, sx1, sy1);
        if (!presented) {
            presented = host->flipScreenRGBA(stereo_xrgb.data(), fb_w, fb_h);
        }

        if (!presented) {
            // Fallback: quantize into a 16-color palette so stereo still works on hosts without true-color support.
            static std::vector<uint8_t> stereo_fb;
            static uint8_t stereo_palmap[16];

            // Fixed 16-entry palette (indices into the 32-color pico palette) biased toward red/cyan + neutrals.
            const uint8_t fixed32[16] = {
                0, 2, 8, 14, 9, 10,  // blacks/reds/orange/yellow
                1, 12, 11, 13,       // blues/greens/light blue
                5, 6, 7, 15, 3, 4    // grays/white/pink/green/brown
            };
            for (int i = 0; i < 16; ++i) stereo_palmap[i] = fixed32[i];

            // Precompute palette RGB for distance match.
            uint8_t pr[16], pg[16], pb2[16];
            for (int i = 0; i < 16; ++i) {
                const uint8_t* c = Real8Gfx::PALETTE_RGB[fixed32[i] & 31];
                pr[i] = c[0]; pg[i] = c[1]; pb2[i] = c[2];
            }

            if (stereo_fb.size() != stereo_pixels) {
                stereo_fb.assign(stereo_pixels, 0);
            } else {
                std::fill(stereo_fb.begin(), stereo_fb.end(), 0);
            }

            for (int y = 0; y < fb_h; ++y) {
                for (int x = 0; x < fb_w; ++x) {
                    const uint32_t rgb = stereo_xrgb[(size_t)y * (size_t)fb_w + (size_t)x];
                    const int r = (rgb >> 16) & 0xFF;
                    const int g = (rgb >> 8) & 0xFF;
                    const int b = rgb & 0xFF;
                    int best = 0;
                    int bestd = 1 << 30;
                    for (int i = 0; i < 16; ++i) {
                        int dr = r - pr[i];
                        int dg = g - pg[i];
                        int db = b - pb2[i];
                        int d = dr*dr + dg*dg + db*db;
                        if (d < bestd) { bestd = d; best = i; }
                    }
                    stereo_fb[(size_t)y * (size_t)fb_w + (size_t)x] = (uint8_t)best;
                }
            }

            host->flipScreenDirty(stereo_fb.data(), fb_w, fb_h, stereo_palmap, sx0, sy0, sx1, sy1);
        }
    }
#else
    // GBA build: stereoscopic output disabled to save IWRAM.
    if (use_dual_present) {
        host->flipScreens(top_buffer, top_w, top_h, bottom_buffer, bottom_w, bottom_h, palette_map);
    } else {
        host->flipScreenDirty(fb, fb_w, fb_h, palette_map, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
    }
#endif
    if (present_decision == FramePresentDecision::Present) {
        if (bottom_active) bottom_dirty = false;
        dirty_x0 = fb_w; dirty_y0 = fb_h; dirty_x1 = -1; dirty_y1 = -1;
    }

}

void Real8VM::log(LogChannel ch, const char* fmt, ...)
{
    if (host) {
        va_list args;
        va_start(args, fmt);
        // Assuming host has a vlog method, or construct string buffer
        char buf[256];
        vsnprintf(buf, 256, fmt, args);
        host->log("%s", buf);
        va_end(args);
    }
}

bool IWRAM_INPUT_CODE Real8VM::btn(int i, int p)
{
    if (p < 0 || p > 7) return false;
    return (btn_states[p] & (1 << i)) != 0;
}

bool IWRAM_INPUT_CODE Real8VM::btnp(int i, int p) {
    if (i < 0 || i > 5 || p < 0 || p > 7) return false;
    int c = btn_counters[p][i];
    if (c == 1) return true;
    if (c > 15 && ((c - 15) % 4 == 0)) return true;
    return false;
}

// --------------------------------------------------------------------------
// PERSISTENCE & MODS
// --------------------------------------------------------------------------

void Real8VM::saveState()
{
    if (currentGameId.empty() || !L) return;
    std::string fname = "/" + currentGameId + ".sav";
    static std::vector<uint8_t> saveBuffer;
    saveBuffer.resize(0x8000);
    if (ram) {
        memcpy(saveBuffer.data(), ram, 0x8000);
    } else {
        memset(saveBuffer.data(), 0, 0x8000);
    }

    lua_getglobal(L, "_p8_save_state");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
            size_t len = 0; const char *str = lua_tolstring(L, -1, &len);
            if (str && len > 0) {
                uint32_t len32 = (uint32_t)len;
                saveBuffer.resize(0x8000 + sizeof(len32) + len);
                uint8_t *dst = saveBuffer.data() + 0x8000;
                memcpy(dst, &len32, sizeof(len32));
                memcpy(dst + sizeof(len32), str, len);
            }
            lua_pop(L, 1);
        } else lua_pop(L, 1);
    } else lua_pop(L, 1);
    host->saveState(fname.c_str(), saveBuffer.data(), saveBuffer.size());
}

bool Real8VM::loadState()
{
    if (currentGameId.empty() || !L) return false;
    std::string fname = "/" + currentGameId + ".sav";
    std::vector<uint8_t> data = host->loadState(fname.c_str());
    if (data.size() < 0x8000) return false;

    if (ram) {
        memcpy(ram, data.data(), 0x8000);
        applyVideoMode(ram[0x5FE1], /*force=*/true);
        applyBottomVideoMode(ram[BOTTOM_VMODE_REQ_ADDR], /*force=*/true);
        applyBottomScreenFlags(ram[Real8VM::BOTTOM_GPIO_ADDR]);
        if (screen_ram) memcpy(screen_ram, &ram[0x6000], 0x2000);
        for (int i = 0; i < 0x2000; i++) screenByteToFB(i, ram[0x6000 + i]);
        for(int i=0; i<16; i++) { 
            gpu.pal(i, ram[0x5F00+i], 0); 
            gpu.pal(i, ram[0x5F10+i], 1); 
        }
        uint16_t trans = ram[0x5F5C] | (ram[0x5F5D] << 8);
        for(int i=0; i<16; i++) gpu.palt(i, (trans >> i) & 1);

        gpu.camera(ram[0x5F28] | (ram[0x5F29]<<8), ram[0x5F2A] | (ram[0x5F2B]<<8));
    }

    if (data.size() > 0x8000 + 4) {
        size_t offset = 0x8000;
        uint32_t len = 0;
        len |= data[offset + 0]; len |= (data[offset + 1] << 8);
        len |= (data[offset + 2] << 16); len |= (data[offset + 3] << 24);
        offset += 4;

        if (offset + len <= data.size()) {
            std::string fullLua((char *)&data[offset], len);
            lua_getglobal(L, "_p8_clear_state");
            if (lua_isfunction(L, -1)) { lua_pcall(L, 0, 0, 0); } else { lua_pop(L, 1); }

            std::string delimiter = "--|CHUNK|--";
            size_t pos = 0;
            while (true) {
                size_t found = fullLua.find(delimiter, pos);
                std::string chunk = (found == std::string::npos) ? fullLua.substr(pos) : fullLua.substr(pos, found - pos);
                if (!chunk.empty()) {
                    lua_getglobal(L, "_p8_load_chunk");
                    if (lua_isfunction(L, -1)) {
                        lua_pushlstring(L, chunk.c_str(), chunk.length());
                        lua_pcall(L, 1, 0, 0);
                    } else lua_pop(L, 1);
                }
                if (found == std::string::npos) break;
                pos = found + delimiter.length();
            }

            lua_getglobal(L, "_p8_apply_state");
            if (lua_isfunction(L, -1)) { lua_pcall(L, 0, 0, 0); } else { lua_pop(L, 1); }
        }
    }
    return true;
}

bool Real8VM::hasState()
{
    if (currentGameId.empty()) return false;
    std::string fname = "/" + currentGameId + ".sav";
    return host->hasSaveState(fname.c_str());
}

// --------------------------------------------------------------------------
// MENU & SETTINGS
// --------------------------------------------------------------------------

void Real8VM::set_menu_item(int index, const char *label, int lua_ref)
{
    if (index < 1 || index > 5) return;
    if (custom_menu_items[index].lua_ref != LUA_NOREF && custom_menu_items[index].lua_ref != -1) {
        luaL_unref(L, LUA_REGISTRYINDEX, custom_menu_items[index].lua_ref);
    }
    if (label == nullptr) {
        custom_menu_items[index].active = false;
        custom_menu_items[index].lua_ref = LUA_NOREF;
        custom_menu_items[index].label = "";
    } else {
        custom_menu_items[index].active = true;
        custom_menu_items[index].lua_ref = lua_ref;
        custom_menu_items[index].label = std::string(label);
    }
}

void Real8VM::clear_menu_items()
{
    for (int i = 1; i <= 5; i++) set_menu_item(i, nullptr, LUA_NOREF);
}

void Real8VM::run_menu_item(int index)
{
    if (index < 1 || index > 5) return;
    int ref = custom_menu_items[index].lua_ref;
    if (ref != LUA_NOREF && ref != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) lua_pop(L, 1);
        } else lua_pop(L, 1);
    }
}

// In real8_vm.cpp

// --------------------------------------------------------------------------
// LIBRETRO SERIALIZATION
// --------------------------------------------------------------------------

#if !defined(__GBA__)
size_t Real8VM::getStateSize() {
    // RAM (32k) + Cart Data (256b) + Audio State (approx ~200-300 bytes)
    // We add sizeof(AudioStateSnapshot) to ensure exact sizing.
    return 0x8000 + sizeof(cart_data_ram) + sizeof(AudioStateSnapshot); 
}

bool Real8VM::serialize(void* data, size_t size) {
    if (!ram || size < getStateSize()) return false;
    
    uint8_t* ptr = (uint8_t*)data;
    
    // 1. Copy Main RAM
    memcpy(ptr, ram, 0x8000); 
    ptr += 0x8000;
    
    // 2. Copy Cart Data
    memcpy(ptr, cart_data_ram, sizeof(cart_data_ram)); 
    ptr += sizeof(cart_data_ram);

    // 3. Copy Audio State (CRITICAL FIX)
    AudioStateSnapshot audioState = audio.getState();
    memcpy(ptr, &audioState, sizeof(AudioStateSnapshot));
    
    return true;
}

bool Real8VM::unserialize(const void* data, size_t size) {
    if (!ram || size < getStateSize()) return false;

    const uint8_t* ptr = (const uint8_t*)data;

    // 1. Restore Main RAM
    memcpy(ram, ptr, 0x8000); 
    ptr += 0x8000;
    applyVideoMode(ram[0x5FE1], /*force=*/true);
    applyBottomVideoMode(ram[BOTTOM_VMODE_REQ_ADDR], /*force=*/true);
    applyBottomScreenFlags(ram[Real8VM::BOTTOM_GPIO_ADDR]);

    // 2. Restore Cart Data
    memcpy(cart_data_ram, ptr, sizeof(cart_data_ram)); 
    ptr += sizeof(cart_data_ram);

    // 3. Restore Audio State
    AudioStateSnapshot audioState;
    memcpy(&audioState, ptr, sizeof(AudioStateSnapshot));
    audio.setState(audioState);
    ptr += sizeof(AudioStateSnapshot);

    // 4. Sync Hardware State from RAM (GPU, Camera, Palettes)
    // (Existing sync logic follows here...)
    for(int i=0; i<16; i++) { gpu.pal(i, ram[0x5F00+i], 0); }
    for(int i=0; i<16; i++) { gpu.pal(i, ram[0x5F10+i], 1); }
    
    uint16_t trans = ram[0x5F5C] | (ram[0x5F5D] << 8);
    for(int i=0; i<16; i++) gpu.palt(i, (trans >> i) & 1);

    int16_t cam_x = (int16_t)(ram[0x5F28] | (ram[0x5F29] << 8));
    int16_t cam_y = (int16_t)(ram[0x5F2A] | (ram[0x5F2B] << 8));
    gpu.camera(cam_x, cam_y);

    uint8_t cx = ram[0x5F20];
    uint8_t cy = ram[0x5F21];
    uint8_t cw = ram[0x5F22] - cx;
    uint8_t ch = ram[0x5F23] - cy;
    gpu.clip(cx, cy, cw, ch);
    
    mark_dirty_rect(0, 0, 127, 127);
    return true;
}
#endif
