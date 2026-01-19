#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../../hal/real8_host.h"
#include "../../core/real8_cart.h"
#include "cart_blob.h"

class PackerHost : public IReal8Host {
public:
    const char* getPlatform() const override { return "Packer"; }

    void setNetworkActive(bool active) override { (void)active; }
    void setWifiCredentials(const char* ssid, const char* pass) override {
        (void)ssid;
        (void)pass;
    }

    void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map) override {
        (void)framebuffer;
        (void)fb_w;
        (void)fb_h;
        (void)palette_map;
    }

    unsigned long getMillis() override {
        using clock = std::chrono::steady_clock;
        static const auto start = clock::now();
        auto now = clock::now();
        return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }

    void log(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }

    void delayMs(int ms) override {
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    std::vector<uint8_t> loadFile(const char* path) override {
        (void)path;
        return {};
    }

    std::vector<std::string> listFiles(const char* ext) override {
        (void)ext;
        return {};
    }

    bool saveState(const char* filename, const uint8_t* data, size_t size) override {
        (void)filename;
        (void)data;
        (void)size;
        return false;
    }

    std::vector<uint8_t> loadState(const char* filename) override {
        (void)filename;
        return {};
    }

    bool hasSaveState(const char* filename) override {
        (void)filename;
        return false;
    }

    void deleteFile(const char* path) override {
        (void)path;
    }

    void getStorageInfo(size_t &used, size_t &total) override {
        used = 0;
        total = 0;
    }

    bool renameGameUI(const char* currentPath) override {
        (void)currentPath;
        return false;
    }

    uint32_t getPlayerInput(int playerIdx) override {
        (void)playerIdx;
        return 0;
    }

    void pollInput() override {
    }

    void openGamepadConfigUI() override {
    }

    std::vector<uint8_t> getInputConfigData() override { return {}; }
    void setInputConfigData(const std::vector<uint8_t>& data) override { (void)data; }

    void pushAudio(const int16_t* samples, int count) override {
        (void)samples;
        (void)count;
    }

    NetworkInfo getNetworkInfo() override { return {false, "", "Offline", 0.0f}; }

    bool downloadFile(const char* url, const char* savePath) override {
        (void)url;
        (void)savePath;
        return false;
    }

    void takeScreenshot() override {}
    void drawWallpaper(const uint8_t* pixels, int w, int h) override { (void)pixels; (void)w; (void)h; }
    void clearWallpaper() override {}
    void updateOverlay() override {}
};

static bool readFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)size);
    if (fread(out.data(), 1, out.size(), f) != out.size()) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static void printUsage(const char* exe) {
    const char* name = (exe && *exe) ? exe : "cart_packer";
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <input.p8.png> <output.bin>\n", name);
    fprintf(stderr, "  %s --template <output.bin> <payload_capacity_bytes>\n", name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Converts a PICO-8 cart image into a raw GBA cart blob.\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  %s game.p8.png cart_blob.bin\n", name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Template example (reserves payload space for later patching):\n");
    fprintf(stderr, "  %s --template cart_blob.bin 262144\n", name);
}

static bool writeTemplateBlob(const char* output, uint32_t payloadCapacity, std::string& err) {
    if (payloadCapacity < 1) {
        err = "Payload capacity must be > 0.";
        return false;
    }

    FILE* out = fopen(output, "wb");
    if (!out) {
        err = std::string("Failed to open ") + output + " for writing";
        return false;
    }

    CartBlobHeader header{};
    memcpy(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE);
    header.flags = CART_BLOB_FLAG_NONE;
    header.raw_size = 0;
    // In template mode, comp_size is used as *payload slot capacity*.
    header.comp_size = payloadCapacity;

    if (fwrite(&header, 1, sizeof(header), out) != sizeof(header)) {
        fclose(out);
        err = "Failed to write template header.";
        return false;
    }

    std::vector<uint8_t> zeros;
    zeros.resize(4096, 0);
    uint32_t remaining = payloadCapacity;
    while (remaining) {
        uint32_t chunk = remaining > (uint32_t)zeros.size() ? (uint32_t)zeros.size() : remaining;
        if (fwrite(zeros.data(), 1, chunk, out) != chunk) {
            fclose(out);
            err = "Failed to write template padding.";
            return false;
        }
        remaining -= chunk;
    }

    fclose(out);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argc > 0 ? argv[0] : nullptr);
        return 1;
    }

    // Template mode: cart_packer --template <output.bin> <payload_capacity_bytes>
    if (argc >= 4 && strcmp(argv[1], "--template") == 0) {
        const char* output = argv[2];
        const char* capStr = argv[3];
        char* endp = nullptr;
        unsigned long cap = strtoul(capStr, &endp, 0);
        if (!endp || endp == capStr || *endp != '\0') {
            fprintf(stderr, "Invalid capacity: %s\n", capStr);
            return 1;
        }
        std::string err;
        if (!writeTemplateBlob(output, (uint32_t)cap, err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        return 0;
    }

    if (argc < 3) {
        printUsage(argc > 0 ? argv[0] : nullptr);
        return 1;
    }

    const char* input = argv[1];
    const char* output = argv[2];

    std::vector<uint8_t> buffer;
    if (!readFile(input, buffer)) {
        fprintf(stderr, "Failed to read %s\n", input);
        return 1;
    }

    PackerHost host;
    GameData game;
    if (!Real8CartLoader::LoadFromBuffer(&host, buffer, game)) {
        fprintf(stderr, "Failed to decode cart: %s\n", input);
        return 1;
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

    FILE* out = fopen(output, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open %s for writing\n", output);
        return 1;
    }

    if (fwrite(&header, 1, sizeof(header), out) != sizeof(header)) {
        fclose(out);
        fprintf(stderr, "Failed to write header to %s\n", output);
        return 1;
    }

    if (fwrite(payload.data(), 1, payload.size(), out) != payload.size()) {
        fclose(out);
        fprintf(stderr, "Failed to write payload to %s\n", output);
        return 1;
    }

    fclose(out);
    return 0;
}
