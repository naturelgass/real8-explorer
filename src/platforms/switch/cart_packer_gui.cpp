#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <process.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lodePNG.h>

#include "cart_blob.h"
#include "../../core/real8_cart.h"
#include "../../hal/real8_host.h"

namespace {
    const int kPadding = 12;
    const int kRowGap = 12;
    const int kLabelHeight = 16;
    const int kEditHeight = 24;
    const int kCheckboxHeight = 18;
    const int kButtonWidth = 110;
    const int kButtonHeight = 26;

    const int kIdTitleEdit = 1001;
    const int kIdPublisherEdit = 1002;
    const int kIdCartEdit = 1003;
    const int kIdBrowseCart = 1004;
    const int kIdToggleStretch = 1005;
    const int kIdToggleCrt = 1006;
    const int kIdToggleInterpol8 = 1007;
    const int kIdReset = 1008;
    const int kIdGenerate = 1009;
    const int kIdIconEdit = 1010;
    const int kIdBrowseIcon = 1011;
    const int kIdVersionEdit = 1012;
    const int kIdWallpaperEdit = 1013;
    const int kIdBrowseWallpaper = 1014;

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

    const UINT kMsgBuildDone = WM_APP + 1;
    const int kTemplateNroResourceId = 301;

    HWND g_titleEdit = nullptr;
    HWND g_publisherEdit = nullptr;
    HWND g_versionEdit = nullptr;
    HWND g_iconEdit = nullptr;
    HWND g_wallpaperEdit = nullptr;
    HWND g_cartEdit = nullptr;
    HWND g_browseIcon = nullptr;
    HWND g_browseWallpaper = nullptr;
    HWND g_browseButton = nullptr;
    HWND g_toggleStretch = nullptr;
    HWND g_toggleCrt = nullptr;
    HWND g_toggleInterpol8 = nullptr;
    HWND g_resetButton = nullptr;
    HWND g_generateButton = nullptr;
    HWND g_logoImage = nullptr;
    HBITMAP g_logoBitmap = nullptr;
    HBRUSH g_windowBrush = nullptr;
    HICON g_appIcon = nullptr;
    COLORREF g_windowBgColor = RGB(0xF0, 0xF0, 0xF0);

    bool g_building = false;
    std::string g_cartPath;
    std::string g_iconPath;
    std::string g_wallpaperPath;
    bool g_titlePlaceholderActive = false;
    bool g_publisherPlaceholderActive = false;
    bool g_versionPlaceholderActive = false;

    const char* kTitlePlaceholder = "My game name";
    const char* kPublisherPlaceholder = "REAL-8";
    const char* kVersionPlaceholder = "1.0.0";

    struct BuildParams {
        HWND hwnd = nullptr;
        std::string cartPath;
        std::string iconPath;
        std::string wallpaperPath;
        std::string title;
        std::string publisher;
        std::string version;
        bool stretch = false;
        bool crt = false;
        bool interpol8 = false;
    };

    struct BuildResult {
        bool success = false;
        std::string message;
    };

    class PackerHost : public IReal8Host {
    public:
        const char* getPlatform() const override { return "SwitchPacker"; }

        void setNetworkActive(bool active) override { (void)active; }
        void setWifiCredentials(const char* ssid, const char* pass) override { (void)ssid; (void)pass; }
        void flipScreen(const uint8_t* framebuffer, int fb_w, int fb_h, uint8_t* palette_map) override {
            (void)framebuffer;
            (void)fb_w;
            (void)fb_h;
            (void)palette_map;
        }
        unsigned long getMillis() override { return 0; }
        void log(const char* fmt, ...) override { (void)fmt; }
        void delayMs(int ms) override { (void)ms; }
        std::vector<uint8_t> loadFile(const char* path) override { (void)path; return {}; }
        std::vector<std::string> listFiles(const char* ext) override { (void)ext; return {}; }
        bool saveState(const char* filename, const uint8_t* data, size_t size) override {
            (void)filename;
            (void)data;
            (void)size;
            return false;
        }
        std::vector<uint8_t> loadState(const char* filename) override { (void)filename; return {}; }
        bool hasSaveState(const char* filename) override { (void)filename; return false; }
        void deleteFile(const char* path) override { (void)path; }
        void getStorageInfo(size_t& used, size_t& total) override { used = 0; total = 0; }
        bool renameGameUI(const char* currentPath) override { (void)currentPath; return false; }
        uint32_t getPlayerInput(int playerIdx) override { (void)playerIdx; return 0; }
        void pollInput() override {}
        void openGamepadConfigUI() override {}
        std::vector<uint8_t> getInputConfigData() override { return {}; }
        void setInputConfigData(const std::vector<uint8_t>& data) override { (void)data; }
        void pushAudio(const int16_t* samples, int count) override { (void)samples; (void)count; }
        NetworkInfo getNetworkInfo() override { return {false, "", "Offline", 0.0f}; }
        bool downloadFile(const char* url, const char* savePath) override { (void)url; (void)savePath; return false; }
        void takeScreenshot() override {}
        void drawWallpaper(const uint8_t* pixels, int w, int h) override { (void)pixels; (void)w; (void)h; }
        void clearWallpaper() override {}
        void updateOverlay() override {}
    };

    static void showMessage(const char* text, UINT flags) {
        MessageBoxA(nullptr, text, "REAL-8 Switch Tools", flags);
    }

    static bool fileExists(const char* path) {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
    }

    static bool buildPath(const char* dir, const char* file, char* out, size_t outSize) {
        if (!dir || !file || !out || outSize == 0) return false;
        size_t dirLen = strlen(dir);
        size_t fileLen = strlen(file);
        bool needsSlash = (dirLen > 0 && dir[dirLen - 1] != '\\' && dir[dirLen - 1] != '/');
        size_t total = dirLen + (needsSlash ? 1 : 0) + fileLen + 1;
        if (total > outSize) return false;
        memcpy(out, dir, dirLen);
        size_t offset = dirLen;
        if (needsSlash) out[offset++] = '\\';
        memcpy(out + offset, file, fileLen);
        out[offset + fileLen] = '\0';
        return true;
    }

    static bool getExeDir(char* out, size_t outSize) {
        if (!out || outSize == 0) return false;
        char path[MAX_PATH] = "";
        DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return false;
        char* lastSlash = strrchr(path, '\\');
        char* lastFwd = strrchr(path, '/');
        char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;
        if (!sep) return false;
        *sep = '\0';
        if (strlen(path) + 1 > outSize) return false;
        strcpy(out, path);
        return true;
    }

    static bool getParentDir(const char* path, char* out, size_t outSize) {
        if (!path || !*path || !out || outSize == 0) return false;
        size_t len = strlen(path);
        while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
            --len;
        }
        if (len == 0) return false;
        const char* lastSlash = nullptr;
        for (size_t i = 0; i < len; ++i) {
            if (path[i] == '\\' || path[i] == '/') lastSlash = path + i;
        }
        if (!lastSlash) return false;
        size_t outLen = (size_t)(lastSlash - path);
        if (outLen + 1 > outSize) return false;
        memcpy(out, path, outLen);
        out[outLen] = '\0';
        return true;
    }

    static bool readFileBytes(const char* path, std::vector<uint8_t>& out, std::string& err) {
        FILE* f = fopen(path, "rb");
        if (!f) {
            err = std::string("Failed to open ") + path;
            return false;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz <= 0) {
            fclose(f);
            err = std::string("File is empty: ") + path;
            return false;
        }
        fseek(f, 0, SEEK_SET);
        out.resize((size_t)sz);
        if (fread(out.data(), 1, out.size(), f) != out.size()) {
            fclose(f);
            err = std::string("Failed to read: ") + path;
            return false;
        }
        fclose(f);
        return true;
    }

    static bool writeFileBytes(const char* path, const std::vector<uint8_t>& data, std::string& err) {
        FILE* f = fopen(path, "wb");
        if (!f) {
            err = std::string("Failed to open for writing: ") + path;
            return false;
        }
        if (!data.empty() && fwrite(data.data(), 1, data.size(), f) != data.size()) {
            fclose(f);
            err = std::string("Failed to write: ") + path;
            return false;
        }
        fclose(f);
        return true;
    }

    static bool findTemplateNroPath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 6; ++i) {
            char candidate[MAX_PATH] = "";
            if (buildPath(currentDir, "Real8Switch_template.nro", candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            if (buildPath(currentDir, "src\\platforms\\switch\\Real8Switch_template.nro",
                          candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }
        return false;
    }

    static bool hasEmbeddedTemplateResource(int resourceId) {
        HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
        return r != nullptr;
    }

    static bool loadEmbeddedTemplateResource(int resourceId, std::vector<uint8_t>& out, const char* label,
                                             std::string& err) {
        HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
        if (!r) {
            err = std::string("Embedded ") + label + " not found in this executable.";
            return false;
        }
        DWORD sz = SizeofResource(nullptr, r);
        if (sz == 0) {
            err = std::string("Embedded ") + label + " is empty.";
            return false;
        }
        HGLOBAL h = LoadResource(nullptr, r);
        if (!h) {
            err = std::string("Failed to load embedded ") + label + " resource.";
            return false;
        }
        void* p = LockResource(h);
        if (!p) {
            err = std::string("Failed to access embedded ") + label + " resource.";
            return false;
        }
        out.assign((const uint8_t*)p, (const uint8_t*)p + sz);
        return true;
    }

    static bool hasEmbeddedTemplateNro() {
        return hasEmbeddedTemplateResource(kTemplateNroResourceId);
    }

    static bool loadEmbeddedTemplateNro(std::vector<uint8_t>& out, std::string& err) {
        return loadEmbeddedTemplateResource(kTemplateNroResourceId, out, "template NRO", err);
    }

    static std::string trimWhitespace(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            end--;
        }
        return s.substr(start, end - start);
    }

    static void setPlaceholder(HWND edit, const char* text, bool& active) {
        if (!edit || !text) return;
        SetWindowTextA(edit, text);
        active = true;
        SendMessageA(edit, EM_SETSEL, 0, 0);
        InvalidateRect(edit, nullptr, TRUE);
    }

    static void clearPlaceholder(HWND edit, bool& active) {
        if (!edit) return;
        SetWindowTextA(edit, "");
        active = false;
        InvalidateRect(edit, nullptr, TRUE);
    }

    static void ensurePlaceholder(HWND edit, const char* text, bool& active) {
        if (!edit || !text) return;
        if (active) return;
        char buf[256] = "";
        GetWindowTextA(edit, buf, sizeof(buf));
        if (buf[0] == '\0') {
            setPlaceholder(edit, text, active);
        }
    }

    static void updateGenerateEnabled() {
        if (!g_generateButton) return;
        bool ready = !g_building;
        if (ready) {
            char titleBuf[256] = "";
            char publisherBuf[256] = "";
            char versionBuf[256] = "";
            if (g_titleEdit) GetWindowTextA(g_titleEdit, titleBuf, sizeof(titleBuf));
            if (g_publisherEdit) GetWindowTextA(g_publisherEdit, publisherBuf, sizeof(publisherBuf));
            if (g_versionEdit) GetWindowTextA(g_versionEdit, versionBuf, sizeof(versionBuf));
            std::string title = trimWhitespace(titleBuf);
            std::string publisher = trimWhitespace(publisherBuf);
            std::string version = trimWhitespace(versionBuf);
            if (g_titlePlaceholderActive) title.clear();
            if (g_publisherPlaceholderActive) publisher.clear();
            if (g_versionPlaceholderActive) version.clear();
            if (title.empty() || publisher.empty() || version.empty()) {
                ready = false;
            }
            if (g_iconPath.empty() || g_cartPath.empty()) {
                ready = false;
            }
        }
        EnableWindow(g_generateButton, ready ? TRUE : FALSE);
    }

    static bool endsWithIgnoreCase(const std::string& value, const char* suffix) {
        if (!suffix) return false;
        size_t valueLen = value.size();
        size_t suffixLen = strlen(suffix);
        if (suffixLen > valueLen) return false;
        size_t offset = valueLen - suffixLen;
        for (size_t i = 0; i < suffixLen; ++i) {
            char a = (char)std::tolower((unsigned char)value[offset + i]);
            char b = (char)std::tolower((unsigned char)suffix[i]);
            if (a != b) return false;
        }
        return true;
    }

    static COLORREF getWindowBackgroundColor() {
        return g_windowBgColor;
    }

    static HBITMAP loadPngBitmap(const char* pngPath, int maxW, int maxH, COLORREF bgColor,
                                 int* outW, int* outH, std::string& err) {
        unsigned char* image = nullptr;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32_file(&image, &w, &h, pngPath);
        if (error || !image) {
            err = std::string("Failed to decode logo PNG: ") + pngPath;
            return nullptr;
        }

        int dstW = (int)w;
        int dstH = (int)h;
        if (maxW > 0 || maxH > 0) {
            double scaleW = maxW > 0 ? (double)maxW / (double)w : 1.0;
            double scaleH = maxH > 0 ? (double)maxH / (double)h : 1.0;
            double scale = scaleW < scaleH ? scaleW : scaleH;
            if (scale <= 0.0) scale = 1.0;
            dstW = (int)(w * scale + 0.5);
            dstH = (int)(h * scale + 0.5);
        }
        if (dstW < 1) dstW = 1;
        if (dstH < 1) dstH = 1;
        if (outW) *outW = dstW;
        if (outH) *outH = dstH;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = dstW;
        bmi.bmiHeader.biHeight = -dstH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) {
            free(image);
            err = "Failed to create logo bitmap.";
            if (bmp) DeleteObject(bmp);
            return nullptr;
        }

        uint8_t bgR = (uint8_t)(bgColor & 0xFF);
        uint8_t bgG = (uint8_t)((bgColor >> 8) & 0xFF);
        uint8_t bgB = (uint8_t)((bgColor >> 16) & 0xFF);

        uint8_t* dst = static_cast<uint8_t*>(bits);
        for (int y = 0; y < dstH; ++y) {
            unsigned srcY = (unsigned)((uint64_t)y * h / (unsigned)dstH);
            for (int x = 0; x < dstW; ++x) {
                unsigned srcX = (unsigned)((uint64_t)x * w / (unsigned)dstW);
                size_t srcIdx = ((size_t)srcY * w + srcX) * 4;
                size_t dstIdx = ((size_t)y * dstW + x) * 4;
                uint8_t srcR = image[srcIdx + 0];
                uint8_t srcG = image[srcIdx + 1];
                uint8_t srcB = image[srcIdx + 2];
                uint8_t srcA = image[srcIdx + 3];
                if (srcA == 255) {
                    dst[dstIdx + 0] = srcB;
                    dst[dstIdx + 1] = srcG;
                    dst[dstIdx + 2] = srcR;
                    dst[dstIdx + 3] = 255;
                } else if (srcA == 0) {
                    dst[dstIdx + 0] = bgB;
                    dst[dstIdx + 1] = bgG;
                    dst[dstIdx + 2] = bgR;
                    dst[dstIdx + 3] = 255;
                } else {
                    uint16_t invA = (uint16_t)(255 - srcA);
                    uint8_t outR = (uint8_t)((srcR * srcA + bgR * invA) / 255);
                    uint8_t outG = (uint8_t)((srcG * srcA + bgG * invA) / 255);
                    uint8_t outB = (uint8_t)((srcB * srcA + bgB * invA) / 255);
                    dst[dstIdx + 0] = outB;
                    dst[dstIdx + 1] = outG;
                    dst[dstIdx + 2] = outR;
                    dst[dstIdx + 3] = 255;
                }
            }
        }

        free(image);
        return bmp;
    }

    static HICON loadPngIcon(const char* pngPath, int maxSize, std::string& err) {
        unsigned char* image = nullptr;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32_file(&image, &w, &h, pngPath);
        if (error || !image) {
            err = std::string("Failed to decode icon PNG: ") + pngPath;
            return nullptr;
        }

        int dstW = (int)w;
        int dstH = (int)h;
        if (maxSize > 0 && ((int)w > maxSize || (int)h > maxSize)) {
            double scaleW = (double)maxSize / (double)w;
            double scaleH = (double)maxSize / (double)h;
            double scale = scaleW < scaleH ? scaleW : scaleH;
            if (scale <= 0.0) scale = 1.0;
            dstW = (int)(w * scale + 0.5);
            dstH = (int)(h * scale + 0.5);
        }
        if (dstW < 1) dstW = 1;
        if (dstH < 1) dstH = 1;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = dstW;
        bmi.bmiHeader.biHeight = -dstH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP colorBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!colorBmp || !bits) {
            free(image);
            err = "Failed to create icon bitmap.";
            if (colorBmp) DeleteObject(colorBmp);
            return nullptr;
        }

        uint8_t* dst = static_cast<uint8_t*>(bits);
        const int dstStride = dstW * 4;
        for (int y = 0; y < dstH; ++y) {
            unsigned srcY = (unsigned)((uint64_t)y * h / (unsigned)dstH);
            uint8_t* row = dst + y * dstStride;
            for (int x = 0; x < dstW; ++x) {
                unsigned srcX = (unsigned)((uint64_t)x * w / (unsigned)dstW);
                size_t srcIdx = ((size_t)srcY * w + srcX) * 4;
                size_t dstIdx = (size_t)x * 4;
                row[dstIdx + 0] = image[srcIdx + 2];
                row[dstIdx + 1] = image[srcIdx + 1];
                row[dstIdx + 2] = image[srcIdx + 0];
                row[dstIdx + 3] = image[srcIdx + 3];
            }
        }

        int maskStride = ((dstW + 31) / 32) * 4;
        std::vector<uint8_t> mask(maskStride * dstH, 0x00);
        for (int y = 0; y < dstH; ++y) {
            unsigned srcY = (unsigned)((uint64_t)y * h / (unsigned)dstH);
            uint8_t* maskRow = mask.data() + y * maskStride;
            for (int x = 0; x < dstW; ++x) {
                unsigned srcX = (unsigned)((uint64_t)x * w / (unsigned)dstW);
                size_t srcIdx = ((size_t)srcY * w + srcX) * 4;
                uint8_t alpha = image[srcIdx + 3];
                if (alpha < 128) {
                    int byteIndex = x / 8;
                    int bitIndex = 7 - (x % 8);
                    maskRow[byteIndex] |= (uint8_t)(1 << bitIndex);
                }
            }
        }

        HBITMAP maskBmp = CreateBitmap(dstW, dstH, 1, 1, mask.data());
        if (!maskBmp) {
            free(image);
            DeleteObject(colorBmp);
            err = "Failed to create icon mask.";
            return nullptr;
        }

        ICONINFO info{};
        info.fIcon = TRUE;
        info.hbmColor = colorBmp;
        info.hbmMask = maskBmp;
        HICON icon = CreateIconIndirect(&info);

        DeleteObject(colorBmp);
        DeleteObject(maskBmp);
        free(image);

        if (!icon) {
            err = "Failed to create icon.";
        }
        return icon;
    }

    static bool findLogoPath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 6; ++i) {
            char candidate[MAX_PATH] = "";
            if (buildPath(currentDir, "REAL8-banner.png", candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            if (buildPath(currentDir, "src\\platforms\\switch\\REAL8-banner.png", candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }
        return false;
    }

    static bool findIconPath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 6; ++i) {
            char candidate[MAX_PATH] = "";
            if (buildPath(currentDir, "icon.png", candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            if (buildPath(currentDir, "src\\platforms\\switch\\icon.png", candidate, sizeof(candidate)) &&
                fileExists(candidate)) {
                if (strlen(candidate) + 1 > outSize) return false;
                strcpy(out, candidate);
                return true;
            }
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }
        return false;
    }


    static bool buildCartBlobFromPng(const char* cartPath, uint32_t flags, std::vector<uint8_t>& outBlob,
                                     std::string& err) {
        std::vector<uint8_t> cartBytes;
        if (!readFileBytes(cartPath, cartBytes, err)) return false;

        PackerHost host;
        GameData game;
        if (!Real8CartLoader::LoadFromBuffer(&host, cartBytes, game)) {
            err = "Failed to parse cart data.";
            return false;
        }

        std::vector<uint8_t> payload;
        payload.reserve(sizeof(game.gfx) + sizeof(game.map) + sizeof(game.sprite_flags) +
                        sizeof(game.music) + sizeof(game.sfx) + game.lua_code.size());
        payload.insert(payload.end(), game.gfx, game.gfx + sizeof(game.gfx));
        payload.insert(payload.end(), game.map, game.map + sizeof(game.map));
        payload.insert(payload.end(), game.sprite_flags, game.sprite_flags + sizeof(game.sprite_flags));
        payload.insert(payload.end(), game.music, game.music + sizeof(game.music));
        payload.insert(payload.end(), game.sfx, game.sfx + sizeof(game.sfx));
        payload.insert(payload.end(), game.lua_code.begin(), game.lua_code.end());

        CartBlobHeader header{};
        memcpy(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE);
        header.flags = flags;
        header.raw_size = (uint32_t)payload.size();
        header.comp_size = (uint32_t)payload.size();

        outBlob.resize(sizeof(CartBlobHeader) + payload.size());
        memcpy(outBlob.data(), &header, sizeof(CartBlobHeader));
        if (!payload.empty()) {
            memcpy(outBlob.data() + sizeof(CartBlobHeader), payload.data(), payload.size());
        }
        return true;
    }

    static bool findTemplateBlobSlot(const std::vector<uint8_t>& bin, size_t& slotOffset, uint32_t& slotCapacity,
                                     std::string& err) {
        const size_t hdrSize = sizeof(CartBlobHeader);
        if (bin.size() < hdrSize + 0x100) {
            err = "Template file is too small.";
            return false;
        }

        for (size_t i = 0; i + hdrSize <= bin.size(); ++i) {
            if (memcmp(&bin[i], CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE) != 0) continue;

            CartBlobHeader h{};
            memcpy(&h, &bin[i], hdrSize);
            if (memcmp(h.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE) != 0) continue;
            if (h.comp_size == 0) continue;
            if (h.raw_size != 0) continue;

            size_t slotEnd = i + hdrSize + (size_t)h.comp_size;
            if (slotEnd > bin.size()) continue;

            slotOffset = i;
            slotCapacity = h.comp_size;
            return true;
        }

        err = "Could not find a cart blob slot in the template.\n"
              "Rebuild the template with: make template";
        return false;
    }

    static bool patchTemplateNro(const std::vector<uint8_t>& templateBin,
                                 const std::vector<uint8_t>& cartBlob,
                                 std::vector<uint8_t>& outBin,
                                 std::string& err) {
        if (cartBlob.size() < sizeof(CartBlobHeader)) {
            err = "Cart blob is too small.";
            return false;
        }

        size_t slotOffset = 0;
        uint32_t slotCapacity = 0;
        if (!findTemplateBlobSlot(templateBin, slotOffset, slotCapacity, err)) return false;

        const size_t hdrSize = sizeof(CartBlobHeader);
        const size_t payloadSize = cartBlob.size() - hdrSize;
        if (payloadSize > slotCapacity) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "Cart is too large for this template slot.\n\n"
                     "Cart payload: %u bytes\nSlot capacity: %u bytes\n\n"
                     "Rebuild the template with a larger CART_TEMPLATE_CAPACITY.",
                     (unsigned)payloadSize, (unsigned)slotCapacity);
            err = buf;
            return false;
        }

        outBin = templateBin;
        memcpy(outBin.data() + slotOffset, cartBlob.data(), cartBlob.size());
        if (slotCapacity > payloadSize) {
            memset(outBin.data() + slotOffset + hdrSize + payloadSize, 0, slotCapacity - payloadSize);
        }
        return true;
    }

#pragma pack(push, 1)
    struct NroAssetSection {
        uint64_t offset;
        uint64_t size;
    };

    struct NroAssetHeader {
        uint32_t magic;
        uint32_t version;
        NroAssetSection icon;
        NroAssetSection nacp;
        NroAssetSection romfs;
    };

    struct RomfsHeader {
        uint64_t headerSize;
        uint64_t dirHashOffset;
        uint64_t dirHashSize;
        uint64_t dirTableOffset;
        uint64_t dirTableSize;
        uint64_t fileHashOffset;
        uint64_t fileHashSize;
        uint64_t fileTableOffset;
        uint64_t fileTableSize;
        uint64_t fileDataOffset;
    };
#pragma pack(pop)

    static bool findNroAssetHeader(const std::vector<uint8_t>& data, size_t& outOffset,
                                   NroAssetHeader& outHeader, std::string& err) {
        const uint32_t kMagic = 0x54455341; // 'ASET'
        if (data.size() < sizeof(NroAssetHeader)) {
            err = "NRO file is too small.";
            return false;
        }

        bool found = false;
        size_t bestOffset = 0;
        NroAssetHeader bestHeader{};

        for (size_t i = 0; i + sizeof(NroAssetHeader) <= data.size(); ++i) {
            if (memcmp(data.data() + i, "ASET", 4) != 0) continue;

            NroAssetHeader header{};
            memcpy(&header, data.data() + i, sizeof(header));
            if (header.magic != kMagic || header.version != 0) continue;

            const uint64_t nacpEnd = header.nacp.offset + header.nacp.size;
            const uint64_t iconEnd = header.icon.offset + header.icon.size;
            const uint64_t romfsEnd = header.romfs.offset + header.romfs.size;
            const uint64_t maxEnd = iconEnd > nacpEnd ? iconEnd : nacpEnd;
            const uint64_t maxEnd2 = romfsEnd > maxEnd ? romfsEnd : maxEnd;

            if (maxEnd2 > data.size()) continue;

            if (!found || i > bestOffset) {
                found = true;
                bestOffset = i;
                bestHeader = header;
            }
        }

        if (!found) {
            err = "NRO asset header not found.";
            return false;
        }

        outOffset = bestOffset;
        outHeader = bestHeader;
        return true;
    }

    static void writeNacpString(uint8_t* dst, size_t dstSize, const std::string& value) {
        if (!dst || dstSize == 0) return;
        memset(dst, 0, dstSize);
        size_t copyLen = value.size();
        if (copyLen >= dstSize) copyLen = dstSize - 1;
        if (copyLen > 0) {
            memcpy(dst, value.data(), copyLen);
        }
    }

    static bool hasJpegSignature(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + 2 > data.size()) return false;
        return data[offset] == 0xFF && data[offset + 1] == 0xD8;
    }

    static bool hasPngSignature(const std::vector<uint8_t>& data, size_t offset) {
        const uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (offset + sizeof(pngSig) > data.size()) return false;
        return memcmp(data.data() + offset, pngSig, sizeof(pngSig)) == 0;
    }

    static size_t align4(size_t value) {
        return (value + 3u) & ~static_cast<size_t>(3u);
    }

    static size_t resolveNroAssetBase(const std::vector<uint8_t>& data, size_t headerOffset,
                                      const NroAssetHeader& header) {
        if (header.icon.size >= 2) {
            size_t relIcon = headerOffset + (size_t)header.icon.offset;
            size_t absIcon = (size_t)header.icon.offset;
            bool relSig = hasJpegSignature(data, relIcon) || hasPngSignature(data, relIcon);
            bool absSig = hasJpegSignature(data, absIcon) || hasPngSignature(data, absIcon);
            if (relSig && !absSig) return headerOffset;
            if (absSig && !relSig) return 0;
        }
        return headerOffset;
    }

    static bool patchNroIcon(std::vector<uint8_t>& data, const std::string& iconPath, std::string& err) {
        if (iconPath.empty()) return true;
        if (!endsWithIgnoreCase(iconPath, ".jpg")) {
            err = "Icon must be a .jpg file.";
            return false;
        }

        std::vector<uint8_t> iconBytes;
        if (!readFileBytes(iconPath.c_str(), iconBytes, err)) return false;
        if (iconBytes.size() < 2 || iconBytes[0] != 0xFF || iconBytes[1] != 0xD8) {
            err = "Icon must be a valid JPG file.";
            return false;
        }

        NroAssetHeader header{};
        size_t headerOffset = 0;
        std::string headerErr;
        if (!findNroAssetHeader(data, headerOffset, header, headerErr)) {
            err = std::string("Icon not patched: ") + headerErr;
            return false;
        }

        size_t assetBase = resolveNroAssetBase(data, headerOffset, header);
        size_t iconOffset = assetBase + (size_t)header.icon.offset;
        size_t iconSize = (size_t)header.icon.size;
        if (iconSize == 0 || iconOffset + iconSize > data.size()) {
            err = "Icon not patched: invalid icon section.";
            return false;
        }
        if (iconBytes.size() > iconSize) {
            err = "Icon is too large for the NRO icon slot.";
            return false;
        }

        memcpy(data.data() + iconOffset, iconBytes.data(), iconBytes.size());
        if (iconBytes.size() < iconSize) {
            memset(data.data() + iconOffset + iconBytes.size(), 0, iconSize - iconBytes.size());
        }
        return true;
    }

    static bool patchNroWallpaper(std::vector<uint8_t>& data, const std::string& wallpaperPath, std::string& err) {
        if (wallpaperPath.empty()) return true;
        if (!endsWithIgnoreCase(wallpaperPath, ".png")) {
            err = "Background must be a .png file.";
            return false;
        }

        std::vector<uint8_t> wallpaperBytes;
        if (!readFileBytes(wallpaperPath.c_str(), wallpaperBytes, err)) return false;
        if (!hasPngSignature(wallpaperBytes, 0)) {
            err = "Background must be a valid PNG file.";
            return false;
        }

        NroAssetHeader header{};
        size_t headerOffset = 0;
        std::string headerErr;
        if (!findNroAssetHeader(data, headerOffset, header, headerErr)) {
            err = std::string("Background not patched: ") + headerErr;
            return false;
        }

        size_t assetBase = resolveNroAssetBase(data, headerOffset, header);
        const size_t romfsOffset = assetBase + (size_t)header.romfs.offset;
        size_t romfsSize = (size_t)header.romfs.size;
        if (romfsSize < sizeof(RomfsHeader) || romfsOffset + romfsSize > data.size()) {
            err = "Background not patched: invalid RomFS section.";
            return false;
        }

        RomfsHeader romfs{};
        memcpy(&romfs, data.data() + romfsOffset, sizeof(romfs));
        if (romfs.headerSize < sizeof(RomfsHeader)) {
            err = "Background not patched: RomFS header is invalid.";
            return false;
        }
        if (romfs.fileDataOffset >= romfsSize) {
            err = "Background not patched: RomFS data section is invalid.";
            return false;
        }
        const size_t fileTableOffset = romfsOffset + (size_t)romfs.fileTableOffset;
        const size_t fileTableSize = (size_t)romfs.fileTableSize;
        if (fileTableOffset + fileTableSize > romfsOffset + romfsSize) {
            err = "Background not patched: RomFS file table is invalid.";
            return false;
        }

        size_t wallpaperEntryOffset = 0;
        uint64_t wallpaperDataOffset = 0;
        uint64_t wallpaperDataSize = 0;
        bool wallpaperFound = false;
        size_t maxDataEnd = 0;

        size_t cursor = fileTableOffset;
        const size_t tableEnd = fileTableOffset + fileTableSize;
        while (cursor < tableEnd) {
            if (cursor + 32 > tableEnd) {
                err = "Background not patched: RomFS file table is truncated.";
                return false;
            }

            uint32_t nameLen = 0;
            uint64_t dataOffset = 0;
            uint64_t dataSize = 0;
            memcpy(&dataOffset, data.data() + cursor + 8, sizeof(dataOffset));
            memcpy(&dataSize, data.data() + cursor + 16, sizeof(dataSize));
            memcpy(&nameLen, data.data() + cursor + 28, sizeof(nameLen));

            size_t nameStart = cursor + 32;
            size_t nameEnd = nameStart + nameLen;
            if (nameEnd > tableEnd) {
                err = "Background not patched: RomFS file name is invalid.";
                return false;
            }

            size_t dataEnd = (size_t)dataOffset + (size_t)dataSize;
            if (dataEnd > maxDataEnd) maxDataEnd = dataEnd;

            if (nameLen > 0) {
                const char* namePtr = reinterpret_cast<const char*>(data.data() + nameStart);
                std::string name(namePtr, nameLen);
                if (name == "wallpaper.png") {
                    wallpaperEntryOffset = cursor;
                    wallpaperDataOffset = dataOffset;
                    wallpaperDataSize = dataSize;
                    wallpaperFound = true;
                }
            }

            size_t entrySize = 32 + align4(nameLen);
            if (entrySize == 0 || cursor + entrySize > tableEnd) {
                err = "Background not patched: RomFS file entry size is invalid.";
                return false;
            }
            cursor += entrySize;
        }

        if (!wallpaperFound) {
            err = "Background not patched: wallpaper.png not found in RomFS.\n"
                  "Rebuild the template after adding romfs/real8/config/wallpaper.png.";
            return false;
        }

        const size_t fileDataBase = romfsOffset + (size_t)romfs.fileDataOffset;
        size_t dataStart = fileDataBase + (size_t)wallpaperDataOffset;
        if (dataStart + wallpaperDataSize > romfsOffset + romfsSize) {
            err = "Background not patched: RomFS data section is invalid.";
            return false;
        }

        size_t newSize = wallpaperBytes.size();
        if (newSize > wallpaperDataSize) {
            if ((size_t)wallpaperDataOffset + (size_t)wallpaperDataSize != maxDataEnd) {
                err = "Background too large for this template.";
                return false;
            }
            size_t newEnd = fileDataBase + (size_t)wallpaperDataOffset + newSize;
            size_t newRomfsSize = newEnd - romfsOffset;
            if (romfsOffset + romfsSize != data.size()) {
                err = "Background too large for this template.";
                return false;
            }
            if (newRomfsSize > romfsSize) {
                data.resize(data.size() + (newRomfsSize - romfsSize), 0);
                romfsSize = newRomfsSize;
                header.romfs.size = romfsSize;
                memcpy(data.data() + headerOffset, &header, sizeof(header));
            }
        }

        if (dataStart + newSize > data.size()) {
            err = "Background too large for this template.";
            return false;
        }

        memcpy(data.data() + dataStart, wallpaperBytes.data(), newSize);
        if (newSize < wallpaperDataSize) {
            memset(data.data() + dataStart + newSize, 0, (size_t)wallpaperDataSize - newSize);
        }

        uint64_t newSize64 = (uint64_t)newSize;
        memcpy(data.data() + wallpaperEntryOffset + 16, &newSize64, sizeof(newSize64));
        return true;
    }

    static bool patchNroNacp(std::vector<uint8_t>& data, const std::string& title,
                             const std::string& publisher, const std::string& version,
                             std::string& warn) {
        if (title.empty() && publisher.empty() && version.empty()) return true;

        NroAssetHeader header{};
        size_t headerOffset = 0;
        std::string err;
        if (!findNroAssetHeader(data, headerOffset, header, err)) {
            warn = std::string("NACP not patched: ") + err;
            return false;
        }

        size_t assetBase = resolveNroAssetBase(data, headerOffset, header);
        const size_t nacpOffset = assetBase + (size_t)header.nacp.offset;
        const size_t nacpSize = (size_t)header.nacp.size;
        const size_t entrySize = 0x300;
        const size_t nameSize = 0x200;
        const size_t authorSize = 0x100;
        const size_t entryCount = 16;
        const size_t versionOffset = 0x3060;
        const size_t versionSize = 0x10;

        if (nacpSize < entrySize * entryCount || nacpOffset + nacpSize > data.size()) {
            warn = "NACP not patched: invalid NACP section.";
            return false;
        }

        for (size_t i = 0; i < entryCount; ++i) {
            size_t entryOffset = nacpOffset + i * entrySize;
            uint8_t* namePtr = data.data() + entryOffset;
            uint8_t* authorPtr = data.data() + entryOffset + nameSize;
            if (!title.empty()) {
                writeNacpString(namePtr, nameSize, title);
            }
            if (!publisher.empty()) {
                writeNacpString(authorPtr, authorSize, publisher);
            }
        }

        if (!version.empty()) {
            if (nacpSize < versionOffset + versionSize) {
                warn = "NACP version not patched: invalid display version field.";
                return false;
            }
            uint8_t* versionPtr = data.data() + nacpOffset + versionOffset;
            writeNacpString(versionPtr, versionSize, version);
        }

        return true;
    }

    static std::string cartBaseName(const std::string& path) {
        std::string name = path;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.size() > 7 && name.substr(name.size() - 7) == ".p8.png") {
            name.resize(name.size() - 7);
            return name;
        }
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name.resize(dot);
        return name;
    }

    static std::string cartDirName(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return "";
        return path.substr(0, slash);
    }

    static bool browseForCart(HWND hwnd, std::string& outPath) {
        OPENFILENAMEA ofn{};
        char filePath[MAX_PATH] = "";
        const char* filter = "PICO-8 Cart (*.p8.png;*.p8)\0*.p8.png;*.p8\0All Files\0*.*\0";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = filter;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameA(&ofn)) {
            outPath = filePath;
            return true;
        }
        return false;
    }

    static bool browseForIcon(HWND hwnd, std::string& outPath) {
        OPENFILENAMEA ofn{};
        char filePath[MAX_PATH] = "";
        const char* filter = "JPG Images (*.jpg)\0*.jpg\0";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = filter;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameA(&ofn)) {
            outPath = filePath;
            return true;
        }
        return false;
    }

    static bool browseForWallpaper(HWND hwnd, std::string& outPath) {
        OPENFILENAMEA ofn{};
        char filePath[MAX_PATH] = "";
        const char* filter = "PNG Images (*.png)\0*.png\0";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = filter;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameA(&ofn)) {
            outPath = filePath;
            return true;
        }
        return false;
    }


    static BuildResult buildStandalone(const BuildParams& params) {
        BuildResult result{};

        std::vector<uint8_t> templateBin;
        std::string err;
        if (hasEmbeddedTemplateNro()) {
            if (!loadEmbeddedTemplateNro(templateBin, err)) {
                result.message = err;
                return result;
            }
        } else {
            char tplPath[MAX_PATH] = "";
            if (!findTemplateNroPath(tplPath, sizeof(tplPath))) {
                result.message =
                    "Template NRO not found. Build it with:\n"
                    "  make template\n"
                    "Then rebuild the tools with:\n"
                    "  make tools";
                return result;
            }
            if (!readFileBytes(tplPath, templateBin, err)) {
                result.message = err;
                return result;
            }
        }

        uint32_t cartFlags = CART_BLOB_FLAG_NONE;
        if (params.stretch) cartFlags |= CART_BLOB_FLAG_STRETCH;
        if (params.crt) cartFlags |= CART_BLOB_FLAG_CRTFILTER;
        if (params.interpol8) cartFlags |= CART_BLOB_FLAG_INTERPOL8;

        std::vector<uint8_t> cartBlob;
        if (!buildCartBlobFromPng(params.cartPath.c_str(), cartFlags, cartBlob, err)) {
            result.message = err;
            return result;
        }

        std::vector<uint8_t> outputNro;
        if (!patchTemplateNro(templateBin, cartBlob, outputNro, err)) {
            result.message = err;
            return result;
        }

        if (!patchNroIcon(outputNro, params.iconPath, err)) {
            result.message = err;
            return result;
        }
        if (!patchNroWallpaper(outputNro, params.wallpaperPath, err)) {
            result.message = err;
            return result;
        }

        std::string nacpWarn;
        patchNroNacp(outputNro, params.title, params.publisher, params.version, nacpWarn);

        std::string base = cartBaseName(params.cartPath);
        std::string outDir = cartDirName(params.cartPath);
        std::string outPath = outDir.empty() ? (base + ".nro") : (outDir + "\\" + base + ".nro");
        if (!writeFileBytes(outPath.c_str(), outputNro, err)) {
            result.message = err;
            return result;
        }

        result.success = true;
        result.message = "Generated:\n" + outPath;
        if (!nacpWarn.empty()) {
            result.message += "\n\n" + nacpWarn;
        }
        return result;
    }

    static unsigned __stdcall buildThreadProc(void* data) {
        BuildParams* params = reinterpret_cast<BuildParams*>(data);
        BuildResult result = buildStandalone(*params);
        BuildResult* heapResult = new BuildResult(result);
        PostMessageA(params->hwnd, kMsgBuildDone, result.success ? 1 : 0, (LPARAM)heapResult);
        delete params;
        return 0;
    }

    static void setBusy(bool busy) {
        g_building = busy;
        if (g_resetButton) EnableWindow(g_resetButton, busy ? FALSE : TRUE);
        if (g_titleEdit) EnableWindow(g_titleEdit, busy ? FALSE : TRUE);
        if (g_publisherEdit) EnableWindow(g_publisherEdit, busy ? FALSE : TRUE);
        if (g_versionEdit) EnableWindow(g_versionEdit, busy ? FALSE : TRUE);
        if (g_iconEdit) EnableWindow(g_iconEdit, busy ? FALSE : TRUE);
        if (g_wallpaperEdit) EnableWindow(g_wallpaperEdit, busy ? FALSE : TRUE);
        if (g_cartEdit) EnableWindow(g_cartEdit, busy ? FALSE : TRUE);
        if (g_browseIcon) EnableWindow(g_browseIcon, busy ? FALSE : TRUE);
        if (g_browseWallpaper) EnableWindow(g_browseWallpaper, busy ? FALSE : TRUE);
        if (g_browseButton) EnableWindow(g_browseButton, busy ? FALSE : TRUE);
        if (g_toggleStretch) EnableWindow(g_toggleStretch, busy ? FALSE : TRUE);
        if (g_toggleCrt) EnableWindow(g_toggleCrt, busy ? FALSE : TRUE);
        if (g_toggleInterpol8) EnableWindow(g_toggleInterpol8, busy ? FALSE : TRUE);
        updateGenerateEnabled();
    }

    static bool isCheckboxChecked(HWND checkbox) {
        return SendMessageA(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    static void handleReset() {
        if (g_titleEdit) setPlaceholder(g_titleEdit, kTitlePlaceholder, g_titlePlaceholderActive);
        if (g_publisherEdit) setPlaceholder(g_publisherEdit, kPublisherPlaceholder, g_publisherPlaceholderActive);
        if (g_versionEdit) setPlaceholder(g_versionEdit, kVersionPlaceholder, g_versionPlaceholderActive);
        if (g_iconEdit) SetWindowTextA(g_iconEdit, "");
        if (g_wallpaperEdit) SetWindowTextA(g_wallpaperEdit, "");
        if (g_cartEdit) SetWindowTextA(g_cartEdit, "");
        if (g_toggleStretch) SendMessageA(g_toggleStretch, BM_SETCHECK, BST_UNCHECKED, 0);
        if (g_toggleCrt) SendMessageA(g_toggleCrt, BM_SETCHECK, BST_UNCHECKED, 0);
        if (g_toggleInterpol8) SendMessageA(g_toggleInterpol8, BM_SETCHECK, BST_UNCHECKED, 0);
        g_cartPath.clear();
        g_iconPath.clear();
        g_wallpaperPath.clear();
        updateGenerateEnabled();
    }

    static void handleBrowseIcon(HWND hwnd) {
        std::string chosen;
        if (!browseForIcon(hwnd, chosen)) {
            return;
        }
        if (!endsWithIgnoreCase(chosen, ".jpg")) {
            showMessage("Please select a .jpg file for the icon.", MB_ICONWARNING | MB_OK);
            return;
        }
        g_iconPath = chosen;
        if (g_iconEdit) SetWindowTextA(g_iconEdit, g_iconPath.c_str());
        updateGenerateEnabled();
    }

    static void handleBrowseWallpaper(HWND hwnd) {
        std::string chosen;
        if (!browseForWallpaper(hwnd, chosen)) {
            return;
        }
        if (!endsWithIgnoreCase(chosen, ".png")) {
            showMessage("Please select a .png file for the background.", MB_ICONWARNING | MB_OK);
            return;
        }
        g_wallpaperPath = chosen;
        if (g_wallpaperEdit) SetWindowTextA(g_wallpaperEdit, g_wallpaperPath.c_str());
        updateGenerateEnabled();
    }

    static void handleBrowse(HWND hwnd) {
        std::string chosen;
        if (!browseForCart(hwnd, chosen)) {
            return;
        }

        g_cartPath = chosen;
        if (g_cartEdit) SetWindowTextA(g_cartEdit, g_cartPath.c_str());

        char titleBuf[256] = "";
        if (g_titleEdit) GetWindowTextA(g_titleEdit, titleBuf, sizeof(titleBuf));
        std::string title = trimWhitespace(titleBuf);
        if (g_titlePlaceholderActive) title.clear();
        if (title.empty()) {
            title = cartBaseName(g_cartPath);
            if (g_titleEdit) SetWindowTextA(g_titleEdit, title.c_str());
            g_titlePlaceholderActive = false;
            if (g_titleEdit) InvalidateRect(g_titleEdit, nullptr, TRUE);
        }
        updateGenerateEnabled();
    }

    static void handleGenerate(HWND hwnd) {
        if (g_building) {
            showMessage("Build already in progress.", MB_ICONWARNING | MB_OK);
            return;
        }

        char titleBuf[256] = "";
        char publisherBuf[256] = "";
        char versionBuf[256] = "";
        if (g_titleEdit) GetWindowTextA(g_titleEdit, titleBuf, sizeof(titleBuf));
        if (g_publisherEdit) GetWindowTextA(g_publisherEdit, publisherBuf, sizeof(publisherBuf));
        if (g_versionEdit) GetWindowTextA(g_versionEdit, versionBuf, sizeof(versionBuf));

        std::string title = trimWhitespace(titleBuf);
        std::string publisher = trimWhitespace(publisherBuf);
        std::string version = trimWhitespace(versionBuf);
        if (g_titlePlaceholderActive) title.clear();
        if (g_publisherPlaceholderActive) publisher.clear();
        if (g_versionPlaceholderActive) version.clear();

        if (g_cartPath.empty()) {
            std::string chosen;
            if (!browseForCart(hwnd, chosen)) {
                return;
            }
            g_cartPath = chosen;
            if (g_cartEdit) SetWindowTextA(g_cartEdit, g_cartPath.c_str());
        }

        if (title.empty() && !g_cartPath.empty()) {
            title = cartBaseName(g_cartPath);
            if (g_titleEdit) SetWindowTextA(g_titleEdit, title.c_str());
            g_titlePlaceholderActive = false;
            if (g_titleEdit) InvalidateRect(g_titleEdit, nullptr, TRUE);
        }

        if (title.empty()) {
            showMessage("Please enter a game title.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (publisher.empty()) {
            showMessage("Please enter a publisher.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (g_iconPath.empty()) {
            showMessage("Please select a game icon (.jpg).", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!fileExists(g_iconPath.c_str())) {
            showMessage("Selected game icon file was not found.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!g_wallpaperPath.empty() && !fileExists(g_wallpaperPath.c_str())) {
            showMessage("Selected background file was not found.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!g_wallpaperPath.empty() && !endsWithIgnoreCase(g_wallpaperPath, ".png")) {
            showMessage("Background must be a .png file.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (g_cartPath.empty() || !fileExists(g_cartPath.c_str())) {
            showMessage("Please select a cart file.", MB_ICONWARNING | MB_OK);
            return;
        }

        BuildParams* params = new BuildParams();
        params->hwnd = hwnd;
        params->cartPath = g_cartPath;
        params->iconPath = g_iconPath;
        params->wallpaperPath = g_wallpaperPath;
        params->title = title;
        params->publisher = publisher;
        params->version = version;
        params->stretch = isCheckboxChecked(g_toggleStretch);
        params->crt = isCheckboxChecked(g_toggleCrt);
        params->interpol8 = isCheckboxChecked(g_toggleInterpol8);

        setBusy(true);

        uintptr_t thread = _beginthreadex(nullptr, 0, buildThreadProc, params, 0, nullptr);
        if (thread == 0) {
            setBusy(false);
            showMessage("Failed to start build thread.", MB_ICONERROR | MB_OK);
            delete params;
            return;
        }
        CloseHandle(reinterpret_cast<HANDLE>(thread));
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE: {
            RECT rect{};
            GetClientRect(hwnd, &rect);

            int contentW = rect.right - (kPadding * 2);
            int rightColW = 180;
            int leftColW = contentW - rightColW - kRowGap;
            if (leftColW < 200) leftColW = 200;
            int rightColX = kPadding + leftColW + kRowGap;

            int y = kPadding;

            CreateWindowExA(0, "STATIC", "Game Title", WS_CHILD | WS_VISIBLE,
                            kPadding, y, leftColW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;
            g_titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          kPadding, y, leftColW, kEditHeight, hwnd, (HMENU)kIdTitleEdit,
                                          GetModuleHandleW(nullptr), nullptr);
            if (g_titleEdit) {
                setPlaceholder(g_titleEdit, kTitlePlaceholder, g_titlePlaceholderActive);
            }
            y += kEditHeight + kRowGap;

            CreateWindowExA(0, "STATIC", "Publisher", WS_CHILD | WS_VISIBLE,
                            kPadding, y, leftColW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;
            g_publisherEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                              kPadding, y, leftColW, kEditHeight, hwnd, (HMENU)kIdPublisherEdit,
                                              GetModuleHandleW(nullptr), nullptr);
            if (g_publisherEdit) {
                setPlaceholder(g_publisherEdit, kPublisherPlaceholder, g_publisherPlaceholderActive);
            }
            y += kEditHeight + kRowGap;

            CreateWindowExA(0, "STATIC", "Game version", WS_CHILD | WS_VISIBLE,
                            kPadding, y, contentW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;
            g_versionEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                            kPadding, y, contentW, kEditHeight, hwnd, (HMENU)kIdVersionEdit,
                                            GetModuleHandleA(nullptr), nullptr);
            if (g_versionEdit) {
                setPlaceholder(g_versionEdit, kVersionPlaceholder, g_versionPlaceholderActive);
            }
            y += kEditHeight + kRowGap;

            CreateWindowExA(0, "STATIC", "Game Icon 256x256 JPG", WS_CHILD | WS_VISIBLE,
                            kPadding, y, contentW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;

            const int browseW = 90;
            const int browseGap = 8;
            int fileEditW = contentW - browseW - browseGap;
            if (fileEditW < 120) fileEditW = contentW - browseW - browseGap;
            if (fileEditW < 80) fileEditW = contentW;

            g_iconEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                         kPadding, y, fileEditW, kEditHeight, hwnd, (HMENU)kIdIconEdit,
                                         GetModuleHandleA(nullptr), nullptr);
            g_browseIcon = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                           kPadding + fileEditW + browseGap, y, browseW, kEditHeight,
                                           hwnd, (HMENU)kIdBrowseIcon, GetModuleHandleA(nullptr), nullptr);
            y += kEditHeight + kRowGap;

            CreateWindowExA(0, "STATIC", "Game background (optional)", WS_CHILD | WS_VISIBLE,
                            kPadding, y, contentW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;

            g_wallpaperEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                              kPadding, y, fileEditW, kEditHeight, hwnd, (HMENU)kIdWallpaperEdit,
                                              GetModuleHandleA(nullptr), nullptr);
            g_browseWallpaper = CreateWindowExA(0, "BUTTON", "Browse...",
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                kPadding + fileEditW + browseGap, y, browseW, kEditHeight,
                                                hwnd, (HMENU)kIdBrowseWallpaper, GetModuleHandleA(nullptr), nullptr);
            y += kEditHeight + kRowGap;

            CreateWindowExA(0, "STATIC", "PICO-8 Cart file", WS_CHILD | WS_VISIBLE,
                            kPadding, y, contentW, kLabelHeight, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += kLabelHeight + 4;

            int cartEditW = fileEditW;
            g_cartEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                         kPadding, y, cartEditW, kEditHeight, hwnd, (HMENU)kIdCartEdit,
                                         GetModuleHandleA(nullptr), nullptr);
            g_browseButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             kPadding + cartEditW + browseGap, y, browseW, kEditHeight,
                                             hwnd, (HMENU)kIdBrowseCart, GetModuleHandleA(nullptr), nullptr);
            y += kEditHeight + kRowGap;

            int logoW = 0;
            int logoH = 0;
            char logoPath[MAX_PATH] = "";
            if (findLogoPath(logoPath, sizeof(logoPath))) {
                std::string logoErr;
                g_logoBitmap = loadPngBitmap(logoPath, rightColW, 120, getWindowBackgroundColor(),
                                             &logoW, &logoH, logoErr);
            }

            if (logoW < 1) logoW = rightColW;
            if (logoH < 1) logoH = 120;

            int logoX = rightColX + (rightColW - logoW) / 2;
            g_logoImage = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_BITMAP,
                                          logoX, kPadding, logoW, logoH,
                                          hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (g_logoImage && g_logoBitmap) {
                SendMessageA(g_logoImage, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_logoBitmap);
            }

            int leftBottom = y;
            int rightBottom = kPadding + logoH;
            y = leftBottom > rightBottom ? leftBottom : rightBottom;
            y += kRowGap;

            const int checkboxGap = 12;
            int checkboxW = (contentW - (checkboxGap * 2)) / 3;
            int checkboxX1 = kPadding;
            int checkboxX2 = checkboxX1 + checkboxW + checkboxGap;
            int checkboxX3 = checkboxX2 + checkboxW + checkboxGap;

            g_toggleStretch = CreateWindowExA(0, "BUTTON", "Stretch game area",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                              checkboxX1, y, checkboxW, kCheckboxHeight,
                                              hwnd, (HMENU)kIdToggleStretch, GetModuleHandleA(nullptr), nullptr);
            g_toggleCrt = CreateWindowExA(0, "BUTTON", "CRT scanline filter",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          checkboxX2, y, checkboxW, kCheckboxHeight,
                                          hwnd, (HMENU)kIdToggleCrt, GetModuleHandleA(nullptr), nullptr);
            g_toggleInterpol8 = CreateWindowExA(0, "BUTTON", "Interpolation filter",
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                checkboxX3, y, checkboxW, kCheckboxHeight,
                                                hwnd, (HMENU)kIdToggleInterpol8, GetModuleHandleA(nullptr), nullptr);
            y += kCheckboxHeight + kRowGap;

            g_resetButton = CreateWindowExA(0, "BUTTON", "Reset",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                            kPadding, y, kButtonWidth, kButtonHeight,
                                            hwnd, (HMENU)kIdReset, GetModuleHandleA(nullptr), nullptr);
            g_generateButton = CreateWindowExA(0, "BUTTON", "Generate",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                               kPadding + kButtonWidth + 10, y, kButtonWidth, kButtonHeight,
                                               hwnd, (HMENU)kIdGenerate, GetModuleHandleA(nullptr), nullptr);
            updateGenerateEnabled();
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == kIdReset) {
                handleReset();
            } else if (id == kIdBrowseIcon) {
                handleBrowseIcon(hwnd);
            } else if (id == kIdBrowseWallpaper) {
                handleBrowseWallpaper(hwnd);
            } else if (id == kIdBrowseCart) {
                handleBrowse(hwnd);
            } else if (id == kIdGenerate) {
                handleGenerate(hwnd);
            } else if (id == kIdTitleEdit) {
                if (code == EN_SETFOCUS && g_titlePlaceholderActive) {
                    clearPlaceholder(g_titleEdit, g_titlePlaceholderActive);
                } else if (code == EN_KILLFOCUS) {
                    ensurePlaceholder(g_titleEdit, kTitlePlaceholder, g_titlePlaceholderActive);
                }
                if (code == EN_CHANGE || code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    updateGenerateEnabled();
                }
            } else if (id == kIdPublisherEdit) {
                if (code == EN_SETFOCUS && g_publisherPlaceholderActive) {
                    clearPlaceholder(g_publisherEdit, g_publisherPlaceholderActive);
                } else if (code == EN_KILLFOCUS) {
                    ensurePlaceholder(g_publisherEdit, kPublisherPlaceholder, g_publisherPlaceholderActive);
                }
                if (code == EN_CHANGE || code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    updateGenerateEnabled();
                }
            } else if (id == kIdVersionEdit) {
                if (code == EN_SETFOCUS && g_versionPlaceholderActive) {
                    clearPlaceholder(g_versionEdit, g_versionPlaceholderActive);
                } else if (code == EN_KILLFOCUS) {
                    ensurePlaceholder(g_versionEdit, kVersionPlaceholder, g_versionPlaceholderActive);
                }
                if (code == EN_CHANGE || code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    updateGenerateEnabled();
                }
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            HWND ctl = (HWND)lParam;
            if ((ctl == g_titleEdit && g_titlePlaceholderActive) ||
                (ctl == g_publisherEdit && g_publisherPlaceholderActive) ||
                (ctl == g_versionEdit && g_versionPlaceholderActive)) {
                SetTextColor(hdc, RGB(0x88, 0x88, 0x88));
            } else {
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            }
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case kMsgBuildDone: {
            BuildResult* result = reinterpret_cast<BuildResult*>(lParam);
            setBusy(false);
            if (result) {
                showMessage(result->message.c_str(), wParam ? (MB_ICONINFORMATION | MB_OK) : (MB_ICONERROR | MB_OK));
                delete result;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            if (g_logoBitmap) {
                DeleteObject(g_logoBitmap);
                g_logoBitmap = nullptr;
            }
            if (g_appIcon) {
                DestroyIcon(g_appIcon);
                g_appIcon = nullptr;
            }
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        return 0;
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmdShow) {
    const char* className = "Real8SwitchToolsWindow";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    g_windowBrush = CreateSolidBrush(g_windowBgColor);
    wc.hbrBackground = g_windowBrush ? g_windowBrush : (HBRUSH)(COLOR_WINDOW + 1);
    char iconPath[MAX_PATH] = "";
    std::string iconErr;
    if (findIconPath(iconPath, sizeof(iconPath))) {
        g_appIcon = loadPngIcon(iconPath, 256, iconErr);
        if (g_appIcon) {
            wc.hIcon = g_appIcon;
            wc.hIconSm = g_appIcon;
        }
    }

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        WS_EX_CONTROLPARENT,
        className,
        "Pico2Switch v1.0 by @natureglass",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        540,
        460,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageA(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_windowBrush) {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }

    return 0;
}
