#pragma once

#include <vector>
#include <string>
#include <deque>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "../hal/real8_host.h"
#include "real8_memattrs.h"

#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
#include "real8_audio.h"
#include "real8_debugger.h"
#endif

#include "real8_gfx.h"
#include "real8_cart.h"

#include "../../lib/z8lua/lua.h"
#include "../../lib/z8lua/lauxlib.h"
#include "../../lib/z8lua/lualib.h"

#ifndef REAL8_PROFILE_GBA
#define REAL8_PROFILE_GBA 0
#endif

#if REAL8_PROFILE_GBA
#define REAL8_PROFILE_ENABLED 1
#else
#define REAL8_PROFILE_ENABLED 0
#endif

#if REAL8_PROFILE_ENABLED
#define REAL8_PROFILE_BEGIN(vm_ptr, id) do { if ((vm_ptr)) (vm_ptr)->profileBegin(id); } while(0)
#define REAL8_PROFILE_END(vm_ptr, id) do { if ((vm_ptr)) (vm_ptr)->profileEnd(id); } while(0)
#define REAL8_PROFILE_FRAME_BEGIN(vm_ptr) do { if ((vm_ptr)) (vm_ptr)->profileFrameBegin(); } while(0)
#define REAL8_PROFILE_FRAME_END(vm_ptr) do { if ((vm_ptr)) (vm_ptr)->profileFrameEnd(); } while(0)
#define REAL8_PROFILE_HOTSPOT(vm_ptr, id) do { if ((vm_ptr)) (vm_ptr)->profileHotspot(id); } while(0)
#else
#define REAL8_PROFILE_BEGIN(vm_ptr, id) do {} while(0)
#define REAL8_PROFILE_END(vm_ptr, id) do {} while(0)
#define REAL8_PROFILE_FRAME_BEGIN(vm_ptr) do {} while(0)
#define REAL8_PROFILE_FRAME_END(vm_ptr) do {} while(0)
#define REAL8_PROFILE_HOTSPOT(vm_ptr, id) do {} while(0)
#endif

#if !defined(REAL8_HAS_LIBRETRO_BUFFERS)
#define REAL8_HAS_LIBRETRO_BUFFERS 0
#endif

// Logging Macros
#define LOG_ONCE(key, fmt, ...) \
    do { \
        static bool _logged_##key = false; \
        if (!_logged_##key && host) { \
            host->log("[ONCE] " fmt, ##__VA_ARGS__); \
            _logged_##key = true; \
        } \
    } while(0)

#define LOG_EVERY_N(n, fmt, ...) \
    do { \
        static int _ctr = 0; \
        if (++_ctr >= n && host) { \
            host->log("[FREQ] " fmt, ##__VA_ARGS__); \
            _ctr = 0; \
        } \
    } while(0)

struct HwState {
    uint8_t distort = 0; // Bitmask for distortion per channel
    uint8_t spriteSheetMemMapping = 0x00;
    uint8_t screenDataMemMapping = 0x60;
    uint8_t mapMemMapping = 0x20;
    uint8_t widthOfTheMap = 128;
};

class Real8VM
{
public:
  static const int WIDTH = 128;
  static const int HEIGHT = 128;
  static const int RAW_WIDTH = 128;
  static const int PICO_WIDTH = 128;
  static const int PICO_HEIGHT = 128;
  static const int PICO_RAW_WIDTH = 128;


  bool hasLastError = false;
  char lastErrorTitle[32] = {0};
  char lastErrorDetail[256] = {0};

  void clearLastError() {
    hasLastError = false;
    lastErrorTitle[0] = 0;
    lastErrorDetail[0] = 0;
  }

  void setLastError(const char* title, const char* fmt, ...);

  // --------------------------------------------------------------------------
  // CORE LIFECYCLE
  // --------------------------------------------------------------------------
  Real8VM(IReal8Host *h);
  ~Real8VM();

  bool initMemory();
  void rebootVM();
  void forceExit();
  
  void runFrame();     
  void show_frame();   
  void setAltFramebuffer(uint8_t *alt, int w, int h) { alt_fb = alt; alt_fb_w = w; alt_fb_h = h; }
  void clearAltFramebuffer() { alt_fb = nullptr; alt_fb_w = PICO_WIDTH; alt_fb_h = PICO_HEIGHT; }
  bool hasAltFramebuffer() const { return alt_fb != nullptr; }

  // Stereoscopic depth buffer (optional)
  void clearDepthBuffer(uint8_t bucket = 0);
  void applyVideoMode(uint8_t requested_mode, bool force = false);

  // --------------------------------------------------------------------------
  // STATE & CONFIG
  // --------------------------------------------------------------------------
  lua_State* getLuaState() { return L; }
  IReal8Host* getHost() { return host; }
  bool skipDirtyRect = false;
  bool isLibretroPlatform = false;
  bool isGbaPlatform = false;

  // True when the shell UI (menus) is active; used to disable certain effects.
  bool isShellUI = false;

  bool reset_requested = false;
  bool exit_requested = false;
  bool quit_requested = false;
  std::string next_cart_path = "";
  std::string param_str = ""; 

  int targetFPS = 30;
  int debugFPS = 0;
  int displayFPS = 0;
  float debugFrameMS = 0.0f; 
  unsigned long app_fps_last_ms = 0;
  int app_fps_counter = 0;
  unsigned long display_fps_last_ms = 0;
  int display_fps_counter = 0;

  bool showStats = false;
  bool crt_filter = false;
  bool stereoscopic = false;
  
#if !defined(__GBA__)
  // --------------------------------------------------------------------------
  // STEREOSCOPIC DEPTH (GPIO 0x5FF0)
  // --------------------------------------------------------------------------
  // Depth bucket is encoded in the low nibble of GPIO byte 0x5FF0 as signed 4-bit
  // two's-complement and clamped to -7..+7.
  //  - 0..7    => 0..7  (backwards compatible with older carts)
  //  - 0xF..0x9 => -1..-7
  // Bucket 0 corresponds to the "screen plane". Positive buckets shift one way,
  // negative buckets shift the opposite way.
  static constexpr int STEREO_BUCKET_MIN = -7;
  static constexpr int STEREO_BUCKET_MAX = 7;
  static constexpr int STEREO_BUCKET_BIAS = 7;     // bucket 0 -> layer index 7
  static constexpr int STEREO_LAYER_COUNT = 15;    // -7..+7 inclusive
  static constexpr uint16_t STEREO_GPIO_ADDR = 0x5FF0;

  static inline int8_t decodeStereoBucket(uint8_t raw) {
    int8_t b = (int8_t)(raw & 0x0F);
    if (b & 0x08) b = (int8_t)(b - 0x10); // -8..-1
    if (b < STEREO_BUCKET_MIN) b = STEREO_BUCKET_MIN;
    if (b > STEREO_BUCKET_MAX) b = STEREO_BUCKET_MAX;
    return b;
  }

  static inline uint8_t stereoLayerIndexFromRaw(uint8_t raw) {
    return (uint8_t)(decodeStereoBucket(raw) + STEREO_BUCKET_BIAS);
  }

  inline int8_t getStereoBucket() const {
    return ram ? decodeStereoBucket(ram[STEREO_GPIO_ADDR]) : 0;
  }

  inline uint8_t getStereoLayerIndex() const {
    return ram ? stereoLayerIndexFromRaw(ram[STEREO_GPIO_ADDR]) : (uint8_t)STEREO_BUCKET_BIAS;
  }

#else
  // --------------------------------------------------------------------------
  // STEREOSCOPIC DEPTH (disabled on GBA build to save IWRAM)
  // --------------------------------------------------------------------------
  static constexpr int STEREO_BUCKET_BIAS = 7;
  inline int8_t getStereoBucket() const { return 0; }
  inline uint8_t getStereoLayerIndex() const { return (uint8_t)STEREO_BUCKET_BIAS; }

#endif

  bool showRepoSnap = true;
  bool showSkin = false;
  bool showRepoGames = false;
  bool swapScreens = false;
  bool stretchScreen = false;
  bool interpolation = false;
  int volume_music = 7;
  int volume_sfx = 10;
  bool patchModActive = false; // True when patch.lua persistent reapply hook is loaded
  
  std::string currentRepoUrl = IReal8Host::DEFAULT_GAMES_REPOSITORY;
  std::string currentCartPath = "";

  // --------------------------------------------------------------------------
  // LIBRETRO HELPERS
  // --------------------------------------------------------------------------

  size_t getStateSize();
  bool serialize(void* data, size_t size);
  bool unserialize(const void* data, size_t size);
  #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
  int16_t audio_buffer[4096]; // Fixed size, plenty of headroom
  #endif
#if REAL8_HAS_LIBRETRO_BUFFERS
  uint32_t screen_buffer[128 * 128]; // Raw 32-bit output for Libretro
  uint32_t palette_lut[32]; // Cache RGBA values for the 32 pico-8 colors
  void updatePaletteLUT();
  // 44100Hz / 60fps = 735 samples * 2 channels = 1470 int16s. Round up for safety.
  int16_t static_audio_buffer[2048];
  bool frame_is_dirty = true;
#endif

  // --------------------------------------------------------------------------
  // MEMORY
  // --------------------------------------------------------------------------
  uint8_t *ram = nullptr;
  uint8_t *rom = nullptr;
  size_t rom_size = 0;
  bool rom_readonly = false;
  bool rom_owned = false;
  int fb_w = PICO_WIDTH;
  int fb_h = PICO_HEIGHT;
  uint8_t *fb = nullptr;
  bool fb_is_linear = false;
  uint8_t *alt_fb = nullptr;
  int alt_fb_w = PICO_WIDTH;
  int alt_fb_h = PICO_HEIGHT;
  // Per-pixel depth bucket buffer for stereoscopic/anaglyph output (one byte per pixel).
  uint8_t *depth_fb = nullptr;

  // Optional: per-depth-layer color buffers for stereoscopic/anaglyph rendering.
  // Layout: 15 layers (-7..+7) * 128 rows. Row index = (layer_index * HEIGHT + y), where layer_index = bucket + STEREO_BUCKET_BIAS.
  // Pixel value is 0..15; 0xFF means "unset" for that layer.
  uint8_t *stereo_layers = nullptr;

  inline uint8_t* fb_row(int y) { return fb + (size_t)y * (size_t)fb_w; }
  inline const uint8_t* fb_row(int y) const { return fb + (size_t)y * (size_t)fb_w; }
  inline uint8_t* depth_row(int y) { return depth_fb + (size_t)y * (size_t)fb_w; }
  inline const uint8_t* depth_row(int y) const { return depth_fb + (size_t)y * (size_t)fb_w; }
  inline uint8_t* stereo_layer_row(int layer_idx, int y) {
    return stereo_layers + ((size_t)layer_idx * (size_t)fb_h + (size_t)y) * (size_t)fb_w;
  }

  uint8_t r8_flags = 0;
  uint8_t r8_vmode_req = 0;
  uint8_t r8_vmode_cur = 0;

  struct MotionState {
    int32_t accel_x = 0;
    int32_t accel_y = 0;
    int32_t accel_z = 0;
    int32_t gyro_x = 0;
    int32_t gyro_y = 0;
    int32_t gyro_z = 0;
    uint32_t flags = 0;
    uint32_t dt_us = 0;
  };
  MotionState motion;

  void setRomView(const uint8_t* data, size_t size, bool readOnly);
  bool ensureWritableRom();

  // Aliases
  uint8_t (*gfx)[128] = nullptr;
  uint8_t (*map_data)[128] = nullptr;
  uint8_t *screen_ram = nullptr;
  uint8_t *sprite_flags = nullptr;
  uint8_t *music_ram = nullptr;
  uint8_t *sfx_ram = nullptr;
  uint8_t *user_data = nullptr;

  EXT_RAM_ATTR uint8_t custom_font[0x800] = {0};
  uint8_t *cf_attr() { return custom_font + 0x000; }
  uint8_t *cf_adj() { return custom_font + 0x008; }
  uint8_t *cf_gfx() { return custom_font + 0x080; }

  // --------------------------------------------------------------------------
  // SUBSYSTEMS
  // --------------------------------------------------------------------------
  IReal8Host *host;
  #if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
  AudioEngine audio;
  Real8Debugger debug;
  #endif
  Real8Gfx gpu; // The Graphics Subsystem

  // --------------------------------------------------------------------------
  // INPUT
  // --------------------------------------------------------------------------
  uint32_t btn_states[8];
  uint32_t last_btn_states[8]; 
  uint32_t last_btn_state = 0; 
  uint32_t btn_mask = 0;
  uint8_t btn_counters[8][6]; 
  uint32_t btn_state = 0; 
  int mouse_x = 0;
  int mouse_y = 0;
  int mouse_buttons = 0;
  int mouse_rel_x = 0;
  int mouse_rel_y = 0;
  int mouse_last_x = 0;
  int mouse_last_y = 0;
  int mouse_wheel_event = 0;
  bool key_pressed_this_frame = false;
  std::deque<std::string> key_queue;
  bool has_key_input = false;
  int controller_count = 1;

  bool btn(int i, int p = 0);
  bool btnp(int i, int p = 0);
  uint32_t get_btn_state(int p) { return (p >= 0 && p < 8) ? btn_states[p] : 0; }
  bool isMenuPressed() { return (btn_mask & (1 << 6)); } 
  void clearButtons() { btn_mask = 0; }
  void resetInputState();

  // --------------------------------------------------------------------------
  // HELPERS (Low Level)
  // --------------------------------------------------------------------------
  inline void screenByteToFB(size_t idx, uint8_t v) {
    if (idx >= 0x2000 || r8_vmode_cur != 0) return;
    int y = (int)(idx >> 6);
    int x = (int)((idx & 0x3F) << 1);
    if (!fb || y < 0 || y >= fb_h || x < 0 || x + 1 >= fb_w) return;
    fb_row(y)[x] = v & 0x0F;
    fb_row(y)[x + 1] = (v >> 4) & 0x0F;
    mark_dirty_rect(x, y, x + 1, y);
  }
  void mark_dirty_rect(int x0, int y0, int x1, int y1);
  int watch_addr = -1; 
  int dirty_x0, dirty_y0, dirty_x1, dirty_y1; // Needs to be public for GFX to access

  HwState hwState;
  
  // --------------------------------------------------------------------------
  // AUDIO & WAVETABLES
  // --------------------------------------------------------------------------
#if !defined(__GBA__) || REAL8_GBA_ENABLE_AUDIO
  float wavetables[8][2048]; 
  void init_wavetables();
  AudioStateSnapshot getAudioState() { return audio.getState(); }
  void setAudioState(const AudioStateSnapshot& s) { audio.setState(s); }
#endif
  // --------------------------------------------------------------------------
  // CARTS & LOADING
  // --------------------------------------------------------------------------
  bool loadGame(const GameData& game);
  void detectCartFPS();

  // --------------------------------------------------------------------------
  // PERSISTENCE
  // --------------------------------------------------------------------------
  std::string currentGameId;
  // Cached LUA source of the last loaded cart (for exporting on desktop hosts)
  std::string loadedLuaSource;
  void saveState();
  bool loadState();
  bool hasState();

  std::string cartDataId = "";
  float cart_data_ram[64] = {0};
  void loadCartData();
  void saveCartData();
  void saveCartToDisk();

  // --------------------------------------------------------------------------
  // MENU
  // --------------------------------------------------------------------------
  struct MenuItem {
    std::string label;
    int lua_ref = -1;
    bool active = false;
  };
  MenuItem custom_menu_items[6];
  void set_menu_item(int index, const char *label, int lua_ref);
  void clear_menu_items();
  void run_menu_item(int index);

  // --------------------------------------------------------------------------
  // UTILS
  // --------------------------------------------------------------------------
  enum LogChannel { LOG_GENERIC, LOG_AUDIO, LOG_GFX, LOG_MEM, LOG_LUA };
  void log(LogChannel ch, const char* fmt, ...);
  bool map_check_flag(int x, int y, int w, int h, int flag);
  bool skip_update_logic = false;

#if REAL8_PROFILE_ENABLED
  enum GbaProfileBucket {
    kProfileVm = 0,
    kProfileDraw,
    kProfileBlit,
    kProfileInput,
    kProfileMenu,
    kProfileIdle,
    kProfileCount
  };

  enum GbaHotspot {
    kHotspotSprMasked = 0,
    kHotspotSspr,
    kHotspotRectfillSlow,
    kHotspotLineSlow,
    kHotspotBlitDirty,
    kHotspotCount
  };

  void profileFrameBegin();
  void profileFrameEnd();
  void profileBegin(int id);
  void profileEnd(int id);
  void profileHotspot(int id);

  uint32_t profile_bucket_cycles[kProfileCount] = {};
  uint32_t profile_bucket_start[kProfileCount] = {};
  uint32_t profile_last_bucket_cycles[kProfileCount] = {};
  uint32_t profile_hotspots[kHotspotCount] = {};
  uint32_t profile_last_hotspots[kHotspotCount] = {};
  uint32_t profile_frame_start_cycles = 0;
  uint32_t profile_last_frame_cycles = 0;
#endif

private:
  lua_State *L = nullptr;

  void cacheLuaRefs();
  void clearLuaRefs();
  int lua_ref_update = LUA_NOREF;
  int lua_ref_update60 = LUA_NOREF;
  int lua_ref_draw = LUA_NOREF;
  int lua_ref_init = LUA_NOREF;
  
  void renderProfileOverlay();
  void initDefaultPalette(); // Still used for VM reboot reset
};

// Crash/debug breadcrumbs (lightweight, thread-unsafe by design)
void real8_set_last_api_call(const char* name);
void real8_set_last_lua_phase(const char* name);
void real8_set_last_cart_path(const char* path);
void real8_set_last_lua_line(int line, const char* source);
const char* real8_get_last_api_call();
const char* real8_get_last_lua_phase();
const char* real8_get_last_cart_path();
int real8_get_last_lua_line();
const char* real8_get_last_lua_source();
