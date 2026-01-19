#include <windows.h>
#include <commdlg.h>
#include <process.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <lodePNG.h>

#include "cart_blob.h"
#include "../../../hal/real8_host.h"
#include "../../../core/real8_cart.h"

namespace {
    const int kPadding = 12;
    const int kButtonWidth = 110;
    const int kButtonHeight = 26;
    const int kRowGap = 12;

    const int kIdBrowseIcon = 1001;
    const int kIdBrowseBanner = 1002;
    const int kIdBrowseAudio = 1003;
    const int kIdBrowseCart = 1004;
    const int kIdReset = 1005;
    const int kIdGenerate = 1006;
    const int kIdSpinner = 1007;
    const int kIdBrowseWallpaper = 1008;
    const int kIdTitleEdit = 1009;
    const int kIdTitleIdEdit = 1010;
    const int kIdPublisherEdit = 1011;
    const int kIdToggleStretched = 1012;
    const int kIdToggleCrtFilter = 1013;
    const int kIdToggleInterpol8 = 1014;
    const int kIdToggleTopNoBack = 1015;
    const int kIdToggleBottomNoBack = 1016;
    const int kIdToggleSkipVblank = 1017;

    const UINT kIdSpinnerTimer = 2001;
    const UINT kMsgBuildDone = WM_APP + 1;

    const int kTemplate3dsxResourceId = 301;
    const int kTemplateElfResourceId = 302;

    HWND g_iconEdit = nullptr;
    HWND g_bannerEdit = nullptr;
    HWND g_audioEdit = nullptr;
    HWND g_wallpaperEdit = nullptr;
    HWND g_cartEdit = nullptr;
    HWND g_titleEdit = nullptr;
    HWND g_titleIdEdit = nullptr;
    HWND g_publisherEdit = nullptr;
    HWND g_toggleStretched = nullptr;
    HWND g_toggleCrtFilter = nullptr;
    HWND g_toggleInterpol8 = nullptr;
    HWND g_toggleTopNoBack = nullptr;
    HWND g_toggleBottomNoBack = nullptr;
    HWND g_toggleSkipVblank = nullptr;
    HWND g_bannerImage = nullptr;
    HWND g_browseIconButton = nullptr;
    HWND g_browseBannerButton = nullptr;
    HWND g_browseAudioButton = nullptr;
    HWND g_browseWallpaperButton = nullptr;
    HWND g_browseCartButton = nullptr;
    HWND g_resetButton = nullptr;
    HWND g_generateButton = nullptr;
    HWND g_spinner = nullptr;
    HBITMAP g_bannerBitmap = nullptr;
    HBRUSH g_windowBrush = nullptr;

    char g_iconPath[MAX_PATH] = "";
    char g_bannerPath[MAX_PATH] = "";
    char g_audioPath[MAX_PATH] = "";
    char g_wallpaperPath[MAX_PATH] = "";
    char g_cartPath[MAX_PATH] = "";
    char g_titleText[128] = "";
    char g_titleIdText[32] = "";
    char g_publisherText[128] = "";

    bool g_building = false;
    HANDLE g_buildThread = nullptr;

    struct StartupFlags {
        int stretched;
        int crtFilter;
        int interpol8;
        int topNoBack;
        int bottomNoBack;
        int skipVblank;
    };

    const StartupFlags kDefaultStartupFlags = { 0, 0, 0, 0, 0, 0 };

    struct BuildParams {
        HWND hwnd;
        std::string iconPath;
        std::string bannerPath;
        std::string audioPath;
        std::string wallpaperPath;
        std::string cartPath;
        std::string title;
        std::string productCode;
        std::string publisher;
        StartupFlags flags;
    };

    struct BuildResult {
        bool success;
        std::string message;
    };

    class PackerHost : public IReal8Host {
    public:
        const char* getPlatform() const override { return "PicoTo3DS"; }

        void setNetworkActive(bool active) override { (void)active; }
        void setWifiCredentials(const char* ssid, const char* pass) override { (void)ssid; (void)pass; }
        void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t* palette_map) override {
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
        bool saveState(const char* filename, const uint8_t* data, size_t size) override { (void)filename; (void)data; (void)size; return false; }
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
        MessageBoxA(nullptr, text, "REAL8 3DS Tools", flags);
    }

    static bool endsWithIgnoreCase(const char* str, const char* suffix) {
        if (!str || !suffix) return false;
        size_t len = strlen(str);
        size_t sufLen = strlen(suffix);
        if (sufLen > len) return false;
        const char* a = str + (len - sufLen);
        for (size_t i = 0; i < sufLen; ++i) {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)suffix[i])) {
                return false;
            }
        }
        return true;
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

    static bool buildOutputBase(const char* input, char* out, size_t outSize) {
        if (!input || !*input || !out || outSize == 0) return false;

        const char* lastSlash = strrchr(input, '\\');
        const char* lastFwd = strrchr(input, '/');
        const char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;

        size_t dirLen = sep ? (size_t)(sep - input + 1) : 0;
        const char* name = sep ? sep + 1 : input;
        size_t nameLen = strlen(name);

        size_t baseLen = nameLen;
        if (endsWithIgnoreCase(name, ".p8.png")) {
            baseLen = nameLen - 7;
        } else if (endsWithIgnoreCase(name, ".png")) {
            baseLen = nameLen - 4;
        } else {
            const char* dot = strrchr(name, '.');
            if (dot) baseLen = (size_t)(dot - name);
        }

        if (dirLen + baseLen + 1 > outSize) return false;
        if (dirLen > 0) memcpy(out, input, dirLen);
        memcpy(out + dirLen, name, baseLen);
        out[dirLen + baseLen] = '\0';
        return true;
    }

    static bool buildOutputPath(const char* base, const char* suffix, char* out, size_t outSize) {
        if (!base || !suffix || !out || outSize == 0) return false;
        size_t baseLen = strlen(base);
        size_t sufLen = strlen(suffix);
        if (baseLen + sufLen + 1 > outSize) return false;
        memcpy(out, base, baseLen);
        memcpy(out + baseLen, suffix, sufLen);
        out[baseLen + sufLen] = '\0';
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

    static bool buildDefaultTemplate3dsxPath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;
        return buildPath(exeDir, "REAL8_template.3dsx", out, outSize);
    }

    static bool buildDefaultTemplateElfPath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;
        return buildPath(exeDir, "REAL8_template.elf", out, outSize);
    }

    static bool buildDefaultRomfsAssetPath(const char* filename, char* out, size_t outSize) {
        if (!filename || !*filename) return false;
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 3; ++i) {
            char romfsDir[MAX_PATH] = "";
            if (buildPath(currentDir, "romfs", romfsDir, sizeof(romfsDir))) {
                if (buildPath(romfsDir, filename, out, outSize) && fileExists(out)) return true;
            }
            if (buildPath(currentDir, filename, out, outSize) && fileExists(out)) return true;
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }
        return false;
    }

    static bool buildDefaultBannerImagePath(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 3; ++i) {
            char candidate[MAX_PATH] = "";
            if (buildPath(currentDir, "banner\\REAL8-banner.png", candidate, sizeof(candidate)) && fileExists(candidate)) {
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

    static bool copyFileBytes(const char* from, const char* to, std::string& err) {
        std::vector<uint8_t> data;
        if (!readFileBytes(from, data, err)) return false;
        return writeFileBytes(to, data, err);
    }

    static bool createTempDir(char* out, size_t outSize, std::string& err) {
        if (!out || outSize == 0) return false;
        char base[MAX_PATH] = "";
        DWORD len = GetTempPathA(MAX_PATH, base);
        if (len == 0 || len >= MAX_PATH) {
            err = "Failed to get temporary folder path.";
            return false;
        }

        for (int i = 0; i < 32; ++i) {
            char candidate[MAX_PATH] = "";
            snprintf(candidate, sizeof(candidate), "%sREAL8_romfs_%lu_%d", base, (unsigned long)GetTickCount(), i);
            if (CreateDirectoryA(candidate, nullptr)) {
                if (strlen(candidate) + 1 > outSize) {
                    err = "Temporary folder path is too long.";
                    RemoveDirectoryA(candidate);
                    return false;
                }
                strcpy(out, candidate);
                return true;
            }
        }

        err = "Failed to create temporary folder.";
        return false;
    }

    static void cleanupRomfsTemp(const std::string& romfsPath, const std::string& tempDir) {
        if (!romfsPath.empty()) {
            DeleteFileA(romfsPath.c_str());
        }
        if (!tempDir.empty()) {
            char tempFile[MAX_PATH] = "";
            char romfsDir[MAX_PATH] = "";
            if (buildPath(tempDir.c_str(), "romfs", romfsDir, sizeof(romfsDir))) {
                if (buildPath(romfsDir, "wallpaper.png", tempFile, sizeof(tempFile))) {
                    DeleteFileA(tempFile);
                }
                if (buildPath(romfsDir, "gamesrepo.txt", tempFile, sizeof(tempFile))) {
                    DeleteFileA(tempFile);
                }
                if (buildPath(romfsDir, "config.dat", tempFile, sizeof(tempFile))) {
                    DeleteFileA(tempFile);
                }
                RemoveDirectoryA(romfsDir);
            }
            if (buildPath(tempDir.c_str(), "wallpaper.png", tempFile, sizeof(tempFile))) {
                DeleteFileA(tempFile);
            }
            if (buildPath(tempDir.c_str(), "gamesrepo.txt", tempFile, sizeof(tempFile))) {
                DeleteFileA(tempFile);
            }
            if (buildPath(tempDir.c_str(), "config.dat", tempFile, sizeof(tempFile))) {
                DeleteFileA(tempFile);
            }
            RemoveDirectoryA(tempDir.c_str());
        }
    }

    static bool hasEmbeddedTemplateResource(int resourceId) {
        HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
        return r != nullptr;
    }

    static bool loadEmbeddedTemplateResource(int resourceId, std::vector<uint8_t>& out, const char* label, std::string& err) {
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

    static bool hasEmbeddedTemplate3dsx() {
        return hasEmbeddedTemplateResource(kTemplate3dsxResourceId);
    }

    static bool hasEmbeddedTemplateElf() {
        return hasEmbeddedTemplateResource(kTemplateElfResourceId);
    }

    static bool loadEmbeddedTemplate3dsx(std::vector<uint8_t>& out, std::string& err) {
        return loadEmbeddedTemplateResource(kTemplate3dsxResourceId, out, "template 3DSX", err);
    }

    static bool loadEmbeddedTemplateElf(std::vector<uint8_t>& out, std::string& err) {
        return loadEmbeddedTemplateResource(kTemplateElfResourceId, out, "template ELF", err);
    }

    static bool buildCartBlobFromPng(const char* cartPath, std::vector<uint8_t>& outBlob, std::string& err) {
        std::vector<uint8_t> pngBytes;
        if (!readFileBytes(cartPath, pngBytes, err)) return false;

        PackerHost host;
        GameData game;
        if (!Real8CartLoader::LoadFromBuffer(&host, pngBytes, game)) {
            err = std::string("Failed to decode cart: ") + cartPath;
            return false;
        }

        std::vector<uint8_t> payload;
        payload.reserve(0x4300 + game.lua_code.size());
        payload.insert(payload.end(), game.gfx, game.gfx + sizeof(game.gfx));
        payload.insert(payload.end(), game.map, game.map + sizeof(game.map));
        payload.insert(payload.end(), game.sprite_flags, game.sprite_flags + sizeof(game.sprite_flags));
        payload.insert(payload.end(), game.music, game.music + sizeof(game.music));
        payload.insert(payload.end(), game.sfx, game.sfx + sizeof(game.sfx));
        payload.insert(payload.end(), game.lua_code.begin(), game.lua_code.end());

        CartBlobHeader header{};
        memcpy(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE);
        header.flags = CART_BLOB_FLAG_NONE;
        header.raw_size = (uint32_t)payload.size();
        header.comp_size = (uint32_t)payload.size();

        outBlob.resize(sizeof(CartBlobHeader) + payload.size());
        memcpy(outBlob.data(), &header, sizeof(CartBlobHeader));
        memcpy(outBlob.data() + sizeof(CartBlobHeader), payload.data(), payload.size());
        return true;
    }

    static bool findTemplateBlobSlot(const std::vector<uint8_t>& bin, size_t& slotOffset, uint32_t& slotCapacity, std::string& err) {
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
            if (i + hdrSize + (size_t)h.comp_size > bin.size()) continue;

            if (h.raw_size != 0) continue;

            size_t check = h.comp_size < 64 ? h.comp_size : 64;
            bool looksEmpty = true;
            for (size_t j = 0; j < check; ++j) {
                if (bin[i + hdrSize + j] != 0) { looksEmpty = false; break; }
            }
            if (!looksEmpty) continue;

            slotOffset = i;
            slotCapacity = h.comp_size;
            return true;
        }

        err = "Could not find a cart blob slot in the template.\n"
              "Rebuild the template with: make template";
        return false;
    }

    static bool patchTemplate3dsx(const std::vector<uint8_t>& templateBin,
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
        memcpy(&outBin[slotOffset], cartBlob.data(), hdrSize);
        memcpy(&outBin[slotOffset + hdrSize], cartBlob.data() + hdrSize, payloadSize);
        if (slotCapacity > payloadSize) {
            memset(&outBin[slotOffset + hdrSize + payloadSize], 0, (size_t)(slotCapacity - payloadSize));
        }

        return true;
    }

    static const uint8_t* mortonLut64() {
        static uint8_t lut[64];
        static bool inited = false;
        if (!inited) {
            for (uint32_t y = 0; y < 8; ++y) {
                for (uint32_t x = 0; x < 8; ++x) {
                    uint32_t m = 0;
                    for (uint32_t i = 0; i < 3; ++i) {
                        m |= ((x >> i) & 1u) << (2u * i);
                        m |= ((y >> i) & 1u) << (2u * i + 1u);
                    }
                    lut[(y << 3) | x] = (uint8_t)m;
                }
            }
            inited = true;
        }
        return lut;
    }

    static uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    static void swizzleCopyRgb565(const uint16_t* src, int w, int h, uint16_t* dst) {
        const uint8_t* mort = mortonLut64();
        const int tilesX = w / 8;
        for (int ty = 0; ty < h; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = 0; tx < w; tx += 8) {
                const int tileX = tx >> 3;
                uint16_t* dstTile = dst + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const uint16_t* srcRow = src + (ty + y) * w + tx;
                    for (int x = 0; x < 8; ++x) {
                        dstTile[mort[(y << 3) | x]] = srcRow[x];
                    }
                }
            }
        }
    }

    static void downscale2x(const std::vector<uint8_t>& rgba, int w, int h, std::vector<uint8_t>& out) {
        const int outW = w / 2;
        const int outH = h / 2;
        out.resize((size_t)outW * outH * 4);
        for (int y = 0; y < outH; ++y) {
            for (int x = 0; x < outW; ++x) {
                int srcX = x * 2;
                int srcY = y * 2;
                const uint8_t* p0 = &rgba[(srcY * w + srcX) * 4];
                const uint8_t* p1 = &rgba[(srcY * w + srcX + 1) * 4];
                const uint8_t* p2 = &rgba[((srcY + 1) * w + srcX) * 4];
                const uint8_t* p3 = &rgba[((srcY + 1) * w + srcX + 1) * 4];
                uint8_t* dst = &out[(y * outW + x) * 4];
                for (int c = 0; c < 4; ++c) {
                    dst[c] = (uint8_t)((p0[c] + p1[c] + p2[c] + p3[c]) / 4);
                }
            }
        }
    }

    static void writeUtf16Field(uint8_t* dst, size_t dstBytes, const std::string& text) {
        size_t maxChars = dstBytes / 2;
        size_t count = text.size();
        if (count > maxChars) count = maxChars;
        for (size_t i = 0; i < count; ++i) {
            dst[i * 2] = (uint8_t)text[i];
            dst[i * 2 + 1] = 0;
        }
    }

    static bool writeSmdhFromPng(const char* pngPath, const char* outPath, const std::string& title, const std::string& publisher, std::string& err) {
        unsigned char* image = nullptr;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32_file(&image, &w, &h, pngPath);
        if (error || !image) {
            err = std::string("Failed to decode icon PNG: ") + pngPath;
            return false;
        }
        if (w != 48 || h != 48) {
            free(image);
            err = "Icon PNG must be 48x48.";
            return false;
        }

        std::vector<uint8_t> rgba;
        rgba.assign(image, image + (w * h * 4));
        free(image);

        std::vector<uint8_t> rgbaSmall;
        downscale2x(rgba, (int)w, (int)h, rgbaSmall);

        std::vector<uint16_t> iconLarge((size_t)w * h);
        for (size_t i = 0; i < iconLarge.size(); ++i) {
            iconLarge[i] = packRgb565(rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2]);
        }

        const int smallW = 24;
        const int smallH = 24;
        std::vector<uint16_t> iconSmall((size_t)smallW * smallH);
        for (size_t i = 0; i < iconSmall.size(); ++i) {
            iconSmall[i] = packRgb565(rgbaSmall[i * 4 + 0], rgbaSmall[i * 4 + 1], rgbaSmall[i * 4 + 2]);
        }

        std::vector<uint16_t> swizzledLarge(iconLarge.size());
        std::vector<uint16_t> swizzledSmall(iconSmall.size());
        swizzleCopyRgb565(iconLarge.data(), (int)w, (int)h, swizzledLarge.data());
        swizzleCopyRgb565(iconSmall.data(), smallW, smallH, swizzledSmall.data());

        std::vector<uint8_t> smdh(0x36C0, 0);
        memcpy(smdh.data(), "SMDH", 4);
        smdh[4] = 0x02;
        smdh[5] = 0x00;

        const size_t titleBase = 0x8;
        const size_t titleEntrySize = 0x200;
        const size_t shortOffset = 0x0;
        const size_t longOffset = 0x80;
        const size_t pubOffset = 0x180;
        const std::string longTitle = "Generated with REAL-8";
        for (int i = 0; i < 16; ++i) {
            size_t entry = titleBase + (size_t)i * titleEntrySize;
            writeUtf16Field(&smdh[entry + shortOffset], 0x80, title);
            writeUtf16Field(&smdh[entry + longOffset], 0x100, longTitle);
            writeUtf16Field(&smdh[entry + pubOffset], 0x80, publisher);
        }

        const size_t iconSmallOffset = 0x2040;
        const size_t iconLargeOffset = 0x24C0;
        memcpy(&smdh[iconSmallOffset], swizzledSmall.data(), swizzledSmall.size() * sizeof(uint16_t));
        memcpy(&smdh[iconLargeOffset], swizzledLarge.data(), swizzledLarge.size() * sizeof(uint16_t));

        return writeFileBytes(outPath, smdh, err);
    }

    static bool patchSmdhTitles(const char* smdhPath,
                                const std::string& shortTitle,
                                const std::string& longTitle,
                                const std::string& publisher,
                                std::string& err) {
        std::vector<uint8_t> smdh;
        if (!readFileBytes(smdhPath, smdh, err)) return false;
        if (smdh.size() < 0x8 + 0x200 * 16) {
            err = "SMDH file is too small.";
            return false;
        }
        if (memcmp(smdh.data(), "SMDH", 4) != 0) {
            err = "Invalid SMDH header.";
            return false;
        }

        const size_t titleBase = 0x8;
        const size_t titleEntrySize = 0x200;
        const size_t shortOffset = 0x0;
        const size_t longOffset = 0x80;
        const size_t pubOffset = 0x180;
        for (int i = 0; i < 16; ++i) {
            size_t entry = titleBase + (size_t)i * titleEntrySize;
            memset(&smdh[entry + shortOffset], 0, 0x80);
            memset(&smdh[entry + longOffset], 0, 0x100);
            memset(&smdh[entry + pubOffset], 0, 0x80);
            writeUtf16Field(&smdh[entry + shortOffset], 0x80, shortTitle);
            writeUtf16Field(&smdh[entry + longOffset], 0x100, longTitle);
            writeUtf16Field(&smdh[entry + pubOffset], 0x80, publisher);
        }

        return writeFileBytes(smdhPath, smdh, err);
    }

    static const char kRsfTemplate[] = R"(BasicInfo:
  Title                   : $(APP_TITLE)
  CompanyCode             : "00"
  ProductCode             : $(APP_PRODUCT_CODE)
  ContentType             : Application
  Logo                    : Homebrew # Nintendo / Licensed / Distributed / iQue / iQueForSystem

TitleInfo:
  UniqueId                : $(APP_UNIQUE_ID)

  Category                : Application
  
CardInfo:
  MediaSize               : 128MB # 128MB / 256MB / 512MB / 1GB / 2GB / 4GB
  MediaType               : Card1 # Card1 / Card2
  CardDevice              : NorFlash # NorFlash(Pick this if you use savedata) / None
  

Option:
  UseOnSD                : true # true if App is to be installed to SD
  FreeProductCode         : true # Removes limitations on ProductCode
  MediaFootPadding        : false # If true CCI files are created with padding
  EnableCrypt             : false # Enables encryption for NCCH and CIA
  EnableCompress          : true # Compresses exefs code
  
AccessControlInfo:
  #UseExtSaveData : true
  #ExtSaveDataId: 0xff3ff
  #UseExtendedSaveDataAccessControl: true
  #AccessibleSaveDataIds: [0x101, 0x202, 0x303, 0x404, 0x505, 0x606]

SystemControlInfo:
  SaveDataSize: 128KB
  RemasterVersion: 7
  StackSize: 0x40000
  
# DO NOT EDIT BELOW HERE OR PROGRAMS WILL NOT LAUNCH (most likely)

AccessControlInfo:
  FileSystemAccess:
   - Debug
   - DirectSdmc
   - DirectSdmcWrite
   
  IdealProcessor                : 0
  AffinityMask                  : 1
  
  Priority                      : 16
   
  MaxCpu                        : 0x9E # Default
  DisableDebug                  : false
  EnableForceDebug              : false
  CanWriteSharedPage            : false
  CanUsePrivilegedPriority      : false
  CanUseNonAlphabetAndNumber    : false
  PermitMainFunctionArgument    : false
  CanShareDeviceMemory          : false
  RunnableOnSleep               : false
  SpecialMemoryArrange          : false
  CoreVersion                   : 2
  DescVersion                   : 2
  
  ReleaseKernelMajor            : "02"
  ReleaseKernelMinor            : "33" 
  MemoryType                    : Application
  HandleTableSize: 512
  IORegisterMapping: 
   - 1ff50000-1ff57fff
   - 1ff70000-1ff77fff
  MemoryMapping: 
   - 1f000000-1f5fffff:r
  SystemCallAccess: 
    ArbitrateAddress: 34
    Break: 60
    CancelTimer: 28
    ClearEvent: 25
    ClearTimer: 29
    CloseHandle: 35
    ConnectToPort: 45
    ControlMemory: 1
    CreateAddressArbiter: 33
    CreateEvent: 23
    CreateMemoryBlock: 30
    CreateMutex: 19
    CreateSemaphore: 21
    CreateThread: 8
    CreateTimer: 26
    DuplicateHandle: 39
    ExitProcess: 3
    ExitThread: 9
    GetCurrentProcessorNumber: 17
    GetHandleInfo: 41
    GetProcessId: 53
    GetProcessIdOfThread: 54
    GetProcessIdealProcessor: 6
    GetProcessInfo: 43
    GetResourceLimit: 56
    GetResourceLimitCurrentValues: 58
    GetResourceLimitLimitValues: 57
    GetSystemInfo: 42
    GetSystemTick: 40
    GetThreadContext: 59
    GetThreadId: 55
    GetThreadIdealProcessor: 15
    GetThreadInfo: 44
    GetThreadPriority: 11
    MapMemoryBlock: 31
    OutputDebugString: 61
    QueryMemory: 2
    ReleaseMutex: 20
    ReleaseSemaphore: 22
    SendSyncRequest1: 46
    SendSyncRequest2: 47
    SendSyncRequest3: 48
    SendSyncRequest4: 49
    SendSyncRequest: 50
    SetThreadPriority: 12
    SetTimer: 27
    SignalEvent: 24
    SleepThread: 10
    UnmapMemoryBlock: 32
    WaitSynchronization1: 36
    WaitSynchronizationN: 37
  InterruptNumbers:
  ServiceAccessControl: 
   - APT:U
   - $hioFIO
   - $hostio0
   - $hostio1
   - ac:u
   - boss:U
   - cam:u
   - cecd:u
   - cfg:u
   - dlp:FKCL
   - dlp:SRVR
   - dsp::DSP
   - frd:u
   - fs:USER
   - gsp::Gpu
   - hid:USER
   - http:C
   - mic:u
   - ndm:u
   - news:s
   - nwm::UDS
   - ptm:u
   - pxi:dev
   - soc:U
   - gsp::Lcd
   - y2r:u
   - ldr:ro
   - ir:USER
   - ir:u
   - csnd:SND
   - am:u
   - ns:s
   - ptm:sysm
   - mcu::HWC
   
SystemControlInfo:
  Dependency: 
    ac: 0x0004013000002402L
    am: 0x0004013000001502L
    boss: 0x0004013000003402L
    camera: 0x0004013000001602L
    cecd: 0x0004013000002602L
    cfg: 0x0004013000001702L
    codec: 0x0004013000001802L
    csnd: 0x0004013000002702L
    dlp: 0x0004013000002802L
    dsp: 0x0004013000001a02L
    friends: 0x0004013000003202L
    gpio: 0x0004013000001b02L
    gsp: 0x0004013000001c02L
    hid: 0x0004013000001d02L
    http: 0x0004013000002902L
    i2c: 0x0004013000001e02L
    ir: 0x0004013000003302L
    mcu: 0x0004013000001f02L
    mic: 0x0004013000002002L
    ndm: 0x0004013000002b02L
    news: 0x0004013000003502L
    nim: 0x0004013000002c02L
    nwm: 0x0004013000002d02L
    pdn: 0x0004013000002102L
    ps: 0x0004013000003102L
    ptm: 0x0004013000002202L
    ro: 0x0004013000003702L
    socket: 0x0004013000002e02L
    spi: 0x0004013000002302L
    ssl: 0x0004013000002f02L
)";

    static bool writeTextFile(const char* path, const char* text, std::string& err) {
        FILE* f = fopen(path, "wb");
        if (!f) {
            err = std::string("Failed to open for writing: ") + path;
            return false;
        }
        size_t len = text ? strlen(text) : 0;
        if (len && fwrite(text, 1, len, f) != len) {
            fclose(f);
            err = std::string("Failed to write: ") + path;
            return false;
        }
        fclose(f);
        return true;
    }

    static std::string sanitizeTitle(const std::string& title) {
        std::string out;
        out.reserve(title.size());
        bool lastWasSpace = false;
        for (char c : title) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 32 || uc >= 127) continue;
            if (c == '"' || c == '\\') continue;
            if (c == ' ') {
                if (out.empty() || lastWasSpace) continue;
                lastWasSpace = true;
                out.push_back(' ');
            } else {
                lastWasSpace = false;
                out.push_back(c);
            }
            if (out.size() >= 64) break;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.empty()) out = "PicoTo3DS";
        return out;
    }

    static std::string sanitizePublisher(const std::string& publisher) {
        std::string out;
        out.reserve(publisher.size());
        bool lastWasSpace = false;
        for (char c : publisher) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 32 || uc >= 127) continue;
            if (c == '"' || c == '\\') continue;
            if (c == ' ') {
                if (out.empty() || lastWasSpace) continue;
                lastWasSpace = true;
                out.push_back(' ');
            } else {
                lastWasSpace = false;
                out.push_back(c);
            }
            if (out.size() >= 64) break;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.empty()) out = "REAL-8";
        if (out.rfind("By ", 0) != 0) {
            out = "By " + out;
        }
        return out;
    }

    static std::string replaceAll(std::string text, const std::string& needle, const std::string& value) {
        if (needle.empty()) return text;
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            text.replace(pos, needle.size(), value);
            pos += value.size();
        }
        return text;
    }

    static std::string buildRsfText(const std::string& title,
                                    const std::string& productCode,
                                    const std::string& uniqueId) {
        std::string rsf = kRsfTemplate;
        rsf = replaceAll(rsf, "$(APP_TITLE)", "\"" + title + "\"");
        rsf = replaceAll(rsf, "$(APP_PRODUCT_CODE)", "\"" + productCode + "\"");
        rsf = replaceAll(rsf, "$(APP_UNIQUE_ID)", uniqueId);
        return rsf;
    }

    static uint32_t hashTitle(const std::string& title) {
        uint32_t hash = 2166136261u;
        for (unsigned char c : title) {
            hash ^= c;
            hash *= 16777619u;
        }
        return hash;
    }

    static std::string formatUniqueId(const std::string& title) {
        uint32_t hash = hashTitle(title);
        uint32_t uniqueId = 0x50000u | (hash & 0xFFFFu);
        char buf[16] = "";
        snprintf(buf, sizeof(buf), "0x%05X", (unsigned)uniqueId);
        return std::string(buf);
    }

    static bool parseProductCode(const std::string& text, std::string& outProductCode, std::string& err) {
        std::string trimmed = text;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
        size_t start = 0;
        while (start < trimmed.size() && (trimmed[start] == ' ' || trimmed[start] == '\t')) ++start;
        if (start > 0) trimmed = trimmed.substr(start);

        if (trimmed.empty()) {
            outProductCode = "CTR-P-REAL";
            return true;
        }

        auto toUpper = [](char c) -> char {
            return (char)std::toupper((unsigned char)c);
        };

        std::string upper;
        upper.reserve(trimmed.size());
        for (char c : trimmed) {
            upper.push_back(toUpper(c));
        }

        if (upper.size() == 4) {
            for (char c : upper) {
                if (!std::isalnum((unsigned char)c)) {
                    err = "Product code must be 4 alphanumerics or full CTR-P-XXXX.";
                    return false;
                }
            }
            outProductCode = "CTR-P-" + upper;
            return true;
        }

        if (upper.size() == 10 && upper.rfind("CTR-P-", 0) == 0) {
            for (size_t i = 6; i < upper.size(); ++i) {
                if (!std::isalnum((unsigned char)upper[i])) {
                    err = "Product code suffix must be 4 alphanumerics.";
                    return false;
                }
            }
            outProductCode = upper;
            return true;
        }

        err = "Product code must be 4 alphanumerics or full CTR-P-XXXX.";
        return false;
    }

    static void syncTitleFields() {
        if (g_titleEdit) {
            GetWindowTextA(g_titleEdit, g_titleText, sizeof(g_titleText));
        }
        if (g_titleIdEdit) {
            GetWindowTextA(g_titleIdEdit, g_titleIdText, sizeof(g_titleIdText));
        }
        if (g_publisherEdit) {
            GetWindowTextA(g_publisherEdit, g_publisherText, sizeof(g_publisherText));
        }
    }

    static void setCheckboxState(HWND checkbox, int value) {
        if (!checkbox) return;
        SendMessageA(checkbox, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    static int getCheckboxState(HWND checkbox) {
        if (!checkbox) return 0;
        return SendMessageA(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
    }

    static void applyStartupFlags(const StartupFlags& flags) {
        setCheckboxState(g_toggleStretched, flags.stretched);
        setCheckboxState(g_toggleCrtFilter, flags.crtFilter);
        setCheckboxState(g_toggleInterpol8, flags.interpol8);
        setCheckboxState(g_toggleTopNoBack, flags.topNoBack);
        setCheckboxState(g_toggleBottomNoBack, flags.bottomNoBack);
        setCheckboxState(g_toggleSkipVblank, flags.skipVblank);
    }

    static StartupFlags readStartupFlagsFromUi() {
        StartupFlags flags = {};
        flags.stretched = getCheckboxState(g_toggleStretched);
        flags.crtFilter = getCheckboxState(g_toggleCrtFilter);
        flags.interpol8 = getCheckboxState(g_toggleInterpol8);
        flags.topNoBack = getCheckboxState(g_toggleTopNoBack);
        flags.bottomNoBack = getCheckboxState(g_toggleBottomNoBack);
        flags.skipVblank = getCheckboxState(g_toggleSkipVblank);
        return flags;
    }

    static bool isStartupFlagsDefault(const StartupFlags& flags) {
        return flags.stretched == 0 &&
               flags.crtFilter == 0 &&
               flags.interpol8 == 0 &&
               flags.topNoBack == 0 &&
               flags.bottomNoBack == 0 &&
               flags.skipVblank == kDefaultStartupFlags.skipVblank;
    }

    static void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    }

    static std::vector<uint8_t> buildConfigDat(const StartupFlags& flags) {
        std::vector<uint8_t> data;
        data.reserve(1 + 4 + 1 + 4);

        uint8_t flags1 = 0;
        if (!flags.topNoBack) flags1 |= (1 << 1);
        if (flags.crtFilter) flags1 |= (1 << 2);
        if (flags.interpol8) flags1 |= (1 << 4);

        uint8_t flags2 = 0;
        flags2 |= (1 << 0);
        if (flags.bottomNoBack) flags2 |= (1 << 1);
        if (flags.stretched) flags2 |= (1 << 2);
        if (flags.skipVblank) flags2 |= (1 << 3);

        data.push_back(flags1);
        appendU32LE(data, 0);
        data.push_back(flags2);
        appendU32LE(data, 0);

        return data;
    }

    static bool findToolPath(const char* toolName, std::string& outPath) {
        char buffer[MAX_PATH] = "";
        DWORD len = SearchPathA(nullptr, toolName, ".exe", MAX_PATH, buffer, nullptr);
        if (len > 0 && len < MAX_PATH) {
            outPath = buffer;
            return true;
        }

        char exeDir[MAX_PATH] = "";
        if (getExeDir(exeDir, sizeof(exeDir))) {
            std::string exeName = std::string(toolName) + ".exe";
            char candidate[MAX_PATH] = "";
            if (buildPath(exeDir, ("bin\\" + exeName).c_str(), candidate, sizeof(candidate))) {
                if (fileExists(candidate)) {
                    outPath = candidate;
                    return true;
                }
            }
        }

        const char* devkitPro = getenv("DEVKITPRO");
        if (devkitPro) {
            std::string exeName = std::string(toolName) + ".exe";
            char candidate[MAX_PATH] = "";
            if (buildPath(devkitPro, ("tools\\bin\\" + exeName).c_str(), candidate, sizeof(candidate))) {
                if (fileExists(candidate)) {
                    outPath = candidate;
                    return true;
                }
            }
        }

        return false;
    }

    static bool find3dstoolPath(std::string& outPath) {
        if (findToolPath("3dstool", outPath)) return true;

        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 4; ++i) {
            char candidate[MAX_PATH] = "";
            if (buildPath(currentDir, "3dstool\\3dstool.exe", candidate, sizeof(candidate)) && fileExists(candidate)) {
                outPath = candidate;
                return true;
            }
            if (buildPath(currentDir, "3dstool.exe", candidate, sizeof(candidate)) && fileExists(candidate)) {
                outPath = candidate;
                return true;
            }
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }

        return false;
    }

    static bool find3dsxtoolPath(std::string& outPath) {
        if (findToolPath("3dsxtool", outPath)) return true;

        const char* defaultDevkitPro = "C:\\devkitPro";
        char candidate[MAX_PATH] = "";
        if (buildPath(defaultDevkitPro, "tools\\bin\\3dsxtool.exe", candidate, sizeof(candidate)) && fileExists(candidate)) {
            outPath = candidate;
            return true;
        }

        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;

        char currentDir[MAX_PATH] = "";
        snprintf(currentDir, sizeof(currentDir), "%s", exeDir);
        for (int i = 0; i < 4; ++i) {
            if (buildPath(currentDir, "3dsxtool.exe", candidate, sizeof(candidate)) && fileExists(candidate)) {
                outPath = candidate;
                return true;
            }
            if (buildPath(currentDir, "tools\\bin\\3dsxtool.exe", candidate, sizeof(candidate)) && fileExists(candidate)) {
                outPath = candidate;
                return true;
            }
            char parentDir[MAX_PATH] = "";
            if (!getParentDir(currentDir, parentDir, sizeof(parentDir))) break;
            snprintf(currentDir, sizeof(currentDir), "%s", parentDir);
        }

        return false;
    }

    static std::string quoteArg(const std::string& arg) {
        if (arg.empty()) return "\"\"";
        if (arg.find_first_of(" \t\"") == std::string::npos) return arg;
        std::string out = "\"";
        int backslashes = 0;
        for (char c : arg) {
            if (c == '\\') {
                ++backslashes;
            } else if (c == '"') {
                out.append(backslashes * 2 + 1, '\\');
                out.push_back('"');
                backslashes = 0;
            } else {
                out.append(backslashes, '\\');
                backslashes = 0;
                out.push_back(c);
            }
        }
        out.append(backslashes * 2, '\\');
        out.push_back('"');
        return out;
    }

    static std::string formatWin32Error(DWORD code) {
        char buffer[256] = "";
        DWORD len = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer,
            (DWORD)sizeof(buffer),
            nullptr);
        if (len == 0) {
            return "Win32 error " + std::to_string(code);
        }
        while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) {
            buffer[--len] = '\0';
        }
        return std::string(buffer, len);
    }

    static bool runTool(const std::string& toolPath, const std::vector<std::string>& args, std::string& err) {
        std::string cmdLine = quoteArg(toolPath);
        for (const auto& arg : args) {
            cmdLine.push_back(' ');
            cmdLine += quoteArg(arg);
        }

        std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
        cmdBuffer.push_back('\0');

        char workDir[MAX_PATH] = "";
        const char* workDirPtr = nullptr;
        if (getParentDir(toolPath.c_str(), workDir, sizeof(workDir))) {
            workDirPtr = workDir;
        }

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessA(
            toolPath.c_str(),
            cmdBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            workDirPtr,
            &si,
            &pi);
        if (!ok) {
            DWORD code = GetLastError();
            err = std::string("Failed to run tool: ") + toolPath + " (" + formatWin32Error(code) + ")";
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
            DWORD code = GetLastError();
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            err = std::string("Failed to read tool exit code: ") + toolPath + " (" + formatWin32Error(code) + ")";
            return false;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (exitCode != 0) {
            err = std::string("Tool failed: ") + toolPath + " (exit " + std::to_string(exitCode) + ")";
            return false;
        }
        return true;
    }

    static bool validateWallpaperPng(const char* pngPath, std::string& err) {
        unsigned char* image = nullptr;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32_file(&image, &w, &h, pngPath);
        if (error || !image) {
            err = std::string("Failed to decode wallpaper PNG: ") + pngPath;
            return false;
        }
        free(image);
        if (w != 400 || h != 240) {
            err = "Wallpaper PNG must be 400x240.";
            return false;
        }
        return true;
    }

    static COLORREF getWindowBackgroundColor(HWND hwnd) {
        if (!hwnd) {
            return GetSysColor(COLOR_WINDOW);
        }
        HBRUSH brush = (HBRUSH)GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
        if (brush) {
            LOGBRUSH lb{};
            if (GetObject(brush, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID) {
                return lb.lbColor;
            }
        }
        return GetSysColor(COLOR_WINDOW);
    }

    static HBITMAP loadPngBitmap(const char* pngPath, int maxW, int maxH, COLORREF bgColor, int* outW, int* outH, std::string& err) {
        unsigned char* image = nullptr;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32_file(&image, &w, &h, pngPath);
        if (error || !image) {
            err = std::string("Failed to decode banner PNG: ") + pngPath;
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
            err = "Failed to create banner bitmap.";
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

    static uint16_t readU16(const std::vector<uint8_t>& data, size_t offset) {
        uint16_t v = 0;
        if (offset + sizeof(v) <= data.size()) {
            memcpy(&v, data.data() + offset, sizeof(v));
        }
        return v;
    }

    static uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
        uint32_t v = 0;
        if (offset + sizeof(v) <= data.size()) {
            memcpy(&v, data.data() + offset, sizeof(v));
        }
        return v;
    }

    static void writeU16(std::vector<uint8_t>& data, size_t offset, uint16_t v) {
        if (offset + sizeof(v) <= data.size()) {
            memcpy(data.data() + offset, &v, sizeof(v));
        }
    }

    static void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t v) {
        if (offset + sizeof(v) <= data.size()) {
            memcpy(data.data() + offset, &v, sizeof(v));
        }
    }

    static bool appendRomfsTo3dsx(const std::vector<uint8_t>& base3dsx,
                                  const std::vector<uint8_t>& romfs,
                                  std::vector<uint8_t>& out3dsx,
                                  std::string& err) {
        if (base3dsx.size() < 0x20) {
            err = "3DSX file is too small.";
            return false;
        }
        if (memcmp(base3dsx.data(), "3DSX", 4) != 0) {
            err = "Invalid 3DSX header.";
            return false;
        }

        const size_t kBaseHeaderSize = 0x20;
        uint16_t headerSize = readU16(base3dsx, 4);
        if (headerSize < kBaseHeaderSize) {
            err = "Invalid 3DSX header size.";
            return false;
        }

        uint32_t smdhOffset = 0;
        uint32_t smdhSize = 0;
        uint32_t existingRomfsOffset = 0;
        if (headerSize >= 0x2C && base3dsx.size() >= kBaseHeaderSize + 12) {
            smdhOffset = readU32(base3dsx, kBaseHeaderSize + 0);
            smdhSize = readU32(base3dsx, kBaseHeaderSize + 4);
            existingRomfsOffset = readU32(base3dsx, kBaseHeaderSize + 8);
        }

        size_t baseSize = base3dsx.size();
        if (existingRomfsOffset > 0 && existingRomfsOffset < baseSize) {
            baseSize = existingRomfsOffset;
        }

        size_t newHeaderSize = headerSize;
        size_t insertSize = 0;
        if (headerSize < 0x2C) {
            newHeaderSize = 0x2C;
            insertSize = newHeaderSize - headerSize;
        }

        out3dsx.clear();
        out3dsx.reserve(baseSize + insertSize + romfs.size());
        out3dsx.insert(out3dsx.end(), base3dsx.begin(), base3dsx.begin() + headerSize);
        if (insertSize > 0) {
            out3dsx.insert(out3dsx.end(), insertSize, 0);
        }
        out3dsx.insert(out3dsx.end(), base3dsx.begin() + headerSize, base3dsx.begin() + baseSize);

        if (insertSize > 0 && smdhOffset >= headerSize) {
            smdhOffset += (uint32_t)insertSize;
        }

        if (newHeaderSize != headerSize) {
            writeU16(out3dsx, 4, (uint16_t)newHeaderSize);
        }
        writeU32(out3dsx, 8, 1);

        if (out3dsx.size() < kBaseHeaderSize + 12) {
            err = "Failed to expand 3DSX header.";
            return false;
        }

        writeU32(out3dsx, kBaseHeaderSize + 0, smdhOffset);
        writeU32(out3dsx, kBaseHeaderSize + 4, smdhSize);

        size_t romfsOffset = out3dsx.size();
        const size_t kRomfsAlign = 0x1000;
        size_t pad = (kRomfsAlign - (romfsOffset % kRomfsAlign)) % kRomfsAlign;
        if (pad) {
            out3dsx.insert(out3dsx.end(), pad, 0);
            romfsOffset += pad;
        }

        if (romfsOffset > 0xFFFFFFFFu) {
            err = "RomFS offset exceeds 32-bit range.";
            return false;
        }
        writeU32(out3dsx, kBaseHeaderSize + 8, (uint32_t)romfsOffset);

        out3dsx.insert(out3dsx.end(), romfs.begin(), romfs.end());
        return true;
    }

    static bool buildRomfsImage(const char* wallpaperPath,
                                const char* gamesrepoPath,
                                const std::vector<uint8_t>* configDat,
                                std::vector<uint8_t>& outRomfs,
                                std::string& outRomfsPath,
                                std::string& outTempDir,
                                std::string& outRomfsDir,
                                std::string& err) {
        std::string toolPath;
        if (!find3dstoolPath(toolPath)) {
            err = "3dstool not found (needed to build RomFS).";
            return false;
        }

        char tempDir[MAX_PATH] = "";
        if (!createTempDir(tempDir, sizeof(tempDir), err)) return false;
        outTempDir = tempDir;

        char romfsDir[MAX_PATH] = "";
        if (!buildPath(tempDir, "romfs", romfsDir, sizeof(romfsDir))) {
            err = "Failed to build RomFS content path.";
            return false;
        }
        CreateDirectoryA(romfsDir, nullptr);
        outRomfsDir = romfsDir;

        bool copiedAny = false;
        if (wallpaperPath && wallpaperPath[0]) {
            char tempWallpaper[MAX_PATH] = "";
            if (!buildPath(romfsDir, "wallpaper.png", tempWallpaper, sizeof(tempWallpaper))) {
                err = "Failed to build temp wallpaper path.";
                return false;
            }
            if (!copyFileBytes(wallpaperPath, tempWallpaper, err)) return false;
            copiedAny = true;
        }

        if (gamesrepoPath && gamesrepoPath[0] && fileExists(gamesrepoPath)) {
            char tempRepo[MAX_PATH] = "";
            if (!buildPath(romfsDir, "gamesrepo.txt", tempRepo, sizeof(tempRepo))) {
                err = "Failed to build temp gamesrepo path.";
                return false;
            }
            if (!copyFileBytes(gamesrepoPath, tempRepo, err)) return false;
            copiedAny = true;
        }

        if (configDat && !configDat->empty()) {
            char tempConfig[MAX_PATH] = "";
            if (!buildPath(romfsDir, "config.dat", tempConfig, sizeof(tempConfig))) {
                err = "Failed to build temp config path.";
                return false;
            }
            if (!writeFileBytes(tempConfig, *configDat, err)) return false;
            copiedAny = true;
        }

        if (!copiedAny) {
            err = "No RomFS assets available.";
            return false;
        }

        char romfsPath[MAX_PATH] = "";
        if (!buildPath(tempDir, "romfs.bin", romfsPath, sizeof(romfsPath))) {
            err = "Failed to build RomFS output path.";
            return false;
        }

        std::string romfsDirArg = romfsDir;
        if (!romfsDirArg.empty()) {
            char last = romfsDirArg.back();
            if (last != '\\' && last != '/') romfsDirArg += "\\";
        }

        if (!runTool(toolPath, {"-cvtf", "romfs", romfsPath, "--romfs-dir", romfsDirArg}, err)) {
            return false;
        }

        outRomfsPath = romfsPath;
        return readFileBytes(romfsPath, outRomfs, err);
    }

    static bool tryBuildCia(const std::vector<uint8_t>& cartBlob,
                            const char* outputBase,
                            const char* outputSmdh,
                            const char* bannerPath,
                            const char* audioPath,
                            const char* romfsPath,
                            const std::string& title,
                            const std::string& productCode,
                            std::string& outputCia,
                            std::string& warn,
                            std::string& err) {
        if (!bannerPath || !bannerPath[0] || !audioPath || !audioPath[0]) {
            warn = "CIA skipped: banner or audio file missing.";
            return true;
        }

        std::string makeromPath;
        std::string bannertoolPath;
        if (!findToolPath("makerom", makeromPath) || !findToolPath("bannertool", bannertoolPath)) {
            warn = "CIA skipped: makerom/bannertool not found in PATH (install devkitPro).";
            return true;
        }

        std::vector<uint8_t> templateElf;
        if (hasEmbeddedTemplateElf()) {
            if (!loadEmbeddedTemplateElf(templateElf, err)) return false;
        } else {
            char tplElf[MAX_PATH] = "";
            if (!buildDefaultTemplateElfPath(tplElf, sizeof(tplElf))) {
                err = "Failed to locate template ELF path.";
                return false;
            }
            if (!readFileBytes(tplElf, templateElf, err)) return false;
        }

        std::vector<uint8_t> patchedElf;
        if (!patchTemplate3dsx(templateElf, cartBlob, patchedElf, err)) return false;

        char outputElf[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, "-cia.elf", outputElf, sizeof(outputElf))) {
            err = "Failed to build output .elf path.";
            return false;
        }
        if (!writeFileBytes(outputElf, patchedElf, err)) return false;

        char outputBnr[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, "-cia.bnr", outputBnr, sizeof(outputBnr))) {
            err = "Failed to build output .bnr path.";
            return false;
        }
        if (!runTool(bannertoolPath, {"makebanner", "-i", bannerPath, "-a", audioPath, "-o", outputBnr}, err)) {
            return false;
        }

        char outputRsf[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, "-cia.rsf", outputRsf, sizeof(outputRsf))) {
            err = "Failed to build output .rsf path.";
            return false;
        }
        std::string cleanTitle = sanitizeTitle(title);
        std::string uniqueId = formatUniqueId(cleanTitle);
        std::string rsfText = buildRsfText(cleanTitle, productCode, uniqueId);
        if (!writeTextFile(outputRsf, rsfText.c_str(), err)) return false;

        char outputCiaPath[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, ".cia", outputCiaPath, sizeof(outputCiaPath))) {
            err = "Failed to build output .cia path.";
            return false;
        }
        outputCia = outputCiaPath;

        std::vector<std::string> makeromArgs = {
            "-f", "cia",
            "-elf", outputElf
        };
        if (romfsPath && romfsPath[0] && fileExists(romfsPath)) {
            makeromArgs.push_back("-romfs");
            makeromArgs.push_back(romfsPath);
        }
        makeromArgs.insert(makeromArgs.end(), {
            "-icon", outputSmdh,
            "-banner", outputBnr,
            "-desc", "app:4",
            "-v",
            "-o", outputCiaPath,
            "-target", "t",
            "-exefslogo",
            "-rsf", outputRsf
        });

        if (!runTool(makeromPath, makeromArgs, err)) return false;

        DeleteFileA(outputRsf);
        DeleteFileA(outputElf);
        DeleteFileA(outputBnr);
        return true;
    }

    static void updateGenerateEnabled() {
        char tpl[MAX_PATH] = "";
        bool templateOk = hasEmbeddedTemplate3dsx();
        if (!templateOk && buildDefaultTemplate3dsxPath(tpl, sizeof(tpl))) {
            templateOk = fileExists(tpl);
        }
        char tplElf[MAX_PATH] = "";
        bool templateElfOk = hasEmbeddedTemplateElf();
        if (!templateElfOk && buildDefaultTemplateElfPath(tplElf, sizeof(tplElf))) {
            templateElfOk = fileExists(tplElf);
        }

        bool cartOk = g_cartPath[0] && fileExists(g_cartPath);
        bool iconOk = g_iconPath[0] && fileExists(g_iconPath);
        bool bannerOk = g_bannerPath[0] && fileExists(g_bannerPath);
        bool audioOk = g_audioPath[0] && fileExists(g_audioPath);
        bool wallpaperOk = true;
        if (g_wallpaperPath[0]) wallpaperOk = fileExists(g_wallpaperPath);
        syncTitleFields();
        bool titleOk = g_titleText[0] != '\0';
        bool titleIdOk = true;
        if (g_titleIdText[0]) {
            std::string tmp;
            std::string err;
            titleIdOk = parseProductCode(g_titleIdText, tmp, err);
        }
        bool ready = templateOk && templateElfOk && cartOk && iconOk && bannerOk && audioOk && wallpaperOk && titleOk && titleIdOk;
        EnableWindow(g_generateButton, ready && !g_building);
    }

    static void handleBrowseFile(HWND owner, const char* filter, HWND edit, char* dest, size_t destSize, const char* requiredExt) {
        char filePath[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = sizeof(filePath);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (GetOpenFileNameA(&ofn)) {
            if (requiredExt && !endsWithIgnoreCase(filePath, requiredExt)) {
                showMessage("Selected file has the wrong extension.", MB_ICONWARNING | MB_OK);
                return;
            }
            snprintf(dest, destSize, "%s", filePath);
            SetWindowTextA(edit, dest);
            updateGenerateEnabled();
        }
    }

    static void handleReset() {
        g_iconPath[0] = '\0';
        g_bannerPath[0] = '\0';
        g_audioPath[0] = '\0';
        g_wallpaperPath[0] = '\0';
        g_cartPath[0] = '\0';
        g_titleText[0] = '\0';
        g_titleIdText[0] = '\0';
        g_publisherText[0] = '\0';
        SetWindowTextA(g_iconEdit, "");
        SetWindowTextA(g_bannerEdit, "");
        SetWindowTextA(g_audioEdit, "");
        SetWindowTextA(g_wallpaperEdit, "");
        SetWindowTextA(g_cartEdit, "");
        SetWindowTextA(g_titleEdit, "");
        SetWindowTextA(g_titleIdEdit, "");
        SetWindowTextA(g_publisherEdit, "");
        applyStartupFlags(kDefaultStartupFlags);
        updateGenerateEnabled();
    }

    static void startSpinner(HWND hwnd) {
        if (g_spinner) {
            SetWindowTextA(g_spinner, "|");
            ShowWindow(g_spinner, SW_SHOW);
        }
        g_building = true;
        SetTimer(hwnd, kIdSpinnerTimer, 100, nullptr);
    }

    static void stopSpinner(HWND hwnd) {
        KillTimer(hwnd, kIdSpinnerTimer);
        g_building = false;
        if (g_spinner) {
            SetWindowTextA(g_spinner, "");
        }
    }

    static void setBusy(bool busy) {
        EnableWindow(g_browseIconButton, !busy);
        EnableWindow(g_browseBannerButton, !busy);
        EnableWindow(g_browseAudioButton, !busy);
        EnableWindow(g_browseWallpaperButton, !busy);
        EnableWindow(g_browseCartButton, !busy);
        EnableWindow(g_resetButton, !busy);
        if (busy) {
            EnableWindow(g_generateButton, false);
        } else {
            updateGenerateEnabled();
        }
        SetWindowTextA(g_generateButton, busy ? "Generating..." : "Generate");
    }

    static unsigned __stdcall buildThreadProc(void* param) {
        BuildParams* params = static_cast<BuildParams*>(param);
        BuildResult* result = new BuildResult();
        std::string err;
        std::string romfsWarn;
        std::string romfsPath;
        std::string romfsTempDir;
        std::string romfsDir;
        std::vector<uint8_t> romfsImage;

        std::vector<uint8_t> templateBin;
        if (hasEmbeddedTemplate3dsx()) {
            if (!loadEmbeddedTemplate3dsx(templateBin, err)) {
                result->success = false;
                result->message = err;
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
        } else {
            char tpl[MAX_PATH] = "";
            if (!buildDefaultTemplate3dsxPath(tpl, sizeof(tpl))) {
                result->success = false;
                result->message = "Failed to locate template path.";
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
            if (!readFileBytes(tpl, templateBin, err)) {
                result->success = false;
                result->message = err;
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
        }

        std::vector<uint8_t> cartBlob;
        if (!buildCartBlobFromPng(params->cartPath.c_str(), cartBlob, err)) {
            result->success = false;
            result->message = err;
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        std::vector<uint8_t> outBin;
        if (!patchTemplate3dsx(templateBin, cartBlob, outBin, err)) {
            result->success = false;
            result->message = err;
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        char outputBase[MAX_PATH] = "";
        if (!buildOutputBase(params->cartPath.c_str(), outputBase, sizeof(outputBase))) {
            result->success = false;
            result->message = "Failed to build output path.";
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        char output3dsx[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, ".3dsx", output3dsx, sizeof(output3dsx))) {
            result->success = false;
            result->message = "Failed to build output .3dsx path.";
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        std::string title = params->title;

        char outputSmdh[MAX_PATH] = "";
        if (!buildOutputPath(outputBase, ".smdh", outputSmdh, sizeof(outputSmdh))) {
            result->success = false;
            result->message = "Failed to build output .smdh path.";
            cleanupRomfsTemp(romfsPath, romfsTempDir);
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        std::string cleanTitle = sanitizeTitle(title);
        std::string cleanPublisher = sanitizePublisher(params->publisher);
        std::string cleanLongTitle = "Generated with REAL-8";
        std::string bannertoolPath;
        if (findToolPath("bannertool", bannertoolPath)) {
            if (!runTool(bannertoolPath, {"makesmdh", "-s", cleanTitle, "-l", cleanTitle, "-p", cleanPublisher,
                                          "-i", params->iconPath.c_str(), "-o", outputSmdh, "-r", "regionfree"}, err)) {
                result->success = false;
                result->message = err;
                cleanupRomfsTemp(romfsPath, romfsTempDir);
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
            if (!patchSmdhTitles(outputSmdh, cleanTitle, cleanLongTitle, cleanPublisher, err)) {
                result->success = false;
                result->message = err;
                cleanupRomfsTemp(romfsPath, romfsTempDir);
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
        } else if (!writeSmdhFromPng(params->iconPath.c_str(), outputSmdh, cleanTitle, cleanPublisher, err)) {
            result->success = false;
            result->message = err;
            cleanupRomfsTemp(romfsPath, romfsTempDir);
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        const char* wallpaperPath = nullptr;
        char defaultWallpaper[MAX_PATH] = "";
        if (!params->wallpaperPath.empty()) {
            wallpaperPath = params->wallpaperPath.c_str();
        } else if (buildDefaultRomfsAssetPath("wallpaper.png", defaultWallpaper, sizeof(defaultWallpaper))) {
            wallpaperPath = defaultWallpaper;
        }

        const char* gamesrepoPath = nullptr;
        char defaultRepo[MAX_PATH] = "";
        if (buildDefaultRomfsAssetPath("gamesrepo.txt", defaultRepo, sizeof(defaultRepo))) {
            gamesrepoPath = defaultRepo;
        }

        StartupFlags startupFlags = params->flags;
        bool configWanted = !isStartupFlagsDefault(startupFlags);
        std::vector<uint8_t> configDat;
        if (configWanted) {
            configDat = buildConfigDat(startupFlags);
        }

        bool wantRomfs = (wallpaperPath != nullptr) || (gamesrepoPath != nullptr) || configWanted;
        bool romfsReady = false;
        const bool romfsRequired = configWanted || !params->wallpaperPath.empty();
        if (wantRomfs) {
            if (wallpaperPath) {
                if (!validateWallpaperPng(wallpaperPath, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
            }
            const std::vector<uint8_t>* configPtr = configWanted ? &configDat : nullptr;
            if (buildRomfsImage(wallpaperPath, gamesrepoPath, configPtr, romfsImage, romfsPath, romfsTempDir, romfsDir, err)) {
                romfsReady = true;
            } else if (romfsRequired) {
                result->success = false;
                result->message = err;
                cleanupRomfsTemp(romfsPath, romfsTempDir);
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            } else {
                romfsWarn = std::string("RomFS skipped: ") + err;
                err.clear();
            }
        } else if (!params->wallpaperPath.empty()) {
            result->success = false;
            result->message = "Wallpaper PNG not found.";
            cleanupRomfsTemp(romfsPath, romfsTempDir);
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        } else {
            romfsWarn = "RomFS skipped: default assets not found.";
        }

        bool wrote3dsx = false;
        if (romfsReady) {
            std::string tool3dsx;
            if (find3dsxtoolPath(tool3dsx)) {
                std::vector<uint8_t> templateElf;
                if (hasEmbeddedTemplateElf()) {
                    if (!loadEmbeddedTemplateElf(templateElf, err)) {
                        result->success = false;
                        result->message = err;
                        cleanupRomfsTemp(romfsPath, romfsTempDir);
                        PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                        delete params;
                        return 0;
                    }
                } else {
                    char tplElf[MAX_PATH] = "";
                    if (!buildDefaultTemplateElfPath(tplElf, sizeof(tplElf))) {
                        result->success = false;
                        result->message = "Failed to locate template ELF path.";
                        cleanupRomfsTemp(romfsPath, romfsTempDir);
                        PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                        delete params;
                        return 0;
                    }
                    if (!readFileBytes(tplElf, templateElf, err)) {
                        result->success = false;
                        result->message = err;
                        cleanupRomfsTemp(romfsPath, romfsTempDir);
                        PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                        delete params;
                        return 0;
                    }
                }

                std::vector<uint8_t> patchedElf;
                if (!patchTemplate3dsx(templateElf, cartBlob, patchedElf, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }

                char outputElf[MAX_PATH] = "";
                if (!buildOutputPath(outputBase, "-3dsx.elf", outputElf, sizeof(outputElf))) {
                    result->success = false;
                    result->message = "Failed to build output 3DSX ELF path.";
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
                if (!writeFileBytes(outputElf, patchedElf, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }

                std::string smdhArg = std::string("--smdh=") + outputSmdh;
                std::string romfsArg = std::string("--romfs=") + romfsDir;
                if (!runTool(tool3dsx, {outputElf, output3dsx, smdhArg, romfsArg}, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
                DeleteFileA(outputElf);
                wrote3dsx = true;
            } else {
                romfsWarn = "RomFS skipped: 3dsxtool not found for 3DSX build.";
            }
        }

        if (!wrote3dsx) {
            std::vector<uint8_t> output3dsxBin = outBin;
            if (romfsReady) {
                std::vector<uint8_t> merged3dsx;
                if (!appendRomfsTo3dsx(outBin, romfsImage, merged3dsx, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
                output3dsxBin.swap(merged3dsx);
            }

            if (!writeFileBytes(output3dsx, output3dsxBin, err)) {
                result->success = false;
                result->message = err;
                cleanupRomfsTemp(romfsPath, romfsTempDir);
                PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                delete params;
                return 0;
            }
        }

        if (!params->bannerPath.empty()) {
            char outputBanner[MAX_PATH] = "";
            if (buildOutputPath(outputBase, "-banner.png", outputBanner, sizeof(outputBanner))) {
                if (!copyFileBytes(params->bannerPath.c_str(), outputBanner, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
            }
        }

        if (!params->audioPath.empty()) {
            char outputAudio[MAX_PATH] = "";
            if (buildOutputPath(outputBase, "-banner.wav", outputAudio, sizeof(outputAudio))) {
                if (!copyFileBytes(params->audioPath.c_str(), outputAudio, err)) {
                    result->success = false;
                    result->message = err;
                    cleanupRomfsTemp(romfsPath, romfsTempDir);
                    PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
                    delete params;
                    return 0;
                }
            }
        }

        std::string ciaWarn;
        std::string outputCia;
        if (!tryBuildCia(cartBlob, outputBase, outputSmdh,
                         params->bannerPath.c_str(), params->audioPath.c_str(), romfsPath.c_str(),
                         title, params->productCode, outputCia, ciaWarn, err)) {
            result->success = false;
            result->message = err;
            cleanupRomfsTemp(romfsPath, romfsTempDir);
            PostMessageA(params->hwnd, kMsgBuildDone, 0, reinterpret_cast<LPARAM>(result));
            delete params;
            return 0;
        }

        result->success = true;
        result->message = "Generated:\n";
        result->message += output3dsx;
        result->message += "\n";
        result->message += outputSmdh;
        if (!params->bannerPath.empty()) {
            result->message += "\n";
            result->message += outputBase;
            result->message += "-banner.png";
        }
        if (!params->audioPath.empty()) {
            result->message += "\n";
            result->message += outputBase;
            result->message += "-banner.wav";
        }
        if (!outputCia.empty()) {
            result->message += "\n";
            result->message += outputCia;
        }
        if (!ciaWarn.empty()) {
            result->message += "\n";
            result->message += ciaWarn;
        }
        if (!romfsWarn.empty()) {
            result->message += "\n";
            result->message += romfsWarn;
        }

        cleanupRomfsTemp(romfsPath, romfsTempDir);
        PostMessageA(params->hwnd, kMsgBuildDone, 1, reinterpret_cast<LPARAM>(result));
        delete params;
        return 0;
    }

    static void handleGenerate(HWND hwnd) {
        if (!g_cartPath[0] || !fileExists(g_cartPath)) {
            showMessage("Select a .p8.png cart first.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!g_iconPath[0] || !fileExists(g_iconPath)) {
            showMessage("Select a 48x48 icon PNG.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!g_bannerPath[0] || !fileExists(g_bannerPath)) {
            showMessage("Select a 256x128 banner PNG.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!g_audioPath[0] || !fileExists(g_audioPath)) {
            showMessage("Select a short WAV audio file.", MB_ICONWARNING | MB_OK);
            return;
        }
        if (g_wallpaperPath[0] && !fileExists(g_wallpaperPath)) {
            showMessage("Selected wallpaper PNG not found.", MB_ICONWARNING | MB_OK);
            return;
        }
        syncTitleFields();
        if (!g_titleText[0]) {
            showMessage("Enter a game title.", MB_ICONWARNING | MB_OK);
            return;
        }
        std::string productCode;
        std::string productCodeErr;
        if (!parseProductCode(g_titleIdText, productCode, productCodeErr)) {
            showMessage(productCodeErr.c_str(), MB_ICONWARNING | MB_OK);
            return;
        }
        if (_stricmp(g_titleIdText, productCode.c_str()) != 0) {
            snprintf(g_titleIdText, sizeof(g_titleIdText), "%s", productCode.c_str());
            SetWindowTextA(g_titleIdEdit, g_titleIdText);
        }

        bool templateOk = hasEmbeddedTemplate3dsx();
        bool templateElfOk = hasEmbeddedTemplateElf();
        if (!templateOk) {
            char tpl3dsx[MAX_PATH] = "";
            if (!buildDefaultTemplate3dsxPath(tpl3dsx, sizeof(tpl3dsx)) || !fileExists(tpl3dsx)) {
                templateOk = false;
            } else {
                templateOk = true;
            }
        }
        if (!templateElfOk) {
            char tplElf[MAX_PATH] = "";
            if (!buildDefaultTemplateElfPath(tplElf, sizeof(tplElf)) || !fileExists(tplElf)) {
                templateElfOk = false;
            } else {
                templateElfOk = true;
            }
        }
        if (!templateOk || !templateElfOk) {
            showMessage(
                "Template files not found.\n\n"
                "Build them with: make template\n"
                "Then place REAL8_template.3dsx and REAL8_template.elf next to PicoTo3DS.exe.",
                MB_ICONERROR | MB_OK);
            return;
        }

        if (g_building) {
            showMessage("Build already in progress.", MB_ICONWARNING | MB_OK);
            return;
        }

        BuildParams* params = new BuildParams();
        params->hwnd = hwnd;
        params->iconPath = g_iconPath;
        params->bannerPath = g_bannerPath;
        params->audioPath = g_audioPath;
        params->wallpaperPath = g_wallpaperPath;
        params->cartPath = g_cartPath;
        params->title = g_titleText;
        params->productCode = productCode;
        params->publisher = g_publisherText;
        params->flags = readStartupFlagsFromUi();

        setBusy(true);
        startSpinner(hwnd);

        uintptr_t thread = _beginthreadex(nullptr, 0, buildThreadProc, params, 0, nullptr);
        if (thread == 0) {
            stopSpinner(hwnd);
            setBusy(false);
            showMessage("Failed to start build thread.", MB_ICONERROR | MB_OK);
            delete params;
            return;
        }
        g_buildThread = reinterpret_cast<HANDLE>(thread);
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE: {
            RECT rect{};
            GetClientRect(hwnd, &rect);

            int y = kPadding;
            int editWidth = rect.right - (kPadding * 2) - (kButtonWidth + 10);
            int buttonX = rect.right - kPadding - kButtonWidth;
            const int columnGap = kRowGap;
            const int topRowWidth = rect.right - (kPadding * 2) - columnGap;
            const int leftColumnWidth = (topRowWidth * 7) / 10;
            const int rightColumnWidth = topRowWidth - leftColumnWidth;
            const int rightColumnX = kPadding + leftColumnWidth + columnGap;
            const int bannerMaxHeight = (16 + 18 + 24) * 2 + kRowGap;
            int bannerDisplayHeight = bannerMaxHeight;
            int bannerDisplayWidth = rightColumnWidth;
            int bannerX = rightColumnX;

            char bannerPath[MAX_PATH] = "";
            if (buildDefaultBannerImagePath(bannerPath, sizeof(bannerPath))) {
                std::string bannerErr;
                COLORREF bgColor = getWindowBackgroundColor(hwnd);
                g_bannerBitmap = loadPngBitmap(bannerPath, rightColumnWidth, bannerMaxHeight, bgColor,
                                               &bannerDisplayWidth, &bannerDisplayHeight, bannerErr);
                if (g_bannerBitmap && bannerDisplayWidth < rightColumnWidth) {
                    bannerX = rightColumnX + (rightColumnWidth - bannerDisplayWidth) / 2;
                }
            }

            const int labelHeight = 16;
            const int labelToEditGap = 2;
            const int editHeight = 24;
            const int baseTitleHeight = (labelHeight + labelToEditGap + editHeight) * 3;
            int titleRowGap = kRowGap;
            if (bannerDisplayHeight > 0 && bannerDisplayHeight < baseTitleHeight + titleRowGap) {
                titleRowGap = bannerDisplayHeight - baseTitleHeight;
                if (titleRowGap < 0) titleRowGap = 0;
            }

            CreateWindowExA(
                0,
                "STATIC",
                "Game Title (required)",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                y,
                leftColumnWidth,
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);
            y += 18;
            g_titleEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          kPadding, y, leftColumnWidth, 24, hwnd, (HMENU)kIdTitleEdit, GetModuleHandleA(nullptr), nullptr);
            y += 24 + titleRowGap;

            CreateWindowExA(
                0,
                "STATIC",
                "Publisher / Authur (optional)",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                y,
                leftColumnWidth,
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);
            y += 18;
            g_publisherEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                              kPadding, y, leftColumnWidth, 24, hwnd, (HMENU)kIdPublisherEdit, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            CreateWindowExA(
                0,
                "STATIC",
                "Title ID / Product code (optional, 4 chars or CTR-P-XXXX)",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                y,
                leftColumnWidth,
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);
            y += 18;
            g_titleIdEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                            kPadding, y, leftColumnWidth, 24, hwnd, (HMENU)kIdTitleIdEdit, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            g_bannerImage = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_BITMAP,
                                            bannerX, kPadding, bannerDisplayWidth, bannerDisplayHeight,
                                            hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (g_bannerImage) {
                if (g_bannerBitmap) {
                    SendMessageA(g_bannerImage, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_bannerBitmap);
                }
            }

            const int minY = kPadding + bannerDisplayHeight + kRowGap;
            if (y < minY) y = minY;

            CreateWindowExA(0, "STATIC", "Select an Icon 48x48 PNG file", WS_CHILD | WS_VISIBLE,
                            kPadding, y, rect.right - (kPadding * 2), 16, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += 18;
            g_iconEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                         kPadding, y, editWidth, 24, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            g_browseIconButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 buttonX, y, kButtonWidth, 24, hwnd, (HMENU)kIdBrowseIcon, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            CreateWindowExA(0, "STATIC", "Select a Banner 256x128 PNG file", WS_CHILD | WS_VISIBLE,
                            kPadding, y, rect.right - (kPadding * 2), 16, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += 18;
            g_bannerEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                           kPadding, y, editWidth, 24, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            g_browseBannerButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   buttonX, y, kButtonWidth, 24, hwnd, (HMENU)kIdBrowseBanner, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            CreateWindowExA(0, "STATIC", "(Optional) Select game background 400x240 size", WS_CHILD | WS_VISIBLE,
                            kPadding, y, rect.right - (kPadding * 2), 16, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += 18;
            g_wallpaperEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                              kPadding, y, editWidth, 24, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            g_browseWallpaperButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                      buttonX, y, kButtonWidth, 24, hwnd, (HMENU)kIdBrowseWallpaper, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            CreateWindowExA(0, "STATIC", "Select a short WAV Audio file", WS_CHILD | WS_VISIBLE,
                            kPadding, y, rect.right - (kPadding * 2), 16, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += 18;
            g_audioEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                          kPadding, y, editWidth, 24, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            g_browseAudioButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  buttonX, y, kButtonWidth, 24, hwnd, (HMENU)kIdBrowseAudio, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            CreateWindowExA(0, "STATIC", "Select a PICO-8 Game .p8.png game file", WS_CHILD | WS_VISIBLE,
                            kPadding, y, rect.right - (kPadding * 2), 16, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            y += 18;
            g_cartEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                         kPadding, y, editWidth, 24, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            g_browseCartButton = CreateWindowExA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 buttonX, y, kButtonWidth, 24, hwnd, (HMENU)kIdBrowseCart, GetModuleHandleA(nullptr), nullptr);
            y += 24 + kRowGap;

            const int toggleColumnGap = kRowGap;
            const int toggleColumnWidth = (rect.right - (kPadding * 2) - (toggleColumnGap * 2)) / 3;
            const int toggleXLeft = kPadding;
            const int toggleXMid = kPadding + toggleColumnWidth + toggleColumnGap;
            const int toggleXRight = kPadding + (toggleColumnWidth + toggleColumnGap) * 2;
            const int toggleRowHeight = 18;
            const int toggleRowGap = 6;
            const int toggleYStart = y;

            g_toggleStretched = CreateWindowExA(0, "BUTTON", "Stretch game area",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 toggleXLeft, toggleYStart, toggleColumnWidth, toggleRowHeight,
                                                 hwnd, (HMENU)kIdToggleStretched, GetModuleHandleA(nullptr), nullptr);
            g_toggleCrtFilter = CreateWindowExA(0, "BUTTON", "CRT scanline filter",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 toggleXMid, toggleYStart, toggleColumnWidth, toggleRowHeight,
                                                 hwnd, (HMENU)kIdToggleCrtFilter, GetModuleHandleA(nullptr), nullptr);
            g_toggleInterpol8 = CreateWindowExA(0, "BUTTON", "Interpolation filter",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 toggleXRight, toggleYStart, toggleColumnWidth, toggleRowHeight,
                                                 hwnd, (HMENU)kIdToggleInterpol8, GetModuleHandleA(nullptr), nullptr);
            g_toggleTopNoBack = CreateWindowExA(0, "BUTTON", "Hide top background/skin",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 toggleXLeft, toggleYStart + toggleRowHeight + toggleRowGap,
                                                 toggleColumnWidth, toggleRowHeight,
                                                 hwnd, (HMENU)kIdToggleTopNoBack, GetModuleHandleA(nullptr), nullptr);
            g_toggleBottomNoBack = CreateWindowExA(0, "BUTTON", "Hide bottom background/skin",
                                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                    toggleXMid, toggleYStart + toggleRowHeight + toggleRowGap,
                                                    toggleColumnWidth, toggleRowHeight,
                                                    hwnd, (HMENU)kIdToggleBottomNoBack, GetModuleHandleA(nullptr), nullptr);
            g_toggleSkipVblank = CreateWindowExA(0, "BUTTON", "Skip VBlank",
                                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                    toggleXRight, toggleYStart + toggleRowHeight + toggleRowGap,
                                                    toggleColumnWidth, toggleRowHeight,
                                                    hwnd, (HMENU)kIdToggleSkipVblank, GetModuleHandleA(nullptr), nullptr);

            applyStartupFlags(kDefaultStartupFlags);

            y = toggleYStart + (toggleRowHeight + toggleRowGap) * 2 + kRowGap;

            g_resetButton = CreateWindowExA(
                0,
                "BUTTON",
                "Reset",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                kPadding,
                y,
                kButtonWidth,
                kButtonHeight,
                hwnd,
                (HMENU)kIdReset,
                GetModuleHandleA(nullptr),
                nullptr);

            g_generateButton = CreateWindowExA(
                0,
                "BUTTON",
                "Generate",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                kPadding + kButtonWidth + 10,
                y,
                kButtonWidth,
                kButtonHeight,
                hwnd,
                (HMENU)kIdGenerate,
                GetModuleHandleA(nullptr),
                nullptr);

            g_spinner = CreateWindowExA(
                0,
                "STATIC",
                "",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                kPadding + (kButtonWidth * 2) + 18,
                y,
                24,
                kButtonHeight,
                hwnd,
                (HMENU)kIdSpinner,
                GetModuleHandleA(nullptr),
                nullptr);

            updateGenerateEnabled();
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kIdBrowseIcon) {
                handleBrowseFile(hwnd, "PNG Images (*.png)\0*.png\0All Files\0*.*\0", g_iconEdit, g_iconPath, sizeof(g_iconPath), ".png");
            } else if (id == kIdBrowseBanner) {
                handleBrowseFile(hwnd, "PNG Images (*.png)\0*.png\0All Files\0*.*\0", g_bannerEdit, g_bannerPath, sizeof(g_bannerPath), ".png");
            } else if (id == kIdBrowseAudio) {
                handleBrowseFile(hwnd, "WAV Audio (*.wav)\0*.wav\0All Files\0*.*\0", g_audioEdit, g_audioPath, sizeof(g_audioPath), ".wav");
            } else if (id == kIdBrowseWallpaper) {
                handleBrowseFile(hwnd, "PNG Images (*.png)\0*.png\0All Files\0*.*\0", g_wallpaperEdit, g_wallpaperPath, sizeof(g_wallpaperPath), ".png");
            } else if (id == kIdBrowseCart) {
                handleBrowseFile(hwnd, "PICO-8 Cart (*.p8.png)\0*.p8.png\0All Files\0*.*\0", g_cartEdit, g_cartPath, sizeof(g_cartPath), ".p8.png");
            } else if (id == kIdReset) {
                handleReset();
            } else if (id == kIdGenerate) {
                handleGenerate(hwnd);
            } else if (id == kIdTitleEdit || id == kIdTitleIdEdit || id == kIdPublisherEdit) {
                if (HIWORD(wParam) == EN_CHANGE) {
                    syncTitleFields();
                    updateGenerateEnabled();
                }
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == kIdSpinnerTimer && g_building && g_spinner) {
                static const char* frames = "|/-\\";
                static int frame = 0;
                char text[2] = { frames[frame], '\0' };
                frame = (frame + 1) % 4;
                SetWindowTextA(g_spinner, text);
            }
            break;
        }
        case kMsgBuildDone: {
            BuildResult* result = reinterpret_cast<BuildResult*>(lParam);
            stopSpinner(hwnd);
            setBusy(false);
            if (result) {
                showMessage(result->message.c_str(), wParam ? (MB_ICONINFORMATION | MB_OK) : (MB_ICONERROR | MB_OK));
                delete result;
            }
            if (g_buildThread) {
                CloseHandle(g_buildThread);
                g_buildThread = nullptr;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            if (g_bannerBitmap) {
                DeleteObject(g_bannerBitmap);
                g_bannerBitmap = nullptr;
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
    const char* className = "Real8ToolsWindow";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    g_windowBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
    wc.hbrBackground = g_windowBrush ? g_windowBrush : (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        0,
        className,
        "PicoTo3DS v1.0 by @natureglass",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        560,
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
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_windowBrush) {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }

    return 0;
}
