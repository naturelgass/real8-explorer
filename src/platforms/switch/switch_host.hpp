#pragma once

#include "../../hal/real8_host.h"
#include "../../core/real8_vm.h"
#include "../../core/real8_gfx.h"
#include "switch_input.hpp"

#include <SDL.h>
#include <switch.h> // libnx
#include <switch/services/hid.h>
#include <curl/curl.h>

#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace fs = std::filesystem;

class SwitchHost : public IReal8Host
{
private:
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *wallpaperTex;
    SDL_AudioDeviceID audioDevice;
    SwitchInput input;
    bool curlReady = false;
    bool nifmReady = false;
    bool sdmcMounted = false;
    bool romfsMounted = false;
    
    std::vector<uint32_t> screenBuffer;
    int screenW = 128;
    int screenH = 128;
    std::vector<uint32_t> wallBuffer;
    int wallW = 0, wallH = 0;
    fs::path rootPath;
    int lastTouchX = 0;
    int lastTouchY = 0;
    HidSixAxisSensorHandle sensorHandle{};
    bool sensorActive = false;
    bool sensorAvailable = false;
    u64 lastSensorUs = 0;
    bool fastForwardOverride = false;

    // Helper for scaling logic
    void calculateGameRect(int winW, int winH, SDL_Rect *outRect, float *outScale)
    {
        bool stretch = (debugVMRef && debugVMRef->stretchScreen);
        int padding = 0;
        if (wallpaperTex) padding = stretch ? 50 : 20;

        int availW = winW - (padding * 2);
        int availH = winH - (padding * 2);

        if (availW < 1) availW = 1;
        if (availH < 1) availH = 1;

        int gameW = (debugVMRef && debugVMRef->fb_w > 0) ? debugVMRef->fb_w : 128;
        int gameH = (debugVMRef && debugVMRef->fb_h > 0) ? debugVMRef->fb_h : 128;
        int mode = (debugVMRef ? debugVMRef->r8_vmode_cur : 0);

        if (mode == 0) {
            if (stretch) {
                outRect->x = padding;
                outRect->y = padding;
                outRect->w = availW;
                outRect->h = availH;
                if (outScale) *outScale = (float)availW / (float)gameW;
                return;
            }
            int drawW = gameW;
            int drawH = gameH;
            outRect->x = (winW - drawW) / 2;
            outRect->y = (winH - drawH) / 2;
            outRect->w = drawW;
            outRect->h = drawH;
            if (outScale) *outScale = 1.0f;
            return;
        }

        int scale = std::min(availW / gameW, availH / gameH);
        if (scale < 1) scale = 1;
        int drawW = gameW * scale;
        int drawH = gameH * scale;
        outRect->x = (winW - drawW) / 2;
        outRect->y = (winH - drawH) / 2;
        outRect->w = drawW;
        outRect->h = drawH;
        if (outScale) *outScale = (float)scale;
        return;
    }

    std::string resolveVirtualPath(const char *filename)
    {
        std::string fname = filename;
        if (!fname.empty() && fname[0] == '/')
            fname = fname.substr(1);

        auto isCartFile = [](const std::string& name) {
            if (name.length() >= 3 && name.compare(name.length() - 3, 3, ".p8") == 0) return true;
            if (name.length() >= 4 && name.compare(name.length() - 4, 4, ".png") == 0) return true;
            return false;
        };

        fs::path targetDir;

        if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".sav")
        {
            targetDir = rootPath / "saves";
        }
        else if (fname == "config.dat" || fname == "wallpaper.png" || fname == "favorites.txt" || fname == "gameslist.json" || fname == "gamesrepo.txt")
        {
            targetDir = rootPath / "config";
        }
        else if (isCartFile(fname))
        {
            targetDir = rootPath / "carts";
        }
        else
        {
            targetDir = rootPath;
        }

        if (!fs::exists(targetDir)) fs::create_directories(targetDir);
        return (targetDir / fname).string();
    }

    struct CurlWriteState {
        FILE *file = nullptr;
        size_t total = 0;
        size_t maxBytes = 0;
        bool overflow = false;
    };

    static size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        CurlWriteState *state = static_cast<CurlWriteState*>(userdata);
        if (!state || !state->file) return 0;
        size_t bytes = size * nmemb;
        if (state->maxBytes > 0 && state->total + bytes > state->maxBytes) {
            state->overflow = true;
            return 0;
        }
        size_t written = fwrite(ptr, 1, bytes, state->file);
        state->total += written;
        return written;
    }

    static bool copyFile(const std::string& srcPath, const std::string& dstPath)
    {
        FILE* in = fopen(srcPath.c_str(), "rb");
        if (!in) return false;

        FILE* out = fopen(dstPath.c_str(), "wb");
        if (!out) {
            fclose(in);
            return false;
        }

        char buf[16 * 1024];
        bool ok = true;
        while (true) {
            size_t n = fread(buf, 1, sizeof(buf), in);
            if (n > 0) {
                if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            }
            if (n < sizeof(buf)) {
                if (ferror(in)) ok = false;
                break;
            }
        }

        fclose(out);
        fclose(in);
        return ok;
    }

    void ensureBundledConfigFiles()
    {
        // Destination: sdmc:/real8/config/ (or fallback working dir)
        fs::path cfgDir = rootPath / "config";
        fs::create_directories(cfgDir);

        struct Entry { const char* name; const char* romfsPath; };
        const Entry bundled[] = {
            {"gamesrepo.txt", "romfs:/real8/config/gamesrepo.txt"},
            {"wallpaper.png", "romfs:/real8/config/wallpaper.png"},
        };

        for (const auto& e : bundled) {
            fs::path dst = cfgDir / e.name;
            bool forceCopy = false;
#ifdef REAL8_SWITCH_STANDALONE
            if (strcmp(e.name, "wallpaper.png") == 0) {
                forceCopy = true;
            }
#endif
            if (!forceCopy && fs::exists(dst)) continue;

            if (!romfsMounted) {
                log("[Switch] ROMFS not mounted; cannot seed %s", e.name);
                continue;
            }

            if (copyFile(e.romfsPath, dst.string())) {
                log("[Switch] Seeded %s from ROMFS", e.name);
            } else {
                log("[Switch] Failed to seed %s from %s", e.name, e.romfsPath);
            }
        }
    }

public:
    Real8VM* debugVMRef = nullptr;
    bool crt_filter = false;
    bool interpolation = false;

    const char *getPlatform() const override { return "Switch"; }
    
    std::string getClipboardText() override { return ""; } // No system clipboard on Switch

    SwitchHost(SDL_Renderer *r) : renderer(r), texture(nullptr), wallpaperTex(nullptr), audioDevice(0)
    {
        // 1. Initialize Switch Sockets (optional, for debugging via nxlink)
        Result nifm_rc = nifmInitialize(NifmServiceType_User);
        if (R_FAILED(nifm_rc)) {
            printf("nifmInitialize failed: 0x%08X\n", nifm_rc);
        } else {
            nifmReady = true;
        }

        Result sock_rc = socketInitializeDefault();
        if (R_FAILED(sock_rc)) {
            printf("nifmInitialize failed: 0x%08X\n", sock_rc);
        } else {
            // Optional: poll connection status before doing DNS/HTTP
            // nifmGetInternetConnectionStatus(...)
        }

        CURLcode curl_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curl_rc != CURLE_OK) {
            printf("curl_global_init failed: %d\n", (int)curl_rc);
        } else {
            curlReady = true;
        }

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
        if (texture) SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        screenBuffer.resize((size_t)screenW * (size_t)screenH);

        HidSixAxisSensorHandle handles[1];
        int handleCount = hidGetSixAxisSensorHandles(handles, 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
        if (handleCount > 0) {
            sensorHandle = handles[0];
            sensorAvailable = true;
        }

        input.init();
        initAudio();

        Result fs_rc = fsdevMountSdmc();
        if (R_FAILED(fs_rc)) {
            printf("fsdevMountSdmc failed: 0x%08X\n", fs_rc);
        } else {
            sdmcMounted = true;
        }

        // 2. Root Path: SD card preferred, fallback to current working directory
        if (sdmcMounted) {
            rootPath = fs::path("sdmc:/real8");
        } else {
            rootPath = fs::current_path() / "real8";
        }
        fs::create_directories(rootPath);
        fs::create_directories(rootPath / "mods");
        fs::create_directories(rootPath / "config");
        fs::create_directories(rootPath / "saves");
        fs::create_directories(rootPath / "carts");

        // Mount ROMFS (bundled files inside the NRO)
        Result romfs_rc = romfsInit();
        if (R_FAILED(romfs_rc)) {
            printf("romfsInit failed: 0x%08X\n", romfs_rc);
        } else {
            romfsMounted = true;
        }

        // Copy bundled defaults on first run (if missing)
        ensureBundledConfigFiles();
    }

    static inline int32_t to_q16_16(float v) {
        return (int32_t)lroundf(v * 65536.0f);
    }

    void updateMotionSensors()
    {
        if (!debugVMRef || !debugVMRef->ram) return;
        if (!sensorAvailable) {
            debugVMRef->motion.flags = 0;
            debugVMRef->motion.dt_us = 0;
            debugVMRef->motion.accel_x = 0;
            debugVMRef->motion.accel_y = 0;
            debugVMRef->motion.accel_z = 0;
            debugVMRef->motion.gyro_x = 0;
            debugVMRef->motion.gyro_y = 0;
            debugVMRef->motion.gyro_z = 0;
            return;
        }

        const bool enabled = (debugVMRef->ram[0x5FE0] & 0x01) != 0;
        if (!enabled) {
            if (sensorActive) {
                hidStopSixAxisSensor(sensorHandle);
                sensorActive = false;
            }
            debugVMRef->motion.flags = 0x03; // accel + gyro present, data invalid
            debugVMRef->motion.dt_us = 0;
            debugVMRef->motion.accel_x = 0;
            debugVMRef->motion.accel_y = 0;
            debugVMRef->motion.accel_z = 0;
            debugVMRef->motion.gyro_x = 0;
            debugVMRef->motion.gyro_y = 0;
            debugVMRef->motion.gyro_z = 0;
            lastSensorUs = 0;
            return;
        }

        if (!sensorActive) {
            hidStartSixAxisSensor(sensorHandle);
            sensorActive = true;
            lastSensorUs = 0;
        }

        HidSixAxisSensorState state = {};
        int count = (int)hidGetSixAxisSensorStates(sensorHandle, &state, 1);
        if (count <= 0) {
            debugVMRef->motion.flags = 0x03;
            debugVMRef->motion.dt_us = 0;
            return;
        }

        u64 ticks = armGetSystemTick();
        u64 freq = armGetSystemTickFreq();
        u64 now_us = (freq != 0) ? (ticks * 1000000ULL) / freq : 0;
        u64 dt = (lastSensorUs == 0) ? 0 : (now_us - lastSensorUs);
        lastSensorUs = now_us;
        if (dt > 0xFFFFFFFFu) dt = 0xFFFFFFFFu;

        debugVMRef->motion.accel_x = to_q16_16(state.acceleration.x);
        debugVMRef->motion.accel_y = to_q16_16(state.acceleration.y);
        debugVMRef->motion.accel_z = to_q16_16(state.acceleration.z);
        debugVMRef->motion.gyro_x = to_q16_16(state.angular_velocity.x);
        debugVMRef->motion.gyro_y = to_q16_16(state.angular_velocity.y);
        debugVMRef->motion.gyro_z = to_q16_16(state.angular_velocity.z);
        debugVMRef->motion.flags = 0x07;
        debugVMRef->motion.dt_us = (uint32_t)dt;
    }

    ~SwitchHost()
    {
        // Cleanup libnx sockets
        if (sensorActive && sensorAvailable) {
            hidStopSixAxisSensor(sensorHandle);
            sensorActive = false;
        }
        if (curlReady) curl_global_cleanup();
        socketExit();
        if (nifmReady) nifmExit();
        if (sdmcMounted) fsdevUnmountDevice("sdmc");
        if (romfsMounted) romfsExit();

        if (audioDevice) SDL_CloseAudioDevice(audioDevice);
        if (wallpaperTex) SDL_DestroyTexture(wallpaperTex);
        if (texture) SDL_DestroyTexture(texture);
    }
    
    // --- Audio ---
    void initAudio()
    {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 22050; // Match VM output
        want.format = AUDIO_S16SYS;
        want.channels = 1; // Mono output like the VM
        want.samples = 1024;
        want.callback = NULL; // We use SDL_QueueAudio

        audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audioDevice > 0) SDL_PauseAudioDevice(audioDevice, 0);
    }

    void pushAudio(const int16_t *samples, int count) override
    {
        if (audioDevice == 0 || samples == nullptr || count == 0) return;

        // Keep queue near real-time to avoid pops
        const Uint32 TARGET_QUEUE_BYTES = 1024 * sizeof(int16_t);
        const Uint32 MAX_WAIT_CYCLES = 500;

        Uint32 queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
        int safety = 0;

        while (queuedBytes > TARGET_QUEUE_BYTES && safety < (int)MAX_WAIT_CYCLES) {
            svcSleepThread(1000000LL);
            queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
            safety++;
        }

        SDL_QueueAudio(audioDevice, samples, count * sizeof(int16_t));
    }

    // --- Input Interfaces ---
    uint32_t getPlayerInput(int playerIdx) override { return input.getMask(playerIdx); }
    void pollInput() override { input.update(); }
    void clearInputState() override { input.clearState(); }
    std::vector<uint8_t> getInputConfigData() override { return input.serialize(); }
    void setInputConfigData(const std::vector<uint8_t>& data) override { input.deserialize(data); }
    bool isKeyDownScancode(int scancode) override { return false; }

    MouseState getMouseState() override
    {
        MouseState ms = {lastTouchX, lastTouchY, 0};

        HidTouchScreenState state = {};
        size_t count = hidGetTouchScreenStates(&state, 1);
        if (count > 0 && state.count > 0) {
            int outputW = 0;
            int outputH = 0;
            SDL_GetRendererOutputSize(renderer, &outputW, &outputH);

            SDL_Rect gameRect;
            float scale = 1.0f;
            calculateGameRect(outputW, outputH, &gameRect, &scale);
            if (scale <= 0.0f) scale = 1.0f;

            const HidTouchState &touch = state.touches[0];
            float touchX = (float)touch.x;
            float touchY = (float)touch.y;

            if (touchX >= gameRect.x && touchX < (gameRect.x + gameRect.w) &&
                touchY >= gameRect.y && touchY < (gameRect.y + gameRect.h)) {
                int relX = (int)(touchX - (float)gameRect.x);
                int relY = (int)(touchY - (float)gameRect.y);

                int gameW = (debugVMRef && debugVMRef->fb_w > 0) ? debugVMRef->fb_w : 128;
                int gameH = (debugVMRef && debugVMRef->fb_h > 0) ? debugVMRef->fb_h : 128;
                bool stretch = (debugVMRef && debugVMRef->stretchScreen);
                float scaleX = stretch ? ((float)gameRect.w / (float)gameW) : scale;
                float scaleY = stretch ? ((float)gameRect.h / (float)gameH) : scale;
                if (scaleX <= 0.0f) scaleX = 1.0f;
                if (scaleY <= 0.0f) scaleY = 1.0f;

                int mx = (int)(relX / scaleX);
                int my = (int)(relY / scaleY);
                if (mx < 0) mx = 0; if (mx >= gameW) mx = gameW - 1;
                if (my < 0) my = 0; if (my >= gameH) my = gameH - 1;

                lastTouchX = mx;
                lastTouchY = my;
                ms.x = mx;
                ms.y = my;
                ms.btn = 1;
            }
        }

        return ms;
    }

    void openGamepadConfigUI() override {
        // Stub: Native dialogs don't exist. 
        // Logic handled by internal VM menus or user editing config.dat manually.
        log("[Switch] External gamepad config UI not supported. Use internal menu.");
    }

    // --- Rendering ---
    void setInterpolation(bool active) {
        interpolation = active;
        // Force recreation on next frame so scale mode is applied immediately
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    void onFramebufferResize(int fb_w, int fb_h) override {
        (void)fb_w;
        (void)fb_h;
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    void drawWallpaper(const uint8_t *pixels, int w, int h) override {
        if (!pixels) return;
        if (w != wallW || h != wallH || !wallpaperTex) {
            if (wallpaperTex) SDL_DestroyTexture(wallpaperTex);
            wallpaperTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            wallW = w; wallH = h;
            wallBuffer.resize(w * h);
        }
        const uint8_t *p = pixels;
        for (int i = 0; i < w * h; i++) {
            uint8_t r = p[0]; uint8_t g = p[1]; uint8_t b = p[2];
            wallBuffer[i] = (255 << 24) | (r << 16) | (g << 8) | b;
            p += 4;
        }
        SDL_UpdateTexture(wallpaperTex, NULL, wallBuffer.data(), w * sizeof(uint32_t));
    }

    void clearWallpaper() override {
        if (wallpaperTex) { SDL_DestroyTexture(wallpaperTex); wallpaperTex = nullptr; }
    }
    
    void updateOverlay() override {}

    void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map) override
    {
        if (!framebuffer || fb_w <= 0 || fb_h <= 0) return;

        updateMotionSensors();

        uint32_t paletteLUT[16];
        for (int i = 0; i < 16; i++) {
            uint8_t p8ID = palette_map ? palette_map[i] : (uint8_t)i;
            const uint8_t *rgb;
            if (p8ID < 16) rgb = Real8Gfx::PALETTE_RGB[p8ID];
            else if (p8ID >= 128 && p8ID < 144) rgb = Real8Gfx::PALETTE_RGB[p8ID - 128 + 16];
            else rgb = Real8Gfx::PALETTE_RGB[p8ID & 0x0F];
            paletteLUT[i] = (255u << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        }

        if (screenW != fb_w || screenH != fb_h) {
            screenW = fb_w;
            screenH = fb_h;
            screenBuffer.resize((size_t)screenW * (size_t)screenH);
        }

        for (int y = 0; y < fb_h; y++) {
            const uint8_t *src_row = framebuffer + (y * fb_w);
            uint32_t *dst_row = screenBuffer.data() + (y * fb_w);
            for (int x = 0; x < fb_w; x++) {
                dst_row[x] = paletteLUT[src_row[x] & 0x0F];
            }
        }

        SDL_RenderClear(renderer);

        int outputW, outputH;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);

        // Draw Wallpaper behind game, scaled to cover
        if (wallpaperTex && wallW > 0 && wallH > 0) {
            float scaleW = (float)outputW / (float)wallW;
            float scaleH = (float)outputH / (float)wallH;
            float scale = (scaleW > scaleH) ? scaleW : scaleH;

            SDL_Rect dst;
            dst.w = (int)(wallW * scale);
            dst.h = (int)(wallH * scale);
            dst.x = (outputW - dst.w) / 2;
            dst.y = (outputH - dst.h) / 2;
            SDL_RenderCopy(renderer, wallpaperTex, NULL, &dst);
        }

        int texW = 0, texH = 0;
        if (!texture || SDL_QueryTexture(texture, NULL, NULL, &texW, &texH) != 0 || texW != fb_w || texH != fb_h) {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fb_w, fb_h);
        }

        const int mode = (debugVMRef ? debugVMRef->r8_vmode_cur : 0);
        SDL_SetTextureScaleMode(texture, (mode == 0 && interpolation) ? SDL_ScaleModeBest : SDL_ScaleModeNearest);

        SDL_UpdateTexture(texture, NULL, screenBuffer.data(), fb_w * sizeof(uint32_t));

        SDL_Rect dstRect;
        SDL_Rect srcRect = {0, 0, fb_w, fb_h};

        float scale = 1.0f;
        calculateGameRect(outputW, outputH, &dstRect, &scale);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);

        // Simple CRT Scanlines
        if (crt_filter) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
            for (int y = dstRect.y; y < dstRect.y + dstRect.h; y += 2) {
                SDL_RenderDrawLine(renderer, dstRect.x, y, dstRect.x + dstRect.w, y);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        SDL_RenderPresent(renderer);
    }

    // --- System / File IO ---
    unsigned long getMillis() override {
        u64 ticks = armGetSystemTick();
        u64 freq = armGetSystemTickFreq();
        if (freq == 0) return 0;
        return (unsigned long)((ticks * 1000u) / freq);
    }
    void delayMs(int ms) override {
        if (ms <= 0) return;
        svcSleepThread((s64)ms * 1000000LL);
    }
    bool isFastForwardHeld() override { return fastForwardOverride; }
    void setFastForwardHeld(bool held) override {
        fastForwardOverride = held;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (renderer) SDL_RenderSetVSync(renderer, held ? SDL_FALSE : SDL_TRUE);
#endif
    }
    
    void log(const char *fmt, ...) override {
        const int BUF_SIZE = 2048;
        char buffer[BUF_SIZE] = {0};

        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, BUF_SIZE - 1, fmt, args);
        va_end(args);
        printf("%s\n", buffer); // viewable via nxlink
    }

    std::vector<uint8_t> loadFile(const char *path) override {
        std::string fullPath = resolveVirtualPath(path);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    std::vector<std::string> listFiles(const char *ext) override {
        std::vector<std::string> results;
        fs::path cartsPath = rootPath / "carts";
        if (!fs::exists(cartsPath)) return results;
        for (const auto &entry : fs::recursive_directory_iterator(cartsPath)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), cartsPath);
                std::string filename = rel.generic_string();
                if (strlen(ext) == 0 || filename.find(ext) != std::string::npos) {
                    results.push_back("/" + filename);
                }
            }
        }
        return results;
    }

    bool saveState(const char *filename, const uint8_t *data, size_t size) override {
        std::string fullPath = resolveVirtualPath(filename);
        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) return false;
        file.write((const char *)data, size);
        return true;
    }

    std::vector<uint8_t> loadState(const char *filename) override {
        std::string fullPath = resolveVirtualPath(filename);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    bool hasSaveState(const char *filename) override {
        return fs::exists(resolveVirtualPath(filename));
    }

    void deleteFile(const char *path) override {
        fs::remove(resolveVirtualPath(path));
    }

    void getStorageInfo(size_t &used, size_t &total) override {
        used = 0;
        total = 32ULL * 1024 * 1024 * 1024; // Fallback mock 32GB
        if (!sdmcMounted) return;

        FsFileSystem *fsHandle = fsdevGetDeviceFileSystem("sdmc");
        if (!fsHandle) return;

        s64 freeSpace = 0;
        s64 totalSpace = 0;
        Result rcTotal = fsFsGetTotalSpace(fsHandle, "/", &totalSpace);
        Result rcFree = fsFsGetFreeSpace(fsHandle, "/", &freeSpace);
        if (R_FAILED(rcTotal) || R_FAILED(rcFree) || totalSpace <= 0) return;

        total = (size_t)totalSpace;
        if (freeSpace < 0) freeSpace = 0;
        if (totalSpace >= freeSpace) used = (size_t)(totalSpace - freeSpace);
    }

    // --- Switch Native Keyboard for Rename ---
    bool renameGameUI(const char *currentPath) override
    {
        std::string fullPath = resolveVirtualPath(currentPath);
        fs::path p(fullPath);
        if (!fs::exists(p)) return false;

        std::string stem = p.stem().string();
        std::string ext = p.extension().string();

        // Use libnx Software Keyboard
        SwkbdConfig kbd;
        char tmp_outstr[64] = {0};
        
        swkbdCreate(&kbd, 0);
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetInitialText(&kbd, stem.c_str());
        swkbdConfigSetGuideText(&kbd, "Enter new filename");
        
        // FIX: swkbdShow returns void or int (depending on version), 
        // but we should check the string length to see if the user accepted.
        
        swkbdShow(&kbd, tmp_outstr, sizeof(tmp_outstr));
        swkbdClose(&kbd);

        // If string is empty, treat as cancelled
        if (strlen(tmp_outstr) == 0) return false;

        std::string newName = std::string(tmp_outstr) + ext;
        fs::path newP = p.parent_path() / newName;

        try {
            fs::rename(p, newP);
            return true;
        } catch (...) {
            return false;
        }
    }

    // --- Network / Extras ---
    NetworkInfo getNetworkInfo() override {
        if (!nifmReady) return {false, "", "NIFM unavailable", 0.0f};

        NifmInternetConnectionType type;
        u32 wifiStrength = 0;
        NifmInternetConnectionStatus status;
        Result rc = nifmGetInternetConnectionStatus(&type, &wifiStrength, &status);
        if (R_FAILED(rc)) return {false, "", "No connection", 0.0f};

        bool connected = (status == NifmInternetConnectionStatus_Connected);
        std::string statusMsg = connected ? (type == NifmInternetConnectionType_WiFi ? "WiFi" : "Ethernet") : "Connecting";

        std::string ip;
        if (connected) {
            u32 ipaddr = 0;
            if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ipaddr)) && ipaddr != 0) {
                u8 b1 = (ipaddr >> 24) & 0xFF;
                u8 b2 = (ipaddr >> 16) & 0xFF;
                u8 b3 = (ipaddr >> 8) & 0xFF;
                u8 b4 = ipaddr & 0xFF;
                char buf[32];
                snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b1, b2, b3, b4);
                ip = buf;
            }
        }

        return {connected, ip, statusMsg, 0.0f};
    }
    void setWifiCredentials(const char *ssid, const char *pass) override {}
    void setNetworkActive(bool active) override {}

    bool downloadFile(const char *url, const char *savePath) override {
        if (!curlReady || !url || !savePath) return false;
        std::string fullPath = resolveVirtualPath(savePath);
        std::string tempPath = fullPath + ".tmp";

        FILE* out = fopen(tempPath.c_str(), "wb");
        if (!out) return false;

        auto performDownload = [&](bool insecure, CurlWriteState &state, long &httpCode, std::string &err) -> CURLcode {
            CURL *curl = curl_easy_init();
            if (!curl) return CURLE_FAILED_INIT;

            char errBuf[CURL_ERROR_SIZE] = {0};
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Real8Switch");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            if (insecure) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            CURLcode rc = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (errBuf[0] != '\0') err = errBuf;
            curl_easy_cleanup(curl);
            return rc;
        };

        CurlWriteState state;
        state.file = out;
        state.maxBytes = 4u * 1024u * 1024u;

        long httpCode = 0;
        std::string err;
        CURLcode rc = performDownload(false, state, httpCode, err);

        if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT || rc == CURLE_SSL_CACERT_BADFILE) {
            fclose(out);
            out = fopen(tempPath.c_str(), "wb");
            if (!out) {
                fs::remove(tempPath);
                return false;
            }
            state = {};
            state.file = out;
            state.maxBytes = 4u * 1024u * 1024u;
            err.clear();
            httpCode = 0;
            rc = performDownload(true, state, httpCode, err);
        }

        fclose(out);

        if (rc != CURLE_OK || state.overflow || state.total == 0) {
            if (!err.empty()) log("[Switch] downloadFile failed: %s (HTTP %ld)", err.c_str(), httpCode);
            fs::remove(tempPath);
            return false;
        }

        if (fs::exists(fullPath)) fs::remove(fullPath);
        fs::rename(tempPath, fullPath);
        return true;
    }

    std::string getRepoUrlFromFile() override
    {
        std::string path = resolveVirtualPath("gamesrepo.txt");
        if (!fs::exists(path)) return "";

        std::ifstream file(path);
        if (file.is_open()) {
            std::string url;
            std::getline(file, url);
            const char* ws = " \t\n\r\f\v";
            url.erase(url.find_last_not_of(ws) + 1);
            url.erase(0, url.find_first_not_of(ws));
            return url;
        }
        return "";
    }

    void saveRepoUrlToFile(const std::string& url) override
    {
        std::string path = resolveVirtualPath("gamesrepo.txt");
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            file << url;
        }
    }

    void takeScreenshot() override {
        // Save to SD card using standard logic
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << rootPath.string() << "/screenshots/scr_" << std::put_time(std::localtime(&now_c), "%Y%m%d_%H%M%S") << ".bmp";
        
        fs::create_directories(rootPath / "screenshots");
        
        const int capW = (screenW > 0) ? screenW : 128;
        const int capH = (screenH > 0) ? screenH : 128;
        SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void *)screenBuffer.data(), capW, capH, 32, capW * 4,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

        if (surface) {
            SDL_SaveBMP(surface, ss.str().c_str());
            SDL_FreeSurface(surface);
            log("Screenshot saved");
        }
    }
};
