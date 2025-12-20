#include "libretro.h"
#include "libretro_host.hpp"
#include "../../../src/core/real8_vm.h"
#include "../../../src/core/real8_cart.h"
#include <vector>
#include <cstring>
#include <cstdlib>

using namespace z8;

// --- Global State ---
static LibretroHost* host = nullptr;
static Real8VM* vm = nullptr;
static GameData gameData;

// Track connected devices. Default to JOYPAD (1).
static unsigned libretro_devices[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };

// --- Callbacks ---
retro_log_printf_t log_cb = nullptr;
retro_video_refresh_t video_cb = nullptr;
retro_audio_sample_t audio_cb = nullptr;
retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_environment_t environ_cb = nullptr;
retro_input_poll_t input_poll_cb = nullptr;
retro_input_state_t input_state_cb = nullptr;

// Define the controller types supported (RetroPad)
static const struct retro_controller_description controller_def[] = {
   { "RetroPad", RETRO_DEVICE_JOYPAD },
   { NULL, 0 }
};

static const struct retro_controller_info controller_info[] = {
   { controller_def, 2 }, // Port 1 supported types
   { controller_def, 2 }, // Port 2 supported types
   { NULL, 0 }
};

extern "C" {

    // --- 1. Input Descriptors ---
    // This maps the internal "RetroPad" buttons to your Core's functions.
    // We define this statically to ensure it remains valid in memory.
    static struct retro_input_descriptor input_desc[] = {
        // Player 1
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "X (Action)" }, 
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "O (Back)" },   
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "X (Turbo)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "O (Turbo)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Pause / Menu" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },

        // Player 2
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "P2 Left" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "P2 Up" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "P2 Down" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "P2 Right" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "P2 X" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "P2 O" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "P2 Pause" },

        { 0 }, // REQUIRED Terminator
    };

    void retro_init(void) {
        if (!host) host = new LibretroHost();
        if (!vm) {
            vm = new Real8VM(host);
            vm->initMemory();
        }

        // 1. Register Controller Types (Tells frontend we use RetroPad)
        // Command 35 is RETRO_ENVIRONMENT_SET_CONTROLLER_INFO
        if (environ_cb) {
            environ_cb(35, (void*)controller_info);
        }

        // 2. Register Button Names (Input Descriptors)
        // Make sure RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS is 11 in libretro.h!
        if (environ_cb) {
            environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_desc);
        }

    }
    
    void retro_deinit(void) {
        if (vm) { delete vm; vm = nullptr; }
        if (host) { delete host; host = nullptr; }
    }

    unsigned retro_api_version(void) { return RETRO_API_VERSION; }

    // --- 2. Port Handshake ---
    // This is the critical fix for "N/A". It tells RetroArch "Yes, I accept this controller".
    void retro_set_controller_port_device(unsigned port, unsigned device) {
        if (port < 2) {
            libretro_devices[port] = device;
        }
    }

    void retro_get_system_info(struct retro_system_info *info) {
        memset(info, 0, sizeof(*info));
        info->library_name     = "Real8";
        info->library_version  = "1.0";
        info->need_fullpath    = false;
        info->valid_extensions = "p8|png";
    }

    void retro_get_system_av_info(struct retro_system_av_info *info) {
        info->geometry.base_width   = 128;
        info->geometry.base_height  = 128;
        info->geometry.max_width    = 128;
        info->geometry.max_height   = 128;
        info->geometry.aspect_ratio = 1.0f;
        info->timing.fps = 60.0;
        info->timing.sample_rate = 22050.0;
    }

    void retro_set_environment(retro_environment_t cb) {
        environ_cb = cb;
        struct retro_log_callback logging;
        if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
            log_cb = logging.log;
        }
    }

    void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
    void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
    void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
    void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
    void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }

    void retro_reset(void) {
        if (vm) vm->rebootVM();
        if (vm && !gameData.cart_id.empty()) {
            vm->loadGame(gameData);
        }
    }

    void retro_run(void) {
        if (!vm) return;

        // Reset/Load Handling
        if (vm->reset_requested) {
            if (!vm->next_cart_path.empty()) {
                std::vector<uint8_t> newCartData = host->loadFile(vm->next_cart_path.c_str());
                if (!newCartData.empty()) {
                    GameData newGame;
                    if (Real8CartLoader::LoadFromBuffer(host, newCartData, newGame)) {
                        gameData = newGame;
                        gameData.cart_id = vm->next_cart_path;
                    }
                }
            }
            vm->rebootVM();
            if (!gameData.cart_id.empty()) {
                vm->loadGame(gameData);
            }
            vm->reset_requested = false;
            vm->next_cart_path = "";
        }

        // Input Polling
        host->pollInput(); 

        for (int i = 0; i < 8; i++) {
             vm->btn_states[i] = 0;
             // Check device type (only P1/P2 usually have RetroPads)
             if (i < 2) {
                 if (libretro_devices[i] == RETRO_DEVICE_JOYPAD) {
                     vm->btn_states[i] = host->getPlayerInput(i);
                 }
             } else {
                 vm->btn_states[i] = host->getPlayerInput(i);
             }
        }
        vm->btn_state = vm->btn_states[0];

        vm->runFrame();
        vm->show_frame(); 

        if (video_cb) {
            video_cb(vm->screen_buffer, 128, 128, 128 * sizeof(uint32_t));
        }
    }

    bool retro_load_game(const struct retro_game_info *info) {
        if (!vm || !info || !info->data) return false;

        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
        if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
            if (log_cb) log_cb(RETRO_LOG_WARN, "XRGB8888 not supported.\n");
            return false;
        }

        if (info->path) {
            host->setContentPath(info->path);
        }

        std::vector<uint8_t> buffer((const uint8_t*)info->data, (const uint8_t*)info->data + info->size);
        if (Real8CartLoader::LoadFromBuffer(host, buffer, gameData)) {
            gameData.cart_id = "libretro_cart"; 
            vm->loadGame(gameData);
            return true;
        }

        return false;
    }

    void retro_unload_game(void) {
        if (vm) vm->forceExit();
    }

    unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
    bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }

    size_t retro_serialize_size(void) {
        if (vm) return vm->getStateSize();
        return 0;
    }

    bool retro_serialize(void *data, size_t size) {
        if (vm) return vm->serialize(data, size);
        return false;
    }

    bool retro_unserialize(const void *data, size_t size) {
        if (vm) return vm->unserialize(data, size);
        return false;
    }

    void *retro_get_memory_data(unsigned id) {
        if (!vm) return nullptr;
        if (id == RETRO_MEMORY_SYSTEM_RAM) return vm->ram;
        return nullptr;
    }

    size_t retro_get_memory_size(unsigned id) {
        if (id == RETRO_MEMORY_SYSTEM_RAM) return 0x8000;
        return 0;
    }

    void retro_cheat_reset(void) {}
    void retro_cheat_set(unsigned index, bool enabled, const char *code) {}
}