#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "real8_vm.h"
#include "real8_compression.h"
#include "real8_bindings.h"
#include "real8_fonts.h"
#include "real8_shell.h"
#include "real8_tools.h"
#include <lodePNG.h> 

#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

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
    
    return 1;
}

// --------------------------------------------------------------------------
// LIFECYCLE
// --------------------------------------------------------------------------

Real8VM::Real8VM(IReal8Host *h) : host(h), debug(this), gpu(this)
{
    L = nullptr;
    ram = nullptr;
    rom = nullptr;
    fb = nullptr;

    dirty_x0 = WIDTH; dirty_y0 = HEIGHT;
    dirty_x1 = 0; dirty_y1 = 0;
    
    memset(screen_buffer, 0, sizeof(screen_buffer));
    updatePaletteLUT();

    gpu.init();
    initDefaultPalette();
    
    volume_music = 7;
    volume_sfx = 10;
    
    audio.init(this);
    crt_filter = false;
    showSkin = false;
    
    std::string fileUrl = host->getRepoUrlFromFile();
    if (!fileUrl.empty()) {
        currentRepoUrl = fileUrl;
    } else {
        currentRepoUrl = IReal8Host::DEFAULT_GAMES_REPOSITORY;
    }

    init_wavetables();
    Real8Tools::LoadSettings(this, host);

}

Real8VM::~Real8VM()
{
    if (L) { lua_close(L); L = nullptr; }
    if (fb) { P8_FREE(fb); fb = nullptr; }
    if (ram) { P8_FREE(ram); ram = nullptr; }
    if (rom) { P8_FREE(rom); rom = nullptr; }
}

bool Real8VM::initMemory()
{
    // 1. Allocate Master RAM (32KB)
    if (!ram) {
        ram = (uint8_t *)calloc(1, 0x8000); 
        if (!ram) return false;
    }

    if (!rom) {
        rom = (uint8_t *)calloc(1, 0x8000);
        if (!rom) return false;
    }

    // 2. Setup Aliases
    gfx = (uint8_t (*)[128])(ram + 0x0000);
    map_data = (uint8_t (*)[128])(ram + 0x2000);
    sprite_flags = ram + 0x3000;
    music_ram = ram + 0x3100;
    sfx_ram = ram + 0x3200;
    user_data = ram + 0x4300;
    screen_ram = ram + 0x6000;

    // 3. Framebuffer
    if (!fb) {
        fb = (uint8_t (*)[RAW_WIDTH])calloc(RAW_WIDTH * HEIGHT, 1);
        if (!fb) return false;
    }

    dirty_x0 = 0; dirty_y0 = 0;
    dirty_x1 = 127; dirty_y1 = 127;
    return true;
}

void Real8VM::rebootVM()
{
    host->log("[VM] Rebooting...");

    if (!next_cart_path.empty()) {
        if (currentCartPath.empty()) currentCartPath = next_cart_path;
        std::string sourcePath = currentCartPath.empty() ? next_cart_path : currentCartPath;
        size_t lastSlash = sourcePath.find_last_of("/\\");
        currentGameId = (lastSlash == std::string::npos) ? sourcePath : sourcePath.substr(lastSlash + 1);
    }
    
    targetFPS = 30;
    patchModActive = false;
    
    // Reset Lua
    if (L) { lua_close(L); L = nullptr; }
    L = luaL_newstate();
    
    reset_requested = false;
    next_cart_path = "";

    // Run lib loading + API registration in protected calls.
    if (L) {
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
        }
    }

    if (L) {
        lua_pushlightuserdata(L, (void*)this);
        lua_setglobal(L, "__pico8_vm_ptr");
        lua_sethook(L, Real8Debugger::luaHook, LUA_MASKLINE, 0);

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
    if (fb) memset(fb, 0, WIDTH * HEIGHT);
    if (rom) memset(rom, 0, 0x8000);
    memset(custom_font, 0, 0x800);
    clear_menu_items();

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
    
    resetInputState();

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
}

void Real8VM::forceExit()
{
    if (debug.paused) debug.forceExit();
    saveCartData();
    for (int i = 0; i < 4; i++) {
        audio.channels[i].sfx_id = -1;
        audio.channels[i].current_vol = 0;
    }
    gpu.pal_reset();
    gpu.fillp(0);
    gpu.draw_mask = 0;
    if (ram) ram[0x5F5E] = 0;
    gpu.camera(0, 0);
    gpu.clip(0, 0, WIDTH, HEIGHT);
    
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
    bool isLibretro = (host && strcmp(host->getPlatform(), "Libretro") == 0);

    // Debugger Paused?
    if (debug.paused && !debug.step_mode) {
        show_frame(); 
        audio.update(host);
        return; 
    }

    // --------------------------------------------------------------------------
    // FRAME TIMING & SKIPPING
    // --------------------------------------------------------------------------
    // We determine early if this frame should actually execute Lua logic.
    static int tick_counter = 0;
    tick_counter++;
    bool is60FPS = (targetFPS == 60);
    bool shouldRunLua = is60FPS || (tick_counter % 2 == 0);

    if (skip_update_logic) {
        skip_update_logic = false; 
        shouldRunLua = false;
    }

    // If we are skipping this frame (30fps simulation), we must still maintain audio
    // but we do NOT process input counters to prevent desync with Lua logic.
    if (!shouldRunLua) {
        if (isLibretro) {
            // FIX: Calculate samples based on actual rate (22050 / 60 = 367.5)
            int samples_needed = (AudioEngine::SAMPLE_RATE / 60) + 1; 
            if (samples_needed > 2048) samples_needed = 2048;
            audio.generateSamples(static_audio_buffer, samples_needed);
            if (host) host->pushAudio(static_audio_buffer, samples_needed);
        } else {
            audio.update(host);
        }
        frame_is_dirty = false; // MARK FRAME AS CLEAN
        return;
    }

    frame_is_dirty = true; // MARK FRAME AS DIRTY

    // --------------------------------------------------------------------------
    // INPUT PROCESSING (Synchronized with Logic Frame)
    // --------------------------------------------------------------------------
    
    if (host) {
        // 1. Poll Events: 
        // Windows needs explicit polling. Libretro handles it externally.
        if (!isLibretro) {
            host->pollInput();
        }

        // 2. Fetch State:
        for (int i = 0; i < 8; i++) {
            last_btn_states[i] = btn_states[i]; // Track history
            btn_states[i] = host->getPlayerInput(i);
        }
    }

    // Update Internal Counters (btnp support)
    // This MUST happen here so counters increment only once per logic frame.
    for (int p = 0; p < 8; p++) {
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

    // --------------------------------------------------------------------------
    // FPS MONITORING
    // --------------------------------------------------------------------------
    static unsigned long last_fps_time = 0;
    static int fps_counter = 0;
    
    if (host) {
        unsigned long now = host->getMillis();
        if (last_fps_time == 0) last_fps_time = now;

        fps_counter++;

        if (now - last_fps_time >= 1000) {
            debugFPS = fps_counter;
            fps_counter = 0;
            last_fps_time = now;
        }
    }

    // --------------------------------------------------------------------------
    // LUA EXECUTION
    // --------------------------------------------------------------------------
    lua_pushcfunction(L, traceback);
    int errHandler = lua_gettop(L);

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
    lua_getglobal(L, "_update60");
    if (lua_isfunction(L, -1)) {
        if (!run_protected(0)) return;
    } else {
        lua_pop(L, 1);
        lua_getglobal(L, "_update");
        if (lua_isfunction(L, -1)) {
            if (!run_protected(0)) return; 
        } else {
            lua_pop(L, 1);
        }
    }

    // Debug Logs
    static int debug_log_timer = 0;
    if (showStats && ++debug_log_timer > 60) { 
        debug_log_timer = 0;
        host->log("[GFX] CAM:%d,%d CLIP:%d,%d PEN:%d MASK:%02X FPS:%d",
                  gpu.cam_x, gpu.cam_y, gpu.clip_x, gpu.clip_y, gpu.getPen(), gpu.draw_mask, debugFPS);
    }

    // _draw
    lua_getglobal(L, "_draw");
    if (lua_isfunction(L, -1)) {
        run_protected(0);
    } else {
        lua_pop(L, 1);
    }

    lua_pop(L, 1); // Pop traceback

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
            gpu.camera(0, 0); gpu.clip(0, 0, WIDTH, HEIGHT);
            int y0 = 128 - 7;
            gpu.rectfill(0, y0, 32, 127, 0); 
            std::string s = "FPS:" + std::to_string(debugFPS);
            gpu.pprint(s.c_str(), s.length(), 1, y0 + 1, 11);
            gpu.camera(bk_cx, bk_cy); gpu.setPen(bk_pen);
        }
    }

    // Update Audio (Normal Path)
    if (isLibretro) {
        int samples_needed = (AudioEngine::SAMPLE_RATE / 60) + 1; 
        if (samples_needed > 2048) samples_needed = 2048;
        audio.generateSamples(static_audio_buffer, samples_needed);
        if (host) host->pushAudio(static_audio_buffer, samples_needed);
    } 
    else {
        audio.update(host);
    }

    mouse_wheel_event = 0;
}

/*
bool Real8VM::loadGame(const GameData& game)
{
    rebootVM();

    // 3DS can get very tight on heap during cart loads (PNG decode + parse + Lua compile).
    // If Lua state creation fails, gracefully abort instead of crashing when calling Lua API.
    if (!L) {
        if (host) host->log("[VM] ERROR: Failed to create Lua state (out of memory?)");
        return false;
    }

    if (ram) {
        memcpy(ram + 0x0000, game.gfx, 0x2000);
        memcpy(ram + 0x2000, game.map, 0x1000);
        memcpy(ram + 0x3000, game.sprite_flags, 0x100);
        memcpy(ram + 0x3100, game.music, 0x100);
        memcpy(ram + 0x3200, game.sfx, 0x1100);
        if (rom) memcpy(rom, ram, 0x8000);
        gpu.pal_reset(); 
    }

    // 4. Audio & Lua Setup
    if (host) host->pushAudio(nullptr, 0);

    auto applyMods = [&]() {
        if (!host) return;
        std::string modCartPath;
        if (!currentCartPath.empty()) modCartPath = currentCartPath;
        else if (!game.cart_id.empty()) modCartPath = game.cart_id;
        else modCartPath = currentGameId;
        Real8Tools::ApplyMods(this, host, modCartPath);
    };

    if (!game.lua_code.empty()) {
        debug.setSource(game.lua_code);
        
        if (luaL_loadbuffer(L, game.lua_code.c_str(), game.lua_code.length(), "cart") != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            host->log("[VM] Lua Parse Error: %s", err);
            lua_pop(L, 1);
            return false;
        }

        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            if (err && strstr(err, "HALT")) { lua_pop(L, 1); return true; }
            host->log("[VM] Lua Runtime Error: %s", err);
            lua_pop(L, 1);
            return false;
        }

        applyMods();

        lua_getglobal(L, "_init");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                host->log("[VM] Error in _init: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    } else {
        applyMods();
    }

    detectCartFPS();
    
    // Mark the entire screen as dirty so the host renders the first frame immediately
    mark_dirty_rect(0, 0, 128, 128);
    
    return true;
}
*/

bool Real8VM::loadGame(const GameData& game)
{
    clearLastError();
    rebootVM();

    if (!L) {
        setLastError("VM INIT", "Failed to create Lua state (OOM or init failure)");
        return false;
    }

    if (ram) {
        memcpy(ram + 0x0000, game.gfx, 0x2000);
        memcpy(ram + 0x2000, game.map, 0x1000);
        memcpy(ram + 0x3000, game.sprite_flags, 0x100);
        memcpy(ram + 0x3100, game.music, 0x100);
        memcpy(ram + 0x3200, game.sfx, 0x1100);
        if (rom) memcpy(rom, ram, 0x8000);
        gpu.pal_reset(); 
    }

    if (host) host->pushAudio(nullptr, 0);

    auto applyMods = [&]() {
        if (!host) return;
        std::string modCartPath;
        if (!currentCartPath.empty()) modCartPath = currentCartPath;
        else if (!game.cart_id.empty()) modCartPath = game.cart_id;
        else modCartPath = currentGameId;
        Real8Tools::ApplyMods(this, host, modCartPath);
    };

    // Install traceback handler for better error messages
    lua_pushcfunction(L, traceback);
    int errHandler = lua_gettop(L);

    if (!game.lua_code.empty()) {
        debug.setSource(game.lua_code);

        if (luaL_loadbuffer(L, game.lua_code.c_str(), game.lua_code.length(), "cart") != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            setLastError("LUA PARSE", "%s", err ? err : "(unknown parse error)");
            lua_pop(L, 2); // error + traceback
            return false;
        }

        // pcall with traceback
        if (lua_pcall(L, 0, 0, errHandler) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (err && strstr(err, "HALT")) { lua_pop(L, 2); return true; }
            setLastError("LUA RUNTIME", "%s", err ? err : "(unknown runtime error)");
            lua_pop(L, 2); // error + traceback
            return false;
        }

        applyMods();

        // _init (make it fatal so you see it immediately)
        lua_getglobal(L, "_init");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, errHandler) != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                setLastError("_INIT ERROR", "%s", err ? err : "(unknown _init error)");
                lua_pop(L, 2); // error + traceback
                return false;
            }
        } else {
            lua_pop(L, 1);
        }
    } else {
        applyMods();
    }

    lua_pop(L, 1); // pop traceback handler

    detectCartFPS();
    mark_dirty_rect(0, 0, 128, 128);
    return true;
}

static inline int p8_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void Real8VM::detectCartFPS()
{
    if (!L) return;
    lua_getglobal(L, "_update60");
    if (lua_isfunction(L, -1)) targetFPS = 60;
    else targetFPS = 30;
    lua_pop(L, 1);
}

// --------------------------------------------------------------------------
// MEMORY & PIXEL ACCESS
// --------------------------------------------------------------------------

void Real8VM::screenByteToFB(size_t idx, uint8_t v)
{
    if (idx >= 0x2000) return;
    int y = idx >> 6;
    int x = (idx & 0x3F) << 1;
    
    if (fb) {
        fb[y][x] = v & 0x0F;
        fb[y][x + 1] = (v >> 4) & 0x0F;
    }
    mark_dirty_rect(x, y, x + 1, y);
}

void Real8VM::mark_dirty_rect(int x0, int y0, int x1, int y1)
{
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

bool Real8VM::map_check_flag(int x, int y, int w, int h, int flag)
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
void Real8VM::updatePaletteLUT() {
    for(int i=0; i<32; i++) {
        const uint8_t* c = Real8Gfx::PALETTE_RGB[i];
        // Libretro usually expects 0x00RRGGBB (XRGB8888)
        // Ensure this matches RETRO_PIXEL_FORMAT_XRGB8888
        palette_lut[i] = (c[0] << 16) | (c[1] << 8) | c[2];
    }
}

void Real8VM::show_frame()
{
    // --------------------------------------------------------
    // LIBRETRO OPTIMIZED PATH
    // --------------------------------------------------------
    if (host && strcmp(host->getPlatform(), "Libretro") == 0) {
        
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

        // 2. Fast Blit (8-bit FB -> 32-bit XRGB)
        uint32_t* dest = screen_buffer;
        
        for (int y = 0; y < 128; y++) {
            const uint8_t* src_row = fb[y];
            
            // Unrolling this loop helps slightly, but compiler usually handles it.
            // We map: Pixel(0-15) -> DrawMap -> PaletteLUT(RGBA)
            for (int x = 0; x < 128; x++) {
                *dest++ = palette_lut[draw_map[src_row[x] & 0x0F]];
            }
        }
        return; // Libretro frontend handles the actual flip
    }

    // --------------------------------------------------------
    // STANDALONE / OTHER PATH (Existing Logic)
    // --------------------------------------------------------

    if (!host) return;

    // If nothing drew since last present, don’t re-upload/re-render.
    if (dirty_x1 < 0 || dirty_y1 < 0) {
        return;
    }

    uint8_t final_palette[16];
    if (ram) for (int i=0;i<16;i++) final_palette[i] = ram[0x5F10+i];
    else gpu.get_screen_palette(final_palette);

    if (alt_fb) host->flipScreens(alt_fb, fb, final_palette);
    else        host->flipScreen(fb, final_palette);

    dirty_x0 = WIDTH; dirty_y0 = HEIGHT; dirty_x1 = -1; dirty_y1 = -1;

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

bool Real8VM::btn(int i, int p)
{
    if (p < 0 || p > 7) return false;
    return (btn_states[p] & (1 << i)) != 0;
}

bool Real8VM::btnp(int i, int p) {
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
    std::vector<uint8_t> saveBuffer; saveBuffer.resize(0x8000);
    if (ram) memcpy(saveBuffer.data(), ram, 0x8000);

    lua_getglobal(L, "_p8_save_state");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
            size_t len = 0; const char *str = lua_tolstring(L, -1, &len);
            if (str && len > 0) {
                uint32_t len32 = (uint32_t)len;
                uint8_t *pLen = (uint8_t *)&len32;
                for(int i=0; i<4; i++) saveBuffer.push_back(pLen[i]);
                saveBuffer.insert(saveBuffer.end(), str, str + len);
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
