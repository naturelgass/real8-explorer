#pragma once
#include "../../../src/hal/real8_host.h"
#include "libretro.h"
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <fstream>
#include <algorithm>

// Global callbacks from libretro.cpp
extern retro_log_printf_t log_cb;
extern retro_audio_sample_batch_t audio_batch_cb;
extern retro_input_state_t input_state_cb;
extern retro_input_poll_t input_poll_cb;
extern retro_environment_t environ_cb;

namespace z8 {

    class LibretroHost : public IReal8Host {
    private:
        std::string gameDirectory = "";

    public:
        LibretroHost() {}
        virtual ~LibretroHost() {}

        // Identify as Libretro so Real8VM takes the optimized path
        const char *getPlatform() override { return "Libretro"; }

        // --- Context Management (New) ---
        // Called by retro_load_game to set the working directory
        void setContentPath(const char* path) {
            if (!path) return;
            std::string p(path);
            size_t pos = p.find_last_of("/\\");
            if (pos != std::string::npos) {
                gameDirectory = p.substr(0, pos);
            } else {
                gameDirectory = ".";
            }
        }

        // --- File I/O (Implemented) ---
        std::vector<uint8_t> loadFile(const char *path) override {
            if (!path) return {};

            std::string fullPath;
            std::string filename(path);

            // Handle absolute vs relative paths
            // If the path implies a root (starts with /), we map it to the game directory for safety
            if (!filename.empty() && (filename[0] == '/' || filename[0] == '\\')) {
                filename = filename.substr(1);
            }

            if (!gameDirectory.empty()) {
                fullPath = gameDirectory + "/" + filename;
            } else {
                fullPath = filename;
            }

            // Standard C++ Binary Read
            std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                // Fallback: Try raw path if relative lookup failed
                file.open(path, std::ios::binary | std::ios::ate);
                if (!file.is_open()) return {};
            }

            std::streamsize size = file.tellg();
            if (size <= 0) return {};

            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(size);
            if (file.read((char *)buffer.data(), size)) {
                return buffer;
            }
            
            return {};
        }

        // --- Audio ---
        void pushAudio(const int16_t *samples, int count) override {
            if (audio_batch_cb && count > 0) {
                // FIX: Convert Mono (VM) to Stereo (Libretro)
                // 1 Mono Sample -> 2 Stereo Samples (L+R)
                // This prevents the "Double Speed" chipmunk effect.

                // Static buffer to reuse memory (avoid reallocating every frame)
                static std::vector<int16_t> stereoBuffer;
                size_t requiredSize = count * 2;
                
                if (stereoBuffer.size() < requiredSize) {
                    stereoBuffer.resize(requiredSize);
                }

                // Duplicate Mono sample to L and R channels
                for (int i = 0; i < count; i++) {
                    stereoBuffer[i * 2]     = samples[i]; // Left
                    stereoBuffer[i * 2 + 1] = samples[i]; // Right
                }

                // Send 'count' frames (where 1 frame = 2 int16s)
                audio_batch_cb(stereoBuffer.data(), count);
            }
        }

        // --- Input ---
        void pollInput() override {
            if (input_poll_cb) input_poll_cb();
        }

        uint32_t getPlayerInput(int playerIdx) override {
            if (!input_state_cb) return 0;
            
            uint32_t buttons = 0;
            // PICO-8 Mapping
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))  buttons |= (1 << 0);
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) buttons |= (1 << 1);
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))    buttons |= (1 << 2);
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))  buttons |= (1 << 3);
            
            // Map O -> A/Y, X -> B/X
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) || 
                input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))     buttons |= (1 << 4); // O
                
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) || 
                input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))     buttons |= (1 << 5); // X
                
            if (input_state_cb(playerIdx, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) buttons |= (1 << 6); // Menu

            return buttons;
        }

        // --- Logging ---
        void log(const char *fmt, ...) override {
            if (log_cb) {
                va_list args;
                va_start(args, fmt);
                log_cb(RETRO_LOG_INFO, fmt, args);
                va_end(args);
            }
        }

        // --- Time ---
        unsigned long getMillis() override {
            return (unsigned long)(std::clock() * 1000 / CLOCKS_PER_SEC);
        }
        
        // --- Stubs ---
        void delayMs(int ms) override {} 
        void flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map) override {} 
        
        void setNetworkActive(bool active) override {}
        void setWifiCredentials(const char *ssid, const char *pass) override {}
        NetworkInfo getNetworkInfo() override { return {false, "", "", 0}; }
        bool downloadFile(const char *url, const char *savePath) override { return false; }
        
        std::vector<std::string> listFiles(const char *ext) override { return {}; }
        bool saveState(const char *filename, const uint8_t *data, size_t size) override { return false; } 
        std::vector<uint8_t> loadState(const char *filename) override { return {}; }
        bool hasSaveState(const char *filename) override { return false; }
        void deleteFile(const char *path) override {}
        void getStorageInfo(size_t &used, size_t &total) override { used=0; total=1024*1024; }
        bool renameGameUI(const char *currentPath) override { return false; }
        
        void openGamepadConfigUI() override {}
        std::vector<uint8_t> getInputConfigData() override { return {}; }
        void setInputConfigData(const std::vector<uint8_t>& data) override {}
        
        void takeScreenshot() override {}
        void drawWallpaper(const uint8_t *pixels, int w, int h) override {}
        void clearWallpaper() override {}
        void updateOverlay() override {}
    };
}