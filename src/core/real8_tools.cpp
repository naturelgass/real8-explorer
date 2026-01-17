#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "real8_tools.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <filesystem>
#include <cctype>

// Include lodePNG here so the main VM doesn't need to depend on it
#include <lodePNG.h>

#ifndef REAL8_STRETCHED
#define REAL8_STRETCHED 0
#endif
#ifndef REAL8_CRTFILTER
#define REAL8_CRTFILTER 0
#endif
#ifndef REAL8_INTERPOL8
#define REAL8_INTERPOL8 0
#endif
#ifndef REAL8_TOP_NOBACK
#define REAL8_TOP_NOBACK 0
#endif
#ifndef REAL8_BOTTOM_NOBACK
#define REAL8_BOTTOM_NOBACK 0
#endif

namespace fs = std::filesystem;

// --------------------------------------------------------------------------
// HELPER CONSTANTS & FUNCTIONS
// --------------------------------------------------------------------------

// PICO-8 Palette for color matching
static const uint8_t TOOLS_PALETTE_RGB[32][3] = {
    {0,0,0},{29,43,83},{126,37,83},{0,135,81},{171,82,54},{95,87,79},{194,195,199},{255,241,232},
    {255,0,77},{255,163,0},{255,236,39},{0,228,54},{41,173,255},{131,118,156},{255,119,168},{255,204,170},
    // Hidden Palette (128-143)
    {41,24,20},{17,29,53},{66,33,54},{18,83,89},{116,47,41},{73,51,59},{162,136,121},{243,239,125},
    {190,18,80},{255,108,36},{168,231,46},{0,181,67},{6,90,181},{117,70,101},{255,110,89},{255,157,129}
};

static std::string EscapeLuaString(const std::string& input)
{
    std::string output = "\"";
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else output += c;
    }
    output += "\"";
    return output;
}

uint8_t Real8Tools::FindClosestP8Color(uint8_t r, uint8_t g, uint8_t b)
{
    int min_dist = 1000000;
    uint8_t best_idx = 0;

    for (int i = 0; i < 16; i++) {
        int dr = r - TOOLS_PALETTE_RGB[i][0];
        int dg = g - TOOLS_PALETTE_RGB[i][1];
        int db = b - TOOLS_PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = i; }
    }
    // Extended 16-31
    for (int i = 16; i < 32; i++) {
        int dr = r - TOOLS_PALETTE_RGB[i][0];
        int dg = g - TOOLS_PALETTE_RGB[i][1];
        int db = b - TOOLS_PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = 128 + (i - 16); }
    }
    return best_idx;
}

void Real8Tools::WriteVarLen(std::vector<uint8_t>& buf, uint32_t val) {
    uint32_t buffer = val & 0x7F;
    while ((val >>= 7)) {
        buffer <<= 8;
        buffer |= ((val & 0x7F) | 0x80);
    }
    while (true) {
        buf.push_back(buffer & 0xFF);
        if (buffer & 0x80) buffer >>= 8;
        else break;
    }
}

static std::string CleanCartName(const std::string& pathOrName)
{
    if (pathOrName.empty()) return "";
    std::string name = pathOrName;
    size_t lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);

    size_t firstDot = name.find('.');
    if (firstDot != std::string::npos) name = name.substr(0, firstDot);
    return name;
}

static std::string GetActiveCartName(Real8VM* vm)
{
    if (!vm) return "";
    if (!vm->currentCartPath.empty()) return CleanCartName(vm->currentCartPath);
    if (!vm->currentGameId.empty()) return CleanCartName(vm->currentGameId);
    return "";
}

static bool readConfigFlags2(const std::vector<uint8_t>& data, uint8_t& outFlags2)
{
    if (data.size() < 6) return false;
    uint32_t inputSize = 0;
    memcpy(&inputSize, &data[1], 4);
    size_t offset = 5 + inputSize;
    if (data.size() <= offset) return false;
    outFlags2 = data[offset];
    return true;
}

// --------------------------------------------------------------------------
// CONFIGURATION & ASSETS
// --------------------------------------------------------------------------

void Real8Tools::LoadSettings(Real8VM* vm, IReal8Host* host)
{
    // Libretro handles settings via Core Options (retro_variables), not files.
    if (strcmp(host->getPlatform(), "Libretro") == 0) return;
    const bool is3ds = (strcmp(host->getPlatform(), "3DS") == 0);

    if (!vm || !host) return;
    std::vector<uint8_t> data = host->loadFile("/config.dat");
    if (data.size() < 1) {
        vm->showRepoSnap = false;
        vm->showStats = false;
        vm->showRepoGames = true;

        if (is3ds) {
            vm->showSkin = (REAL8_TOP_NOBACK == 0);
            vm->crt_filter = (REAL8_CRTFILTER != 0);
            vm->interpolation = (REAL8_INTERPOL8 != 0);
            vm->stretchScreen = (REAL8_STRETCHED != 0);
        } else {
            vm->showSkin = true;
            vm->crt_filter = false;
            vm->interpolation = false;
            vm->stretchScreen = false;
        }

        SaveSettings(vm, host);
        if (vm->showSkin || (is3ds && (REAL8_BOTTOM_NOBACK == 0))) { LoadSkin(vm, host); }
        return;
    }

    // 1. Load Flags 1
    uint8_t flags = data[0];
    vm->showRepoSnap   = (flags & (1 << 0));
    vm->showSkin       = (flags & (1 << 1));
    vm->crt_filter     = (flags & (1 << 2));
    vm->showStats      = (flags & (1 << 3));
    vm->interpolation  = (flags & (1 << 4));

    // 2. Load Input Data (Skipped/Handled by Host usually, but reading size to advance offset)
    uint32_t inputSize = 0;
    if (data.size() > 5) memcpy(&inputSize, &data[1], 4);
    
    size_t offset = 5 + inputSize;

    // 3. Load Flags 2
    if (data.size() > offset) {
        uint8_t flags2 = data[offset];
        vm->showRepoGames = (flags2 & (1 << 0));
        vm->stretchScreen = (flags2 & (1 << 2));
        offset++;
    }

    // 4. Load Repo URL
    if (data.size() > offset + 4) {
        uint32_t urlLen = 0;
        urlLen |= data[offset+0];
        urlLen |= (data[offset+1] << 8);
        urlLen |= (data[offset+2] << 16);
        urlLen |= (data[offset+3] << 24);
        offset += 4;

        if (urlLen > 0 && data.size() >= offset + urlLen) {
            vm->currentRepoUrl = std::string((char*)&data[offset], urlLen);
            offset += urlLen;
        }
    }

    // --- LOAD VOLUME ---
    if (data.size() > offset) {
        vm->volume_music = data[offset++];
    }
    if (data.size() > offset) {
        vm->volume_sfx = data[offset++];
    }

    if (vm->showSkin || (is3ds && (REAL8_BOTTOM_NOBACK == 0))) { LoadSkin(vm, host); }
}

void Real8Tools::SaveSettings(Real8VM* vm, IReal8Host* host)
{

    // Libretro settings are read-only or managed by the frontend.
    if (strcmp(host->getPlatform(), "Libretro") == 0) return;

    if (!vm || !host) return;

    const bool is3ds = (strcmp(host->getPlatform(), "3DS") == 0);

    uint8_t flags = 0;
    if (vm->showRepoSnap) flags |= (1 << 0);
    if (vm->showSkin) flags |= (1 << 1);
    if (vm->crt_filter) flags |= (1 << 2);
    if (vm->showStats) flags |= (1 << 3);
    if (vm->interpolation) flags |= (1 << 4);

    // Host handles input config persistence details, we just ask for the blob
    std::vector<uint8_t> inputData = host->getInputConfigData();
    uint32_t inputSize = (uint32_t)inputData.size();

    std::vector<uint8_t> buffer;
    buffer.push_back(flags);
    uint8_t* sizePtr = (uint8_t*)&inputSize;
    for(int i=0; i<4; i++) buffer.push_back(sizePtr[i]);
    buffer.insert(buffer.end(), inputData.begin(), inputData.end());

    uint8_t flags2 = 0;
    if (vm->showRepoGames) flags2 |= (1 << 0);
    if (vm->stretchScreen) flags2 |= (1 << 2);
    if (is3ds) {
        uint8_t existingFlags2 = 0;
        std::vector<uint8_t> existing = host->loadFile("/config.dat");
        if (readConfigFlags2(existing, existingFlags2)) {
            if (existingFlags2 & (1 << 1)) flags2 |= (1 << 1);
        } else if (REAL8_BOTTOM_NOBACK != 0) {
            flags2 |= (1 << 1);
        }
    }
    buffer.push_back(flags2);

    uint32_t urlLen = (uint32_t)vm->currentRepoUrl.length();
    uint8_t* lenPtr = (uint8_t*)&urlLen;
    for(int i=0; i<4; i++) buffer.push_back(lenPtr[i]);
    if (urlLen > 0) buffer.insert(buffer.end(), vm->currentRepoUrl.begin(), vm->currentRepoUrl.end());

    buffer.push_back((uint8_t)vm->volume_music);
    buffer.push_back((uint8_t)vm->volume_sfx);

    host->saveState("/config.dat", buffer.data(), buffer.size());
}

void Real8Tools::LoadSkin(Real8VM* vm, IReal8Host* host)
{
    if (!host) return;
    std::vector<uint8_t> data = host->loadFile("/wallpaper.png");
    if (data.empty()) { vm->showSkin = false; return; }
    
    unsigned w, h; unsigned char *image = nullptr;
    unsigned error = lodepng_decode32(&image, &w, &h, data.data(), data.size());
    if (!error && image) {
        host->drawWallpaper(image, w, h);
        host->updateOverlay();
        free(image);
    }
}

// --------------------------------------------------------------------------
// MODDING SYSTEM
// --------------------------------------------------------------------------

void Real8Tools::ApplyMods(Real8VM* vm, IReal8Host* host, const std::string &cartPath)
{
    if (!vm || !host) return;

    std::string resolvedCartPath = cartPath;
    if (resolvedCartPath.empty()) resolvedCartPath = GetActiveCartName(vm);

    size_t lastSlash = resolvedCartPath.find_last_of("/\\");
    std::string filename = (lastSlash == std::string::npos) ? resolvedCartPath : resolvedCartPath.substr(lastSlash + 1);
    size_t lastDot = filename.find_first_of('.');
    std::string gameId = (lastDot != std::string::npos) ? filename.substr(0, lastDot) : filename;
    std::string modBasePath = "/mods/" + gameId;

    if (gameId.empty()) {
        host->log("[MODS] No game id resolved; skipping mod search.");
        return;
    }

    host->log("[MODS] Checking for mods in %s", modBasePath.c_str());

    bool modApplied = false;

    std::string sprPath = modBasePath + "/sprites.png";
    if (InjectSpriteMod(vm, host, sprPath, 0x0000)) {
        host->log("[MODS] Applied sprite sheet mod: %s", sprPath.c_str());
        modApplied = true;
    }

    for (int i = 1; i <= 8; i++) {
        std::string bankFilename = gameId + "_gfx_" + std::to_string(i) + ".png";
        std::string bankPath = modBasePath + "/" + bankFilename;
        if (InjectSpriteMod(vm, host, bankPath, (i - 1) * 0x2000)) {
            host->log("[MODS] Applied sprite bank %d: %s", i, bankPath.c_str());
            modApplied = true;
        }
    }

    std::string mapPath = modBasePath + "/map.bin";
    if (InjectBinaryMod(vm, host, mapPath, 0x2000)) {
        host->log("[MODS] Applied map mod: %s", mapPath.c_str());
        modApplied = true;
    } else {
        std::string specificMapPath = modBasePath + "/" + gameId + "_map.bin";
        if (InjectBinaryMod(vm, host, specificMapPath, 0x2000)) {
            host->log("[MODS] Applied map mod: %s", specificMapPath.c_str());
            modApplied = true;
        }
    }

    std::string luaPath = modBasePath + "/patch.lua";
    if (InjectLuaMod(vm, host, luaPath, true)) {
        host->log("[MODS] Applied Lua patch: %s", luaPath.c_str());
        modApplied = true;
    }

    if (!modApplied) host->log("[MODS] No mods found for %s", gameId.c_str());
    else host->log("[MODS] Mod application completed for %s", gameId.c_str());
}

bool Real8Tools::InjectSpriteMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t dest_offset)
{
    std::vector<uint8_t> data = host->loadFile(path.c_str());
    if (data.empty()) return false;
    unsigned w, h; unsigned char *image = nullptr;
    unsigned error = lodepng_decode32(&image, &w, &h, data.data(), data.size());

    bool applied = false;
    if (!error && image && w == 128 && h == 128 && dest_offset < 0x8000 && vm->ram) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x += 2) {
                int idx1 = (y * 128 + x) * 4;
                uint8_t c1 = FindClosestP8Color(image[idx1], image[idx1 + 1], image[idx1 + 2]);
                int idx2 = (y * 128 + (x + 1)) * 4;
                uint8_t c2 = FindClosestP8Color(image[idx2], image[idx2 + 1], image[idx2 + 2]);
                uint8_t packed = (c2 << 4) | (c1 & 0x0F);
                vm->ram[dest_offset + (y * 64) + (x / 2)] = packed;
            }
        }
        applied = true;
    } else if (host) {
        if (error || !image) {
            host->log("[MODS] Failed to decode sprite mod: %s", path.c_str());
        } else if (w != 128 || h != 128) {
            host->log("[MODS] Sprite mod has invalid size (%ux%u): %s", w, h, path.c_str());
        }
    }
    if (image) free(image);
    return applied;
}

bool Real8Tools::InjectLuaMod(Real8VM* vm, IReal8Host* host, const std::string &path, bool persistent)
{
    if (!vm || !host) return false;
    lua_State* L = vm->getLuaState();
    if (!L) return false;

    std::vector<uint8_t> data = host->loadFile(path.c_str());
    if (data.empty()) return false;
    std::string script((char *)data.data(), data.size());

    int result = LUA_OK;

    if (persistent) {
        // Wrap patch.lua so we can re-run it every frame via __real8_patch_apply.
        vm->patchModActive = false;

        std::string wrapped = "local function __real8_patch_apply()\n";
        wrapped += script;
        if (!script.empty() && script.back() != '\n') wrapped += "\n";
        wrapped += "end\n";
        wrapped += "__real8_patch_apply()\n";
        wrapped += "_G.__real8_patch_apply = __real8_patch_apply\n";

        result = luaL_dostring(L, wrapped.c_str());
        if (result == LUA_OK) vm->patchModActive = true;
    } else {
        result = luaL_dostring(L, script.c_str());
    }

    if (result != LUA_OK) {
        host->log("[MODS] Lua Error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool Real8Tools::InjectBinaryMod(Real8VM* vm, IReal8Host* host, const std::string &path, uint32_t addr)
{
    std::vector<uint8_t> data = host->loadFile(path.c_str());
    if (data.empty()) return false;
    if (vm->ram && addr + data.size() <= 0x8000) {
        memcpy(vm->ram + addr, data.data(), data.size());
        if (vm->rom && !vm->rom_readonly) {
            memcpy(vm->rom, vm->ram, 0x8000);
        }
        return true;
    }
    return false;
}

// --------------------------------------------------------------------------
// EXPORTERS
// --------------------------------------------------------------------------



void Real8Tools::ExportLUA(Real8VM* vm, IReal8Host* host, const std::string &outputFile)
{
    if (!vm || !host) return;
    if (outputFile.empty()) return;

#if defined(__GBA__)
    host->log("[EXPORT] ExportLUA is not supported on this platform.");
    return;
#else
    if (!vm->ram) {
        host->log("[EXPORT] No RAM available to export cart data.");
        vm->gpu.renderMessage("SYSTEM", "NO CART DATA", 11);
        vm->show_frame();
        host->delayMs(500);
        return;
    }

    // We store the last loaded cart source in the VM during loadGame()
    const std::string &lua = vm->loadedLuaSource;
    if (lua.empty()) {
        host->log("[EXPORT] No LUA source available to export.");
        vm->gpu.renderMessage("SYSTEM", "NO LUA SOURCE", 11);
        vm->show_frame();
        host->delayMs(500);
        return;
    }

    fs::path outPath(outputFile);
    std::error_code ec;
    if (outPath.has_parent_path()) {
        fs::create_directories(outPath.parent_path(), ec);
    }
    if (ec) {
        host->log("[EXPORT] Failed to create output folder: %s", outPath.parent_path().string().c_str());
        return;
    }

    auto hex = [](int v) -> char {
        static const char* digits = "0123456789abcdef";
        return digits[v & 0xF];
    };

    auto writeHexByte = [&](std::stringstream& ss, uint8_t b) {
        ss << hex(b >> 4) << hex(b & 0xF);
    };

    std::stringstream ss;

    // Header
    ss << "pico-8 cartridge // http://www.pico-8.com\n";
    ss << "version 41\n";

    // LUA
    ss << "__lua__\n";
    ss << lua;
    if (!lua.empty() && lua.back() != '\n') ss << "\n";

    // GFX (0x0000 - 0x1FFF), 128 lines x 128 nibbles
    ss << "__gfx__\n";
    {
        const uint8_t* gfx = vm->ram + 0x0000;
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                uint8_t b = gfx[y * 64 + (x / 2)];
                int v = (x % 2 == 0) ? (b & 0x0F) : ((b >> 4) & 0x0F);
                ss << hex(v);
            }
            ss << "\n";
        }
    }

    // GFF (sprite flags) (0x3000 - 0x30FF), 256 bytes
    ss << "__gff__\n";
    {
        const uint8_t* gff = vm->ram + 0x3000;
        for (int i = 0; i < 256; i++) {
            writeHexByte(ss, gff[i]);
            if ((i % 32) == 31) ss << "\n"; // 32 bytes per line
        }
        if ((256 % 32) != 0) ss << "\n";
    }

    // MAP (extended 64 rows): 0x2000 bytes total
    // - rows 0..31 from 0x2000..0x2FFF
    // - rows 32..63 from shared area 0x1000..0x1FFF
    ss << "__map__\n";
    {
        auto mapByte = [&](int idx) -> uint8_t {
            if (idx < 0x1000) return vm->ram[0x2000 + idx];
            return vm->ram[0x1000 + (idx - 0x1000)];
        };

        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 128; x++) {
                uint8_t b = mapByte(y * 128 + x);
                writeHexByte(ss, b);
            }
            ss << "\n";
        }
    }

    // SFX (0x3200 - 0x42FF), 64 sfx * 68 bytes
    ss << "__sfx__\n";
    {
        const uint8_t* sfx = vm->ram + 0x3200;
        for (int s = 0; s < 64; s++) {
            int base = s * 68;

            // header bytes (offset 64..67)
            for (int h = 0; h < 4; h++) writeHexByte(ss, sfx[base + 64 + h]);
            ss << " ";

            // 32 notes: pitch(2 hex) + instrument(1) + volume(1) + effect(1)
            for (int n = 0; n < 32; n++) {
                uint8_t pitch = sfx[base + n * 2 + 0];
                uint8_t b2    = sfx[base + n * 2 + 1];
                int instr = (b2 >> 5) & 0x7;
                int vol   = (b2 >> 2) & 0x7;
                int eff   = (b2 >> 0) & 0x3;

                writeHexByte(ss, pitch);
                ss << hex(instr) << hex(vol) << hex(eff);

                if (n != 31) ss << " ";
            }
            ss << "\n";
        }
    }

    // MUSIC (0x3100 - 0x31FF), 64 patterns * 4 bytes
    ss << "__music__\n";
    {
        const uint8_t* music = vm->ram + 0x3100;

        auto chanToText = [&](uint8_t mb) -> std::string {
            uint8_t v = mb & 0x7F; // strip loop/stop flags (0x80)
            if (v & 0x40) return "-1";
            return std::to_string((int)(v & 0x3F));
        };

        for (int p = 0; p < 64; p++) {
            uint8_t m0 = music[p * 4 + 0];
            uint8_t m1 = music[p * 4 + 1];
            uint8_t m2 = music[p * 4 + 2];
            uint8_t m3 = music[p * 4 + 3];

            int flags = 0;
            if (m0 & 0x80) flags |= 1; // loop start
            if (m1 & 0x80) flags |= 2; // loop back
            if (m2 & 0x80) flags |= 4; // stop

            ss << hex(flags >> 4) << hex(flags & 0xF) << " ";
            ss << chanToText(m0) << " " << chanToText(m1) << " " << chanToText(m2) << " " << chanToText(m3) << "\n";
        }
    }

    std::string data = ss.str();
    if (host->saveState(outPath.string().c_str(), (const uint8_t*)data.data(), data.size())) {
        host->log("[EXPORT] Cart exported to: %s", outPath.string().c_str());
        vm->gpu.renderMessage("SYSTEM", "CART EXPORTED", 11);
        vm->show_frame();
        host->delayMs(500);
    } else {
        host->log("[EXPORT] Failed to write: %s", outPath.string().c_str());
        vm->gpu.renderMessage("SYSTEM", "EXPORT FAILED", 11);
        vm->show_frame();
        host->delayMs(500);
    }
#endif
}


void Real8Tools::ExportGFX(Real8VM* vm, IReal8Host* host, const std::string &outputFolder)
{
    if (!vm || !host) return;
    std::string gameName = GetActiveCartName(vm);
    if (gameName.empty()) return;

    fs::path modFolder = fs::path(outputFolder) / gameName;
    std::error_code ec;
    fs::create_directories(modFolder, ec);
    if (ec) {
        host->log("[EXPORT] Failed to create mod folder: %s", modFolder.string().c_str());
        return;
    }

    std::vector<unsigned char> image; image.resize(128 * 128 * 4);
    
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            uint8_t palIdx = vm->gpu.sget(x, y);
            
            // Note: We access the GPU palette or the static tool palette. 
            // Using tool palette ensures accurate P8 colors regardless of current screen draw state.
            const uint8_t* c = TOOLS_PALETTE_RGB[palIdx & 31];
            int idx = (y * 128 + x) * 4;
            image[idx + 0] = c[0]; 
            image[idx + 1] = c[1]; 
            image[idx + 2] = c[2]; 
            image[idx + 3] = 255; 
        }
    }
    std::string filename = (modFolder / (gameName + "_gfx_1.png")).string();
    lodepng_encode32_file(filename.c_str(), image.data(), 128, 128);
}

void Real8Tools::ExportMAP(Real8VM* vm, IReal8Host* host, const std::string &outputFolder)
{
    if (!vm || !host || !vm->ram) return;
    std::string gameName = GetActiveCartName(vm);
    if (gameName.empty()) return;

    fs::path modFolder = fs::path(outputFolder) / gameName;
    std::error_code ec;
    fs::create_directories(modFolder, ec);
    if (ec) {
        host->log("[EXPORT] Failed to create mod folder: %s", modFolder.string().c_str());
        return;
    }

    std::string filename = (modFolder / (gameName + "_map.bin")).string();
    host->saveState(filename.c_str(), vm->ram + 0x2000, 4096);
}

std::vector<Real8Tools::StaticVarEntry> Real8Tools::CollectStaticVars(Real8VM* vm)
{
    lua_State* L = vm->getLuaState();
    if (!L) return {};

    // 1. Define Blacklist (System Globals + Standard PICO-8 API)
    static const std::set<std::string> blacklist = {
        "_G", "_VERSION", "package", "string", "table", "math", "coroutine", 
        "os", "io", "debug", "__pico8_vm_ptr", "bit", "bit32",
        "_init", "_update", "_draw", "_update60",
        "camera", "circ", "circfill", "cls", "color", "cursor", "fget", "fillp", 
        "flip", "fset", "line", "map", "mget", "mset", "music", "pal", "palt", 
        "pget", "print", "pset", "rect", "rectfill", "sfx", "sget", "spr", "sspr", 
        "sset", "time", "tline", "add", "all", "btn", "btnp", "ceil", "cos", 
        "del", "deli", "flr", "foreach", "max", "mid", "min", "pairs", "rnd", 
        "shl", "shr", "sin", "sqrt", "srand", "sub", "tonum", "tostr", "type", 
        "count", "extcmd", "menuitem", "run", "stop", "trace", "reload", "cstore", 
        "memcpy", "memset", "peek", "poke", "serial", "stat", "printh", "cartdata",
        "dget", "dset", "ipairs", "next", "assert", "dofile", "error", "getmetatable",
        "load", "loadfile", "pcall", "rawequal", "rawget", "rawlen", "rawset", 
        "select", "setmetatable", "tonumber", "tostring", "xpcall"
    };

    auto isAllowedName = [](const std::string& value) {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
    };

    std::vector<StaticVarEntry> exportedVars;

    lua_pushglobaltable(L);
    lua_pushnil(L);

    while (lua_next(L, -2) != 0) {
        if (lua_isstring(L, -2)) {
            std::string key = lua_tostring(L, -2);
            if (blacklist.find(key) == blacklist.end() && isAllowedName(key)) {
                int type = lua_type(L, -1);
                StaticVarEntry entry{};
                entry.name = key;

                if (type == LUA_TNUMBER) {
                    double d = lua_tonumber(L, -1);
                    entry.value = (d == (int)d) ? std::to_string((int)d) : std::to_string(d);
                    entry.type = StaticVarEntry::Type::Number;
                    exportedVars.push_back(entry);
                }
                else if (type == LUA_TBOOLEAN) {
                    entry.value = lua_toboolean(L, -1) ? "true" : "false";
                    entry.type = StaticVarEntry::Type::Boolean;
                    exportedVars.push_back(entry);
                }
                else if (type == LUA_TSTRING) {
                    entry.value = lua_tostring(L, -1);
                    entry.type = StaticVarEntry::Type::String;
                    exportedVars.push_back(entry);
                }
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    std::sort(exportedVars.begin(), exportedVars.end(), [](const StaticVarEntry& a, const StaticVarEntry& b) {
        return a.name < b.name;
    });

    return exportedVars;
}

void Real8Tools::ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder)
{
    std::vector<StaticVarEntry> exportedVars = CollectStaticVars(vm);
    ExportStaticVars(vm, host, outputFolder, exportedVars);
}

void Real8Tools::ExportStaticVars(Real8VM* vm, IReal8Host* host, const std::string &outputFolder, const std::vector<StaticVarEntry>& entries)
{
    std::string gameName = GetActiveCartName(vm);
    if (gameName.empty()) {
        host->log("[EXPORT] No game loaded.");
        return;
    }

    std::vector<StaticVarEntry> exportedVars = entries;
    if (exportedVars.empty()) {
        host->log("[EXPORT] No static vars found.");
        return;
    }

    std::sort(exportedVars.begin(), exportedVars.end(), [](const StaticVarEntry& a, const StaticVarEntry& b) {
        return a.name < b.name;
    });

    std::stringstream ss;
    ss << "-- Patch for: " << gameName << "\n";
    ss << "-- Generated by Real-8 VM Tools\n\n";

    for (const auto &entry : exportedVars) {
        std::string valStr;
        switch (entry.type) {
            case StaticVarEntry::Type::Number:
            case StaticVarEntry::Type::Boolean:
                valStr = entry.value;
                break;
            case StaticVarEntry::Type::String:
                valStr = EscapeLuaString(entry.value);
                break;
        }
        ss << entry.name << " = " << valStr << "\n";
    }

    fs::path modFolder = fs::path(outputFolder) / gameName;
    std::error_code ec;
    fs::create_directories(modFolder, ec);
    if (ec) {
        host->log("[EXPORT] Failed to create mod folder: %s", modFolder.string().c_str());
        return;
    }

    fs::path patchPath = modFolder / "patch.lua";
    std::string filename = patchPath.string();
    if (host->saveState(filename.c_str(), (const uint8_t*)ss.str().c_str(), ss.str().length())) {
        host->log("[EXPORT] Variables exported to: %s", filename.c_str());
        vm->gpu.renderMessage("SYSTEM", "PATCH EXPORTED", 11);
        vm->show_frame();
        host->delayMs(500);
    } else {
        host->log("[EXPORT] Failed to save patch.lua");
    }
}

// 0:Sine, 1:Tri, 2:Saw, 3:LongSaw, 4:Square, 5:Pulse, 6:Organ, 7:Noise
static const uint8_t P8_MIDI_PRG[] = { 
    80, 81, 81, 80, 80, 16, 118, 95
};

void Real8Tools::ExportMusic(Real8VM* vm, IReal8Host* host, const std::string &outputFolder)
{
    if (vm->currentGameId.empty() || !vm->ram) return;

    std::string cleanName = vm->currentGameId;
    size_t lastDot = cleanName.find_last_of('.');
    if (lastDot != std::string::npos) cleanName = cleanName.substr(0, lastDot);
    if (cleanName.length() > 3 && cleanName.substr(cleanName.length()-3) == ".p8") 
        cleanName = cleanName.substr(0, cleanName.length()-3);

    host->log("[EXPORT] Exporting Music: %s", cleanName.c_str());

    int sfxNotesOffset = 0;
    int sfxSpeedOffset = 65;
    
    // Check layout detection
    bool textLayout = false;
    for(int i=0; i<3; i++) {
        uint8_t b1 = vm->sfx_ram[i*68 + 1];
        uint8_t b65 = vm->sfx_ram[i*68 + 65];
        if (b1 >= 1 && b1 <= 32 && b65 == 0) textLayout = true;
    }

    if (textLayout) {
        sfxNotesOffset = 4;
        sfxSpeedOffset = 1;
    }

    const int PPQ = 480; 
    const int CHANNELS = 4;
    std::vector<uint8_t> tracks[CHANNELS];
    uint32_t deltas[CHANNELS] = {0};
    int lastInstr[CHANNELS] = {-1, -1, -1, -1};

    int songIdx = 1;
    bool recording = false;
    int patterns = 0;

    auto saveSong = [&]() {
        if (!recording || patterns == 0) return;

        std::vector<uint8_t> f;
        f.insert(f.end(), {'M','T','h','d', 0,0,0,6, 0,1, 0,(uint8_t)(CHANNELS+1), (uint8_t)(PPQ>>8), (uint8_t)(PPQ&0xFF)});

        std::vector<uint8_t> t;
        WriteVarLen(t, 0);
        t.insert(t.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); 
        WriteVarLen(t, 0); 
        t.insert(t.end(), {0xFF, 0x2F, 0x00});
        
        f.insert(f.end(), {'M','T','r','k'});
        uint32_t sz = t.size();
        f.push_back(sz>>24); f.push_back(sz>>16); f.push_back(sz>>8); f.push_back(sz);
        f.insert(f.end(), t.begin(), t.end());

        for(int c=0; c<CHANNELS; c++) {
            std::vector<uint8_t>& trk = tracks[c];
            WriteVarLen(trk, 0); 
            trk.insert(trk.end(), {0xFF, 0x2F, 0x00}); 
            
            f.insert(f.end(), {'M','T','r','k'});
            uint32_t sz = trk.size();
            f.push_back(sz>>24); f.push_back(sz>>16); f.push_back(sz>>8); f.push_back(sz);
            f.insert(f.end(), trk.begin(), trk.end());
            
            trk.clear(); 
            deltas[c] = 0; 
            lastInstr[c] = -1;
        }

        std::stringstream ss;
        ss << outputFolder << "/" << cleanName << "_" << std::setw(2) << std::setfill('0') << songIdx++ << ".mid";
        host->saveState(ss.str().c_str(), f.data(), f.size());
        
        recording = false; 
        patterns = 0;
    };

    for (int pat = 0; pat < 64; pat++) {
        uint8_t m0 = vm->music_ram[pat*4+0];
        uint8_t m1 = vm->music_ram[pat*4+1];
        uint8_t m2 = vm->music_ram[pat*4+2];
        uint8_t m3 = vm->music_ram[pat*4+3];

        bool loopStart = (m0 & 0x80);
        bool stop = (m2 & 0x80); 

        int sfx[4];
        int r0 = m0 & 0x7F; sfx[0] = (r0 > 63) ? 64 : r0;
        int r1 = m1 & 0x7F; sfx[1] = (r1 > 63) ? 64 : r1;
        int r2 = m2 & 0x7F; sfx[2] = (r2 > 63) ? 64 : r2;
        int r3 = m3 & 0x7F; sfx[3] = (r3 > 63) ? 64 : r3;

        bool empty = true;
        for(int i=0; i<4; i++) {
            if(sfx[i] < 64) {
                int spd = vm->sfx_ram[(sfx[i]*68) + sfxSpeedOffset];
                if (spd > 0) empty = false;
            }
        }
        
        if (loopStart || (recording && empty)) {
            saveSong();
        }
        
        if (empty) continue; 

        if (!recording) recording = true;
        patterns++;

        int spd = 16; 
        for(int i=0; i<4; i++) {
            if(sfx[i] < 64) {
                int s = vm->sfx_ram[(sfx[i]*68) + sfxSpeedOffset]; 
                if(s > 0) { spd = s; break; } 
            }
        }
        uint32_t ticks = spd * 8; 

        for(int c=0; c<4; c++) {
            int id = sfx[c];
            int defaultMidiCh = c;

            if(id < 64) {
                uint8_t* dat = vm->sfx_ram + (id * 68) + sfxNotesOffset;

                for(int row=0; row<32; row++) {
                    uint8_t b0 = dat[row*2];
                    uint8_t b1 = dat[row*2+1];
                    
                    uint8_t pitch = b0 & 0x3F;
                    uint8_t instr = (b1 >> 5) & 0x7;
                    uint8_t vol   = (b1 >> 2) & 0x7;

                    int chOut = (instr == 6) ? 9 : defaultMidiCh;

                    if(chOut != 9 && lastInstr[c] != instr) {
                        lastInstr[c] = instr;
                        WriteVarLen(tracks[c], deltas[c]); 
                        deltas[c] = 0;
                        tracks[c].push_back(0xC0 | chOut);
                        tracks[c].push_back(P8_MIDI_PRG[instr]);
                    }

                    if(vol > 0) {
                        uint8_t note;
                        if(chOut == 9) {
                            if(pitch < 12) note = 36;
                            else if(pitch < 24) note = 38;
                            else if(pitch < 40) note = 42;
                            else note = 46;
                        } else {
                            note = pitch + 36; 
                        }

                        WriteVarLen(tracks[c], deltas[c]); 
                        deltas[c] = 0;
                        tracks[c].push_back(0x90 | chOut);
                        tracks[c].push_back(note);
                        tracks[c].push_back((vol * 127) / 7);

                        WriteVarLen(tracks[c], ticks);
                        tracks[c].push_back(0x80 | chOut);
                        tracks[c].push_back(note);
                        tracks[c].push_back(0);
                    } else {
                        deltas[c] += ticks;
                    }
                }
            } else {
                deltas[c] += (32 * ticks);
            }
        }
        if(stop) saveSong();
    }
    if(recording) saveSong();
    
    vm->gpu.renderMessage("SYSTEM", "MUSIC EXPORTED", 11);
    vm->show_frame();
    host->delayMs(500);
}
