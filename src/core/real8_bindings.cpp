#include "real8_bindings.h"
#include "real8_vm.h"
#include "real8_gfx.h"
#include "real8_fonts.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <string>
#include <ctime>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <unordered_map>

#include "../../lib/z8lua/trigtables.h"

// z8lua is based on Lua 5.2, which lacks the Lua 5.3 function 'lua_isyieldable'.
// We define it here to always return 1 (true), allowing the VM to yield.
#ifndef lua_isyieldable
#define lua_isyieldable(L) (1)
#endif

// --- CONFIGURATION ---
#define ENABLE_GAME_LOGS 1 // Set to 1 to enable logs, 0 to disable
// ---------------------

#define REAL8_TRACE_API(name) real8_set_last_api_call(name)

int debug_spr_count = 0;
int debug_print_count = 0;
int debug_cls_count = 0;

// CONSTANT: Tau is 2*PI
static const float TAU = 6.28318530718f;

#if defined(__GBA__)
#define REAL8_EWRAM __attribute__((section(".ewram")))
#else
#define REAL8_EWRAM
#endif

#if defined(__GBA__)
#define IWRAM_CODE __attribute__((section(".iwram"), long_call))
#else
#define IWRAM_CODE
#endif

#ifndef REAL8_GBA_IWRAM_BINDINGS
#define REAL8_GBA_IWRAM_BINDINGS 1
#endif

#if REAL8_GBA_IWRAM_BINDINGS
#define IWRAM_BINDINGS_CODE IWRAM_CODE
#else
#define IWRAM_BINDINGS_CODE
#endif
static Real8VM *g_vm = nullptr;

static constexpr int kTrigLutSize = 1024;
static constexpr int kTrigLutMask = kTrigLutSize - 1;
static constexpr int kTrigLutQuarter = kTrigLutSize / 4;
static int16_t g_sinLut[kTrigLutSize] REAL8_EWRAM;
static bool g_trigLutReady = false;

static void initTrigLut()
{
    if (g_trigLutReady)
        return;
    const float step = TAU / (float)kTrigLutSize;
    for (int i = 0; i < kTrigLutSize; ++i)
    {
        float angle = (float)i * step;
        g_sinLut[i] = (int16_t)(sinf(angle) * 32767.0f);
    }
    g_trigLutReady = true;
}

static inline int trig_lut_index(lua_Number a)
{
    uint32_t frac = (uint32_t)a.bits() & 0xffff;
    return (int)((frac * kTrigLutSize) >> 16) & kTrigLutMask;
}

static void reg(lua_State *L, const char *n, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_setglobal(L, n);
}

// Helper to fetch cached VM pointer
static inline Real8VM *get_vm(lua_State *L)
{
    (void)L;
    return g_vm;
}

// Platform-agnostic millis()
static unsigned long l_millis(lua_State *L)
{
    auto *vm = get_vm(L);
    if (vm && vm->host)
        return vm->host->getMillis();
    return 0;
}

static unsigned long start_ms = 0;

// Helper to sync RAM changes back to internal VM caches/registers
static void vm_sync_ram(Real8VM *vm, uint32_t start_addr, int length);

// Forward declaration for helper used before definition
static uint8_t read_mapped_byte(Real8VM *vm, uint32_t addr);

// Fast conversion used by most API calls (avoid heavy math on GBA)
static inline int to_int_floor(lua_State *L, int idx)
{
    if (lua_isboolean(L, idx)) {
        return lua_toboolean(L, idx) ? 1 : 0;
    }
    lua_Number v = lua_tonumber(L, idx);
    int32_t bits = v.bits();
    int32_t i = bits >> 16;
    if (bits < 0 && (bits & 0xffff)) {
        --i;
    }
    return (int)i;
}

static inline int to_int(lua_State *L, int idx)
{
    return (int)lua_tointeger(L, idx);
}

static int l_stat(lua_State *L)
{
    auto *vm = get_vm(L);
    int id = to_int_floor(L, 1);
    bool devkit_enabled = false;
    bool ptr_lock = false;
    if (vm && vm->ram) {
        uint8_t flags = vm->ram[0x5F2D];
        devkit_enabled = (flags & 0x01) != 0;
        ptr_lock = (flags & 0x04) != 0;
    }

    switch (id)
    {
    // --- MEMORY & CPU ---
    case 0: // Memory Usage (KB)
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
        int kbytes = lua_gc(L, LUA_GCCOUNT, 0);
        int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
        lua_pushnumber(L, (double)kbytes + ((double)bytes / 1024.0));
        return 1;
    }
    case 1: // CPU Usage (0.0 - 1.0)
    {
        float budget_ms = (vm && vm->targetFPS > 30) ? 16.666f : 33.333f;
        float cpu_usage = (vm && vm->debugFrameMS > 0 && budget_ms > 0.0f) ? (vm->debugFrameMS / budget_ms) : 0.0f;
        lua_pushnumber(L, cpu_usage);
        return 1;
    }
    case 2: // System CPU (stub)
        lua_pushnumber(L, 0);
        return 1;
    case 3: // Current display
        lua_pushinteger(L, 0);
        return 1;

    // --- DISPLAY & SYSTEM ---
    case 4: // Clipboard
    {
        if (vm && vm->host) {
            std::string clip = vm->host->getClipboardText();
            lua_pushstring(L, clip.c_str());
        } else {
            lua_pushstring(L, "");
        }
        return 1;
    }
    case 5: // PICO-8 Version
        lua_pushnumber(L, 41); // 41 corresponds to 0.2.5g approximately
        return 1;
    case 6: // Parameter String (cmdline args)
        // Fixed: Added L arg
        lua_pushstring(L, vm ? vm->param_str.c_str() : ""); 
        return 1;
    case 7: // Current FPS
        lua_pushnumber(L, vm ? vm->displayFPS : 0);
        return 1;
    case 8: // Target FPS
        // Fixed: target_fps -> targetFPS, added L arg
        lua_pushnumber(L, (vm && vm->targetFPS > 30) ? 60 : 30);
        return 1;
    case 9: // PICO-8 App FPS
        lua_pushnumber(L, vm ? vm->debugFPS : 0);
        return 1;
    case 10: // Unknown
        lua_pushinteger(L, 0);
        return 1;
    case 11: // Number of displays
        lua_pushinteger(L, 1);
        return 1;
    case 12: // Pause menu rectangle x0
    case 13: // Pause menu rectangle y0
    case 14: // Pause menu rectangle x1
    case 15: // Pause menu rectangle y1
        lua_pushinteger(L, 0);
        return 1;

    // --- AUDIO INFO (16-26) ---
#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    case 16: // SFX index on Channel 0
    case 17: // SFX index on Channel 1
    case 18: // SFX index on Channel 2
    case 19: // SFX index on Channel 3
    {
        int ch = id - 16;
        int sfx = (vm && vm->host) ? vm->audio.get_sfx_id(ch) : -1; 
        lua_pushinteger(L, sfx);
        return 1;
    }

    case 20: // Note (Pitch) on Channel 0
    case 21: // Note (Pitch) on Channel 1
    case 22: // Note (Pitch) on Channel 2
    case 23: // Note (Pitch) on Channel 3
    {
        int ch = id - 20;
        int note = (vm && vm->host) ? vm->audio.get_note(ch) : -1;
        lua_pushinteger(L, note);
        return 1;
    }

    case 24: // Current Music Pattern (-1 if stopped)
    {
        lua_pushinteger(L, vm ? vm->audio.get_music_pattern() : -1);
        return 1;
    }
    case 25: // Total Patterns Played (since last music())
    {
        lua_pushinteger(L, vm ? vm->audio.get_music_patterns_played() : 0);
        return 1;
    }
    case 26: // Ticks Played on Current Pattern
    {
        lua_pushinteger(L, vm ? vm->audio.get_music_ticks_on_pattern() : 0);
        return 1;
    }
#endif
    // --- INPUT ---
    case 28: // Raw keyboard (SDL scancode)
    {
        if (lua_gettop(L) < 2) {
            lua_pushboolean(L, 0);
            return 1;
        }
        int scan_code = to_int_floor(L, 2);
        bool down = (vm && vm->host) ? vm->host->isKeyDownScancode(scan_code) : false;
        lua_pushboolean(L, down);
        return 1;
    }
    case 29: // Controller count (fixed-point)
    {
        int count = vm ? vm->controller_count : 0;
        lua_pushnumber(L, (double)count / 65536.0);
        return 1;
    }
    case 30: // Key pressed? (Boolean logic often used here)
    {
        if (!devkit_enabled) {
            lua_pushboolean(L, 0);
            return 1;
        }
        bool has_key = vm && !vm->key_queue.empty();
        lua_pushboolean(L, has_key);
        return 1;
    }

    case 31: // String input characters
    {
        if (!devkit_enabled || !vm || vm->key_queue.empty()) {
            lua_pushnil(L);
            return 1;
        }
        std::string key = vm->key_queue.front();
        vm->key_queue.pop_front();
        lua_pushlstring(L, key.c_str(), key.size());
        lua_pushinteger(L, 0);
        return 2;
    }
    case 32: // Mouse X
        lua_pushinteger(L, (vm && devkit_enabled) ? vm->mouse_x : 0);
        return 1;
    case 33: // Mouse Y
        lua_pushinteger(L, (vm && devkit_enabled) ? vm->mouse_y : 0);
        return 1;
    case 34: // Mouse Buttons (Bitmask: 1=Left, 2=Right, 4=Middle)
        lua_pushinteger(L, (vm && devkit_enabled) ? vm->mouse_buttons : 0);
        return 1;
    case 35: // Mouse horizontal wheel
        lua_pushinteger(L, 0);
        return 1;
    case 36: // Mouse Wheel
    {
        int event = (vm && devkit_enabled) ? vm->mouse_wheel_event : 0;
        if (event > 0) event = 1;
        else if (event < 0) event = -1;
        lua_pushinteger(L, event);
        return 1;
    }
    case 37: // Mouse wheel (unused)
        lua_pushinteger(L, 0);
        return 1;
    case 38: // Mouse relative X
        lua_pushinteger(L, (vm && devkit_enabled && ptr_lock) ? vm->mouse_rel_x : 0);
        return 1;
    case 39: // Mouse relative Y
        lua_pushinteger(L, (vm && devkit_enabled && ptr_lock) ? vm->mouse_rel_y : 0);
        return 1;

#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    // --- AUDIO INFO (46-57) ---
    case 46:
    case 47:
    case 48:
    case 49:
    {
        int ch = id - 46;
        int sfx = (vm && vm->host) ? vm->audio.get_sfx_id_hp(ch) : -1; 
        lua_pushinteger(L, sfx);
        return 1;
    }

    case 50:
    case 51:
    case 52:
    case 53:
    {
        int ch = id - 50;
        int row = (vm && vm->host) ? vm->audio.get_note_row_hp(ch) : -1;
        lua_pushinteger(L, row);
        return 1;
    }

    case 54: // Current Music Pattern
        lua_pushinteger(L, vm ? vm->audio.get_music_pattern_hp() : -1);
        return 1;
    case 55: // Patterns Played since last music()
        lua_pushinteger(L, vm ? vm->audio.get_music_patterns_played_hp() : 0);
        return 1;
    case 56: // Ticks Played on Current Pattern
        lua_pushinteger(L, vm ? vm->audio.get_music_ticks_on_pattern_hp() : 0);
        return 1;
    case 57: // Music Playing?
        lua_pushboolean(L, vm && vm->audio.is_music_playing());
        return 1;
#endif
    // --- RTC (Real Time Clock) ---
    case 80: // Year
    case 81: // Month
    case 82: // Day
    case 83: // Hour
    case 84: // Minute
    case 85: // Second
    case 90: // System Year
    case 91: // System Month
    case 92: // System Day
    case 93: // System Hour
    case 94: // System Minute
    case 95: // System Second
    {
        time_t t = time(NULL);
        struct tm *tm = (id >= 90) ? localtime(&t) : gmtime(&t);
        
        int val = 0;
        int local_id = (id >= 90) ? (id - 10) : id;

        switch(local_id) {
            case 80: val = tm->tm_year + 1900; break;
            case 81: val = tm->tm_mon + 1; break; 
            case 82: val = tm->tm_mday; break;
            case 83: val = tm->tm_hour; break;
            case 84: val = tm->tm_min; break;
            case 85: val = tm->tm_sec; break;
        }
        lua_pushinteger(L, val);
        return 1;
    }

    // --- METADATA ---
    case 99: // Raw GC count (bytes)
    {
        int kbytes = lua_gc(L, LUA_GCCOUNT, 0);
        int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
        lua_pushnumber(L, (double)kbytes * 1024.0 + (double)bytes);
        return 1;
    }
    case 100: // Breadcrumb label (Current cart filename/label)
        if (vm && !vm->currentGameId.empty()) {
            lua_pushstring(L, vm->currentGameId.c_str()); 
        } else {
            lua_pushnil(L);
        }
        return 1;
    case 101: // BBS cart id
        lua_pushinteger(L, 0);
        return 1;
    case 102: // Site / environment
        lua_pushinteger(L, 0);
        return 1;
    case 108: // PCM buffer info (stub)
    case 109: // PCM buffer info (stub)
        lua_pushinteger(L, 0);
        return 1;
    case 110: // Frame-by-frame mode flag
        #if !defined(__GBA__)
        lua_pushboolean(L, vm && vm->debug.step_mode);
        #endif
        return 1;
    case 120: // Bytestream availability
    case 121: // Bytestream availability
        lua_pushboolean(L, 0);
        return 1;
    case 124: // Current path
        lua_pushstring(L, vm ? vm->currentCartPath.c_str() : "");
        return 1;

    // --- REAL-8 EXTENSIONS ---
    case 140: // Active REAL-8 mode index
        lua_pushinteger(L, vm ? vm->r8_vmode_cur : 0);
        return 1;
    case 141: // Raw requested mode byte (debug)
        lua_pushinteger(L, (vm && vm->ram) ? vm->ram[0x5FE1] : 0);
        return 1;
#if defined(__GBA__)
    case 142:
    case 143:
    case 144:
    case 145:
    case 146:
    case 147:
        lua_pushnumber(L, 0);
        return 1;
    case 148:
    case 149:
        lua_pushinteger(L, 0);
        return 1;
#else
    case 142: // Accel X (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.accel_x) : lua_Number(0));
        return 1;
    case 143: // Accel Y (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.accel_y) : lua_Number(0));
        return 1;
    case 144: // Accel Z (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.accel_z) : lua_Number(0));
        return 1;
    case 145: // Gyro X (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.gyro_x) : lua_Number(0));
        return 1;
    case 146: // Gyro Y (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.gyro_y) : lua_Number(0));
        return 1;
    case 147: // Gyro Z (Q16.16)
        lua_pushnumber(L, vm ? lua_Number::frombits(vm->motion.gyro_z) : lua_Number(0));
        return 1;
    case 148: // Sensor flags bitmask
        lua_pushinteger(L, vm ? (int)vm->motion.flags : 0);
        return 1;
    case 149: // dt_us since last sample
        lua_pushinteger(L, vm ? (int)vm->motion.dt_us : 0);
        return 1;
#endif
    case 150: // Framebuffer width
        lua_pushinteger(L, vm ? vm->fb_w : 0);
        return 1;
    case 151: // Framebuffer height
        lua_pushinteger(L, vm ? vm->fb_h : 0);
        return 1;
    case 152: // Bottom framebuffer width (3DS)
        if (vm && vm->host && vm->host->getPlatform() &&
            std::strcmp(vm->host->getPlatform(), "3DS") == 0) {
            lua_pushinteger(L, vm->bottom_fb_w);
        } else {
            lua_pushinteger(L, 0);
        }
        return 1;
    case 153: // Bottom framebuffer height (3DS)
        if (vm && vm->host && vm->host->getPlatform() &&
            std::strcmp(vm->host->getPlatform(), "3DS") == 0) {
            lua_pushinteger(L, vm->bottom_fb_h);
        } else {
            lua_pushinteger(L, 0);
        }
        return 1;
    case 154: // Current host platform
        lua_pushstring(L, (vm && vm->host && vm->host->getPlatform()) ? vm->host->getPlatform() : "");
        return 1;

    default:
        lua_pushnumber(L, 0);
        return 1;
    }
}

// Helper: Is this character valid in a Lua identifier?
static bool is_ident_char(char c) {
    return isalnum(c) || c == '_';
}

// Robust PICO-8 Transpiler (Only handles @, %, $)
std::string transpile_pico8_memory_ops(const std::string& src) {
    std::string out;
    out.reserve(src.size() * 1.2);

    enum State { CODE, STRING_S, STRING_D, COMMENT_LINE, COMMENT_BLOCK };
    State state = CODE;
    
    // Track if the previous token was an "Operand" (Number, String, Ident, closing bracket)
    // This helps us distinguish between % (Modulo) and % (Peek2)
    bool last_was_operand = false;

    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char next = (i + 1 < src.size()) ? src[i+1] : 0;

        // --- State Machine Update (Strings/Comments) ---
        if (state == CODE) {
            if (c == '\'') { state = STRING_S; out += c; last_was_operand = true; continue; }
            if (c == '"')  { state = STRING_D; out += c; last_was_operand = true; continue; }
            if (c == '-' && next == '-') {
                out += c; // Keep comment start
                if (i + 3 < src.size() && src[i+2] == '[' && src[i+3] == '[') state = COMMENT_BLOCK;
                else state = COMMENT_LINE;
                continue;
            }
            // Note: llex.c handles // comments, so we don't strictly need to hide them, 
            // but hiding them prevents accidental replacement inside comments.
            if (c == '/' && next == '/') {
                state = COMMENT_LINE;
                out += c; continue;
            }
        }
        else {
            // Inside String/Comment: Just copy and check for exit
            out += c;
            if (state == COMMENT_LINE && c == '\n') state = CODE;
            else if (state == COMMENT_BLOCK && c == ']' && next == ']') state = CODE;
            else if (state == STRING_S && c == '\'') state = CODE;
            else if (state == STRING_D && c == '"') state = CODE;
            continue;
        }

        // --- Transpilation Logic (CODE State only) ---
        
        // Skip whitespace, don't change last_was_operand
        if (isspace(c)) {
            out += c;
            continue;
        }

        // 1. Handle Memory Operators
        bool is_peek  = (c == '@');
        bool is_peek4 = (c == '$');
        // % is Peek2 ONLY if it is NOT an infix operator (i.e. previous token was NOT an operand)
        bool is_peek2 = (c == '%' && !last_was_operand);

        if (is_peek || is_peek2 || is_peek4) {
            std::string func = is_peek ? "peek" : (is_peek2 ? "peek2" : "peek4");
            
            // Output: peek(
            out += func + "(";
            
            // Look ahead to grab the address (Number or Identifier)
            size_t j = i + 1;
            while (j < src.size() && isspace(src[j])) j++; // Skip space
            
            // Simple scanner: consume 0x..., numbers, or identifiers
            // PICO-8 allows: @0x5f00, @my_var
            size_t start_arg = j;
            while (j < src.size() && (is_ident_char(src[j]) || src[j] == '.')) {
                j++;
            }
            
            if (j > start_arg) {
                // Append argument + closing paren
                out += src.substr(start_arg, j - start_arg);
                out += ")";
                i = j - 1; // Advance main loop
                last_was_operand = true; // Result of peek() is an operand
            } else {
                // Parse error or stray symbol? Just output original char
                out += c;
                last_was_operand = false; 
            }
            continue;
        }

        // 2. Track Token Type (Operand vs Operator)
        if (is_ident_char(c)) {
            last_was_operand = true;
        } else if (c == ')' || c == ']') {
            last_was_operand = true;
        } else {
            // Operators (+, -, *, =, etc) and open brackets ({, [) reset this
            last_was_operand = false;
        }

        out += c;
    }
    return out;
}

// Helper to check if a char is valid for a PICO-8 identifier
static bool is_ident(char c) {
    return isalnum(c) || c == '_';
}

std::string transpile_pico8(const std::string& src) {
    std::string out;
    out.reserve(src.size() * 1.5); // Pre-allocate to avoid reallocations

    enum State { CODE, STRING_S, STRING_D, COMMENT_LINE, COMMENT_BLOCK };
    State state = CODE;
    
    // For string tracking
    bool escape = false; 

    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char next = (i + 1 < src.size()) ? src[i+1] : 0;
        char prev = (i > 0) ? src[i-1] : 0;

        // --- 1. STATE MANAGEMENT ---
        if (state == CODE) {
            if (c == '\'') { state = STRING_S; out += c; continue; }
            if (c == '"')  { state = STRING_D; out += c; continue; }
            if (c == '-' && next == '-') {
                // Check for block comment --[[
                if (i + 3 < src.size() && src[i+2] == '[' && src[i+3] == '[') {
                    state = COMMENT_BLOCK;
                } else {
                    state = COMMENT_LINE;
                }
                out += c; continue;
            }
            // PICO-8 supports C-style comments //
            if (c == '/' && next == '/') {
                state = COMMENT_LINE;
                out += "--"; // Convert to Lua comment
                i++; // Skip next /
                continue;
            }
        } 
        else if (state == STRING_S || state == STRING_D) {
            out += c;
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if ((state == STRING_S && c == '\'') || (state == STRING_D && c == '"')) {
                state = CODE;
            }
            continue;
        } 
        else if (state == COMMENT_LINE) {
            out += c;
            if (c == '\n') state = CODE;
            continue;
        }
        else if (state == COMMENT_BLOCK) {
            out += c;
            if (c == ']' && next == ']') {
                state = CODE;
                out += next;
                i++;
            }
            continue;
        }

        // --- 2. SYNTAX REPLACEMENT (Only happens in CODE state) ---

        // A. Handle != -> ~=
        if (c == '!' && next == '=') {
            out += "~=";
            i++;
            continue;
        }

        // B. Handle Shorthand Print "?"
        // Logic: If we see '?' at start of line or after whitespace, convert to print(
        // Note: Closing the parenthesis is hard without a full parser, but PICO-8 
        // usually treats ? "text" as valid until newline. 
        // For simple compatibility, map ? to print. 
        // (Robust solution requires parsing to EOL).
        if (c == '?') {
            out += "print";
            continue;
        }

        // C. Memory Operators (@, %, $)
        // Logic: Convert @addr to peek(addr)
        // This is a "Scanner" limitation: We assume the address is a simple alphanumeric token.
        // If the code is @(0x5000+offset), this simple logic fails.
        if (c == '@' || c == '%' || c == '$') {
            std::string func = (c == '@') ? "peek" : (c == '%') ? "peek2" : "peek4";
            
            out += func;
            out += "(";
            
            // Look ahead to capture the identifier/number
            size_t j = i + 1;
            // Skip whitespace
            while (j < src.size() && isspace(src[j])) j++;
            
            // Consume valid identifier chars (hex, alphanumeric)
            // Note: PICO-8 allows @0x5f00.
            bool has_content = false;
            while (j < src.size() && (is_ident(src[j]) || src[j] == '.')) {
                 out += src[j];
                 j++;
                 has_content = true;
            }
            
            if (!has_content) {
                // Was just a stray @, restore it (or handle error)
                out += c; 
            } else {
                out += ")";
                i = j - 1; // Advance main loop
            }
            continue;
        }

        // D. Bitwise Operators (Only if your z8lua build does not support standard 5.3 ops)
        // Note: z8lua usually patches the parser to handle this. 
        // If you MUST transpiles >>> to lshr(), you need a full parser (AST), 
        // not just a tokenizer, because you need to wrap the LEFT operand:
        // "a + b >>> 2" becomes "lshr(a+b, 2)". Text replacement cannot know "a+b" is a group.
        
        // Default: copy char
        out += c;
    }
    return out;
}

static int l_sin(lua_State *L)
{
    // z8lua does not strictly replace math functions unless using their lib.
    // We keep the inverted Y logic here.
    initTrigLut();
    lua_Number a = lua_tonumber(L, 1);
    int idx = trig_lut_index(a);
    int32_t bits = (int32_t)g_sinLut[idx] << 1;
    lua_pushnumber(L, lua_Number::frombits(-bits));
    return 1;
}

static int l_cos(lua_State *L)
{
    initTrigLut();
    lua_Number a = lua_tonumber(L, 1);
    int idx = (trig_lut_index(a) + kTrigLutQuarter) & kTrigLutMask;
    int32_t bits = (int32_t)g_sinLut[idx] << 1;
    lua_pushnumber(L, lua_Number::frombits(bits));
    return 1;
}

static inline lua_Number pico8_atan2_fixed(lua_Number x, lua_Number y)
{
    int32_t bits = 0x4000;
    if (x) {
        int64_t xb = x.bits();
        int64_t yb = y.bits();
        int64_t q = (std::abs(yb) << 16) / std::abs(xb);
        if (q > 0x10000) {
            bits -= atantable[(int64_t(1) << 32) / q >> 5];
        } else {
            bits = atantable[q >> 5];
        }
    }
    if (x.bits() < 0) bits = 0x8000 - bits;
    if (y.bits() > 0) bits = -bits & 0xffff;
    if (x && y.bits() == int32_t(0x80000000)) bits = -bits & 0xffff;
    return lua_Number::frombits(bits);
}

static int l_atan2(lua_State *L)
{
    lua_Number x = lua_tonumber(L, 1);
    lua_Number y = lua_tonumber(L, 2);
    lua_pushnumber(L, pico8_atan2_fixed(x, y));
    return 1;
}

// FAST PSET (Unsafe/Internal)
inline void fast_pset(Real8VM *vm, int x, int y, uint8_t c)
{
    if ((uint32_t)x < 128 && (uint32_t)y < 128)
    {
        // Assuming packed nibbles:
        // vm->screen_buffer[y][x/2] = ... bit manipulation
        // OR if you unpack to 1 byte per pixel for speed (recommended for ESP32):
        vm->screen_ram[y * 128 + x] = c;
    }
}

// We can use standard lua_tointeger to get the raw representation for bitwise logic
// if we assume standard PICO-8 behavior, but using the explicit conversion is safest
// to avoid Lua 5.4 integer/float mix-ups in the C-API.

static inline int32_t to_pico_fixed(lua_State *L, int idx)
{
    // z8lua treats 'true' as 1.0 (0x00010000) automatically in math,
    // but explicit checking is safe.
    if (lua_isboolean(L, idx))
        return lua_toboolean(L, idx) ? 65536 : 0;

    lua_Number v = lua_tonumber(L, idx);
    return v.bits();
}

// Added boolean check. PICO-8 allows bitwise ops on booleans (True=1, False=0)
static inline uint32_t l_mask(lua_State *L, int idx)
{
    if (lua_isboolean(L, idx))
    {
        return lua_toboolean(L, idx) ? 1 : 0;
    }
    lua_Number v = luaL_optnumber(L, idx, (lua_Number)0);
    return (uint32_t)v.bits();
}

// Helper to push raw 16.16 bits back as Lua number
static inline void push_pico_fixed(lua_State *L, int32_t v)
{
    lua_pushnumber(L, lua_Number::frombits(v));
}

static int l_band(lua_State *L)
{
    // PICO-8/z8lua allows multiple args: band(x, y, z...)
    int argc = lua_gettop(L);
    uint32_t v = (uint32_t)to_pico_fixed(L, 1);
    for (int i = 2; i <= argc; ++i)
    {
        v &= (uint32_t)to_pico_fixed(L, i);
    }
    push_pico_fixed(L, (int32_t)v);
    return 1;
}

static int l_bor(lua_State *L)
{
    int argc = lua_gettop(L);
    uint32_t v = (uint32_t)to_pico_fixed(L, 1);
    for (int i = 2; i <= argc; ++i)
    {
        v |= (uint32_t)to_pico_fixed(L, i);
    }
    push_pico_fixed(L, (int32_t)v);
    return 1;
}

static int l_bxor(lua_State *L)
{
    int argc = lua_gettop(L);
    uint32_t v = (uint32_t)to_pico_fixed(L, 1);
    for (int i = 2; i <= argc; ++i)
    {
        v ^= (uint32_t)to_pico_fixed(L, i);
    }
    push_pico_fixed(L, (int32_t)v);
    return 1;
}

static int l_shl(lua_State *L)
{
    int32_t v = to_pico_fixed(L, 1);
    int bits = to_int_floor(L, 2);
    // PICO-8 logic shift
    uint32_t out = (uint32_t)v << (bits & 31);
    push_pico_fixed(L, (int32_t)out);
    return 1;
}

static int l_shr(lua_State *L)
{
    int32_t v = to_pico_fixed(L, 1);
    int bits = to_int_floor(L, 2);
    // Arithmetic shift (preserves sign)
    int32_t out = v >> (bits & 31);
    push_pico_fixed(L, out);
    return 1;
}

static int l_lshr(lua_State *L)
{
    uint32_t v = (uint32_t)to_pico_fixed(L, 1);
    int bits = to_int_floor(L, 2);
    // Logical shift (zero fill)
    uint32_t out = v >> (bits & 31);
    push_pico_fixed(L, (int32_t)out);
    return 1;
}

// A simple state machine is safer than regex for code transpilation
std::string robust_preprocess(const std::string& src) {
    std::string out;
    out.reserve(src.size() * 1.2);
    
    bool in_string = false;
    bool in_comment = false;
    char string_char = 0;

    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        
        // Handle Comments
        if (!in_string && !in_comment && c == '-' && i + 1 < src.size() && src[i+1] == '-') {
            in_comment = true;
        }
        if (in_comment && c == '\n') in_comment = false;

        // Handle Strings
        if (!in_comment) {
            if (!in_string && (c == '"' || c == '\'')) {
                in_string = true;
                string_char = c;
            } else if (in_string && c == string_char) {
                // Check for escaped quote logic here if needed
                in_string = false;
            }
        }

        // Apply Replacements ONLY if not in string/comment
        if (!in_string && !in_comment) {
            // Example: Change != to ~=
            if (c == '!' && i + 1 < src.size() && src[i+1] == '=') {
                out += "~=";
                i++; // Skip next char
                continue;
            }
            // Add your @, %, $ logic here looking ahead/behind
        }
        
        out += c;
    }
    return out;
}

static int l_flip(lua_State *L)
{
    auto *vm = get_vm(L);

    // 1. MEASURE CPU TIME (The Fix)
    // We need to know when the PREVIOUS flip finished (which is when Lua started running).
    static unsigned long last_flip_exit_time = 0;
    unsigned long now = l_millis(L);

    if (last_flip_exit_time != 0)
    {
        // The time spent OUTSIDE of flip() is the time spent in Lua (Update/Draw)
        vm->debugFrameMS = (float)(now - last_flip_exit_time);
    }
    else
    {
        vm->debugFrameMS = 0;
    }

    // --- EXISTING LOGIC STARTS HERE ---

    // Address 0x5F2C (24364) = 5 triggers Horizontal Mirroring (0-63 -> 127-64)
    if (vm->ram && vm->ram[0x5F2C] == 5 && vm->fb)
    {
        const int w = vm->fb_w;
        const int h = vm->fb_h;
        const int half = w / 2;
        for (int y = 0; y < h; y++)
        {
            uint8_t* row = vm->fb_row(y);
            for (int x = 0; x < half; x++)
            {
                row[w - 1 - x] = row[x];
            }
        }
    }

    // 1. Output Graphics
    vm->show_frame();

    // 2. Process Audio
    // if (vm->host) vm->audio.update(vm->host); <-- CAUSES DOUBLE SPEED AUDIO

    // 3. Sync Input (Loop through 8 players)
    if (vm->host)
    {
        // A. Poll hardware events once per frame
        vm->host->pollInput(); 

        // B. Update state for all 8 players
        for (int p = 0; p < 8; p++)
        {
            vm->last_btn_states[p] = vm->btn_states[p];
            // Fetch specific bitmask for player 'p'
            vm->btn_states[p] = vm->host->getPlayerInput(p);
        }
    }
    
    // 4. Update Internal VM State
    // "btn_state" (singular) usually refers to P1 for legacy code, 
    // but we generally rely on the array now.
    vm->last_btn_state = vm->btn_states[0];
    vm->btn_state = vm->btn_states[0];
    vm->btn_mask = vm->btn_states[0]; // Ensure this syncs for P1 shortcuts

    if (vm->ram)
    {
        for (int p = 0; p < 8; p++)
        {
            // 0x5F30 is P1, 0x5F31 is P2 ... 0x5F37 is P8
            // PICO-8 button states are the lower 6 bits (0x3F)
            vm->ram[0x5F30 + p] = (uint8_t)(vm->btn_states[p] & 0x3F);
        }
    }

    // EXIT CONDITION: Menu button (ID 6) on Player 1
    if (vm->btn(6, 0))
    {
        luaL_error(L, "HALT");
    }

    // FPS Control
    int target_ms = 33;
    if (vm && vm->targetFPS > 30)
        target_ms = 16;

    static unsigned long last_flip_time = 0;
    long elapsed = (long)(now - last_flip_time);
    long wait = target_ms - elapsed;

    if (wait > 0 && vm->host)
    {
        vm->host->delayMs((uint32_t)wait);
    }

    // Reset timer AFTER delay to maintain cadence
    last_flip_time = l_millis(L);

    return 0;
}

static int l_bnot(lua_State *L)
{
    uint32_t v = to_pico_fixed(L, 1);
    push_pico_fixed(L, ~v);
    return 1;
}

static int l_sqrt(lua_State *L)
{
    int64_t root = 0;
    int64_t x = int64_t(lua_tonumber(L, 1).bits()) << 16;
    if (x > 0) {
        for (int64_t a = int64_t(1) << 46; a; a >>= 2, root >>= 1) {
            if (x >= a + root) {
                x -= a + root;
                root += a << 1;
            }
        }
    }
    lua_pushnumber(L, lua_Number::frombits((int32_t)root));
    return 1;
}

// Use fmin/fmax to handle float/fixed comparisons correctly
static int l_min(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc < 1)
    {
        lua_pushnumber(L, 0);
        return 1;
    }
    double m = lua_tonumber(L, 1);
    for (int i = 2; i <= argc; ++i)
    {
        if (!lua_isnil(L, i))
        {
            double v = lua_tonumber(L, i);
            if (v < m)
                m = v;
        }
    }
    lua_pushnumber(L, m);
    return 1;
}

static int l_max(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc < 1)
    {
        lua_pushnumber(L, 0);
        return 1;
    }
    double m = lua_tonumber(L, 1);
    for (int i = 2; i <= argc; ++i)
    {
        if (!lua_isnil(L, i))
        {
            double v = lua_tonumber(L, i);
            if (v > m)
                m = v;
        }
    }
    lua_pushnumber(L, m);
    return 1;
}

static int l_time(lua_State *L)
{
    // Using platform agnostic millis
    unsigned long now = l_millis(L);
    double t = (double)(now - start_ms) / 1000.0;
    lua_pushnumber(L, t);
    return 1;
}

static int l_atan(lua_State *L)
{
    lua_Number x = lua_tonumber(L, 1);
    lua_Number t = pico8_atan2_fixed(lua_Number::frombits(0x10000), x);
    uint32_t bits = (uint32_t)t.bits() + 0x4000;
    bits &= 0xffff;
    lua_pushnumber(L, lua_Number::frombits((int32_t)bits));
    return 1;
}

// -------- Graphics
static int l_cls(lua_State *L)
{

    debug_cls_count++;

    auto *vm = get_vm(L);
    int c = lua_gettop(L) >= 1 ? to_int_floor(L, 1) : 0;
    vm->gpu.cls(c);
    // Explicitly reset cursor on CLS as per PICO-8 spec
    vm->gpu.setCursor(0, 0);
    return 0;
}
static int IWRAM_BINDINGS_CODE l_pset(lua_State *L)
{
    REAL8_TRACE_API("pset");
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
    {
        c = to_int_floor(L, 3) & 0x0F;
    }
    vm->gpu.pset(x, y, (uint8_t)c);
    return 0;
}
static int IWRAM_BINDINGS_CODE l_pget(lua_State *L)
{
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);

    // 1. Get Camera Position
    int cx = vm->gpu.cam_x;
    int cy = vm->gpu.cam_y;

    // 2. Apply Camera & Check Bounds
    // PICO-8 pget is relative to the camera.
    int rx = x + cx;
    int ry = y + cy;

    if ((uint32_t)rx > 127u || (uint32_t)ry > 127u)
    {
        lua_pushinteger(L, 0);
        return 1;
    }

    // 3. Read from Screen RAM (vm->screen_ram is the source of truth)
    uint8_t val = 0;

    // Calculate offset: 64 bytes per row (128 pixels / 2 pixels per byte)
    int offset = (ry * 64) + (rx >> 1);

    if (vm->screen_ram)
    {
        val = vm->screen_ram[offset];
    }
    else if (vm->ram)
    {
        // Fallback to main RAM if screen_ram is not separated
        val = vm->ram[0x6000 + offset];
    }

    // 4. Extract Nibble
    // PICO-8 Format: Even X = Low Nibble, Odd X = High Nibble
    int pixel = (rx & 1) ? (val >> 4) : (val & 0x0F);

    lua_pushinteger(L, pixel);
    return 1;
}
static int IWRAM_BINDINGS_CODE l_line(lua_State *L)
{
    REAL8_TRACE_API("line");
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);

    int x0, y0, x1, y1;
    int c_arg_idx = 5;

    if (argc <= 3)
    {
        // Continuation syntax: line(x1, y1, [col])
        x0 = vm->gpu.last_line_x;
        y0 = vm->gpu.last_line_y;
        x1 = to_int_floor(L, 1);
        y1 = to_int_floor(L, 2);
        c_arg_idx = 3;
    }
    else
    {
        // Standard syntax: line(x0, y0, x1, y1, [col])
        x0 = to_int_floor(L, 1);
        y0 = to_int_floor(L, 2);
        x1 = to_int_floor(L, 3);
        y1 = to_int_floor(L, 4);
    }

    int c = vm->gpu.getPen();
    if (argc >= c_arg_idx && !lua_isnil(L, c_arg_idx))
    {
        c = to_int_floor(L, c_arg_idx) & 0x0F;
    }

    vm->gpu.line(x0, y0, x1, y1, (uint8_t)c);

    // CRITICAL: Update state for next call
    vm->gpu.last_line_x = x1;
    vm->gpu.last_line_y = y1;

    return 0;
}
static int IWRAM_BINDINGS_CODE l_rectfill(lua_State *L)
{
    REAL8_TRACE_API("rectfill");
    auto *vm = get_vm(L);
    int x0 = to_int_floor(L, 1);
    int y0 = to_int_floor(L, 2);
    int x1 = to_int_floor(L, 3);
    int y1 = to_int_floor(L, 4);
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 5 && !lua_isnil(L, 5))
    {
        c = to_int_floor(L, 5) & 0x0F;
    }
    vm->gpu.rectfill(x0, y0, x1, y1, (uint8_t)c);
    return 0;
}
static int l_rect(lua_State *L)
{
    REAL8_TRACE_API("rect");
    auto *vm = get_vm(L);
    int x0 = to_int_floor(L, 1);
    int y0 = to_int_floor(L, 2);
    int x1 = to_int_floor(L, 3);
    int y1 = to_int_floor(L, 4);
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 5 && !lua_isnil(L, 5))
    {
        c = to_int_floor(L, 5) & 0x0F;
    }
    vm->gpu.rect(x0, y0, x1, y1, (uint8_t)c);
    return 0;
}
static int l_rrectfill(lua_State *L)
{
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);
    int w = to_int_floor(L, 3);
    int h = to_int_floor(L, 4);
    int r = (lua_gettop(L) >= 5 && !lua_isnil(L, 5)) ? to_int_floor(L, 5) : 0;
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 6 && !lua_isnil(L, 6))
    {
        c = to_int_floor(L, 6) & 0x0F;
    }
    vm->gpu.rrectfill(x, y, w, h, r, (uint8_t)c);
    return 0;
}
static int l_rrect(lua_State *L)
{
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);
    int w = to_int_floor(L, 3);
    int h = to_int_floor(L, 4);
    int r = (lua_gettop(L) >= 5 && !lua_isnil(L, 5)) ? to_int_floor(L, 5) : 0;
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 6 && !lua_isnil(L, 6))
    {
        c = to_int_floor(L, 6) & 0x0F;
    }
    vm->gpu.rrect(x, y, w, h, r, (uint8_t)c);
    return 0;
}

// --- P8SCII HELPERS ---

// --- UTF-8 -> P8SCII conversion ---
// PICO-8 source code is UTF-8 and can include icon glyphs such as the PICO-8 O / X button glyphs.
// Internally, the PICO-8 font expects single-byte P8SCII codes (0..255).
// This helper converts any recognised UTF-8 PICO-8 glyph sequences into
// their corresponding single-byte P8SCII value before the print() parser
// runs. Inspired by Zepto-8's emojiconversion.cpp, but implemented without
// <regex> / <codecvt> to stay lightweight (and GBA-friendly).

static inline int p8_utf8_cp_len(uint8_t lead)
{
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static const std::unordered_map<std::string, uint8_t> &p8_utf8_to_code_map()
{
    static std::unordered_map<std::string, uint8_t> m;
    static bool inited = false;

    if (!inited)
    {
        // The complete PICO-8 UTF-8 charmap, from 0 to 255.
        // NOTE: Some glyphs are a base emoji + U+FE0F variation selector (VS16),
        // which is encoded as bytes EF B8 8F in UTF-8.
        static char const utf8_chars[] =
        "\0¹²³⁴⁵⁶⁷⁸\t\nᵇᶜ\rᵉᶠ▮■□⁙⁘‖◀▶「」¥•、。゛゜"
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNO"
        "PQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~○"
        "█▒🐱⬇️░✽●♥☉웃⌂⬅️😐♪🅾️◆…➡️★⧗⬆️ˇ∧❎▤▥あいうえおか"
        "きくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよ"
        "らりるれろわをんっゃゅょアイウエオカキクケコサシスセソタチツテト"
        "ナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンッャュョ◜◝";

        auto const *bytes = (uint8_t const *)utf8_chars;
        size_t total = sizeof(utf8_chars) - 1; // includes the leading \0 in the table content
        size_t pos = 0;

        for (int code = 0; code < 256 && pos < total; ++code)
        {
            int l = p8_utf8_cp_len(bytes[pos]);
            if (l < 1) l = 1;

            // Merge U+FE0F variation selector with previous codepoint, if present
            if (pos + (size_t)l + 2 < total
                && bytes[pos + (size_t)l]     == 0xEF
                && bytes[pos + (size_t)l + 1] == 0xB8
                && bytes[pos + (size_t)l + 2] == 0x8F)
            {
                l += 3;
            }

            if (l > 1 && pos + (size_t)l <= total)
            {
                // Store only multibyte sequences; ASCII is passed through as-is.
                m.emplace(std::string((char const *)bytes + pos, (size_t)l), (uint8_t)code);
            }

            pos += (size_t)l;
        }

        inited = true;
    }

    return m;
}

static std::string p8_utf8_to_p8scii(char const *s, size_t len)
{
    auto const &map = p8_utf8_to_code_map();

    std::string out;
    out.reserve(len);

    size_t i = 0;
    while (i < len)
    {
        uint8_t b = (uint8_t)s[i];

        // ASCII and control bytes pass through untouched.
        if (b < 0x80u)
        {
            out.push_back((char)b);
            ++i;
            continue;
        }

        // Try to match one of PICO-8's multibyte UTF-8 glyph sequences.
        // Longest-first avoids partial matches (e.g. emoji + VS16).
        bool matched = false;
        for (int l = 7; l >= 2; --l)
        {
            if (i + (size_t)l > len) continue;
            auto it = map.find(std::string(s + i, (size_t)l));
            if (it != map.end())
            {
                out.push_back((char)it->second);
                i += (size_t)l;
                matched = true;
                break;
            }
        }

        // Unknown UTF-8: fall back to raw bytes.
        if (!matched)
        {
            out.push_back((char)b);
            ++i;
        }
    }

    return out;
}

std::string p8_normalize_lua_strings(const std::string& src)
{
    std::string out;
    out.reserve(src.size());

    enum State { CODE, STRING_S, STRING_D, COMMENT_LINE, COMMENT_BLOCK };
    State state = CODE;
    bool escape = false;
    char quote = 0;
    std::string str_buf;

    auto flush_string = [&]() {
        if (!str_buf.empty())
        {
            out += p8_utf8_to_p8scii(str_buf.c_str(), str_buf.size());
            str_buf.clear();
        }
    };

    for (size_t i = 0; i < src.size(); ++i)
    {
        char c = src[i];
        char next = (i + 1 < src.size()) ? src[i + 1] : 0;

        if (state == CODE)
        {
            if (c == '-' && next == '-')
            {
                out += c;
                out += next;
                i++;
                if (i + 2 < src.size() && src[i + 1] == '[' && src[i + 2] == '[')
                    state = COMMENT_BLOCK;
                else
                    state = COMMENT_LINE;
                continue;
            }
            if (c == '/' && next == '/')
            {
                out += c;
                out += next;
                i++;
                state = COMMENT_LINE;
                continue;
            }
            if (c == '\'' || c == '"')
            {
                state = (c == '\'') ? STRING_S : STRING_D;
                quote = c;
                escape = false;
                out += c;
                continue;
            }
            out += c;
            continue;
        }

        if (state == COMMENT_LINE)
        {
            out += c;
            if (c == '\n') state = CODE;
            continue;
        }
        if (state == COMMENT_BLOCK)
        {
            out += c;
            if (c == ']' && next == ']')
            {
                out += next;
                i++;
                state = CODE;
            }
            continue;
        }

        if (state == STRING_S || state == STRING_D)
        {
            if (escape)
            {
                str_buf += c;
                escape = false;
                continue;
            }
            if (c == '\\')
            {
                str_buf += c;
                escape = true;
                continue;
            }
            if (c == quote)
            {
                flush_string();
                out += c;
                state = CODE;
                continue;
            }
            str_buf += c;
            continue;
        }
    }

    if (state == STRING_S || state == STRING_D)
    {
        flush_string();
    }

    return out;
}


// Helper: Convert PICO-8 Hex char (0-9, a-f) to integer (0-15)
static int p8_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Helper: Convert PICO-8 param char (0-9, a-z) to integer (0-35)
static int p8_param_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 0;
}

static int p8_pow2_frames(int n) {
    if (n < 1) return 0;
    if (n > 9) n = 9;
    return 1 << (n - 1);
}

// Helper: Get character width (Standard PICO-8 is fixed 4, but good for future var-width)
static int p8_char_width(unsigned char c) {
    // Standard PICO-8 font is 3x5 pixels inside a 4x6 cell
    // Special wide chars could be handled here
    if (c >= 0x80) return 8; // Double width glyphs (kana/icons) often used in P8
    return 4;
}

static int l_print(lua_State *L)
{
    REAL8_TRACE_API("print");
    debug_print_count++;
    auto *vm = get_vm(L);
    
    size_t len;
    const char *str = luaL_optlstring(L, 1, "", &len);


    // Convert UTF-8 PICO-8 icon glyphs (e.g. O/X button glyphs, arrows) into single-byte P8SCII codes
    // so the renderer sees the expected character indices.
    std::string p8scii_converted;
    {
        bool has_high = false;
        for (size_t j = 0; j < len; ++j)
        {
            if (((uint8_t)str[j]) & 0x80u) { has_high = true; break; }
        }
        if (has_high && len > 0)
        {
            p8scii_converted = p8_utf8_to_p8scii(str, len);
            str = p8scii_converted.c_str();
            len = p8scii_converted.size();
        }
    }
    // 1. Determine Initial State (Cursor, Color)
    int argc = lua_gettop(L);
    int x = vm->gpu.getCursorX();
    int y = vm->gpu.getCursorY();
    int c = vm->gpu.getPen(); // Default to current pen

    // Parse arguments: print(str, [x, y], [col])
    // PICO-8 allows print(str, col) or print(str, x, y, col)
    if (argc == 2) {
        // print(str, col)
        c = to_int_floor(L, 2) & 0x0F;
    } 
    else if (argc >= 3) {
        // print(str, x, y, [col])
        x = to_int_floor(L, 2);
        y = to_int_floor(L, 3);
        if (argc >= 4) {
            c = to_int_floor(L, 4) & 0x0F;
        }
    }

    // 2. State Machine for P8SCII Parsing
    int cur_x = x;
    int cur_y = y;
    int cur_c = c;
    int cur_bg = -1; // -1 = Transparent
    int home_x = x;
    int home_y = y;
    int tab_stop = 4;
    int line_height = 0;
    int wrap_border = 128;
    int frames_between_chars = 0;
    int force_char_w = -1;
    int force_char_h = -1;
    int last_adv_w = 4;
    int last_char_x = x;
    int last_char_y = y;
    bool has_last_char = false;
    bool wrap_mode = true;
    bool wide_mode = false;
    bool tall_mode = false;
    bool stripey_mode = false;
    bool pinball_mode = false;
    bool invert_mode = false;
    bool padding_mode = false;
    bool solid_bg_mode = false;
    bool custom_font_mode = false;
    bool hit_null = false;

    auto draw_glyph = [&](const uint8_t *rows, int src_w, int src_h, int draw_x, int draw_y, int dst_w, int dst_h, uint8_t fg, int bg) {
        int bg_col = bg;
        if (solid_bg_mode && bg_col < 0) bg_col = 0;
        if (invert_mode) {
            int inv_bg = (bg_col >= 0) ? bg_col : 0;
            bg_col = fg;
            fg = inv_bg;
        }

        int pad = padding_mode ? 1 : 0;
        if (bg_col >= 0) {
            vm->gpu.rectfill(draw_x - pad, draw_y - pad, draw_x + dst_w - 1 + pad, draw_y + dst_h - 1 + pad, (uint8_t)bg_col);
        }

        for (int ty = 0; ty < dst_h; ++ty) {
            int sy = (ty * src_h) / std::max(1, dst_h);
            uint8_t row = rows[sy];
            for (int tx = 0; tx < dst_w; ++tx) {
                if (stripey_mode && (tx & 1)) continue;
                int sx = (tx * src_w) / std::max(1, dst_w);
                if (row & (0x80 >> sx)) {
                    vm->gpu.pset(draw_x + tx, draw_y + ty, fg);
                }
            }
        }
    };

    auto render_char = [&](uint8_t ch, int draw_x, int draw_y, int &out_adv_w, int &out_h) {
        int base_w = 4;
        int base_h = 6;
        int draw_off_x = 0;
        int draw_off_y = 0;

        if (custom_font_mode) {
            uint8_t *a = vm->cf_attr();
            int wdef = (ch < 128) ? a[0x000] : a[0x001];
            int h = a[0x002];
            int xo = (int8_t)a[0x003];
            int yo = (int8_t)a[0x004];
            if (h > 0) {
                int adj = 0;
                int yup = 0;
                if (ch >= 16) {
                    uint8_t nib = vm->cf_adj()[(ch - 16) >> 1];
                    nib = (ch & 1) ? (nib >> 4) : (nib & 0x0F);
                    static const int8_t map[8] = {0, 1, 2, 3, -4, -3, -2, -1};
                    adj = map[nib & 7];
                    if (nib & 8) yup = 1;
                }

                int offset = (int)ch * 8;
                if (offset + 8 <= 0x780) {
                    base_w = 8;
                    base_h = std::min(8, h);
                    draw_off_x = xo;
                    draw_off_y = yo + yup;
                    out_adv_w = std::max(0, wdef + adj);
                    if (out_adv_w == 0) out_adv_w = wdef;
                    if (force_char_w > 0) out_adv_w = force_char_w;

                    int target_w = (force_char_w > 0) ? force_char_w : base_w;
                    int target_h = (force_char_h > 0) ? force_char_h : base_h;
                    target_w = std::max(1, target_w);
                    target_h = std::max(1, target_h);
                    if (wide_mode || pinball_mode) target_w *= 2;
                    if (tall_mode || pinball_mode) target_h *= 2;

                    const uint8_t *rows = vm->cf_gfx() + offset;
                    draw_glyph(rows, 8, base_h, draw_x + draw_off_x, draw_y + draw_off_y, target_w, target_h, (uint8_t)cur_c, cur_bg);
                    out_h = target_h;
                    if (wide_mode || pinball_mode) out_adv_w *= 2;
                    return;
                }
            }
        }

        const uint8_t *rows = p8_4x6_bits(ch);
        int target_w = (force_char_w > 0) ? force_char_w : base_w;
        int target_h = (force_char_h > 0) ? force_char_h : base_h;
        target_w = std::max(1, target_w);
        target_h = std::max(1, target_h);
        if (wide_mode || pinball_mode) target_w *= 2;
        if (tall_mode || pinball_mode) target_h *= 2;

        out_adv_w = (force_char_w > 0) ? force_char_w : base_w;
        out_h = target_h;
        draw_glyph(rows, base_w, base_h, draw_x, draw_y, target_w, target_h, (uint8_t)cur_c, cur_bg);
        if (wide_mode || pinball_mode) out_adv_w *= 2;
    };

    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)str[i];

        // --- CONTROL CODES ---
        
        // \f (0x0c): Set Foreground Color
        if (b == 0x0c) { 
            if (i + 1 < len) {
                cur_c = p8_hex_val(str[++i]);
            }
            continue;
        }

        // \# (0x02): Set Background Color
        if (b == 0x02) { 
            if (i + 1 < len) {
                cur_bg = p8_hex_val(str[++i]);
            }
            continue;
        }

        // \* (0x01): Repeat Next Character P0 times
        if (b == 0x01) {
            if (i + 2 < len) {
                int times = p8_param_val(str[++i]);
                uint8_t ch = (uint8_t)str[++i];
                for (int t = 0; t < times; ++t) {
                    int adv = 0;
                    int h = 0;
                    last_char_x = cur_x;
                    last_char_y = cur_y;
                    render_char(ch, cur_x, cur_y, adv, h);
                    has_last_char = true;
                    last_adv_w = adv;
                    cur_x += adv;
                    line_height = std::max(line_height, h);
                    for (int f = 0; f < frames_between_chars; ++f) l_flip(L);
                }
            }
            continue;
        }

        // \- (0x03): Cursor X Offset
        if (b == 0x03) {
            if (i + 1 < len) {
                int delta = p8_param_val(str[++i]) - 16;
                cur_x += delta;
            }
            continue;
        }

        // \| (0x04): Cursor Y Offset
        if (b == 0x04) {
            if (i + 1 < len) {
                int delta = p8_param_val(str[++i]) - 16;
                cur_y += delta;
            }
            continue;
        }

        // \+ (0x05): Cursor XY Offset
        if (b == 0x05) {
            if (i + 2 < len) {
                int delta_x = p8_param_val(str[++i]) - 16;
                int delta_y = p8_param_val(str[++i]) - 16;
                cur_x += delta_x;
                cur_y += delta_y;
            }
            continue;
        }

        // \n (0x0a): Newline
        if (b == 0x0a) {
            cur_x = home_x;
            int lh = (line_height > 0) ? line_height : 6;
            cur_y += lh;
            line_height = 0;
            continue;
        }
        
        // \r (0x0d): Carriage Return
        if (b == 0x0d) {
            cur_x = home_x;
            continue;
        }

        // \t (0x09): Tab
        if (b == 0x09) {
            int tab_px = std::max(1, tab_stop) * 4;
            int rel = cur_x - home_x;
            cur_x = ((rel + tab_px) / tab_px) * tab_px + home_x;
            continue;
        }

        // \b (0x08): Backspace
        if (b == 0x08) {
            cur_x -= last_adv_w;
            continue;
        }

        // \v (0x0b): Decorate previous character
        if (b == 0x0b) {
            if (i + 2 < len && has_last_char) {
                int offset = p8_param_val(str[++i]);
                uint8_t ch = (uint8_t)str[++i];
                int x_off = (offset % 4) - 2;
                int y_off = (offset / 4) - 8;
                int adv = 0;
                int h = 0;
                render_char(ch, last_char_x + x_off, last_char_y + y_off, adv, h);
            }
            continue;
        }

        // \a (0x07): Audio command (consume until space)
        if (b == 0x07) {
            while (i + 1 < len && str[i + 1] != ' ') {
                ++i;
            }
            if (i + 1 < len && str[i + 1] == ' ') ++i;
            continue;
        }

        // \014 (0x0e): Switch to custom font
        if (b == 0x0e) {
            custom_font_mode = true;
            continue;
        }

        // \015 (0x0f): Switch back to default font
        if (b == 0x0f) {
            custom_font_mode = false;
            continue;
        }

        // \^ (0x06): Special Commands
        if (b == 0x06) {
            if (i + 1 < len) {
                char cmd = str[++i];
                if (cmd >= '1' && cmd <= '9') {
                    int frames = p8_pow2_frames(p8_param_val(cmd));
                    for (int f = 0; f < frames; ++f) l_flip(L);
                } else if (cmd == 'd' && i + 1 < len) {
                    frames_between_chars = p8_param_val(str[++i]);
                } else if (cmd == 'c' && i + 1 < len) {
                    int col = p8_param_val(str[++i]) & 0x0F;
                    vm->gpu.cls(col);
                    cur_x = 0;
                    cur_y = 0;
                    home_x = 0;
                    home_y = 0;
                    line_height = 0;
                } else if (cmd == 'g') {
                    cur_x = home_x;
                    cur_y = home_y;
                } else if (cmd == 'h') {
                    home_x = cur_x;
                    home_y = cur_y;
                } else if (cmd == 'j' && i + 2 < len) {
                    int x4 = p8_param_val(str[++i]) * 4;
                    int y4 = p8_param_val(str[++i]) * 4;
                    cur_x = x4;
                    cur_y = y4;
                } else if (cmd == 's' && i + 1 < len) {
                    tab_stop = p8_param_val(str[++i]);
                } else if (cmd == 'r' && i + 1 < len) {
                    wrap_border = p8_param_val(str[++i]) * 4;
                } else if (cmd == 'x' && i + 1 < len) {
                    force_char_w = p8_param_val(str[++i]);
                } else if (cmd == 'y' && i + 1 < len) {
                    force_char_h = p8_param_val(str[++i]);
                } else if (cmd == 'w') {
                    wide_mode = true;
                } else if (cmd == 't') {
                    tall_mode = true;
                } else if (cmd == '=') {
                    stripey_mode = true;
                } else if (cmd == 'p') {
                    pinball_mode = true;
                    wide_mode = true;
                    tall_mode = true;
                    stripey_mode = true;
                } else if (cmd == 'i') {
                    invert_mode = true;
                } else if (cmd == 'b') {
                    padding_mode = true;
                } else if (cmd == '#') {
                    solid_bg_mode = true;
                } else if (cmd == '$') {
                    wrap_mode = true;
                } else if (cmd == ':') {
                    if (i + 16 < len) {
                        uint8_t rows[8] = {0};
                        for (int r = 0; r < 8; ++r) {
                            char hi = str[++i];
                            char lo = str[++i];
                            rows[r] = (uint8_t)((p8_hex_val(hi) << 4) | p8_hex_val(lo));
                        }
                        int adv = (force_char_w > 0) ? force_char_w : 8;
                        int target_w = std::max(1, adv);
                        int target_h = (force_char_h > 0) ? force_char_h : 8;
                        target_h = std::max(1, target_h);
                        if (wide_mode || pinball_mode) target_w *= 2;
                        if (tall_mode || pinball_mode) target_h *= 2;
                        draw_glyph(rows, 8, 8, cur_x, cur_y, target_w, target_h, (uint8_t)cur_c, cur_bg);
                        cur_x += (wide_mode || pinball_mode) ? adv * 2 : adv;
                        line_height = std::max(line_height, target_h);
                        last_adv_w = (wide_mode || pinball_mode) ? adv * 2 : adv;
                        last_char_x = cur_x - last_adv_w;
                        last_char_y = cur_y;
                        has_last_char = true;
                        for (int f = 0; f < frames_between_chars; ++f) l_flip(L);
                    }
                } else if (cmd == '.') {
                    if (i + 8 < len) {
                        uint8_t rows[8] = {0};
                        for (int r = 0; r < 8; ++r) {
                            rows[r] = (uint8_t)str[++i];
                        }
                        int adv = (force_char_w > 0) ? force_char_w : 8;
                        int target_w = std::max(1, adv);
                        int target_h = (force_char_h > 0) ? force_char_h : 8;
                        target_h = std::max(1, target_h);
                        if (wide_mode || pinball_mode) target_w *= 2;
                        if (tall_mode || pinball_mode) target_h *= 2;
                        draw_glyph(rows, 8, 8, cur_x, cur_y, target_w, target_h, (uint8_t)cur_c, cur_bg);
                        cur_x += (wide_mode || pinball_mode) ? adv * 2 : adv;
                        line_height = std::max(line_height, target_h);
                        last_adv_w = (wide_mode || pinball_mode) ? adv * 2 : adv;
                        last_char_x = cur_x - last_adv_w;
                        last_char_y = cur_y;
                        has_last_char = true;
                        for (int f = 0; f < frames_between_chars; ++f) l_flip(L);
                    }
                } else if (cmd == '!') {
                    if (i + 4 < len && vm->ram) {
                        int addr = (p8_hex_val(str[++i]) << 12) |
                                   (p8_hex_val(str[++i]) << 8) |
                                   (p8_hex_val(str[++i]) << 4) |
                                   (p8_hex_val(str[++i]));
                        int remaining = (int)(len - (i + 1));
                        if (remaining > 0 && addr < 0x8000) {
                            int max_write = std::min(remaining, 0x8000 - addr);
                            std::memcpy(&vm->ram[addr], &str[i + 1], max_write);
                            vm_sync_ram(vm, addr, max_write);
                            i += max_write;
                        }
                    }
                } else if (cmd == '@') {
                    if (i + 8 < len && vm->ram) {
                        int addr = (p8_hex_val(str[++i]) << 12) |
                                   (p8_hex_val(str[++i]) << 8) |
                                   (p8_hex_val(str[++i]) << 4) |
                                   (p8_hex_val(str[++i]));
                        int size = (p8_hex_val(str[++i]) << 12) |
                                   (p8_hex_val(str[++i]) << 8) |
                                   (p8_hex_val(str[++i]) << 4) |
                                   (p8_hex_val(str[++i]));
                        int remaining = (int)(len - (i + 1));
                        if (remaining > 0 && size > 0 && addr < 0x8000) {
                            int max_write = std::min(size, remaining);
                            max_write = std::min(max_write, 0x8000 - addr);
                            if (max_write > 0) {
                                std::memcpy(&vm->ram[addr], &str[i + 1], max_write);
                                vm_sync_ram(vm, addr, max_write);
                                i += max_write;
                            }
                        }
                    }
                } else if (cmd == '-') {
                    if (i + 1 < len) {
                        char off = str[++i];
                        if (off == 'w') wide_mode = false;
                        else if (off == 't') tall_mode = false;
                        else if (off == '=') stripey_mode = false;
                        else if (off == 'p') { pinball_mode = false; wide_mode = false; tall_mode = false; stripey_mode = false; }
                        else if (off == 'i') invert_mode = false;
                        else if (off == 'b') padding_mode = false;
                        else if (off == '#') solid_bg_mode = false;
                        else if (off == '$') wrap_mode = false;
                    }
                }
            }
            continue;
        }

        // \- (0x00?): Terminator (PICO-8 strings often null-term, but lua strings have explicit len)
        if (b == 0) { hit_null = true; break; } 

        // --- DRAW CHARACTER ---
        int adv = 0;
        int h = 0;
        last_char_x = cur_x;
        last_char_y = cur_y;
        render_char(b, cur_x, cur_y, adv, h);
        has_last_char = true;
        last_adv_w = (adv > 0) ? adv : last_adv_w;
        cur_x += adv;
        line_height = std::max(line_height, h);

        for (int f = 0; f < frames_between_chars; ++f) l_flip(L);

        if (wrap_mode && wrap_border > 0 && cur_x >= wrap_border) {
            cur_x = home_x;
            int lh = (line_height > 0) ? line_height : 6;
            cur_y += lh;
            line_height = 0;
        }
    }

    // 3. Update Persistent Cursor
    // If explicit coordinates were passed, PICO-8 usually leaves cursor at end of string
    // If only (str) was passed, it does CRLF logic (handled by caller script usually, but we set end pos)
    if (argc >= 3) {
        vm->gpu.setCursor(cur_x, cur_y);
    } else {
        // Standard print(str) implies a newline at the end in some contexts, 
        // but strictly print() just sets the cursor to the next line start usually.
        // We align with PICO-8: cursor moves to start of next line + scroll handling (scrolling not impl here)
        if (hit_null) {
            vm->gpu.setCursor(cur_x, cur_y);
        } else {
            int lh = (line_height > 0) ? line_height : 6;
            vm->gpu.setCursor(0, cur_y + lh);
        }
    }

    // Return the final X position (undocumented but common in some Lua variants, PICO-8 returns nothing usually)
    // PICO-8 0.2.2+ print returns nothing.
    return 0;
}

static int l_mid(lua_State *L)
{
    double x = luaL_optnumber(L, 1, 0.0);
    double y = luaL_optnumber(L, 2, 0.0);
    double z = luaL_optnumber(L, 3, 0.0);
    lua_pushnumber(L, std::max(std::min(x, y), std::min(std::max(x, y), z)));
    return 1;
}

static int l_color(lua_State *L)
{
    auto *vm = get_vm(L);
    int c = to_int_floor(L, 1) & 0x0F;
    vm->gpu.setPen((uint8_t)c);
    return 0;
}

static int l_cursor(lua_State *L)
{
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);
    if (argc == 0)
    {
        vm->gpu.setCursor(0, 0);
        return 0;
    }
    int x = to_int_floor(L, 1), y = to_int_floor(L, 2);
    vm->gpu.setCursor(x, y);
    if (argc >= 3)
    {
        int c = to_int_floor(L, 3) & 0x0F;
        vm->gpu.setPen((uint8_t)c);
    }
    return 0;
}

static int l_peek(lua_State *L)
{
    REAL8_TRACE_API("peek");
    auto *vm = get_vm(L);
    int addr = to_int_floor(L, 1); // Use safe cast

    // Strict Bounds Check for PICO-8 32k RAM
    if (!vm || !vm->ram || addr < 0 || addr > 0x7FFF)
    {
        lua_pushinteger(L, 0);
        return 1;
    }

    uint8_t result = read_mapped_byte(vm, (uint32_t)addr);
    lua_pushinteger(L, result);
    return 1;
}

static void vm_sync_ram(Real8VM *vm, uint32_t start_addr, int length)
{
    if (!vm->ram)
        return;
    uint32_t end_addr = start_addr + length;

    // 1. GFX RAM (0x0000 - 0x1FFF)
    if (start_addr < 0x2000)
    {
        uint32_t s = start_addr;
        uint32_t e = (end_addr > 0x2000) ? 0x2000 : end_addr;
        for (uint32_t i = s; i < e; ++i)
        {
            uint8_t val = vm->ram[i];
            int base_idx = i * 2; // 2 pixels per byte
            int y = base_idx / 128;
            int x = base_idx % 128;
            if (y < 128)
            {
                vm->gfx[y][x] = val & 0x0F;
                vm->gfx[y][x + 1] = (val >> 4) & 0x0F;
            }
        }
    }

    // 2. Map Data (0x2000 - 0x2FFF)
    if (end_addr > 0x2000 && start_addr < 0x3000)
    {
        uint32_t s = (start_addr < 0x2000) ? 0x2000 : start_addr;
        uint32_t e = (end_addr > 0x3000) ? 0x3000 : end_addr;
        for (uint32_t i = s; i < e; ++i)
        {
            int offset = i - 0x2000;
            vm->map_data[offset / 128][offset % 128] = vm->ram[i];
        }
    }

    // 3. Sprite Flags (0x3000 - 0x30FF)
    if (end_addr > 0x3000 && start_addr < 0x3100)
    {
        uint32_t s = (start_addr < 0x3000) ? 0x3000 : start_addr;
        uint32_t e = (end_addr > 0x3100) ? 0x3100 : end_addr;
        for (uint32_t i = s; i < e; ++i)
        {
            vm->sprite_flags[i - 0x3000] = vm->ram[i];
        }
    }

    // 4. Draw State Registers (0x5F00 - 0x5F5F)
    if (end_addr > 0x5F00 && start_addr < 0x5F60)
    {
        // Memory Mapping Registers (0x5F54 - 0x5F57)
        if (end_addr > 0x5F54 && start_addr <= 0x5F57)
        {
            if (start_addr <= 0x5F54 && end_addr > 0x5F54)
                vm->hwState.spriteSheetMemMapping = vm->ram[0x5F54];
            if (start_addr <= 0x5F55 && end_addr > 0x5F55)
                vm->hwState.screenDataMemMapping = vm->ram[0x5F55];
            if (start_addr <= 0x5F56 && end_addr > 0x5F56)
                vm->hwState.mapMemMapping = vm->ram[0x5F56];
            if (start_addr <= 0x5F57 && end_addr > 0x5F57)
                vm->hwState.widthOfTheMap = vm->ram[0x5F57];
        }

        // Draw Palette (0x5F00 - 0x5F0F)
        for (int i = 0; i < 16; ++i)
        {
            if (start_addr <= (0x5F00 + i) && end_addr > (0x5F00 + i))
            {
                vm->gpu.pal(i, vm->ram[0x5F00 + i], 0);
            }
        }
        // Screen Palette (0x5F10 - 0x5F1F)
        for (int i = 0; i < 16; ++i)
        {
            if (start_addr <= (0x5F10 + i) && end_addr > (0x5F10 + i))
            {
                vm->gpu.pal(i, vm->ram[0x5F10 + i], 1);
            }
        }
        // Clip (0x5F20-0x5F23)
        if (end_addr > 0x5F20 && start_addr <= 0x5F23)
        {
            vm->gpu.clip(vm->ram[0x5F20], vm->ram[0x5F21], vm->ram[0x5F22] - vm->ram[0x5F20], vm->ram[0x5F23] - vm->ram[0x5F21]);
        }
        // Camera (0x5F28-0x5F2B)
        if (end_addr > 0x5F28 && start_addr <= 0x5F2B)
        {
            int intcx = vm->ram[0x5F28] | (vm->ram[0x5F29] << 8);
            int intcy = vm->ram[0x5F2A] | (vm->ram[0x5F2B] << 8);
            vm->gpu.camera(intcx, intcy);
        }

        // Transparency (0x5F5C - 0x5F5D)
        // Check if the write touches either the low OR high byte of the transparency mask
        if (end_addr > 0x5F5C && start_addr <= 0x5F5D)
        {
            // Reconstruct the full 16-bit mask. 
            // vm->ram[0x5F5C] controls colors 0-7, vm->ram[0x5F5D] controls colors 8-15.
            uint16_t mask = vm->ram[0x5F5C] | (vm->ram[0x5F5D] << 8);
            
            for (int c = 0; c < 16; c++)
            {
                // Update the internal VM state for every color
                vm->gpu.palt(c, (mask >> c) & 1);
            }
        }
        // Bitwise Draw Mask (0x5F5E)
        if (start_addr <= 0x5F5E && end_addr > 0x5F5E)
        {
            vm->gpu.draw_mask = vm->ram[0x5F5E];
        }
    }

    // 5. Stereo GPIO (0x5F80 - 0x5F8F)
    if (end_addr > 0x5F80 && start_addr <= 0x5F8F)
    {
        auto clamp_s8 = [](uint8_t v) -> uint8_t {
            int8_t s = (int8_t)v;
            if (s < -3) s = -3;
            if (s > 3) s = 3;
            return (uint8_t)s;
        };

        if (start_addr <= 0x5F81 && end_addr > 0x5F81)
            vm->ram[0x5F81] = (uint8_t)(vm->ram[0x5F81] & 0x03);

        const uint16_t clamp_addrs[] = {
            0x5F82, 0x5F83
        };
        for (uint16_t addr : clamp_addrs) {
            if (start_addr <= addr && end_addr > addr) {
                vm->ram[addr] = clamp_s8(vm->ram[addr]);
            }
        }

        if (start_addr <= Real8VM::BOTTOM_GPIO_ADDR && end_addr > Real8VM::BOTTOM_GPIO_ADDR) {
            vm->ram[Real8VM::BOTTOM_GPIO_ADDR] = (uint8_t)(vm->ram[Real8VM::BOTTOM_GPIO_ADDR] & 0x03);
            vm->applyBottomScreenFlags(vm->ram[Real8VM::BOTTOM_GPIO_ADDR]);
        }
    }

    // 6. REAL-8 GPIO (0x5FE0 - 0x5FE4)
    if (end_addr > 0x5FE0 && start_addr <= 0x5FE4)
    {
        if (start_addr <= 0x5FE0 && end_addr > 0x5FE0) {
            vm->r8_flags = vm->ram[0x5FE0];
        }
        if (start_addr <= 0x5FE1 && end_addr > 0x5FE1) {
            vm->applyVideoMode(vm->ram[0x5FE1], /*force=*/false);
        }
        if (start_addr <= 0x5FE2 && end_addr > 0x5FE2) {
            vm->ram[0x5FE2] = vm->r8_vmode_cur;
        }
        if (start_addr <= Real8VM::BOTTOM_VMODE_REQ_ADDR && end_addr > Real8VM::BOTTOM_VMODE_REQ_ADDR) {
            vm->applyBottomVideoMode(vm->ram[Real8VM::BOTTOM_VMODE_REQ_ADDR], /*force=*/false);
        }
        if (start_addr <= Real8VM::BOTTOM_VMODE_CUR_ADDR && end_addr > Real8VM::BOTTOM_VMODE_CUR_ADDR) {
            vm->ram[Real8VM::BOTTOM_VMODE_CUR_ADDR] = vm->bottom_vmode_cur;
        }
    }
}

struct MappedAddr {
    uint32_t addr;
    bool is_screen;
    bool is_sprite;
};

static MappedAddr map_ram_address(Real8VM *vm, uint32_t addr)
{
    MappedAddr out{addr, false, false};
    if (!vm)
        return out;

    if (addr < 0x2000)
    {
        if (vm->hwState.screenDataMemMapping == 0)
        {
            out.addr = addr + 0x6000;
            out.is_screen = true;
        }
        else
        {
            out.is_sprite = true;
        }
    }
    else if (addr >= 0x6000 && addr < 0x8000)
    {
        if (vm->hwState.spriteSheetMemMapping == 0x60)
        {
            out.addr = addr - 0x6000;
            out.is_sprite = true;
        }
        else
        {
            out.is_screen = true;
        }
    }

    return out;
}

static uint8_t read_screen_byte(Real8VM *vm, uint32_t addr)
{
    if (!vm || !vm->ram)
        return 0;
    if (addr < 0x6000 || addr >= 0x8000)
        return vm->ram[addr];

    uint32_t offset = addr - 0x6000;
    if (!vm->fb || vm->r8_vmode_cur != 0)
        return vm->ram[addr];

    int y = offset >> 6;
    int x = (offset & 63) << 1;
    if (y >= vm->fb_h || (x + 1) >= vm->fb_w)
        return 0;

    uint8_t p1 = vm->fb_row(y)[x];
    uint8_t p2 = vm->fb_row(y)[x + 1];
    uint8_t val = (p1 & 0x0F) | ((p2 & 0x0F) << 4);
    vm->ram[addr] = val;
    return val;
}

static uint8_t read_mapped_byte(Real8VM *vm, uint32_t addr)
{
    if (!vm || !vm->ram)
        return 0;
    if (addr >= 0x8000)
        return 0;

    const bool mapping_active = (vm->hwState.spriteSheetMemMapping == 0x60 || vm->hwState.screenDataMemMapping == 0);
    if (!mapping_active)
    {
        if (addr >= 0x6000 && addr < 0x8000)
            return read_screen_byte(vm, addr);
        return vm->ram[addr];
    }

    MappedAddr mapped = map_ram_address(vm, addr);
    if (mapped.addr >= 0x8000)
        return 0;
    if (mapped.is_screen)
        return read_screen_byte(vm, mapped.addr);
    return vm->ram[mapped.addr];
}

static void write_mapped_byte(Real8VM *vm, uint32_t addr, uint8_t val)
{
    if (!vm || !vm->ram)
        return;
    MappedAddr mapped = map_ram_address(vm, addr);
    if (mapped.addr >= 0x8000)
        return;
    vm->ram[mapped.addr] = val;

    if (mapped.is_screen && mapped.addr >= 0x6000 && mapped.addr < 0x8000)
    {
        vm->screenByteToFB(mapped.addr - 0x6000, val);
    }
    else if (mapped.addr < 0x6000)
    {
        vm_sync_ram(vm, mapped.addr, 1);
    }
}

static int l_poke(lua_State *L)
{
    REAL8_TRACE_API("poke");
    auto *vm = get_vm(L);
    if (!vm || !vm->ram)
        return 0;

    int addr = to_int_floor(L, 1);
    int argc = lua_gettop(L);

    if (addr < 0 || addr > 0x7FFF)
        return 0;

    for (int i = 2; i <= argc; ++i)
    {
        if (addr > 0x7FFF)
            break;

        uint8_t val = (uint8_t)to_int_floor(L, i);

        if (vm->watch_addr != -1 && addr == vm->watch_addr && vm->host) {
            lua_Debug ar;
            lua_getstack(L, 1, &ar);
            lua_getinfo(L, "nSl", &ar);
            vm->host->log("[WATCH] Addr 0x%04X written value %d at line %d (%s)", 
                        addr, val, ar.currentline, ar.short_src);
        }

        // --- Defender.p8 Compatibility ---
        // Defender writes 255 (0xFF) to 0x5F5C, making colors 0-7 transparent.
        // This hides the player ship (Color 7) and bullets (Color 6).
        // We prevent setting the lower transparency byte to FULL transparent (0xFF).
        if (addr == 0x5F5C && val == 0xFF) {
            // Force Color 7 (White) and Color 6 (Gray) to remain Opaque (0)
            // 0xFF (11111111) -> 0x3F (00111111) -> Colors 6 & 7 visible
            val = 0x3F; 
        }

        write_mapped_byte(vm, (uint32_t)addr, val);

        addr++;
    }
    return 0;
}

static int l_memcpy(lua_State *L)
{
    REAL8_TRACE_API("memcpy");
    auto *vm = get_vm(L);
    int dest = to_int_floor(L, 1);
    int src = to_int_floor(L, 2);
    int len = to_int_floor(L, 3);

    if (!vm || !vm->ram || len <= 0)
        return 0;

    // Bounds check
    if (dest < 0 || src < 0 || dest >= 0x8000 || src >= 0x8000)
        return 0;
    if (len > 0x8000 - dest)
        len = 0x8000 - dest;
    if (len > 0x8000 - src)
        len = 0x8000 - src;
    if (len <= 0)
        return 0;

    const bool mapping_active = (vm->hwState.spriteSheetMemMapping == 0x60 || vm->hwState.screenDataMemMapping == 0);
    const bool src_hits_screen = (src < 0x8000 && (src + len) > 0x6000);
    if (mapping_active)
    {
        std::vector<uint8_t> temp(len);
        for (int i = 0; i < len; ++i)
            temp[i] = read_mapped_byte(vm, (uint32_t)(src + i));
        for (int i = 0; i < len; ++i)
            write_mapped_byte(vm, (uint32_t)(dest + i), temp[i]);
        return 0;
    }

    // If reading FROM Screen (0x6000+), we must reconstruct the RAM data from the
    // visual Framebuffer because vm->spr() likely bypasses screen_ram for speed.
    if (src_hits_screen && vm->fb && vm->r8_vmode_cur == 0)
    {
        int s_start = std::max((int)src, 0x6000);
        int s_end = std::min((int)(src + len), 0x8000);

        for (int addr = s_start; addr < s_end; addr++)
        {
            int offset = addr - 0x6000;
            int y = offset >> 6;        // Divide by 64 (rows)
            int x = (offset & 63) << 1; // Mod 64 * 2 pixels per byte

            if (y < vm->fb_h && (x + 1) < vm->fb_w)
            {
                // Scavenge pixels directly from the visual buffer
                uint8_t p1 = vm->fb_row(y)[x];
                uint8_t p2 = vm->fb_row(y)[x + 1];

                // Re-pack into PICO-8 4-bit nibbles
                uint8_t val = (p1 & 0x0F) | ((p2 & 0x0F) << 4);

                // Force update both RAM caches
                vm->ram[addr] = val;
                if (vm->screen_ram)
                    vm->screen_ram[offset] = val;
            }
        }
    }
    // ----------------------------------------------

    // 1. Raw Copy
    memmove(&vm->ram[dest], &vm->ram[src], len);

    // 2. Screen RAM Sync (Visuals) -- Handled in previous fix, kept here
    if (dest < 0x8000 && (dest + len) > 0x6000)
    {
        int start = std::max((int)dest, 0x6000);
        int end = std::min((int)(dest + len), 0x8000);

        // Update Internal Screen Buffer
        if (vm->screen_ram)
        {
            memcpy(&vm->screen_ram[start - 0x6000], &vm->ram[start], end - start);
        }

        // Update Visuals (Framebuffer)
        for (int addr = start; addr < end; addr++)
        {
            vm->screenByteToFB(addr - 0x6000, vm->ram[addr]);
        }
        // Important: Mark screen dirty so the host knows to render the changes
        vm->mark_dirty_rect(0, 0, 127, 127);
    }

    // 3. Hardware State Sync
    if (dest < 0x6000)
    {
        vm_sync_ram(vm, (uint32_t)dest, len);
    }

    return 0;
}

static int l_memset(lua_State *L)
{
    REAL8_TRACE_API("memset");
    auto *vm = get_vm(L);
    int dest = to_int_floor(L, 1);
    uint8_t val = (uint8_t)to_int_floor(L, 2);
    int len = to_int_floor(L, 3);

    if (!vm || !vm->ram || len <= 0)
        return 0;

    // Bounds Check and Clamping
    if (dest < 0 || dest >= 0x8000)
        return 0;
    if (len > 0x8000 - dest)
        len = 0x8000 - dest;
    if (len <= 0)
        return 0;

    bool mapping_active = (vm->hwState.spriteSheetMemMapping == 0x60 || vm->hwState.screenDataMemMapping == 0);
    if (mapping_active)
    {
        for (int i = 0; i < len; ++i)
            write_mapped_byte(vm, (uint32_t)(dest + i), val);
        return 0;
    }

    // 1. Write to Main RAM (The Source of Truth)
    // We do this once, globally. No split paths for RAM writing.
    memset(&vm->ram[dest], val, len);

    // 2. Screen RAM Sync (Visuals + Internal Buffer)
    // Check if ANY part of the write touches the screen area (0x6000 - 0x7FFF)
    if (dest < 0x8000 && (dest + len) > 0x6000)
    {
        int start = std::max((int)dest, 0x6000);
        int end = std::min((int)(dest + len), 0x8000);
        int count = end - start;

        // --- Sync Internal Screen Buffer ---
        // This ensures subsequent peeks or memcpys see the change
        if (vm->screen_ram)
        {
            memset(&vm->screen_ram[start - 0x6000], val, count);
        }

        // --- Visual Update (Framebuffer) ---
        if (vm->r8_vmode_cur == 0) {
            uint8_t c1 = val & 0x0F;
            uint8_t c2 = (val >> 4) & 0x0F;

            // Optimization: If high/low nibbles match (solid color),
            // and we have direct FB access, fill pixels faster.
            if (vm->fb && c1 == c2)
            {
                int offset = start - 0x6000;
                for (int k = 0; k < count; k++)
                {
                    int idx = offset + k;
                    int y = idx >> 6;        // idx / 64
                    int x = (idx & 63) << 1; // (idx % 64) * 2
                    // vm->fb usually stores 1 byte per pixel
                    if (y < vm->fb_h && (x + 1) < vm->fb_w)
                    {
                        vm->fb_row(y)[x] = c1;
                        vm->fb_row(y)[x + 1] = c2;
                    }
                }
            }
            else
            {
                // Slow/Safe path: Convert packed byte to pixels via host/VM method
                for (int addr = start; addr < end; addr++)
                {
                    vm->screenByteToFB(addr - 0x6000, val);
                }
            }
            vm->mark_dirty_rect(0, 0, vm->fb_w - 1, vm->fb_h - 1);
        }
    }

    // 3. Hardware Sync (GFX/Map/Registers)
    // Check if the write touches memory below 0x6000
    if (dest < 0x6000)
    {
        vm_sync_ram(vm, (uint32_t)dest, len);
    }

    return 0;
}

static void ellipse_points(Real8VM *vm, int cx, int cy, int x, int y, uint8_t c, bool fill)
{
    if (fill)
    {
        // CHANGE: pset -> put_pixel_checked
        for (int xi = cx - x; xi <= cx + x; ++xi)
        {
            vm->gpu.put_pixel_checked(xi, cy + y, c);
            vm->gpu.put_pixel_checked(xi, cy - y, c);
        }
    }
    else
    {
        vm->gpu.put_pixel_checked(cx + x, cy + y, c);
        vm->gpu.put_pixel_checked(cx - x, cy + y, c);
        vm->gpu.put_pixel_checked(cx + x, cy - y, c);
        vm->gpu.put_pixel_checked(cx - x, cy - y, c);
        vm->gpu.put_pixel_checked(cx + y, cy - x, c);
        vm->gpu.put_pixel_checked(cx + x, cy - y, c);
    }
}

static int l_ovalcommon(lua_State *L, bool fill)
{
    REAL8_TRACE_API(fill ? "ovalfill" : "oval");
    auto *vm = get_vm(L);
    int x0 = to_int_floor(L, 1), y0 = to_int_floor(L, 2);
    int x1 = to_int_floor(L, 3), y1 = to_int_floor(L, 4);
    int c = (int)luaL_optinteger(L, 5, 7);
    int rx = abs(x1 - x0) / 2, ry = abs(y1 - y0) / 2;
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    long rx2 = rx * rx, ry2 = ry * ry;
    long x = 0, y = ry;
    long p = ry2 - rx2 * ry + rx2 / 4;
    long dx = 2 * ry2 * x, dy = 2 * rx2 * y;
    while (dx < dy)
    {
        ellipse_points(vm, cx, cy, x, y, (uint8_t)c, fill);
        x++;
        dx += 2 * ry2;
        if (p < 0)
            p += dx + ry2;
        else
        {
            y--;
            dy -= 2 * rx2;
            p += dx - dy + ry2;
        }
    }
    p = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0)
    {
        ellipse_points(vm, cx, cy, x, y, (uint8_t)c, fill);
        y--;
        dy -= 2 * rx2;
        if (p > 0)
            p += rx2 - dy;
        else
        {
            x++;
            dx += 2 * ry2;
            p += dx - dy + rx2;
        }
    }
    return 0;
}

static int l_oval(lua_State *L) { return l_ovalcommon(L, false); }
static int l_ovalfill(lua_State *L) { return l_ovalcommon(L, true); }

static int l_circ(lua_State *L)
{
    REAL8_TRACE_API("circ");
    auto *vm = get_vm(L);
    int cx = to_int_floor(L, 1);
    int cy = to_int_floor(L, 2);
    int r = to_int_floor(L, 3);
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 4 && !lua_isnil(L, 4))
    {
        c = to_int_floor(L, 4) & 0x0F;
    }
    vm->gpu.circ(cx, cy, r, (uint8_t)c);
    return 0;
}

static int l_circfill(lua_State *L)
{
    REAL8_TRACE_API("circfill");
    auto *vm = get_vm(L);
    int cx = to_int_floor(L, 1);
    int cy = to_int_floor(L, 2);
    int r = to_int_floor(L, 3);
    int c = vm->gpu.getPen();
    if (lua_gettop(L) >= 4 && !lua_isnil(L, 4))
    {
        c = to_int_floor(L, 4) & 0x0F;
    }
    vm->gpu.circfill(cx, cy, r, (uint8_t)c);
    return 0;
}

static int l_tline(lua_State *L)
{
    REAL8_TRACE_API("tline");
    auto *vm = get_vm(L);
    int x0 = to_int_floor(L, 1);
    int y0 = to_int_floor(L, 2);
    int x1 = to_int_floor(L, 3);
    int y1 = to_int_floor(L, 4);

    double mx = luaL_optnumber(L, 5, 0.0);
    double my = luaL_optnumber(L, 6, 0.0);
    double mdx = luaL_optnumber(L, 7, 0.0);
    double mdy = luaL_optnumber(L, 8, 0.0);

    // Retrieve Transparency Mask from RAM (safe fallback if vm->ram is null)
    uint16_t palt_mask = (vm->ram) ? (vm->ram[0x5F5C] | (vm->ram[0x5F5D] << 8)) : 0x0001;

    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        int tx = ((int)mx) & 127;
        int ty = ((int)my) & 127;

        uint8_t c = vm->gpu.sget(tx, ty);

        // Check transparency before drawing
        // If the bit at index 'c' is 0, it is Opaque. If 1, it is Transparent.
        if (!((palt_mask >> (c & 0xF)) & 1))
        {
            vm->gpu.pset(x0, y0, c);
        }

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }

        mx += mdx;
        my += mdy;
    }
    return 0;
}

static int l_pal(lua_State *L)
{
    REAL8_TRACE_API("pal");
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);

    // 1. Reset
    if (argc == 0)
    {
        vm->gpu.pal_reset();
        if (vm->ram)
        {
            for (int i = 0; i < 16; i++)
                vm->ram[0x5F00 + i] = i; // Draw Pal
            for (int i = 0; i < 16; i++)
                vm->ram[0x5F10 + i] = i; // Screen Pal
        }
        return 0;
    }

    // 2. Table Mode
    if (lua_istable(L, 1))
    {
        int p = (argc >= 2) ? to_int_floor(L, 2) : 0;

        // Iterate over the 16 hardware palette slots
        for (int i = 0; i < 16; i++)
        {
            bool found = false;
            int val = 0;

            // Priority 1: Check for explicit PICO-8 0-based index (e.g., {[0]=0})
            // This is what Phoenix.p8 uses for the screen palette.
            lua_rawgeti(L, 1, i);
            if (!lua_isnil(L, -1))
            {
                val = to_int_floor(L, -1);
                found = true;
            }
            lua_pop(L, 1);

            // Priority 2: Check for Lua 1-based index (e.g., {1, 2, 3})
            // If the table didn't have key [i], it might be a standard Lua array
            // where index 1 maps to color 0.
            if (!found)
            {
                lua_rawgeti(L, 1, i + 1);
                if (!lua_isnil(L, -1))
                {
                    val = to_int_floor(L, -1);
                    found = true;
                }
                lua_pop(L, 1);
            }

            // Only update if the key existed. This allows sparse tables
            // (e.g., {[10]=14}) to update single colors without resetting others.
            if (found)
            {
                vm->gpu.pal(i, val, p);

                if (vm->ram)
                {
                    if (p == 0)
                        vm->ram[0x5F00 + i] = val & 0xF;
                    else if (p == 1)
                        vm->ram[0x5F10 + i] = val & 0xFF;
                }
            }
        }
        return 0;
    }

    // 3. Single arg reset: pal(p)
    if (argc == 1)
    {
        int p = to_int_floor(L, 1);
        if (p == 0)
        {
            for (int i = 0; i < 16; i++)
                vm->gpu.pal(i, i, 0);
            if (vm->ram)
                for (int i = 0; i < 16; i++)
                    vm->ram[0x5F00 + i] = i;
        }
        else if (p == 1)
        {
            for (int i = 0; i < 16; i++)
                vm->gpu.pal(i, i, 1);
            if (vm->ram)
                for (int i = 0; i < 16; i++)
                    vm->ram[0x5F10 + i] = i;
        }
        return 0;
    }

    // 4. Standard: pal(c0, c1, p)
    int c0 = to_int_floor(L, 1);
    int c1 = to_int_floor(L, 2);
    int p = (int)luaL_optinteger(L, 3, 0);

    vm->gpu.pal(c0, c1, p);

    // UPDATE RAM
    if (vm->ram)
    {
        if (p == 0)
            vm->ram[0x5F00 + (c0 & 0xF)] = c1 & 0xF; // Draw Palette
        else if (p == 1)
            vm->ram[0x5F10 + (c0 & 0xF)] = c1 & 0xFF; // Screen Palette
    }
    return 0;
}

static int l_clip(lua_State *L)
{
    REAL8_TRACE_API("clip");
    auto *vm = get_vm(L);
    int x = 0, y = 0, w = 128, h = 128;
    if (lua_gettop(L) >= 4)
    {
        x = to_int_floor(L, 1);
        y = to_int_floor(L, 2);
        w = to_int_floor(L, 3);
        h = to_int_floor(L, 4);
    }
    vm->gpu.clip(x, y, w, h);

    // UPDATE RAM (0x5F20 - 0x5F23)
    if (vm->ram)
    {
        vm->ram[0x5F20] = x;
        vm->ram[0x5F21] = y;
        vm->ram[0x5F22] = (x + w); // PICO-8 stores X1, not W
        vm->ram[0x5F23] = (y + h); // PICO-8 stores Y1, not H
    }
    return 0;
}

static int l_palt(lua_State *L)
{
    REAL8_TRACE_API("palt");
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);

    // 1. Reset (palt())
    if (argc == 0)
    {
        vm->gpu.palt_reset();
        // Reset RAM mirror (Standard: Color 0 transparent, rest opaque = 0x0001)
        if (vm->ram)
        {
            vm->ram[0x5F5C] = 1;
            vm->ram[0x5F5D] = 0;
        }
        return 0;
    }

    // 2. Bitfield Mode (palt(mask))
    if (argc == 1)
    {
        uint16_t mask = (uint16_t)to_int_floor(L, 1);

        // Update VM internal state
        for (int i = 0; i < 16; i++)
        {
            vm->gpu.palt(i, (mask >> i) & 1);
        }

        // Update RAM mirror (0x5F5C is low byte, 0x5F5D is high byte)
        if (vm->ram)
        {
            vm->ram[0x5F5C] = mask & 0xFF;
            vm->ram[0x5F5D] = (mask >> 8) & 0xFF;
        }
        return 0;
    }

    // 3. Single Color Mode (palt(c, t))
    int c = to_int_floor(L, 1) & 0xF;
    bool t = lua_toboolean(L, 2);

    vm->gpu.palt(c, t);

    // Update RAM mirror logic
    if (vm->ram)
    {
        uint16_t mask = vm->ram[0x5F5C] | (vm->ram[0x5F5D] << 8);
        if (t)
            mask |= (1 << c);
        else
            mask &= ~(1 << c);

        vm->ram[0x5F5C] = mask & 0xFF;
        vm->ram[0x5F5D] = (mask >> 8) & 0xFF;
    }

    return 0;
}
static int l_fillp(lua_State *L)
{
    REAL8_TRACE_API("fillp");
    auto *vm = get_vm(L);
    if (lua_gettop(L) == 0)
    {
        vm->gpu.fillp(0);
        return 0;
    }
    uint32_t p = (uint32_t)to_int_floor(L, 1);
    vm->gpu.fillp(p);
    return 0;
}
static int l_camera(lua_State *L)
{
    REAL8_TRACE_API("camera");
    auto *vm = get_vm(L);

    // PICO-8 Standard:
    // camera() with no args defaults to x=0, y=0 (Reset).
    // It does NOT act as a getter.

    int x = 0;
    int y = 0;

    if (lua_gettop(L) >= 1)
        x = to_int_floor(L, 1);
    if (lua_gettop(L) >= 2)
        y = to_int_floor(L, 2);

    vm->gpu.camera(x, y);

    // Sync with RAM
    if (vm->ram)
    {
        vm->ram[0x5F28] = x & 0xFF;
        vm->ram[0x5F29] = (x >> 8) & 0xFF;
        vm->ram[0x5F2A] = y & 0xFF;
        vm->ram[0x5F2B] = (y >> 8) & 0xFF;
    }
    return 0;
}

static int l_map_check(lua_State *L)
{
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);
    int w = to_int_floor(L, 3);
    int h = to_int_floor(L, 4);
    int flag = to_int_floor(L, 5);
    lua_pushboolean(L, vm->map_check_flag(x, y, w, h, flag));
    return 1;
}

static int IWRAM_BINDINGS_CODE l_spr(lua_State *L)
{
    REAL8_TRACE_API("spr");
    debug_spr_count++;

    auto *vm = get_vm(L);
    int n = to_int_floor(L, 1);

    // Cast to int16_t to handle wrapping (e.g., 65535 becomes -1)
    int x = (int16_t)to_int_floor(L, 2);
    int y = (int16_t)to_int_floor(L, 3);

    // PICO-8 entities might use fractional sizes (e.g. 6px hitbox / 8 = 0.75).
    // Direct (int) cast truncates 0.75 to 0 (invisible).
    // We use ceil() to ensure it draws at least 1 tile.
    double dw = luaL_optnumber(L, 4, 1.0);
    double dh = luaL_optnumber(L, 5, 1.0);

    int w = (int)ceil(dw);
    int h = (int)ceil(dh);

    bool fx = lua_toboolean(L, 6), fy = lua_toboolean(L, 7);
    vm->gpu.spr(n, x, y, w, h, fx, fy);
    return 0;
}
// In real8_bindings.cpp, replace the l_sspr function:

static int IWRAM_BINDINGS_CODE l_sspr(lua_State *L)
{
    REAL8_TRACE_API("sspr");
    auto *vm = get_vm(L);
    int sx = to_int_floor(L, 1), sy = to_int_floor(L, 2);
    int sw = to_int_floor(L, 3), sh = to_int_floor(L, 4);
    
    // Destination can be floats in PICO-8 but usually int logic suffices
    int dx = to_int_floor(L, 5);
    int dy = to_int_floor(L, 6);
    
    // Optional scaling
    int dw = (lua_gettop(L) >= 7 && !lua_isnil(L, 7)) ? to_int_floor(L, 7) : sw;
    int dh = (lua_gettop(L) >= 8 && !lua_isnil(L, 8)) ? to_int_floor(L, 8) : sh;
    
    // Capture Flip Flags (Args 9 and 10)
    bool fx = (lua_gettop(L) >= 9) ? lua_toboolean(L, 9) : false;
    bool fy = (lua_gettop(L) >= 10) ? lua_toboolean(L, 10) : false;

    vm->gpu.sspr(sx, sy, sw, sh, dx, dy, dw, dh, fx, fy);
    return 0;
}
static int l_sget(lua_State *L)
{
    REAL8_TRACE_API("sget");
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1), y = to_int_floor(L, 2);
    // Mask sprite sheet reads to 4 bits as well.
    lua_pushinteger(L, vm->gpu.sget(x, y) & 0x0F);
    return 1;
}
static int l_sset(lua_State *L)
{
    REAL8_TRACE_API("sset");
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1);
    int y = to_int_floor(L, 2);
    int v;
    if (lua_isnoneornil(L, 3))
        v = vm->gpu.getPen() & 0x0F;
    else
        v = to_int_floor(L, 3) & 0x0F;
    vm->gpu.sset(x, y, (uint8_t)v);
    return 0;
}

static int IWRAM_BINDINGS_CODE l_map(lua_State *L)
{
    REAL8_TRACE_API("map");
    auto *vm = get_vm(L);
    int n = lua_gettop(L);
    int mx, my, sx, sy, w, h, layer;

    const bool bigMap = vm->hwState.mapMemMapping >= 0x80;
    int mapSize = bigMap ? (0x10000 - (vm->hwState.mapMemMapping << 8)) : 8192;
    if (bigMap) {
        const int userDataSize = 0x8000 - 0x4300;
        if (mapSize > userDataSize) mapSize = userDataSize;
    }
    int mapW = (vm->hwState.widthOfTheMap == 0) ? 256 : vm->hwState.widthOfTheMap;
    if (mapW <= 0) mapW = 128;
    int mapH = (mapW > 0) ? (mapSize / mapW) : 0;

    // Defaults
    mx = 0;
    my = 0;
    sx = 0;
    sy = 0;
    w = mapW;
    h = mapH;
    layer = 0; // Layer 0 acts as "all layers" often, or -1 in some implementations

    if (n > 0)
    {
        if (!lua_isnil(L, 1))
            mx = to_int_floor(L, 1);
        if (n >= 2 && !lua_isnil(L, 2))
            my = to_int_floor(L, 2);
        if (n >= 3 && !lua_isnil(L, 3))
            sx = to_int_floor(L, 3);
        if (n >= 4 && !lua_isnil(L, 4))
            sy = to_int_floor(L, 4);

        // Critical Fix: Explicitly check nil to preserve defaults
        if (n >= 5 && !lua_isnil(L, 5))
            w = to_int_floor(L, 5);
        if (n >= 6 && !lua_isnil(L, 6))
            h = to_int_floor(L, 6);
        if (n >= 7 && !lua_isnil(L, 7))
            layer = to_int_floor(L, 7);
    }

    // PICO-8 treats layer bitmask 0 as "draw everything" usually,
    // but your VM implementation likely expects -1 or a specific flag.
    // If your VM uses -1 for "all layers", keep your existing default.
    // Based on your previous code, you used -1.
    if (n < 7)
        layer = -1;

    vm->gpu.map(mx, my, sx, sy, w, h, layer);
    return 0;
}
static int l_mget(lua_State *L)
{
    REAL8_TRACE_API("mget");
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1), y = to_int_floor(L, 2);
    lua_pushinteger(L, vm->gpu.mget(x, y));
    return 1;
}
static int l_mset(lua_State *L)
{
    REAL8_TRACE_API("mset");
    auto *vm = get_vm(L);
    int x = to_int_floor(L, 1), y = to_int_floor(L, 2);
    int v = to_int_floor(L, 3);
    vm->gpu.mset(x, y, (uint8_t)v);
    return 0;
}
static int l_fget(lua_State *L)
{
    auto *vm = get_vm(L);
    int n = to_int_floor(L, 1);
    if (lua_gettop(L) >= 2)
    {
        int f = to_int_floor(L, 2) & 7;
        lua_pushboolean(L, (vm->sprite_flags[n & 0xFF] >> f) & 1);
    }
    else
    {
        lua_pushinteger(L, vm->sprite_flags[n & 0xFF]);
    }
    return 1;
}
static int l_fset(lua_State *L)
{
    auto *vm = get_vm(L);
    int n = to_int_floor(L, 1) & 0xFF;
    if (lua_gettop(L) >= 3)
    {
        int f = to_int_floor(L, 2) & 7;
        bool v = lua_toboolean(L, 3);
        uint8_t mask = (uint8_t)(1u << f);
        if (v)
            vm->sprite_flags[n] |= mask;
        else
            vm->sprite_flags[n] &= ~mask;
    }
    else
    {
        int v = to_int_floor(L, 2);
        vm->sprite_flags[n] = (uint8_t)(v & 0xFF);
    }
    return 0;
}

static int l_btn(lua_State *L)
{
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);

    // PICO-8 quirk: btn() with no args returns a bitfield of PLAYER 0 state only
    if (argc == 0)
    {
        lua_pushinteger(L, (int)vm->get_btn_state(0));
        return 1;
    }

    int i = to_int_floor(L, 1);
    int p = 0; // Default to Player 0

    // Check if player index is provided
    if (argc >= 2 && !lua_isnil(L, 2))
    {
        p = to_int_floor(L, 2);
    }

    // Pass player index 'p' to the VM
    lua_pushboolean(L, vm->btn(i, p));
    return 1;
}

static int l_btnp(lua_State *L)
{
    auto *vm = get_vm(L);
    int argc = lua_gettop(L);

    // Same quirk for btnp()
    if (argc == 0)
    {
        lua_pushinteger(L, (int)vm->get_btn_state(0));
        return 1;
    }

    int i = to_int_floor(L, 1);
    int p = 0;

    if (argc >= 2 && !lua_isnil(L, 2))
    {
        p = to_int_floor(L, 2);
    }

    lua_pushboolean(L, vm->btnp(i, p));
    return 1;
}
static int l_sfx(lua_State *L)
{
    auto *vm = get_vm(L);
    if (!vm) return 0;
    int idx = to_int_floor(L, 1);
    int ch = (int)luaL_optinteger(L, 2, -1);
    int offset = (int)luaL_optinteger(L, 3, 0);
    int length = (int)luaL_optinteger(L, 4, -1);

    #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
    vm->audio.play_sfx(idx, ch, offset, length);
    #endif

    return 0;
}
static int l_music(lua_State *L)
{
    auto *vm = get_vm(L);
    if (!vm) return 0;
    int pat = to_int_floor(L, 1);
    int fade_len = (int)luaL_optinteger(L, 2, 0);
    int mask = (int)luaL_optinteger(L, 3, 0x0f);

    #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
        vm->audio.play_music(pat, fade_len, mask);
    #endif

    return 0;
}
#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
void Real8VM::init_wavetables()
{
    for (int i = 0; i < 2048; i++)
    {
        float t = (float)i / 2048.0f;
        
        // 0: Triangle
        wavetables[0][i] = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        
        // 1: Tilted Triangle
        float k = 0.875f;
        wavetables[1][i] = (t < k) ? (2.0f * t / k - 1.0f) : (1.0f - 2.0f * (t - k) / (1.0f - k)) * -1.0f;

        // 2: Sawtooth
        wavetables[2][i] = 2.0f * t - 1.0f;

        // 3: Square
        wavetables[3][i] = (t < 0.5f) ? 1.0f : -1.0f;

        // 4: Pulse
        wavetables[4][i] = (t < 0.3125f) ? 1.0f : -1.0f;

        // 5: Organ (Triangle mixed with 2x Triangle)
        float w0 = wavetables[0][i];
        float t2 = t + t;
        if (t2 >= 1.0f) t2 -= 1.0f;
        float w1 = (t2 < 0.5f) ? (4.0f * t2 - 1.0f) : (3.0f - 4.0f * t2);
        wavetables[5][i] = (w0 + w1) * 0.5f;

        // 6: Noise (Procedural - leave empty or zero)
        wavetables[6][i] = 0.0f;

        // 7: Phaser (Uses Triangle shape, modulation happens at runtime)
        wavetables[7][i] = wavetables[0][i];
    }
}
#endif

// --------------------------------------------------------------------------
// PERSISTENT DATA IMPLEMENTATION
// --------------------------------------------------------------------------

void Real8VM::loadCartData()
{
    // Reset to 0
    memset(cart_data_ram, 0, sizeof(cart_data_ram));

    if (cartDataId.empty())
        return;

    // We store cartdata in the saves folder: /saves/cdata_[id].dat
    std::string path = "/saves/cdata_" + cartDataId + ".dat";
    std::vector<uint8_t> data = host->loadFile(path.c_str());

    if (data.size() == sizeof(cart_data_ram))
    {
        memcpy(cart_data_ram, data.data(), sizeof(cart_data_ram));
    }
}

void Real8VM::saveCartData()
{
    if (cartDataId.empty())
        return;

    std::string path = "/saves/cdata_" + cartDataId + ".dat";

    // Write the raw memory of the float array to disk
    host->saveState(path.c_str(), (uint8_t *)cart_data_ram, sizeof(cart_data_ram));
}

void Real8VM::saveCartToDisk()
{
    // Safety checks
    if (!host || currentGameId.empty() || !rom) 
        return;

    // PICO-8's cstore() is complex: it technically requires patching the 
    // original .p8 (text) or .p8.png (image) file with new binary data.
    //
    // For an embedded/custom engine, the safest and easiest way to support this
    // is to dump the modified ROM (0x0000-0x4300 usually) to a "sidecar" file.
    // When loading a game, you would check if this sidecar file exists and 
    // load it over the ROM after the main cartridge loads.

    // Saving the full 32k ROM as a raw binary dump for persistence
    std::string path = "/saves/" + currentGameId + ".rom";
    
    // We assume 'rom' is a pointer to the 32k ROM buffer
    // and host->saveState handles binary writing (like it does for cartdata)
    host->saveState(path.c_str(), rom, 0x8000);
    
    host->log("[Real8] cstore: Saved ROM modifications to %s", path.c_str());
}

// cartdata("id_string")
static int l_cartdata(lua_State *L)
{
    auto *vm = get_vm(L);

    if (vm)
    {
        const char *id = luaL_checkstring(L, 1);
        vm->cartDataId = std::string(id);
        vm->loadCartData(); // Load existing or clear memory
    }
    return 0;
}

// val = dget(index)
static int l_dget(lua_State *L)
{
    auto *vm = get_vm(L);

    if (vm)
    {
        int idx = (int)lua_tonumber(L, 1);
        if (idx >= 0 && idx < 64)
        {
            lua_pushnumber(L, vm->cart_data_ram[idx]);
        }
        else
        {
            lua_pushnumber(L, 0);
        }
        return 1;
    }
    return 0;
}

// dset(index, val)
static int l_dset(lua_State *L)
{
    auto *vm = get_vm(L);

    if (vm)
    {
        int idx = (int)lua_tonumber(L, 1);
        float val = (float)lua_tonumber(L, 2);

        if (idx >= 0 && idx < 64)
        {
            // Update RAM only
            vm->cart_data_ram[idx] = val;

            // OPTIONAL: Mark a flag in your VM to say "Save me later"
            // vm->cart_data_dirty = true;
        }
    }
    return 0;
}

static int l_peek2(lua_State *L)
{
    REAL8_TRACE_API("peek2");
    auto *vm = get_vm(L);
    int addr = to_int_floor(L, 1);

    if (!vm || !vm->ram || addr < 0 || addr > 0x7FFF)
    {
        lua_pushinteger(L, 0);
        return 1;
    }

    // Safe read of 2 bytes (Handle edge case at 0x7FFF)
    uint8_t low = read_mapped_byte(vm, (uint32_t)addr);
    uint8_t high = (addr < 0x7FFF) ? read_mapped_byte(vm, (uint32_t)addr + 1) : 0;

    lua_pushinteger(L, low | (high << 8));
    return 1;
}

static int l_poke2(lua_State *L)
{
    REAL8_TRACE_API("poke2");
    auto *vm = get_vm(L);
    if (vm && vm->ram)
    {
        int addr = to_int_floor(L, 1);
        int val = to_int_floor(L, 2);
        if (addr >= 0 && addr < 0x7FFF)
        {
            write_mapped_byte(vm, addr, val & 0xFF);
            write_mapped_byte(vm, addr + 1, (val >> 8) & 0xFF);
        }
    }
    return 0;
}

static int l_peek4(lua_State *L)
{
    REAL8_TRACE_API("peek4");
    auto *vm = get_vm(L);
    // PICO-8 uses 16.16 fixed point numbers. peek4 returns a standard Lua number.
    if (vm && vm->ram)
    {
        int addr = (int)lua_tonumber(L, 1);
        if (addr >= 0 && addr < 0x7FFC)
        {
            // Read 4 bytes as 16.16 fixed point
            int32_t raw = read_mapped_byte(vm, (uint32_t)addr)
                        | (read_mapped_byte(vm, (uint32_t)addr + 1) << 8)
                        | (read_mapped_byte(vm, (uint32_t)addr + 2) << 16)
                        | (read_mapped_byte(vm, (uint32_t)addr + 3) << 24);
            lua_pushnumber(L, (double)raw / 65536.0);
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int l_poke4(lua_State *L)
{
    REAL8_TRACE_API("poke4");
    auto *vm = get_vm(L);
    if (vm && vm->ram)
    {
        int addr = to_int_floor(L, 1);

        // Handle boolean explicitly for fixed point conversion
        double val;
        if (lua_isboolean(L, 2))
            val = lua_toboolean(L, 2) ? 1.0 : 0.0;
        else
            val = lua_tonumber(L, 2);

        int32_t fixed = (int32_t)(val * 65536.0);
        if (addr >= 0 && addr < 0x7FFC)
        {
            write_mapped_byte(vm, addr, fixed & 0xFF);
            write_mapped_byte(vm, addr + 1, (fixed >> 8) & 0xFF);
            write_mapped_byte(vm, addr + 2, (fixed >> 16) & 0xFF);
            write_mapped_byte(vm, addr + 3, (fixed >> 24) & 0xFF);
        }
    }
    return 0;
}

static int l_menuitem(lua_State *L)
{
    auto *vm = get_vm(L);
    if (!vm)
        return 0;

    // Arg 1: Index (1-5)
    int idx = (int)luaL_checkinteger(L, 1);

    // Arg 2: Label (Optional)
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
    {
        const char *label = luaL_checkstring(L, 2);

        // Arg 3: Callback (Optional but expected)
        int ref = LUA_NOREF;
        if (lua_gettop(L) >= 3 && lua_isfunction(L, 3))
        {
            lua_pushvalue(L, 3);                  // Push function to top of stack
            ref = luaL_ref(L, LUA_REGISTRYINDEX); // Pop and store ref
        }

        vm->set_menu_item(idx, label, ref);
    }
    else
    {
        // If label is nil or missing, clear the item
        vm->set_menu_item(idx, nullptr, LUA_NOREF);
    }

    return 0;
}

// Prints to the host standard output (Serial or Console)
static int l_printh(lua_State *L)
{
    if (ENABLE_GAME_LOGS)
    {
        // printh(str, [filename], [overwrite]) - we only care about str
        const char *s = luaL_optstring(L, 1, "");
        printf("[P8-PRINTH] %s\n", s);
    }
    return 0;
}

static int l_run(lua_State *L)
{
    // 1. Get VM Instance
    auto *vm = get_vm(L);

    if (!vm)
        return 0;

    // 2. Check for optional filename argument: run("file.p8")
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1))
    {
        vm->next_cart_path = std::string(lua_tostring(L, 1));

        // PICO-8 breadcrumb support:
        // If string is empty "", it acts as a normal reset
        if (vm->next_cart_path == "")
            vm->next_cart_path = "";
    }
    else
    {
        vm->next_cart_path = ""; // Signal to reload current
    }

    // Add this temporarily to check if RAM is valid
    printf("DEBUG RAM [0x1000]: %02X %02X %02X %02X\n",
           vm->ram[0x1000], vm->ram[0x1001], vm->ram[0x1002], vm->ram[0x1003]);

    // 3. Set Flag
    vm->reset_requested = true;

    // 4. Force immediate exit of Lua execution stack.
    // This throws a Lua error "HALT", which we catch in runFrame()
    // to stop _update/_draw immediately.
    return luaL_error(L, "HALT");
}

// Stub for stop()
static int l_stop(lua_State *L)
{
    auto *vm = get_vm(L);
    if (lua_gettop(L) >= 1)
    {
        // stop("Message")
        const char *msg = luaL_optstring(L, 1, "");
        printf("STOP: %s\n", msg);
    }
    // Throw HALT to stop the Lua VM loop immediately
    return luaL_error(L, "HALT");
}

// Stub for extcmd(cmd)
static int l_extcmd(lua_State *L)
{
    auto *vm = get_vm(L);
    const char *cmd = luaL_checkstring(L, 1);

    // extcmd("reset"): Soft Reset
    if (strcmp(cmd, "reset") == 0)
    {
        if (vm)
            vm->reset_requested = true;
        return luaL_error(L, "HALT");
    }

    // extcmd("shutdown"): Graceful Exit to Shell/Browser
    if (strcmp(cmd, "shutdown") == 0)
    {
        if (vm) 
            vm->exit_requested = true; // Signal Shell to exit to browser
        
        // Stop Lua immediately so we don't process further frames
        return luaL_error(L, "HALT"); 
    }

    // extcmd("pause"): Trigger Pause Menu
    if (strcmp(cmd, "pause") == 0)
    {
        // To support this, you would need a vm->menu_requested flag
        // handled in Real8Shell similar to exit_requested.
        // For now, ignoring it is safe.
        return 0;
    }

    // extcmd("set_title", "My Game"): Change Window Title
    if (strcmp(cmd, "set_title") == 0)
    {
        if (lua_gettop(L) >= 2)
        {
            const char *title = luaL_checkstring(L, 2);
            // printf("[Real8] Title set: %s\n", title);
            // If host supports it: vm->host->setWindowTitle(title);
        }
        return 0;
    }

    return 0;
}

static int l_yield(lua_State *L)
{
    // 1. If inside a coroutine, standard Lua yield
    if (lua_isyieldable(L))
    {
        return lua_yield(L, lua_gettop(L));
    }

    // 2. If main thread, PICO-8 treats this as flip()
    return l_flip(L);
}

// --------------------------------------------------------------------------
// OPTIMIZED BINDINGS (Paste these before register_pico8_api)
// --------------------------------------------------------------------------

// PICO-8 RNG State
static uint32_t rng_seed = 0xDEADBEEF;

// Internal RNG function (LCG similar to PICO-8)
static uint32_t pico_random()
{
    rng_seed = (rng_seed * 1664525u + 1013904223u) & 0xFFFFFFFFu;
    return (rng_seed >> 16);
}

static int l_srand(lua_State *L)
{
    // Save current seed to return
    uint32_t old_seed = rng_seed;

    // Set new seed
    double v = luaL_optnumber(L, 1, 0.0);
    rng_seed = (uint32_t)(v * 65536.0);
    if (rng_seed == 0)
        rng_seed = 0xDEADBEEF;

    // Return old seed (converted back from raw integer to 16.16 float)
    lua_pushnumber(L, (double)old_seed / 65536.0);
    return 1;
}

static int l_rnd(lua_State *L)
{
    // 1. Check if the argument is a table FIRST.
    // Calling luaL_optnumber on a table raises a type error immediately,
    // so we must handle the table case before parsing numbers.
    if (lua_istable(L, 1))
    {
        size_t len = lua_rawlen(L, 1);
        if (len == 0)
            return 0; // Return nothing (nil) for empty tables

        // PICO-8 uses 1-based indexing
        int idx = (pico_random() % len) + 1;
        lua_rawgeti(L, 1, idx);
        return 1;
    }

    // 2. Fallback to Number Logic
    // Now safe to parse argument 1 as a number (defaults to 1.0 if nil)
    double limit = luaL_optnumber(L, 1, 1.0);

    // Standard rnd(n) -> 0..n
    uint32_t r = pico_random(); // 0..65535
    double val = ((double)r / 65536.0) * limit;
    lua_pushnumber(L, val);
    return 1;
}

// Optimized string.sub (PICO-8 style: lenient bounds)
// [real8_bindings.cpp]

static int l_sub(lua_State *L)
{
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);

    // PICO-8 defaults: start=1, end=-1 (length)
    int start = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? to_int_floor(L, 2) : 1;
    int end = (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ? to_int_floor(L, 3) : -1;

    // Handle PICO-8 negative wrapping
    if (start < 0)
        start = len + start + 1;
    if (end < 0)
        end = len + end + 1;

    // Clamp
    if (start < 1)
        start = 1;
    if (end > (int)len)
        end = (int)len;

    if (start > end)
    {
        lua_pushstring(L, "");
    }
    else
    {
        lua_pushlstring(L, s + start - 1, end - start + 1);
    }
    return 1;
}


static int l_load_p8_file(lua_State *L) {
    size_t len;
    const char *str = luaL_checklstring(L, 1, &len);
    
    // 1. C++ Transpiler (Memory Ops Only)
    std::string transpiled = transpile_pico8_memory_ops(std::string(str, len));
    std::string normalized = p8_normalize_lua_strings(transpiled);

    // 2. Lua Compiler (Handles +=, //, ?, etc. via z8lua native llex)
    // Note: z8lua usually expects the chunk name to start with '@' or '=' for debugging
    int status = luaL_loadbuffer(L, normalized.c_str(), normalized.size(), "p8_code");

    if (status != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2); // Move nil before error message
        return 2; // return nil, err_msg
    }
    
    return 1; // return chunk
}

static int internal_tonum(lua_State *L, int idx, int flags)
{
    // 1. Handle Number (Identity)
    if (lua_type(L, idx) == LUA_TNUMBER)
    {
        lua_pushvalue(L, idx);
        return 1;
    }

    // 2. Handle Boolean (PICO-8 returns 1 or 0)
    if (lua_isboolean(L, idx))
    {
        int val = lua_toboolean(L, idx);
        lua_pushnumber(L, val ? 1.0 : 0.0);
        return 1;
    }

    // 3. Handle String
    if (lua_isstring(L, idx))
    {
        size_t len;
        const char *s_raw = lua_tolstring(L, idx, &len);

        if (flags & 2)
        {
            if (len < 4)
            {
                lua_pushnil(L);
                return 1;
            }
            uint32_t raw = (uint8_t)s_raw[0]
                         | ((uint32_t)(uint8_t)s_raw[1] << 8)
                         | ((uint32_t)(uint8_t)s_raw[2] << 16)
                         | ((uint32_t)(uint8_t)s_raw[3] << 24);
            push_pico_fixed(L, (int32_t)raw);
            return 1;
        }

        // Skip leading whitespace
        size_t pos = 0;
        while (pos < len && isspace((unsigned char)s_raw[pos]))
        {
            pos++;
        }

        size_t remaining = len - pos;

        // Empty or pure whitespace = nil
        if (remaining == 0)
        {
            lua_pushnil(L);
            return 1;
        }

        const char *s = s_raw + pos;
        if (memchr(s, '\0', remaining))
        {
            lua_pushnil(L);
            return 1;
        }

        std::string tmp(s, remaining);
        const char *cstr = tmp.c_str();

        char *end = nullptr;
        double res = 0.0;
        bool force_hex = (flags & 1) != 0;

        // PICO-8 supports "0x..." for both Integers and Fixed Point (0x0.8000 = 0.5)
        // For Mario.p8, we specifically need to catch the integer cases safely.
        if (force_hex || (remaining > 2 && cstr[0] == '0' && (cstr[1] == 'x' || cstr[1] == 'X')))
        {
            const char *p = cstr;
            bool neg = false;
            if (*p == '+' || *p == '-') {
                neg = (*p == '-');
                p++;
            }
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
            }

            uint32_t int_part = 0;
            int int_digits = 0;
            while (*p && isxdigit((unsigned char)*p)) {
                int_part = (int_part << 4) | (uint32_t)p8_hex_val(*p);
                p++;
                int_digits++;
            }

            uint32_t frac_part = 0;
            int frac_digits = 0;
            if (*p == '.') {
                p++;
                while (*p && isxdigit((unsigned char)*p)) {
                    if (frac_digits < 4) {
                        frac_part = (frac_part << 4) | (uint32_t)p8_hex_val(*p);
                        frac_digits++;
                    }
                    p++;
                }
            }

            const char *tail = p;
            while (*tail && isspace((unsigned char)*tail)) {
                tail++;
            }

            if ((int_digits == 0 && frac_digits == 0) || *tail != '\0')
            {
                lua_pushnil(L);
                return 1;
            }

            uint32_t raw = int_part << 16;
            if (frac_digits > 0) {
                raw |= frac_part << (16 - 4 * frac_digits);
            }
            int32_t signed_raw = neg ? -(int32_t)raw : (int32_t)raw;
            push_pico_fixed(L, signed_raw);
            return 1;
        }

        // Binary (0b) Handling
        else if (!force_hex && remaining > 2 && cstr[0] == '0' && (cstr[1] == 'b' || cstr[1] == 'B'))
        {
            const char *bin = cstr + 2;
            long int_part = 0;
            double frac_part = 0.0;
            double div = 2.0;
            bool in_frac = false;

            while (*bin)
            {
                if (*bin == '.')
                {
                    if (in_frac)
                        break;
                    in_frac = true;
                }
                else if (*bin == '0' || *bin == '1')
                {
                    if (!in_frac)
                    {
                        int_part = (int_part << 1) | (*bin - '0');
                    }
                    else
                    {
                        if (*bin == '1')
                            frac_part += (1.0 / div);
                        div *= 2.0;
                    }
                }
                else
                {
                    break;
                }
                bin++;
            }
            end = (char *)bin;
            res = (double)int_part + frac_part;
        }
        else
        {
            // Standard Decimal
            res = strtod(cstr, &end);
        }

        // PICO-8 Tolerance: Skip trailing whitespace
        while (*end && isspace((unsigned char)*end))
        {
            end++;
        }

        // Strictness: Any NON-SPACE trailing garbage = nil
        if (end == cstr || *end != '\0')
        {
            lua_pushnil(L);
        }
        else
        {
            lua_pushnumber(L, res);
        }
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

// --- Lua Binding: tonum(val) ---
static int l_tonum(lua_State *L)
{
    // Standard Lua call expects arg at index 1
    int flags = 0;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
    {
        if (lua_isboolean(L, 2))
            flags = lua_toboolean(L, 2) ? 1 : 0;
        else
            flags = (int)lua_tonumber(L, 2);
    }
    return internal_tonum(L, 1, flags);
}

// --- Lua Binding: split(str, [sep], [convert]) ---
static int l_split(lua_State *L)
{
    REAL8_TRACE_API("split");
    {
        lua_Debug ar;
        if (lua_getstack(L, 1, &ar)) {
            lua_getinfo(L, "Sl", &ar);
            real8_set_last_lua_line(ar.currentline, ar.short_src);
        }
    }
    size_t len;
    if (lua_gettop(L) == 0 || lua_isnil(L, 1))
    {
        lua_newtable(L);
        return 1;
    }
    const char *str = luaL_checklstring(L, 1, &len); // Check Index 1
    const char *sep = ",";
    size_t sep_len = 1;
    bool convert_nums = true;

    // Arg 2: Separator
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
    {
        if (lua_type(L, 2) != LUA_TNUMBER)
        {
            sep = luaL_checklstring(L, 2, &sep_len);
        }
    }

    // Arg 3: Convert numbers (default true)
    if (lua_gettop(L) >= 3)
    {
        convert_nums = lua_toboolean(L, 3);
    }

    lua_newtable(L); // Result table
    int table_idx = 1;

    // Handle empty separator (char by char)
    if (sep_len == 0)
    {
        for (size_t i = 0; i < len; i++)
        {
            lua_pushlstring(L, &str[i], 1); // Push token

            if (convert_nums && (isdigit((unsigned char)str[i]) || str[i] == '-' || str[i] == '.'))
            {
                // Try convert token at top of stack (-1)
                internal_tonum(L, -1, 0);
                if (!lua_isnil(L, -1))
                {
                    lua_remove(L, -2); // remove string, keep number
                }
                else
                {
                    lua_pop(L, 1); // pop nil, keep string
                }
            }
            lua_rawseti(L, -2, table_idx++);
        }
        return 1;
    }

    // Standard split
    const char *ptr = str;
    const char *end = str + len;
    
    // Loop condition and logic to capture trailing empty strings
    while (ptr < end)
    {
        const char *found = NULL;
        if (sep_len <= (size_t)(end - ptr))
        {
            const char *scan = ptr;
            const char *limit = end - sep_len;
            for (; scan <= limit; ++scan)
            {
                if (memcmp(scan, sep, sep_len) == 0)
                {
                    found = scan;
                    break;
                }
            }
        }

        const char *token_end = found ? found : end;
        size_t token_len = (size_t)(token_end - ptr);

        lua_pushlstring(L, ptr, token_len); // Push token

        if (convert_nums)
        {
            // Simple check to avoid running full parser on obvious text
            if (token_len > 0 && (isdigit((unsigned char)ptr[0]) || ptr[0] == '-' || ptr[0] == '.'))
            {
                // Convert token at top of stack (-1)
                internal_tonum(L, -1, 0);
                if (!lua_isnil(L, -1))
                {
                    lua_remove(L, -2); // remove string, keep number
                }
                else
                {
                    lua_pop(L, 1); // pop nil, keep string
                }
            }
        }

        lua_rawseti(L, -2, table_idx++);

        if (!found)
            break;
            
        ptr = token_end + sep_len;
        
        // If the separator was at the very end, push the final empty string
        if (ptr == end) {
            lua_pushstring(L, "");
            lua_rawseti(L, -2, table_idx++);
            break;
        }
    }

    return 1;
}

static int l_add(lua_State *L)
{
    REAL8_TRACE_API("add");
    luaL_checktype(L, 1, LUA_TTABLE);
    int len = (int)lua_rawlen(L, 1);

    // Case 1: add(t, v, index)
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
    {
        int idx = to_int_floor(L, 3);
        lua_pushvalue(L, 2); // Push v

        // This is effectively table.insert(t, idx, v)
        // We need to shift elements up if idx <= len.
        // Lua's table.insert does this, but since we are in C, we can just call table.insert helper logic
        // Or essentially use lua_rotate if using Lua 5.3 API, but simple way:

        // Move elements up
        for (int i = len; i >= idx; --i)
        {
            lua_rawgeti(L, 1, i);
            lua_rawseti(L, 1, i + 1);
        }
        lua_rawseti(L, 1, idx);

        lua_pushvalue(L, 2); // return v
        return 1;
    }

    // Case 2: add(t, v) - Append
    lua_pushvalue(L, 2);
    lua_rawseti(L, 1, len + 1);
    lua_pushvalue(L, 2);
    return 1;
}

// Replaces Lua 'del'
static int l_del(lua_State *L)
{
    REAL8_TRACE_API("del");
    luaL_checktype(L, 1, LUA_TTABLE);
    // del(t, v) removes first instance of v, returns v
    if (lua_gettop(L) < 2)
        return 0;

    int len = (int)lua_rawlen(L, 1);
    bool found = false;

    // Scan table
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, 1, i);
        if (lua_compare(L, -1, 2, LUA_OPEQ))
        {
            lua_pop(L, 1); // pop item

            // Move everything down (Lua table.remove logic)
            // This is slow O(N), but matches PICO-8 behavior
            for (int j = i; j < len; j++)
            {
                lua_rawgeti(L, 1, j + 1);
                lua_rawseti(L, 1, j);
            }
            lua_pushnil(L);
            lua_rawseti(L, 1, len); // Clear last

            found = true;
            break;
        }
        lua_pop(L, 1);
    }

    if (found)
        lua_pushvalue(L, 2); // Return deleted item
    else
        lua_pushnil(L);
    return 1;
}

static int l_flr(lua_State *L)
{
    double x = luaL_optnumber(L, 1, 0.0);
    lua_pushnumber(L, floor(x));
    return 1;
}

static int l_ceil(lua_State *L)
{
    double x = luaL_optnumber(L, 1, 0.0);
    lua_pushnumber(L, ceil(x));
    return 1;
}

static int l_abs(lua_State *L)
{
    double x = luaL_optnumber(L, 1, 0.0);
    lua_pushnumber(L, fabs(x)); // fabs for floats
    return 1;
}

static int l_sgn(lua_State *L)
{
    double x = luaL_optnumber(L, 1, 0.0);
    // PICO-8 sgn: x < 0 -> -1, x >= 0 -> 1
    lua_pushinteger(L, (x < 0) ? -1 : 1);
    return 1;
}

// Replaces Lua 'deli' (delete by index)
static int l_deli(lua_State *L)
{
    REAL8_TRACE_API("deli");
    luaL_checktype(L, 1, LUA_TTABLE);
    int len = (int)lua_rawlen(L, 1);
    // Default index is length (remove last)
    int idx = (lua_gettop(L) >= 2) ? to_int_floor(L, 2) : len;

    if (idx < 1 || idx > len)
    {
        lua_pushnil(L);
        return 1;
    }

    // Get value to return
    lua_rawgeti(L, 1, idx);

    // Shift elements down
    for (int i = idx; i < len; i++)
    {
        lua_rawgeti(L, 1, i + 1);
        lua_rawseti(L, 1, i);
    }

    // Clear last element
    lua_pushnil(L);
    lua_rawseti(L, 1, len);

    return 1; // Return the deleted value
}

static int l_count(lua_State *L)
{
    if (lua_isnil(L, 1))
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);

    // count(t) -> return length
    if (lua_gettop(L) == 1 || lua_isnil(L, 2))
    {
        lua_pushinteger(L, lua_rawlen(L, 1));
        return 1;
    }

    // count(t, val) -> count instances of val
    int count = 0;
    int len = (int)lua_rawlen(L, 1);

    // We must traverse 1..len (PICO-8 arrays)
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, 1, i);
        if (lua_compare(L, -1, 2, LUA_OPEQ))
        {
            count++;
        }
        lua_pop(L, 1);
    }
    lua_pushinteger(L, count);
    return 1;
}

// Iterator function called by the for-loop
static int l_all_iter(lua_State *L)
{
    // Upvalue 1: Table
    // Upvalue 2: Current Index (integer)

    int i = lua_tointeger(L, lua_upvalueindex(2));
    i++;

    // Update Index
    lua_pushinteger(L, i);
    lua_replace(L, lua_upvalueindex(2));

    // Get t[i]
    lua_rawgeti(L, lua_upvalueindex(1), i);

    if (lua_isnil(L, -1))
    {
        return 0; // Stop iteration
    }

    return 1; // Return value
}

// The all(t) function
static int l_all(lua_State *L)
{
    REAL8_TRACE_API("all");
    // If the argument is nil, return a dummy iterator that does nothing.
    // This prevents crashes when iterating over uninitialized tables.
    if (lua_isnil(L, 1))
    {
        lua_pushcfunction(L, [](lua_State *L) -> int
                          { return 0; });
        return 1;
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    lua_pushvalue(L, 1);   // Push table (becomes Upvalue 1)
    lua_pushinteger(L, 0); // Push index 0 (becomes Upvalue 2)

    // Push closure with 2 upvalues
    lua_pushcclosure(L, l_all_iter, 2);
    return 1;
}

static int l_chr(lua_State *L)
{
    int val = to_int_floor(L, 1) & 0xFF;
    char c = (char)val;
    lua_pushlstring(L, &c, 1);
    return 1;
}

static int l_ord(lua_State *L)
{
    REAL8_TRACE_API("ord");
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);

    // PICO-8 Default: Index 1.
    // If arg 2 is present and NOT nil, use it. Otherwise default to 1.
    int idx = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? to_int_floor(L, 2) : 1;

    // Convert 1-based (Lua) to 0-based (C)
    idx--;

    int count = (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ? to_int_floor(L, 3) : 1;
    if (count < 1)
    {
        lua_pushnil(L);
        return 1;
    }

    int pushed = 0;
    for (int i = 0; i < count; ++i)
    {
        int pos = idx + i;
        if (pos >= 0 && pos < (int)len)
        {
            lua_pushinteger(L, (unsigned char)s[pos]);
        }
        else
        {
            lua_pushnil(L);
        }
        pushed++;
    }
    return pushed;
}

static int l_tostr(lua_State *L)
{
    // 1. Handle nil
    if (lua_isnil(L, 1))
    {
        lua_pushstring(L, "[nil]");
        return 1;
    }

    // 2. Handle boolean
    if (lua_isboolean(L, 1))
    {
        lua_pushstring(L, lua_toboolean(L, 1) ? "true" : "false");
        return 1;
    }

    // 3. Handle string
    if (lua_type(L, 1) == LUA_TSTRING)
    {
        lua_pushvalue(L, 1);
        return 1;
    }

    // 4. Handle Number with Flags
    // tostr(val, [flags])
    int flags = 0;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
    {
        if (lua_isboolean(L, 2))
            flags = lua_toboolean(L, 2) ? 1 : 0;
        else
            flags = (int)lua_tonumber(L, 2);
    }

    if (flags & 2)
    {
        int32_t fixed = to_pico_fixed(L, 1);
        char buf[4];
        buf[0] = (char)(fixed & 0xFF);
        buf[1] = (char)((fixed >> 8) & 0xFF);
        buf[2] = (char)((fixed >> 16) & 0xFF);
        buf[3] = (char)((fixed >> 24) & 0xFF);
        lua_pushlstring(L, buf, 4);
    }
    else if (flags & 1)
    {
        double val = lua_tonumber(L, 1);
        // Convert to PICO-8 16.16 fixed point
        int32_t fixed = (int32_t)(val * 65536.0);
        uint16_t upper = (fixed >> 16) & 0xFFFF;
        uint16_t lower = fixed & 0xFFFF;

        char buf[16];
        snprintf(buf, 16, "0x%04x.%04x", upper, lower);
        lua_pushstring(L, buf);
    }
    else
    {
        // Standard number to string
        lua_pushstring(L, lua_tostring(L, 1));
    }

    return 1;
}

static int l_type(lua_State *L)
{
    int t = lua_type(L, 1);
    if (t == LUA_TBOOLEAN)
    {
        lua_pushstring(L, "bool");
    }
    else
    {
        lua_pushstring(L, lua_typename(L, t));
    }
    return 1;
}

static int l_getmetatable(lua_State *L)
{
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1))
    {
        lua_pushnil(L);
        return 1;
    }

    // If __metatable is set, return it instead of the real metatable.
    lua_pushstring(L, "__metatable");
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1))
    {
        return 1;
    }

    // Pop the nil __metatable; leave the real metatable on top.
    lua_pop(L, 1);
    return 1;
}

static int l_setmetatable(lua_State *L)
{
    int t = lua_type(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table expected");
    if (luaL_getmetafield(L, 1, "__metatable"))
    {
        return luaL_error(L, "cannot change a protected metatable");
    }
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    return 1;
}

static int l_rawequal(lua_State *L)
{
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    lua_pushboolean(L, lua_rawequal(L, 1, 2));
    return 1;
}

static int l_rawlen(lua_State *L)
{
    int t = lua_type(L, 1);
    luaL_argcheck(L, t == LUA_TTABLE || t == LUA_TSTRING, 1, "table or string expected");
    lua_pushinteger(L, lua_rawlen(L, 1));
    return 1;
}

static int l_rawget(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    lua_rawget(L, 1);
    return 1;
}

static int l_rawset(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    luaL_checkany(L, 3);
    lua_settop(L, 3);
    lua_rawset(L, 1);
    return 1;
}

static int l_select(lua_State *L)
{
    int n = lua_gettop(L);
    if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#')
    {
        lua_pushinteger(L, n - 1);
        return 1;
    }

    int i = luaL_checkint(L, 1);
    if (i < 0)
        i = n + i;
    else if (i > n)
        i = n;
    luaL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - i;
}

static int l_zero_len(lua_State *L)
{
    lua_pushinteger(L, 0);
    return 1;
}

static int l_string_len(lua_State *L)
{
    luaL_checkany(L, 1);
    lua_len(L, 1);
    return 1;
}

static int l_string_index(lua_State *L)
{
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        // string.sub(s, k, k)
        lua_getglobal(L, "string");
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "sub");
            if (lua_isfunction(L, -1))
            {
                lua_pushvalue(L, 1);
                lua_pushvalue(L, 2);
                lua_pushvalue(L, 2);
                lua_call(L, 3, 1);

                size_t clen = 0;
                const char *c = lua_tolstring(L, -1, &clen);
                if (c && clen == 1 && c[0] >= '0' && c[0] <= '9')
                {
                    lua_pop(L, 2); // pop string table and char
                    lua_pushinteger(L, (lua_Integer)(c[0] - '0'));
                    return 1;
                }

                lua_remove(L, -2); // remove string table, keep char
                return 1;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }

    if (lua_type(L, 2) == LUA_TSTRING)
    {
        lua_getglobal(L, "string");
        if (lua_istable(L, -1))
        {
            lua_pushvalue(L, 2);
            lua_rawget(L, -2);
            if (!lua_isnil(L, -1))
            {
                lua_remove(L, -2);
                return 1;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        const char *k = lua_tostring(L, 2);
        if (k)
        {
            if (strcmp(k, "sub") == 0)
            {
                lua_getglobal(L, "string");
                if (lua_istable(L, -1))
                {
                    lua_getfield(L, -1, "sub");
                    lua_remove(L, -2);
                    return 1;
                }
                lua_pop(L, 1);
            }
            else if (strcmp(k, "len") == 0)
            {
                lua_pushcfunction(L, l_string_len);
                return 1;
            }
        }
    }

    lua_pushnil(L);
    return 1;
}

static bool push_global_table(lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }
    return lua_istable(L, -1);
}

static int l_foreach(lua_State *L)
{
    // PICO-8 compatibility: foreach(nil, f) should simply do nothing, not crash.
    if (lua_isnil(L, 1))
    {
        return 0;
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // If you implemented 'all' or 'count' logic to handle arrays
    // PICO-8 uses 1-based indexing up to #t
    int len = (int)lua_rawlen(L, 1);

    for (int i = 1; i <= len; ++i)
    {
        lua_pushvalue(L, 2);  // Push function
        lua_rawgeti(L, 1, i); // Push t[i]

        // PICO-8 foreach passes the value.
        // Calling: f(val)
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            // Handle error if needed, or just pop error message
            lua_pop(L, 1);
        }
    }
    return 0;
}

// Add to your bindings
static int l_reload(lua_State *L)
{
    auto *vm = get_vm(L);
    // standard: reload(dest_addr, src_addr, len, [filename])
    // simplified for port: reload(dest, src, len) -> copies from VM->ROM to VM->RAM

    int dest = (int)luaL_optinteger(L, 1, 0);
    int src = (int)luaL_optinteger(L, 2, 0);
    int len = (int)luaL_optinteger(L, 3, 0x4300); // Default to generic GFX+Map size

    // Bounds checking
    if (dest < 0)
        dest = 0;
    if (src < 0)
        src = 0;
    if (dest + len > 0x8000)
        len = 0x8000 - dest;

    // Assuming vm->rom exists and holds the original cart data
    if (vm->rom && vm->ram && len > 0)
    {
        // If filename (arg 4) is present, PICO-8 loads from another cart.
        // For a basic port, you can ignore arg 4 or return 0 to indicate failure.
        size_t rom_size = (vm->rom_size != 0) ? vm->rom_size : (vm->rom_readonly ? 0u : 0x8000u);
        size_t copy_len = 0;
        if ((size_t)src < rom_size) {
            copy_len = std::min((size_t)len, rom_size - (size_t)src);
            memcpy(&vm->ram[dest], &vm->rom[src], copy_len);
        }
        if (copy_len < (size_t)len) {
            memset(&vm->ram[dest + copy_len], 0, (size_t)len - copy_len);
        }
    }
    return 0;
}

static int l_cstore(lua_State *L)
{
    auto *vm = get_vm(L);
    int dest = (int)luaL_optinteger(L, 1, 0);
    int src = (int)luaL_optinteger(L, 2, 0);
    int len = (int)luaL_optinteger(L, 3, 0x4300);

    // If args are 0,0,0 (or omitted), it means "flush all RAM to ROM"
    if (dest == 0 && src == 0 && len == 0)
        len = 0x4300;

    if (vm->ram)
    {
        // Apply mask 0x8000 to prevent overflow
        if (dest + len > 0x8000)
            len = 0x8000 - dest;
        if (src + len > 0x8000)
            len = 0x8000 - src;
        if (len <= 0)
            return 0;

        if (!vm->ensureWritableRom()) {
            if (vm->host) vm->host->log("[Real8] cstore: failed to allocate ROM buffer");
            return 0;
        }

        memcpy(&vm->rom[dest], &vm->ram[src], len);

        // Actually write to disk immediately (Standard PICO-8 behavior for cstore)
        vm->saveCartToDisk();
    }
    return 0;
}

static int l_rotl(lua_State *L)
{
    uint32_t x = (uint32_t)to_pico_fixed(L, 1);
    int n = to_int_floor(L, 2) & 31;
    uint32_t res = (x << n) | (x >> (32 - n));
    push_pico_fixed(L, (int32_t)res);
    return 1;
}

static int l_rotr(lua_State *L)
{
    uint32_t x = (uint32_t)to_pico_fixed(L, 1);
    int n = to_int_floor(L, 2) & 31;
    uint32_t res = (x >> n) | (x << (32 - n));
    push_pico_fixed(L, (int32_t)res);
    return 1;
}

static int l_serial(lua_State *L)
{
    auto *vm = get_vm(L);
    int channel = to_int_floor(L, 1);
    
    // --- 1. Standard Output / Debug (Channel 0) ---
    // Usage: serial(0, "Debug Log")
    if (channel == 0) {
        if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
            // Log to console/serial monitor
            const char* msg = lua_tostring(L, 2);
            if (vm->host) vm->host->log("%s", msg); 
        }
        return 0;
    }

    // --- 2. GPIO Write (Digital) ---
    // Usage: serial(0x800, pin, value)
    if (channel == 0x800) {
        int pin = to_int_floor(L, 2);
        int val = to_int_floor(L, 3);
        if (vm->host) vm->host->gpioWrite(pin, val);
        return 0;
    }

    // --- 3. GPIO Read (Digital) ---
    // Usage: val = serial(0x801, pin)
    if (channel == 0x801) {
        int pin = to_int_floor(L, 2);
        int val = (vm->host) ? vm->host->gpioRead(pin) : 0;
        lua_pushinteger(L, val);
        return 1; // Returns 1 value to Lua
    }

    // --- 4. Analog Write / PWM ---
    // Usage: serial(0x802, pin, duty_cycle)
    if (channel == 0x802) {
        int pin = to_int_floor(L, 2);
        int val = to_int_floor(L, 3); // 0-255 usually
        if (vm->host) vm->host->gpioAnalogWrite(pin, val);
        return 0;
    }

    // --- 5. Analog Read ---
    // Usage: val = serial(0x803, pin)
    if (channel == 0x803) {
        int pin = to_int_floor(L, 2);
        // Normalize 0.0 - 1.0 or 0 - 4095 depending on preference
        int val = (vm->host) ? vm->host->gpioAnalogRead(pin) : 0; 
        lua_pushinteger(L, val);
        return 1;
    }

    // --- 6. Bulk Data Stream (Official PICO-8 Style) ---
    // Usage: serial(0x400, ram_addr, len)
    // Great for NeoPixels or UART buffers
    if (channel == 0x400) { // Arbitrary channel for data stream
        int addr = to_int_floor(L, 2);
        int len = to_int_floor(L, 3);
        
        if (vm->ram && vm->host && addr >= 0 && addr + len <= 0x8000) {
            // Send raw pointer to host to handle platform specifics
            vm->host->sendSerialStream(&vm->ram[addr], len);
        }
        return 0;
    }

    return 0;
}

static int l_assert(lua_State *L)
{
    if (!lua_toboolean(L, 1))
    {
        const char *msg = luaL_optstring(L, 2, "assertion failed!");
        luaL_error(L, "%s", msg); // This triggers your HALT/RunFrame error catch
    }
    return lua_gettop(L);
}

static int l_holdframe(lua_State *L)
{
    auto *vm = get_vm(L);
    vm->skip_update_logic = true;
    return 0;
}

// Add this static function
static int l_inext(lua_State *L)
{
    // Arguments: table, index (optional)
    luaL_checktype(L, 1, LUA_TTABLE);
    int i = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? to_int_floor(L, 2) : 0;

    i++; // Next index

    // Check if t[i] exists
    lua_rawgeti(L, 1, i);
    if (lua_isnil(L, -1))
    {
        return 0; // End of loop
    }

    // Return: index, value
    lua_pushinteger(L, i); // Push index
    lua_insert(L, -2);     // Move index before value
    return 2;
}

static int l_pairs_empty(lua_State *L)
{
    (void)L;
    return 0;
}

static int l_pairsmeta(lua_State *L, const char *method, int iszero, lua_CFunction iter)
{
    // PICO-8 0.2.0 behavior: pairs(nil) returns an empty function
    if (lua_isnil(L, 1))
    {
        lua_pushcfunction(L, l_pairs_empty);
        return 1;
    }

    if (!luaL_getmetafield(L, 1, method))
    {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_pushcfunction(L, iter);
        lua_pushvalue(L, 1);
        if (iszero)
            lua_pushinteger(L, 0);
        else
            lua_pushnil(L);
    }
    else
    {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 3);
    }
    return 3;
}

static int l_next(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_next(L, 1))
        return 2;
    lua_pushnil(L);
    return 1;
}

static int l_pairs(lua_State *L)
{
    return l_pairsmeta(L, "__pairs", 0, l_next);
}

static int l_ipairs_aux(lua_State *L)
{
    int i = luaL_checkint(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    i++;
    lua_pushinteger(L, i);
    lua_rawgeti(L, 1, i);
    return lua_isnil(L, -1) ? 1 : 2;
}

static int l_ipairs(lua_State *L)
{
    return l_pairsmeta(L, "__ipairs", 1, l_ipairs_aux);
}

// --- SYSTEM STATE HELPERS ---
static int l_sys_get_state(lua_State* L) {
    // 1. Time
    unsigned long now = l_millis(L);
    double t = (double)(now - start_ms) / 1000.0;
    lua_pushnumber(L, t);
    // 2. RNG
    lua_pushnumber(L, (double)rng_seed);
    return 2;
}

static int l_sys_set_state(lua_State* L) {
    // 1. Restore Time
    double target_t = lua_tonumber(L, 1);
    unsigned long now = l_millis(L);
    start_ms = now - (unsigned long)(target_t * 1000.0);
    
    // 2. Restore RNG
    double seed = lua_tonumber(L, 2);
    rng_seed = (uint32_t)seed;
    return 0;
}

// In real8_bindings.cpp

int l_load_p8_code(lua_State* L) {
    size_t len;
    const char* raw_code = luaL_checklstring(L, 1, &len);
    std::string src(raw_code, len);
    
    // 1. Run C++ Transpiler
    std::string clean_lua = transpile_pico8(src);
    std::string normalized = p8_normalize_lua_strings(clean_lua);

    // 2. Load the transpiled code into Lua (compiles to bytecode)
    if (luaL_loadbuffer(L, normalized.c_str(), normalized.size(), "p8_code") != LUA_OK) {
        // Return nil, error_message
        lua_pushnil(L);
        lua_pushvalue(L, -2); // Error message is at top
        return 2;
    }
    
    // Return function (the compiled chunk)
    return 1;
}


static int l_pairs_safe(lua_State *L)
{
    if (lua_isnil(L, 1))
    {
        // Return an iterator that does nothing (returns nil immediately)
        lua_pushcfunction(L, [](lua_State *L) -> int
                          { return 0; });
        return 1;
    }
    // Fallback to standard Lua pairs
    lua_getglobal(L, "pairs"); // This gets the standard pairs from _G (renamed later)
    lua_insert(L, 1);          // Move standard pairs to function position
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

// Helper to get number from boolean or number
static inline double get_op_val(lua_State *L, int idx)
{
    if (lua_isboolean(L, idx))
        return lua_toboolean(L, idx) ? 1.0 : 0.0;
    return luaL_optnumber(L, idx, 0.0);
}

static int l_op_add(lua_State *L)
{
    lua_pushnumber(L, get_op_val(L, 1) + get_op_val(L, 2));
    return 1;
}
static int l_op_sub(lua_State *L)
{
    lua_pushnumber(L, get_op_val(L, 1) - get_op_val(L, 2));
    return 1;
}
static int l_op_mul(lua_State *L)
{
    lua_pushnumber(L, get_op_val(L, 1) * get_op_val(L, 2));
    return 1;
}

static int l_op_div(lua_State *L)
{
    double b = get_op_val(L, 2);
    if (b == 0)
        b = 0.0001; // Avoid DIV0 crash? PICO-8 behaves specifically here
    lua_pushnumber(L, get_op_val(L, 1) / b);
    return 1;
}

static int l_op_idiv(lua_State *L)
{
    double b = get_op_val(L, 2);
    if (b == 0)
        b = 0.0001;
    lua_pushnumber(L, floor(get_op_val(L, 1) / b));
    return 1;
}
static int l_op_mod(lua_State *L)
{
    // fmod in C is different than Lua %, but for PICO-8 basics:
    double a = get_op_val(L, 1);
    double b = get_op_val(L, 2);
    lua_pushnumber(L, a - floor(a / b) * b);
    return 1;
}

static int l_op_unm(lua_State *L)
{
    lua_pushnumber(L, -get_op_val(L, 1));
    return 1;
}

static void register_boolean_ops(lua_State *L)
{
    // PICO-8 allows arithmetic on booleans (true=1, false=0)
    // We attach metamethods to the boolean type
    lua_pushboolean(L, 1);
    if (lua_getmetatable(L, -1) == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 1);
        lua_pushvalue(L, -2);    // push table
        lua_setmetatable(L, -2); // set it as metatable
        lua_pop(L, 1);           // pop boolean
    }
    // The metatable is now at top of stack
    lua_pushcfunction(L, l_op_add);
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, l_op_sub);
    lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, l_op_mul);
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, l_op_div);
    lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, l_op_idiv);
    lua_setfield(L, -2, "__idiv");
    lua_pushcfunction(L, l_op_mod);
    lua_setfield(L, -2, "__mod");
    lua_pushcfunction(L, l_op_unm);
    lua_setfield(L, -2, "__unm");
    lua_pop(L, 1); // pop metatable
}

static void apply_pico8_shim_bindings(lua_State *L)
{
    // Ensure core tables exist
    push_global_table(L, "math");
    lua_pop(L, 1);
    push_global_table(L, "string");
    lua_pop(L, 1);
    push_global_table(L, "table");
    lua_pop(L, 1);

    // Aliases: t(), sub(), len()
    lua_getglobal(L, "t");
    bool has_t = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!has_t) {
        lua_getglobal(L, "time");
        if (!lua_isnil(L, -1)) {
            lua_setglobal(L, "t");
        } else {
            lua_pop(L, 1);
        }
    }

    lua_getglobal(L, "sub");
    bool need_sub = lua_isnil(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "len");
    bool need_len = lua_isnil(L, -1);
    lua_pop(L, 1);

    if ((need_sub || need_len) && push_global_table(L, "string")) {
        if (need_sub) {
            lua_getfield(L, -1, "sub");
            if (!lua_isnil(L, -1)) {
                lua_setglobal(L, "sub");
            } else {
                lua_pop(L, 1);
            }
        }

        if (need_len) {
            lua_getfield(L, -1, "len");
            if (!lua_isnil(L, -1)) {
                lua_setglobal(L, "len");
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    } else if (need_sub || need_len) {
        lua_pop(L, 1);
    }

    // math.atan2 alias
    if (push_global_table(L, "math")) {
        lua_getfield(L, -1, "atan2");
        bool has_atan2 = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!has_atan2) {
            lua_getglobal(L, "atan2");
            if (!lua_isnil(L, -1)) {
                lua_setfield(L, -2, "atan2");
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }

    // loadstring alias
    lua_getglobal(L, "loadstring");
    bool has_loadstring = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!has_loadstring) {
        lua_getglobal(L, "p8_load");
        if (!lua_isnil(L, -1)) {
            lua_setglobal(L, "loadstring");
        } else {
            lua_pop(L, 1);
        }
    }

    // mapdraw alias
    lua_getglobal(L, "mapdraw");
    bool has_mapdraw = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!has_mapdraw) {
        lua_getglobal(L, "map");
        if (!lua_isnil(L, -1)) {
            lua_setglobal(L, "mapdraw");
        } else {
            lua_pop(L, 1);
        }
    }

    // Button glyph globals
    static const unsigned char kBtnGlyph0[] = {0xC3, 0xA2, 0xC2, 0xAC, 0xE2, 0x80, 0xA6, 0xC3, 0xAF, 0xC2, 0xB8, 0xC2, 0x8F};
    static const unsigned char kBtnGlyph1[] = {0xC3, 0xA2, 0xC5, 0xBE, 0xC2, 0xA1, 0xC3, 0xAF, 0xC2, 0xB8, 0xC2, 0x8F};
    static const unsigned char kBtnGlyph2[] = {0xC3, 0xA2, 0xC2, 0xAC, 0xE2, 0x80, 0xA0, 0xC3, 0xAF, 0xC2, 0xB8, 0xC2, 0x8F};
    static const unsigned char kBtnGlyph3[] = {0xC3, 0xA2, 0xC2, 0xAC, 0xE2, 0x80, 0xA1, 0xC3, 0xAF, 0xC2, 0xB8, 0xC2, 0x8F};
    static const unsigned char kBtnGlyph4[] = {0xC3, 0xB0, 0xC5, 0xB8, 0xE2, 0x80, 0xA6, 0xC2, 0xBE, 0xC3, 0xAF, 0xC2, 0xB8, 0xC2, 0x8F};
    static const unsigned char kBtnGlyph5[] = {0xC3, 0xA2, 0xC2, 0x9D, 0xC5, 0xBD};

    lua_pushglobaltable(L);
    lua_pushlstring(L, (const char *)kBtnGlyph0, sizeof(kBtnGlyph0));
    lua_pushinteger(L, 0);
    lua_rawset(L, -3);
    lua_pushlstring(L, (const char *)kBtnGlyph1, sizeof(kBtnGlyph1));
    lua_pushinteger(L, 1);
    lua_rawset(L, -3);
    lua_pushlstring(L, (const char *)kBtnGlyph2, sizeof(kBtnGlyph2));
    lua_pushinteger(L, 2);
    lua_rawset(L, -3);
    lua_pushlstring(L, (const char *)kBtnGlyph3, sizeof(kBtnGlyph3));
    lua_pushinteger(L, 3);
    lua_rawset(L, -3);
    lua_pushlstring(L, (const char *)kBtnGlyph4, sizeof(kBtnGlyph4));
    lua_pushinteger(L, 4);
    lua_rawset(L, -3);
    lua_pushlstring(L, (const char *)kBtnGlyph5, sizeof(kBtnGlyph5));
    lua_pushinteger(L, 5);
    lua_rawset(L, -3);

    unsigned char c = 0;
    c = 139; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 0); lua_rawset(L, -3);
    c = 145; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 1); lua_rawset(L, -3);
    c = 148; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 2); lua_rawset(L, -3);
    c = 131; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 3); lua_rawset(L, -3);
    c = 142; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 4); lua_rawset(L, -3);
    c = 151; lua_pushlstring(L, (const char *)&c, 1); lua_pushinteger(L, 5); lua_rawset(L, -3);
    lua_pop(L, 1);

    // __len for nil and 0
    lua_pushinteger(L, 0);
    lua_newtable(L);
    lua_pushcfunction(L, l_zero_len);
    lua_setfield(L, -2, "__len");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);

    lua_pushnil(L);
    lua_newtable(L);
    lua_pushcfunction(L, l_zero_len);
    lua_setfield(L, -2, "__len");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);

    // String metatable __index override
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1) == 0) {
        lua_newtable(L);
    }
    lua_pushcfunction(L, l_string_index);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

void register_pico8_api(lua_State *L)
{
    lua_getglobal(L, "__pico8_vm_ptr");
    g_vm = (Real8VM *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    auto *vm = g_vm;
    const bool isGba = (vm && vm->host && strcmp(vm->host->getPlatform(), "GBA") == 0);
    auto gbaLog = [&](const char* msg) {
        if (isGba && vm->host) vm->host->log("%s", msg);
    };

    gbaLog("[BOOT] REG BEGIN");

    lua_pushnil(L);
    lua_setglobal(L, "io");
    lua_pushnil(L);
    lua_setglobal(L, "os");
    lua_pushnil(L);
    lua_setglobal(L, "package");
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    gbaLog("[BOOT] REG LIBS OK");

    // --- Graphics ---
    reg(L, "cls", l_cls);
    reg(L, "pset", l_pset);
    reg(L, "pget", l_pget);
    reg(L, "line", l_line);
    reg(L, "rect", l_rect);
    reg(L, "rectfill", l_rectfill);
    reg(L, "rrect", l_rrect);
    reg(L, "rrectfill", l_rrectfill);
    reg(L, "circ", l_circ);
    reg(L, "circfill", l_circfill);
    reg(L, "oval", l_oval);
    reg(L, "ovalfill", l_ovalfill);
    reg(L, "tline", l_tline);
    reg(L, "spr", l_spr);
    reg(L, "sspr", l_sspr);
    reg(L, "sget", l_sget);
    reg(L, "sset", l_sset);
    reg(L, "fget", l_fget);
    reg(L, "fset", l_fset);
    reg(L, "pal", l_pal);
    reg(L, "palt", l_palt);
    reg(L, "fillp", l_fillp);
    reg(L, "camera", l_camera);
    reg(L, "clip", l_clip);
    reg(L, "color", l_color);
    reg(L, "cursor", l_cursor);
    gbaLog("[BOOT] REG GFX OK");

    // --- Map ---
    reg(L, "map", l_map);
    reg(L, "mget", l_mget);
    reg(L, "mset", l_mset);

    // Optimization: Point both to the same implementation
    reg(L, "check_flag", l_map_check);
    reg(L, "_map_check_cpu", l_map_check);
    gbaLog("[BOOT] REG MAP OK");

    // --- Math ---
    reg(L, "sin", l_sin);
    reg(L, "cos", l_cos);
    reg(L, "atan2", l_atan2);
    reg(L, "atan", l_atan);
    reg(L, "sqrt", l_sqrt);
    reg(L, "min", l_min);
    reg(L, "max", l_max);
    reg(L, "mid", l_mid);
    reg(L, "flr", l_flr);
    reg(L, "ceil", l_ceil);
    reg(L, "abs", l_abs);
    reg(L, "sgn", l_sgn);
    reg(L, "srand", l_srand);
    reg(L, "rnd", l_rnd);
    reg(L, "rotl", l_rotl);
    reg(L, "rotr", l_rotr);
    gbaLog("[BOOT] REG MATH OK");

    // --- Bitwise (Direct calls) ---
    reg(L, "band", l_band);
    reg(L, "bor", l_bor);
    reg(L, "bxor", l_bxor);
    reg(L, "bnot", l_bnot);
    reg(L, "shl", l_shl);
    reg(L, "shr", l_shr);
    gbaLog("[BOOT] REG BIT OK");

    reg(L, "p8_loadstring", l_load_p8_code);

    // --- String/Types ---
    reg(L, "tostr", l_tostr);
    reg(L, "tonum", l_tonum);
    reg(L, "chr", l_chr);
    reg(L, "ord", l_ord);
    reg(L, "sub", l_sub);
    reg(L, "split", l_split);
    reg(L, "type", l_type);
    reg(L, "getmetatable", l_getmetatable);
    reg(L, "setmetatable", l_setmetatable);
    reg(L, "rawequal", l_rawequal);
    reg(L, "rawlen", l_rawlen);
    reg(L, "rawget", l_rawget);
    reg(L, "rawset", l_rawset);
    reg(L, "select", l_select);
    gbaLog("[BOOT] REG STR OK");

    // --- Tables ---
    reg(L, "add", l_add);
    reg(L, "del", l_del);
    reg(L, "deli", l_deli);
    reg(L, "count", l_count);
    reg(L, "all", l_all);
    reg(L, "all_iter", l_all_iter);
    reg(L, "foreach", l_foreach);
    reg(L, "pairs", l_pairs);
    reg(L, "ipairs", l_ipairs);
    reg(L, "next", l_next);
    gbaLog("[BOOT] REG TABLE OK");

    // --- Memory ---
    reg(L, "peek", l_peek);
    reg(L, "poke", l_poke);
    reg(L, "peek2", l_peek2);
    reg(L, "poke2", l_poke2);
    reg(L, "peek4", l_peek4);
    reg(L, "poke4", l_poke4);
    reg(L, "memcpy", l_memcpy);
    reg(L, "memset", l_memset);
    reg(L, "dget", l_dget);
    reg(L, "dset", l_dset);
    reg(L, "cartdata", l_cartdata);
    reg(L, "reload", l_reload);

    reg(L, "_p8_sys_get", l_sys_get_state);
    reg(L, "_p8_sys_set", l_sys_set_state);
    gbaLog("[BOOT] REG MEM OK");

    // --- System/IO ---
    reg(L, "run", l_run);
    reg(L, "stop", l_stop);
    reg(L, "extcmd", l_extcmd);
    reg(L, "yield", l_yield);
    reg(L, "flip", l_flip);
    reg(L, "time", l_time);
    reg(L, "stat", l_stat);
    reg(L, "printh", l_printh);
    reg(L, "menuitem", l_menuitem);
    reg(L, "reload", l_reload);
    reg(L, "cstore", l_cstore);
    reg(L, "serial", l_serial);
    reg(L, "pairs_safe", l_pairs_safe);
    reg(L, "assert", l_assert);
    reg(L, "holdframe", l_holdframe);
    reg(L, "inext", l_inext);
    reg(L, "lshr", l_lshr);
    reg(L, "trace", l_printh);
    reg(L, "print", l_print);
    gbaLog("[BOOT] REG SYS OK");

    // --- Audio ---
    reg(L, "sfx", l_sfx);
    reg(L, "music", l_music);

    // --- Input ---
    reg(L, "btn", l_btn);
    reg(L, "btnp", l_btnp);
    gbaLog("[BOOT] REG AIN OK");

    // --- Internal/Helpers ---
    register_boolean_ops(L); // Operator overloading
    reg(L, "p8_load", l_load_p8_file);
    gbaLog("[BOOT] REG HELPERS OK");

    // Coroutines
    lua_getglobal(L, "coroutine");
    lua_getfield(L, -1, "create");
    lua_setglobal(L, "cocreate");
    lua_getfield(L, -1, "resume");
    lua_setglobal(L, "coresume");
    lua_getfield(L, -1, "status");
    lua_setglobal(L, "costatus");
    lua_pop(L, 1);
    gbaLog("[BOOT] REG CORO OK");

    // Table Pack/Unpack (Lua 5.3 Compat)
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "pack");
    lua_setglobal(L, "pack");
    lua_getfield(L, -1, "unpack");
    lua_setglobal(L, "unpack");
    lua_pop(L, 1);
    gbaLog("[BOOT] REG PACK OK");

    apply_pico8_shim_bindings(L);

    start_ms = l_millis(L);

    const char *shim = R"LUASHIM(
    local _G = _G

    local system_libs = {}
    if _G then system_libs[_G] = true end
    if math then system_libs[math] = true end
    if string then system_libs[string] = true end
    if table then system_libs[table] = true end
    if coroutine then system_libs[coroutine] = true end
    if os then system_libs[os] = true end
    if debug then system_libs[debug] = true end
    if package then system_libs[package] = true end
    if io then system_libs[io] = true end

    local function ser_str(s) return string.format("%q", s) end
    local function ser_key(k)
        if type(k)=="number" then return "["..k.."]" end
        if type(k)=="string" and string.match(k, "^[_%a][_%w]*$") then return k end
        return "['"..string.gsub(tostring(k), "\n", "\\n").."']"
    end

    function _p8_save_state()
        printh("[LUA] Saving State (Final)...")
        collectgarbage()
        
        -- CAPTURE SYSTEM STATE (Time/RNG)
        local st, sr = _p8_sys_get()
        _G._P8_SYS = { t=st, r=sr }

        local seen_objs = {}
        local obj_list = {}
        local id_counter = 0
        local queue = {} 
        local q_head = 1
        local q_tail = 1

        local function enqueue(val)
            if type(val) == "table" and not system_libs[val] and not seen_objs[val] then
                id_counter = id_counter + 1
                seen_objs[val] = id_counter
                obj_list[id_counter] = val
                queue[q_tail] = val
                q_tail = q_tail + 1
            end
        end

        for k,v in pairs(_G) do
            local skip = false
            if type(k)=="string" and (k=="_G" or k=="_ENV" or k=="math" or k=="string" or k=="table" or k=="_P8S") then skip=true end
            if not skip then enqueue(k); enqueue(v) end
        end

        while q_head < q_tail do
            local obj = queue[q_head]; q_head = q_head + 1
            for k,v in pairs(obj) do enqueue(k); enqueue(v) end
            local mt = getmetatable(obj)
            if mt then enqueue(mt) end
        end

        local chunks = {}; local current_chunk = {}; local chunk_size = 0; local MAX_CHUNK = 12000
        local function flush_chunk(is_root)
             table.insert(chunks, table.concat(current_chunk))
             table.insert(chunks, "--|CHUNK|--")
             current_chunk = {}; chunk_size = 0
             if is_root then table.insert(current_chunk, "local r=_G._P8S.root; ")
             else table.insert(current_chunk, "local o=_G._P8S.objs; ") end
        end

        table.insert(current_chunk, "_G._P8S={objs={}, root={}}; local o=_G._P8S.objs; ")
        chunk_size = 50

        for i=1, id_counter do
            local obj = obj_list[i]
            local parts = { "o["..i.."]={" }
            for k,v in pairs(obj) do
                local k_str, v_str
                if type(k)=="table" and seen_objs[k] then k_str="{"..seen_objs[k].."}"
                elseif type(k)=="string" or type(k)=="number" or type(k)=="boolean" then k_str=ser_key(k) end
                
                if type(v)=="table" and seen_objs[v] then v_str="{"..seen_objs[v].."}"
                elseif type(v)=="number" or type(v)=="boolean" then v_str=tostring(v)
                elseif type(v)=="string" then v_str=ser_str(v) end
                
                if k_str and v_str then table.insert(parts, k_str .. "=" .. v_str .. ",") end
            end
            local mt = getmetatable(obj)
            if mt and seen_objs[mt] then table.insert(parts, "__p8_mt={"..seen_objs[mt].."},") end
            table.insert(parts, "};")
            local line = table.concat(parts); table.insert(current_chunk, line); chunk_size = chunk_size + #line
            if chunk_size > MAX_CHUNK then flush_chunk(false) end
        end
        flush_chunk(false) 
        
        table.insert(current_chunk, "local r=_G._P8S.root; ")
        for k,v in pairs(_G) do
            local skip = false
            if type(k)=="string" and (k=="_G" or k=="_ENV" or k=="math" or k=="string" or k=="table" or k=="_P8S") then skip=true end
            if not skip then
                local v_str
                if type(v)=="table" and seen_objs[v] then v_str="{"..seen_objs[v].."}"
                elseif type(v)=="number" or type(v)=="boolean" then v_str=tostring(v)
                elseif type(v)=="string" then v_str=ser_str(v) end
                if v_str then
                    local assignment
                    if type(k)=="string" then assignment = "r[" .. ser_str(k) .. "]=" .. v_str .. ";"
                    elseif type(k)=="number" then assignment = "r[" .. tostring(k) .. "]=" .. v_str .. ";"
                    elseif type(k)=="boolean" then assignment = "r[" .. tostring(k) .. "]=" .. v_str .. ";"
                    end
                    if assignment then table.insert(current_chunk, assignment) end
                end
            end
        end
        
        _G._P8_SYS = nil -- Cleanup temp
        table.insert(chunks, table.concat(current_chunk))
        return table.concat(chunks)
    end

    function _p8_clear_state()
       printh("[LUA] Clearing State...")
       _G._P8S = nil; collectgarbage()
    end
    function _p8_load_chunk(str)
       local fn, e = load(str); if not fn then printh("ParseErr:"..tostring(e)) return end
       local ok, e2 = pcall(fn); if not ok then printh("ExecErr:"..tostring(e2)) return end
    end
    function _p8_apply_state()
       printh("[LUA] Applying State...")
       local data = _G._P8S
       if not data or not data.objs then return end
       local objs = data.objs
       local function resolve(v)
           if type(v)=="table" and v[1] then return objs[v[1]] end
           return v
       end
       for id, obj in pairs(objs) do
           for k,v in pairs(obj) do
               local rk = resolve(k); local rv = resolve(v)
               if rk ~= k then obj[k] = nil; obj[rk] = rv else obj[k] = rv end
           end
           if obj.__p8_mt then
               local mt = resolve(obj.__p8_mt)
               if type(mt)=="table" then setmetatable(obj, mt) end
               obj.__p8_mt = nil
           end
       end
       for k,v in pairs(data.root) do
           local fv = resolve(v)
           if k ~= "_G" and k ~= "_ENV" and not system_libs[_G[k]] then _G[k] = fv end
       end
       
       -- RESTORE SYSTEM STATE (Time/RNG)
       if _G._P8_SYS then
           _p8_sys_set(_G._P8_SYS.t, _G._P8_SYS.r)
           _G._P8_SYS = nil
       end
       
       _G._P8S = nil
       printh("[LUA] Done.")
       collectgarbage()
    end


    )LUASHIM";
 
    const char *shim_src = shim;
    std::string shim_ascii;
    if (isGba) {
        shim_ascii.reserve(strlen(shim));
        const unsigned char *p = (const unsigned char *)shim;
        while (*p) {
            if (*p < 0x80) shim_ascii.push_back((char)*p);
            ++p;
        }
        shim_src = shim_ascii.c_str();
    }

    if (isGba) {
        gbaLog("[BOOT] REG SHIM SKIP");
    } else {
        gbaLog("[BOOT] REG SHIM LOAD");
        if (luaL_loadstring(L, shim_src) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            printf("Shim Error: %s\n", err ? err : "(unknown)");
            if (isGba && vm && vm->host) vm->host->log("[BOOT] REG SHIM LOAD ERR: %s", err ? err : "(unknown)");
            lua_pop(L, 1);
        }
        else
        {
            gbaLog("[BOOT] REG SHIM LOAD OK");
            gbaLog("[BOOT] REG SHIM EXEC");
            if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            {
                const char* err = lua_tostring(L, -1);
                printf("Shim Error: %s\n", err ? err : "(unknown)");
                if (isGba && vm && vm->host) vm->host->log("[BOOT] REG SHIM EXEC ERR: %s", err ? err : "(unknown)");
                lua_pop(L, 1);
            }
            else
            {
                gbaLog("[BOOT] REG SHIM EXEC OK");
            }
        }
    }

    const char *overlay = R"LUAFPS(
    function __p8_sys_overlay(fps)
      camera(0, 0)
      clip(0, 0, 128, 128)
      local bar_h = 8
      local bar_w = 32
      local y0 = 128 - bar_h  
      rectfill(0, y0, bar_w, 126, 0)
      print("FPS:"..tostr(fps), 2, y0 + 1, 11)
    end
  )LUAFPS";

    gbaLog("[BOOT] REG OVERLAY");
    if (luaL_dostring(L, overlay) != LUA_OK)
    {
        lua_pop(L, 1);
        gbaLog("[BOOT] REG OVERLAY ERR");
    } else {
        gbaLog("[BOOT] REG OVERLAY OK");
    }
    gbaLog("[BOOT] REG DONE");
}
