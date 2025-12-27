#pragma once

#include "../../hal/real8_host.h"
#include "../../core/real8_vm.h"
#include "../../core/real8_gfx.h"
#include "linux_input.hpp"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <spawn.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;

namespace fs = std::filesystem;

class LinuxHost : public IReal8Host
{
private:
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *wallpaperTex;
    SDL_AudioDeviceID audioDevice;
    LinuxInput input;

    std::ofstream logFile;

    uint32_t screenBuffer[128 * 128];
    std::vector<uint32_t> wallBuffer;
    int wallW = 0;
    int wallH = 0;

    fs::path dataRoot;
    fs::path configRoot;

    static fs::path getHomePath()
    {
        const char *home = getenv("HOME");
        if (home && home[0] != '\0') return fs::path(home);
        return fs::current_path();
    }

    static fs::path getEnvPath(const char *envName, const fs::path &fallback)
    {
        const char *env = getenv(envName);
        if (env && env[0] != '\0') return fs::path(env);
        return fallback;
    }

    static bool endsWith(const std::string &value, const char *suffix)
    {
        size_t len = strlen(suffix);
        return value.size() >= len && value.compare(value.size() - len, len, suffix) == 0;
    }

    static bool isConfigFile(const std::string &fname)
    {
        return fname == "config.dat" || fname == "wallpaper.png" || fname == "favorites.txt" ||
            fname == "gameslist.json" || fname == "gamesrepo.txt";
    }

    std::string resolveVirtualPath(const char *filename)
    {
        if (!filename) return "";

        fs::path inputPath(filename);
        if (inputPath.is_absolute() && fs::exists(inputPath)) return inputPath.string();

        std::string fname = filename;
        if (!fname.empty() && fname[0] == '/') fname = fname.substr(1);

        fs::path targetDir;
        if (endsWith(fname, ".sav")) {
            targetDir = dataRoot / "saves";
        } else if (isConfigFile(fname)) {
            targetDir = configRoot;
        } else {
            targetDir = dataRoot;
        }

        if (!fs::exists(targetDir)) fs::create_directories(targetDir);
        return (targetDir / fname).string();
    }

    fs::path getScreenshotDir() const
    {
        const char *xdgPics = getenv("XDG_PICTURES_DIR");
        if (xdgPics && xdgPics[0] != '\0') {
            return fs::path(xdgPics) / "Real8 Screenshots";
        }

        fs::path home = getHomePath();
        fs::path pics = home / "Pictures";
        if (fs::exists(pics)) return pics / "Real8 Screenshots";
        return dataRoot / "screenshots";
    }

    static bool runCommand(const std::vector<std::string> &args)
    {
        if (args.empty()) return false;

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = 0;
        int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
        if (rc != 0) return false;

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return false;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    bool downloadWithTool(const std::string &url, const std::string &tempPath)
    {
        std::vector<std::string> args = {
            "curl",
            "--fail",
            "--location",
            "--silent",
            "--show-error",
            "--output",
            tempPath,
            url
        };

        if (runCommand(args)) return true;

        args = {
            "wget",
            "--quiet",
            "--output-document",
            tempPath,
            url
        };

        return runCommand(args);
    }

    void calculateGameRect(int winW, int winH, SDL_Rect *outRect, float *outScale)
    {
        int padding = 0;
        if (wallpaperTex) padding = 50;

        int availW = winW - (padding * 2);
        int availH = winH - (padding * 2);

        if (availW < 1) availW = 1;
        if (availH < 1) availH = 1;

        bool stretch = (debugVMRef && debugVMRef->stretchScreen);
        if (stretch) {
            outRect->x = padding;
            outRect->y = padding;
            outRect->w = availW;
            outRect->h = availH;
            if (outScale) *outScale = (float)availW / 128.0f;
            return;
        }

        float scale = std::min((float)availW / 128.0f, (float)availH / 128.0f);
        int drawW = (int)(128.0f * scale);
        int drawH = (int)(128.0f * scale);

        outRect->x = (winW - drawW) / 2;
        outRect->y = (winH - drawH) / 2;
        outRect->w = drawW;
        outRect->h = drawH;

        if (outScale) *outScale = scale;
    }

public:
    Real8VM *debugVMRef = nullptr;
    bool crt_filter = false;
    bool interpolation = false;

    const char *getPlatform() override { return "Linux"; }

    std::string getClipboardText() override
    {
        if (SDL_HasClipboardText()) {
            char *text = SDL_GetClipboardText();
            if (text) {
                std::string result(text);
                SDL_free(text);
                return result;
            }
        }
        return "";
    }

    LinuxHost(SDL_Renderer *r)
        : renderer(r), texture(nullptr), wallpaperTex(nullptr), audioDevice(0)
    {
        dataRoot = getEnvPath("XDG_DATA_HOME", getHomePath() / ".local" / "share") / "real8";
        configRoot = getEnvPath("XDG_CONFIG_HOME", getHomePath() / ".config") / "real8";

        fs::create_directories(dataRoot);
        fs::create_directories(configRoot);
        fs::create_directories(dataRoot / "mods");
        fs::create_directories(dataRoot / "saves");
        fs::create_directories(dataRoot / "screenshots");

        logFile.open((dataRoot / "logs.txt").string(), std::ios::out | std::ios::trunc);
        if (logFile.is_open()) {
            logFile << "=== REAL-8 SESSION STARTED ===\n";
            logFile.flush();
        }

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
        if (texture) SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

        input.init();
        initAudio();
    }

    ~LinuxHost()
    {
        if (logFile.is_open()) logFile.close();
        if (audioDevice) SDL_CloseAudioDevice(audioDevice);
        if (wallpaperTex) SDL_DestroyTexture(wallpaperTex);
        if (texture) SDL_DestroyTexture(texture);
    }

    void initAudio()
    {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 22050;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = NULL;

        audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audioDevice != 0) SDL_PauseAudioDevice(audioDevice, 0);
    }

    void pushAudio(const int16_t *samples, int count) override
    {
        if (audioDevice == 0) return;
        if (samples == nullptr && count == 0) {
            SDL_ClearQueuedAudio(audioDevice);
            return;
        }
        if (samples == nullptr || count == 0) return;

        const Uint32 TARGET_QUEUE_BYTES = 1024 * sizeof(int16_t);
        const Uint32 MAX_WAIT_CYCLES = 500;

        Uint32 queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
        int safety = 0;

        while (queuedBytes > TARGET_QUEUE_BYTES && safety < (int)MAX_WAIT_CYCLES) {
            SDL_Delay(1);
            queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
            safety++;
        }

        SDL_QueueAudio(audioDevice, samples, count * sizeof(int16_t));
    }

    void setInterpolation(bool active)
    {
        interpolation = active;
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    void drawWallpaper(const uint8_t *pixels, int w, int h) override
    {
        if (!pixels) return;
        if (w != wallW || h != wallH || !wallpaperTex) {
            if (wallpaperTex) SDL_DestroyTexture(wallpaperTex);
            wallpaperTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            wallW = w;
            wallH = h;
            wallBuffer.resize(w * h);
        }
        const uint8_t *p = pixels;
        for (int i = 0; i < w * h; i++) {
            wallBuffer[i] = (255u << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
            p += 4;
        }
        SDL_UpdateTexture(wallpaperTex, NULL, wallBuffer.data(), w * sizeof(uint32_t));
    }

    void clearWallpaper() override
    {
        if (wallpaperTex) { SDL_DestroyTexture(wallpaperTex); wallpaperTex = nullptr; }
    }

    void updateOverlay() override {}

    void flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map) override
    {
        uint32_t paletteLUT[16];

        for (int i = 0; i < 16; i++) {
            uint8_t p8ID = palette_map[i];
            const uint8_t *rgb;
            if (p8ID < 16) rgb = Real8Gfx::PALETTE_RGB[p8ID];
            else if (p8ID >= 128 && p8ID < 144) rgb = Real8Gfx::PALETTE_RGB[p8ID - 128 + 16];
            else rgb = Real8Gfx::PALETTE_RGB[p8ID & 0x0F];
            paletteLUT[i] = (255u << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        }

        int idx = 0;
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                screenBuffer[idx++] = paletteLUT[framebuffer[y][x] & 0x0F];
            }
        }

        SDL_RenderClear(renderer);

        int outputW, outputH;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);

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
        if (!texture || SDL_QueryTexture(texture, NULL, NULL, &texW, &texH) != 0 || texW != 128) {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
            SDL_SetTextureScaleMode(texture, interpolation ? SDL_ScaleModeBest : SDL_ScaleModeNearest);
        }

        SDL_UpdateTexture(texture, NULL, screenBuffer, 128 * sizeof(uint32_t));

        SDL_Rect dstRect;
        SDL_Rect srcRect = {0, 0, 128, 128};
        calculateGameRect(outputW, outputH, &dstRect, NULL);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);

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

    unsigned long getMillis() override { return SDL_GetTicks(); }
    void delayMs(int ms) override { SDL_Delay(ms); }

    void log(const char *fmt, ...) override
    {
        const int BUF_SIZE = 4096;
        char buffer[BUF_SIZE];

        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, BUF_SIZE - 4, fmt, args);
        va_end(args);

        fprintf(stdout, "%s\n", buffer);
        fflush(stdout);

        if (logFile.is_open()) {
            logFile << buffer << "\n";
            logFile.flush();
        }
    }

    uint32_t getPlayerInput(int playerIdx) override { return input.getMask(playerIdx); }
    void pollInput() override { input.update(); }
    void clearInputState() override { input.clearState(); }

    bool isKeyDownScancode(int scancode) override
    {
        if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return false;
        const Uint8 *state = SDL_GetKeyboardState(NULL);
        return state && state[scancode];
    }

    void openGamepadConfigUI() override
    {
        log("[Linux] External gamepad config UI not supported.");
    }

    std::vector<uint8_t> getInputConfigData() override { return input.serialize(); }
    void setInputConfigData(const std::vector<uint8_t> &data) override { input.deserialize(data); }

    MouseState getMouseState() override
    {
        int x = 0, y = 0, w = 0, h = 0;
        Uint32 buttons = SDL_GetMouseState(&x, &y);
        SDL_GetRendererOutputSize(renderer, &w, &h);

        SDL_Rect gameRect;
        float scale = 1.0f;
        calculateGameRect(w, h, &gameRect, &scale);

        int relX = x - gameRect.x;
        int relY = y - gameRect.y;

        bool stretch = (debugVMRef && debugVMRef->stretchScreen);
        float scaleX = stretch ? ((float)gameRect.w / 128.0f) : scale;
        float scaleY = stretch ? ((float)gameRect.h / 128.0f) : scale;
        if (scaleX <= 0.0f) scaleX = 1.0f;
        if (scaleY <= 0.0f) scaleY = 1.0f;

        MouseState ms;
        ms.x = (int)(relX / scaleX);
        ms.y = (int)(relY / scaleY);
        if (ms.x < 0) ms.x = 0; if (ms.x > 127) ms.x = 127;
        if (ms.y < 0) ms.y = 0; if (ms.y > 127) ms.y = 127;
        ms.btn = 0;
        if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ms.btn |= 1;
        if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) ms.btn |= 2;
        if (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ms.btn |= 4;
        return ms;
    }

    std::vector<uint8_t> loadFile(const char *path) override
    {
        std::string fullPath = resolveVirtualPath(path);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    std::vector<std::string> listFiles(const char *ext) override
    {
        std::vector<std::string> results;
        if (!fs::exists(dataRoot)) return results;
        for (const auto &entry : fs::directory_iterator(dataRoot)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (strlen(ext) == 0 || filename.find(ext) != std::string::npos) {
                    results.push_back("/" + filename);
                }
            }
        }
        return results;
    }

    bool saveState(const char *filename, const uint8_t *data, size_t size) override
    {
        std::string fullPath = resolveVirtualPath(filename);
        fs::path parent = fs::path(fullPath).parent_path();
        if (!parent.empty()) fs::create_directories(parent);

        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) return false;
        file.write((const char *)data, size);
        return true;
    }

    std::vector<uint8_t> loadState(const char *filename) override
    {
        std::string fullPath = resolveVirtualPath(filename);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    bool hasSaveState(const char *filename) override { return fs::exists(resolveVirtualPath(filename)); }
    void deleteFile(const char *path) override { fs::remove(resolveVirtualPath(path)); }

    void getStorageInfo(size_t &used, size_t &total) override
    {
        used = 0;
        total = 1024 * 1024 * 1024;

        struct statvfs vfs;
        if (statvfs(dataRoot.string().c_str(), &vfs) != 0) return;

        unsigned long long totalBytes = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
        unsigned long long freeBytes = (unsigned long long)vfs.f_bfree * vfs.f_frsize;

        total = (size_t)totalBytes;
        if (totalBytes >= freeBytes) used = (size_t)(totalBytes - freeBytes);
    }

    bool renameGameUI(const char *currentPath) override
    {
        (void)currentPath;
        log("[Linux] Rename UI not supported.");
        return false;
    }

    NetworkInfo getNetworkInfo() override { return {true, "127.0.0.1", "DESKTOP MODE", 0.0f}; }
    void setWifiCredentials(const char *ssid, const char *pass) override { (void)ssid; (void)pass; }
    void setNetworkActive(bool active) override { (void)active; }

    bool downloadFile(const char *url, const char *savePath) override
    {
        if (!url || !savePath) return false;

        std::string fullPath = resolveVirtualPath(savePath);
        fs::path destPath(fullPath);
        fs::create_directories(destPath.parent_path());

        std::string tempPath = fullPath + ".tmp";
        if (fs::exists(tempPath)) fs::remove(tempPath);

        bool ok = downloadWithTool(url, tempPath);
        if (!ok) {
            if (fs::exists(tempPath)) fs::remove(tempPath);
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
            const char *ws = " \t\n\r\f\v";
            url.erase(url.find_last_not_of(ws) + 1);
            url.erase(0, url.find_first_not_of(ws));
            return url;
        }
        return "";
    }

    void saveRepoUrlToFile(const std::string &url) override
    {
        std::string path = resolveVirtualPath("gamesrepo.txt");
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            file << url;
        }
    }

    void takeScreenshot() override
    {
        fs::path dir = getScreenshotDir();
        fs::create_directories(dir);

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << dir.string() << "/screenshot_" << std::put_time(std::localtime(&now_c), "%Y-%m-%d_%H-%M-%S") << ".bmp";
        std::string fullPath = ss.str();

        SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
            (void *)screenBuffer,
            128, 128,
            32,
            128 * 4,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
        );

        if (surface) {
            if (SDL_SaveBMP(surface, fullPath.c_str()) == 0) {
                log("[SYSTEM] Screenshot saved: %s", fullPath.c_str());
            } else {
                log("[ERROR] Failed to save screenshot: %s", SDL_GetError());
            }
            SDL_FreeSurface(surface);
        }
    }
};
