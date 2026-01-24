#pragma once

#include <cstdint>
#include <vector>
#include "../../hal/real8_host.h"

class Real8VM;

class GbaHost : public IReal8Host {
public:
    GbaHost();
    ~GbaHost() override = default;

    const char* getPlatform() const override { return "GBA"; }

    void waitForVBlank();

    void setNetworkActive(bool active) override;
    void setWifiCredentials(const char* ssid, const char* pass) override;

    void clearBorders();
    void setSplashBackdrop(bool enabled);
    void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map) override;
    void flipScreenDirty(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map,
                         int x0, int y0, int x1, int y1) override;
    void resetVideo();
    void beginFrame() override;
    bool queueSprite(const uint8_t* spriteSheet, int n, int x, int y, int w, int h, bool fx, bool fy) override;
    void cancelSpriteBatch() override;
    void setProfileVM(Real8VM* vm);

    unsigned long getMillis() override;
    void log(const char* fmt, ...) override;
    void delayMs(int ms) override;

    std::vector<uint8_t> loadFile(const char* path) override;
    std::vector<std::string> listFiles(const char* ext) override;
    bool saveState(const char* filename, const uint8_t* data, size_t size) override;
    std::vector<uint8_t> loadState(const char* filename) override;
    bool hasSaveState(const char* filename) override;
    void deleteFile(const char* path) override;
    void getStorageInfo(size_t &used, size_t &total) override;
    bool renameGameUI(const char* currentPath) override;

    uint32_t getPlayerInput(int playerIdx) override;
    void pollInput() override;
    void consumeLatchedInput() override;

    void openGamepadConfigUI() override;
    std::vector<uint8_t> getInputConfigData() override;
    void setInputConfigData(const std::vector<uint8_t>& data) override;

    void pushAudio(const int16_t* samples, int count) override;

    NetworkInfo getNetworkInfo() override;
    bool downloadFile(const char* url, const char* savePath) override;
    void takeScreenshot() override;

    void drawWallpaper(const uint8_t* pixels, int w, int h) override;
    void clearWallpaper() override;
    void updateOverlay() override;

    void* allocLinearFramebuffer(size_t bytes, size_t align) override;
    void freeLinearFramebuffer(void* ptr) override;

    void renderDebugOverlay();
    void showJitFailureMessage(const char* text, int ms);

private:
    void initVideo();
#if REAL8_GBA_ENABLE_AUDIO
    void initAudio();
    void submitAudioFrame();
#endif
    void blitFrameTiles(const uint8_t *framebuffer, int fb_w, int fb_h, int x0, int y0, int x1, int y1);
    void blitFrameDirty(const uint8_t *framebuffer, int fb_w, int fb_h, int x0, int y0, int x1, int y1);
    void flushSpriteBatch();
    void pushDebugLine(const char* line);
    void drawDebugOverlay();
    void drawChar4x6(int x, int y, char c, uint8_t color);
    void drawText4x6(int x, int y, const char* text, uint8_t color);
    void fillRect(int x, int y, int w, int h, uint8_t color);
    void putPixel(int x, int y, uint8_t color);
    uint16_t keysHeldState = 0;
    uint16_t keysDownState = 0;
    uint32_t inputMask = 0;
    uint32_t latchedInputMask = 0;

    static constexpr int kDebugLineLen = 40;
    static constexpr int kDebugLines = 6;
    char debugLines[kDebugLines][kDebugLineLen] = {};
    int debugLineHead = 0;
    int debugLineCount = 0;
    bool debugDirty = false;
    uint8_t lastPalette[16] = {};
    bool paletteValid = false;
    bool tileModeActive = false;
    bool splashBackdropActive = false;
    bool tilesPending = false;
    int tilesX0 = 0;
    int tilesY0 = 0;
    int tilesX1 = 0;
    int tilesY1 = 0;
    const uint8_t* tilesFb = nullptr;
    bool inputPolled = false;
#if REAL8_GBA_ENABLE_AUDIO
    static constexpr int kAudioSampleRate = 22050;
    static constexpr int kAudioFrameSamples = 368;
    static constexpr int kAudioRingSamples = 4096;
    alignas(4) int16_t audioRing[kAudioRingSamples] = {};
    int audioRingHead = 0;
    int audioRingTail = 0;
    int audioRingCount = 0;
    alignas(4) int8_t audioFrames[2][kAudioFrameSamples] = {};
    int audioFrameIndex = 0;
    bool audioInit = false;
#endif
    int objCount = 0;
    const uint8_t* objSpriteSheet = nullptr;
    const uint8_t* lastObjSpriteSheet = nullptr;
    bool objPending = false;
    Real8VM* profileVm = nullptr;
    int gameW = 128;
    int gameH = 128;
};
