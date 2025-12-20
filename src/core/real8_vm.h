#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "../hal/real8_host.h"
#include "real8_memattrs.h"
#include "real8_audio.h"
#include "real8_debugger.h"
#include "real8_gfx.h"
#include "real8_cart.h"

#include "../../lib/z8lua/lua.h"
#include "../../lib/z8lua/lauxlib.h"
#include "../../lib/z8lua/lualib.h"

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
};

class Real8VM
{
public:
  static const int WIDTH = 128;
  static const int HEIGHT = 128;
  static const int RAW_WIDTH = 128;

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

  // --------------------------------------------------------------------------
  // STATE & CONFIG
  // --------------------------------------------------------------------------
  lua_State* getLuaState() { return L; }
  IReal8Host* getHost() { return host; }

  bool reset_requested = false;
  bool exit_requested = false;
  bool quit_requested = false;
  std::string next_cart_path = "";
  std::string param_str = ""; 

  int targetFPS = 30;
  int debugFPS = 0;
  float debugFrameMS = 0.0f; 

  bool showStats = false;
  bool crt_filter = false;
  bool showRepoSnap = true;
  bool showLocalSnap = true;
  bool showSkin = false;
  bool showRepoGames = false;
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
  int16_t audio_buffer[4096]; // Fixed size, plenty of headroom
  uint32_t screen_buffer[128 * 128]; // Raw 32-bit output for Libretro
  uint32_t palette_lut[32]; // Cache RGBA values for the 32 pico-8 colors
  void updatePaletteLUT();
  // 44100Hz / 60fps = 735 samples * 2 channels = 1470 int16s. Round up for safety.
  int16_t static_audio_buffer[2048];
  bool frame_is_dirty = true;

  // --------------------------------------------------------------------------
  // MEMORY
  // --------------------------------------------------------------------------
  uint8_t *ram = nullptr;
  uint8_t *rom = nullptr;
  uint8_t (*fb)[RAW_WIDTH] = nullptr; 

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
  AudioEngine audio;
  Real8Debugger debug;
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
  int mouse_wheel_delta = 0;
  bool key_pressed_this_frame = false;
  std::string key_input_buffer = "";
  bool has_key_input = false;

  bool btn(int i, int p = 0);
  bool btnp(int i, int p = 0);
  uint32_t get_btn_state(int p) { return (p >= 0 && p < 8) ? btn_states[p] : 0; }
  bool isMenuPressed() { return (btn_mask & (1 << 6)); } 
  void clearButtons() { btn_mask = 0; }
  void resetInputState();

  // --------------------------------------------------------------------------
  // HELPERS (Low Level)
  // --------------------------------------------------------------------------
  void screenByteToFB(size_t idx, uint8_t v);
  void mark_dirty_rect(int x0, int y0, int x1, int y1);
  int watch_addr = -1; 
  int dirty_x0, dirty_y0, dirty_x1, dirty_y1; // Needs to be public for GFX to access

  HwState hwState;
  
  // --------------------------------------------------------------------------
  // AUDIO & WAVETABLES
  // --------------------------------------------------------------------------
  float wavetables[8][2048]; 
  void init_wavetables();
  AudioStateSnapshot getAudioState() { return audio.getState(); }
  void setAudioState(const AudioStateSnapshot& s) { audio.setState(s); }

  // --------------------------------------------------------------------------
  // CARTS & LOADING
  // --------------------------------------------------------------------------
  bool loadGame(const GameData& game);
  void detectCartFPS();

  // --------------------------------------------------------------------------
  // PERSISTENCE
  // --------------------------------------------------------------------------
  std::string currentGameId;
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

private:
  lua_State *L = nullptr;
  
  void initDefaultPalette(); // Still used for VM reboot reset
};
