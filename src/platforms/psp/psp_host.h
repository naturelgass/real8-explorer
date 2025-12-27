#pragma once

#include "../../hal/real8_host.h"
#include "../../core/real8_vm.h"
#include "psp_input.h"

#include <pspkernel.h>
#include <pspthreadman.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class PspHost : public IReal8Host
{
public:
    PspHost();
    ~PspHost() override;

    const char *getPlatform() override { return "PSP"; }

    void flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map) override;

    unsigned long getMillis() override;
    void log(const char *fmt, ...) override;
    void delayMs(int ms) override;
    bool isFastForwardHeld() override;

    std::vector<uint8_t> loadFile(const char *path) override;
    std::vector<std::string> listFiles(const char *ext) override;
    bool saveState(const char *filename, const uint8_t *data, size_t size) override;
    std::vector<uint8_t> loadState(const char *filename) override;
    bool hasSaveState(const char *filename) override;
    void deleteFile(const char *path) override;
    void getStorageInfo(size_t &used, size_t &total) override;
    bool renameGameUI(const char *currentPath) override;

    uint32_t getPlayerInput(int playerIdx) override;
    void pollInput() override;
    void clearInputState() override;
    std::vector<uint8_t> getInputConfigData() override;
    void setInputConfigData(const std::vector<uint8_t>& data) override;
    void openGamepadConfigUI() override;

    void pushAudio(const int16_t *samples, int count) override;

    NetworkInfo getNetworkInfo() override;
    bool downloadFile(const char *url, const char *savePath) override;
    void setNetworkActive(bool active) override;
    void setWifiCredentials(const char *ssid, const char *pass) override;

    void takeScreenshot() override;

    void drawWallpaper(const uint8_t *pixels, int w, int h) override;
    void clearWallpaper() override;
    void updateOverlay() override;

    void setInterpolation(bool active);

    Real8VM* debugVMRef = nullptr;
    bool crt_filter = false;
    bool interpolation = false;

private:
    void initGu();
    void initAudio();
    void shutdownAudio();
    void resetAudioFifo();
    void seedWallpaperFromPbp();

    std::string resolveVirtualPath(const char *filename);
    void calculateGameRect(float &outX, float &outY, float &outW, float &outH, float &outScale) const;

    static int audioThreadEntry(SceSize args, void *argp);
    int audioThread();

    PspInput input;

    uint16_t *gameTexture = nullptr;

    uint16_t *wallTexture = nullptr;
    int wallW = 0;
    int wallH = 0;
    int wallTexW = 0;
    int wallTexH = 0;

    std::string rootPath;

    SceUID audioThreadId = -1;
    SceLwMutexWorkarea audioMutex;
    bool audioMutexInit = false;
    int audioChannel = -1;
    int outputSampleRate = 22050;
    bool audioRunning = false;

    std::vector<int16_t> audioRing;
    size_t ringHead = 0;
    size_t ringTail = 0;
    size_t ringCount = 0;
};
