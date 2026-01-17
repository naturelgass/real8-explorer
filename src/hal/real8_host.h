#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>

struct NetworkInfo
{
    bool connected;
    std::string ip;
    std::string statusMsg;
    float transferProgress;
};

struct MouseState
{
    int x;
    int y;
    uint8_t btn;
};

class IReal8Host
{
public:

    static constexpr const char *REAL8_APPNAME = "REAL-8 EXPLORER";
    static constexpr const char *REAL8_VERSION = "1.1.0";
    static constexpr const char *DEFAULT_GAMES_REPOSITORY = "https://raw.githubusercontent.com/naturelgass/real8games/main/gameslist.json";
    
    bool interlaced = false;

    virtual ~IReal8Host() {}

    virtual std::string getClipboardText() { return ""; }
    
    virtual const char *getPlatform() { return "Generic"; }

    virtual void setConsoleState(bool active) {}
    virtual bool isConsoleOpen() { return false; }
    virtual void waitForDebugEvent() { /* Default: do nothing */ }

    virtual void setNetworkActive(bool active) = 0;
    virtual void setWifiCredentials(const char *ssid, const char *pass) = 0;

    // Reads/Writes the repo URL to a text file (e.g. gamesrepo.txt)
    virtual std::string getRepoUrlFromFile() { return ""; }
    virtual void saveRepoUrlToFile(const std::string& url) {}
    
    // --- Graphics ---
    virtual void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map) = 0;
    virtual void flipScreens(const uint8_t *top, int top_w, int top_h,
                             const uint8_t *bottom, int bottom_w, int bottom_h,
                             uint8_t *palette_map) {
        (void)top;
        (void)top_w;
        (void)top_h;
        flipScreen(bottom, bottom_w, bottom_h, palette_map);
    }
    virtual void flipScreenDirty(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map,
                                 int x0, int y0, int x1, int y1) {
        (void)x0;
        (void)y0;
        (void)x1;
        (void)y1;
        flipScreen(framebuffer, fb_w, fb_h, palette_map);
    }

// Optional true-color flip (stereo/anaglyph). Pixel format: 0x00RRGGBB (XRGB8888).
virtual bool flipScreenRGBADirty(const uint32_t* xrgb8888, int w, int h,
                                 int x0, int y0, int x1, int y1) {
    (void)xrgb8888; (void)w; (void)h; (void)x0; (void)y0; (void)x1; (void)y1;
    return false;
}
virtual bool flipScreenRGBA(const uint32_t* xrgb8888, int w, int h) {
    (void)xrgb8888; (void)w; (void)h;
    return false;
}

// Optional RGB565 flip (0brrrrrggggggbbbbb).
virtual bool flipScreenRGB565Dirty(const uint16_t* rgb565, int w, int h,
                                   int x0, int y0, int x1, int y1) {
    (void)rgb565; (void)w; (void)h; (void)x0; (void)y0; (void)x1; (void)y1;
    return false;
}
    virtual bool flipScreenRGB565(const uint16_t* rgb565, int w, int h) {
        (void)rgb565; (void)w; (void)h;
        return false;
    }

    // Optional DMA-friendly framebuffer allocation (platform-specific).
    virtual void* allocLinearFramebuffer(size_t bytes, size_t align) {
        (void)bytes;
        (void)align;
        return nullptr;
    }
    virtual void freeLinearFramebuffer(void* ptr) { (void)ptr; }

    virtual void onFramebufferResize(int fb_w, int fb_h) { (void)fb_w; (void)fb_h; }

    virtual void beginFrame() {}
    virtual bool queueSprite(const uint8_t* spriteSheet, int n, int x, int y, int w, int h, bool fx, bool fy) {
        (void)spriteSheet;
        (void)n;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)fx;
        (void)fy;
        return false;
    }
    virtual void cancelSpriteBatch() {}
    virtual void setTopPreviewBlankHint(bool blank) { (void)blank; }
    virtual void clearTopPreviewBlankHint() {}

    // --- System ---
    virtual unsigned long getMillis() = 0;
    virtual void log(const char *fmt, ...) = 0;
    virtual void delayMs(int ms) = 0;
    virtual bool isFastForwardHeld() { return false; }

    // --- File System ---
    virtual std::vector<uint8_t> loadFile(const char *path) = 0;
    virtual std::vector<std::string> listFiles(const char *ext) = 0;
    virtual bool saveState(const char *filename, const uint8_t *data, size_t size) = 0;
    virtual std::vector<uint8_t> loadState(const char *filename) = 0;
    virtual bool hasSaveState(const char *filename) = 0;
    virtual void deleteFile(const char *path) = 0;
    virtual void getStorageInfo(size_t &used, size_t &total) = 0;
    virtual bool renameGameUI(const char *currentPath) = 0;

    // --- Input (Updated for 8 Players) ---
    // Returns bitmask for a specific player index (0-7)
    virtual uint32_t getPlayerInput(int playerIdx) = 0;
    
    // Updates internal input state (poll SDL)
    virtual void pollInput() = 0; 
    // Clears host-level input state (e.g., sticky keys on exit)
    virtual void clearInputState() {}
    // Allows hosts to clear any latched presses after a logic frame consumes them.
    virtual void consumeLatchedInput() {}
    
    virtual MouseState getMouseState() { return {0, 0, 0}; }
    virtual bool isKeyDownScancode(int scancode) { return false; }

    // --- Input Configuration ---
    virtual void openGamepadConfigUI() = 0;
    virtual std::vector<uint8_t> getInputConfigData() = 0;
    virtual void setInputConfigData(const std::vector<uint8_t>& data) = 0;

    // --- Audio ---
    virtual void pushAudio(const int16_t *samples, int count) = 0;

    // --- Network / OS Actions ---
    virtual NetworkInfo getNetworkInfo() = 0;
    virtual bool downloadFile(const char *url, const char *savePath) = 0;
    virtual void takeScreenshot() = 0;

    virtual void drawWallpaper(const uint8_t *pixels, int w, int h) = 0;
    virtual void clearWallpaper() = 0;
    virtual void updateOverlay() = 0;

    // GPIO / Serial Extensions (Default implementation for PC/Windows)
    virtual void gpioWrite(int pin, int value) {}
    virtual int gpioRead(int pin) { return 0; }
    virtual void gpioAnalogWrite(int pin, int value) {}
    virtual int gpioAnalogRead(int pin) { return 0; }
    virtual void sendSerialStream(const uint8_t *data, int len) {}
};
