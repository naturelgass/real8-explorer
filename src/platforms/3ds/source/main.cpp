#include <3ds.h>

#include "3ds_host.hpp"
#include "real8_vm.h"
#if defined(REAL8_3DS_STANDALONE)
#include "real8_menu.h"
#include "real8_cart.h"
#if defined(REAL8_3DS_EMBED_CART)
#include "cart_blob.h"
#endif
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#else
#include "real8_shell.h"
#endif

#if defined(REAL8_3DS_STANDALONE)
#ifndef REAL8_3DS_STANDALONE_CART
#define REAL8_3DS_STANDALONE_CART "romfs:/game.p8.png"
#endif
#endif

namespace {
struct FrameStats {
    u64 frameStartMs = 0;
    u64 accumFrameMs = 0;
    u64 accumWorkMs = 0;
    int frames = 0;

    void beginFrame() { frameStartMs = osGetTime(); }

    void endFrame(ThreeDSHost* host, u64 workEndMs) {
        if (frameStartMs == 0) return;
        u64 frameEndMs = osGetTime();
        u64 frameMs = frameEndMs - frameStartMs;
        u64 workMs = workEndMs - frameStartMs;

        accumFrameMs += frameMs;
        accumWorkMs += workMs;
        frames++;

        if (frames >= 60 && host) {
            double avgFrame = (double)accumFrameMs / (double)frames;
            double avgWork = (double)accumWorkMs / (double)frames;
            host->log("[PERF] frame %.2f ms (work %.2f ms) avg over %d frames", avgFrame, avgWork, frames);
            accumFrameMs = 0;
            accumWorkMs = 0;
            frames = 0;
        }
    }
};

void applyN3DSSpeedup(ThreeDSHost* host) {
    bool isNew3ds = false;
    Result rc = APT_CheckNew3DS(&isNew3ds);
    if (R_FAILED(rc)) {
        if (host) host->log("[3DS] APT_CheckNew3DS failed: 0x%08lX", (unsigned long)rc);
        return;
    }
    if (!isNew3ds) {
        if (host) host->log("[3DS] Old3DS detected. Speedup not enabled.");
        return;
    }

    osSetSpeedupEnable(true);

    u32 beforeLimit = 0;
    u32 afterLimit = 0;
    (void)APT_GetAppCpuTimeLimit(&beforeLimit);
    Result rcSet = APT_SetAppCpuTimeLimit(80);
    (void)APT_GetAppCpuTimeLimit(&afterLimit);

    if (host) {
        if (R_FAILED(rcSet)) {
            host->log("[3DS] N3DS speedup enabled; CPU time limit set failed: 0x%08lX (was %lu)",
                      (unsigned long)rcSet, (unsigned long)beforeLimit);
        } else {
            host->log("[3DS] N3DS speedup enabled; CPU time limit %lu -> %lu",
                      (unsigned long)beforeLimit, (unsigned long)afterLimit);
        }
    }
}
} // namespace

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ThreeDSHost *host = new ThreeDSHost();
    applyN3DSSpeedup(host);
    Real8VM *vm = new Real8VM(host);
    host->debugVMRef = vm;

    if (!vm->initMemory()) {
        delete vm;
        delete host;
        return 1;
    }

#if defined(REAL8_3DS_STANDALONE)
#if defined(REAL8_3DS_EMBED_CART)
    extern const unsigned char cart_blob_bin[];
    extern const unsigned char cart_blob_bin_end[];

    auto loadEmbeddedCartBlob = [](GameData& outData, std::string& err) -> bool {
        const size_t blobSize = (size_t)(cart_blob_bin_end - cart_blob_bin);
        if (blobSize < sizeof(CartBlobHeader)) {
            err = "Embedded cart blob is missing or too small.";
            return false;
        }

        CartBlobHeader header{};
        memcpy(&header, cart_blob_bin, sizeof(header));
        if (memcmp(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE) != 0) {
            err = "Embedded cart blob has invalid magic.";
            return false;
        }

        const size_t payloadSize = (size_t)header.raw_size;
        if (payloadSize < 0x4300) {
            err = "Embedded cart blob payload is too small.";
            return false;
        }
        if (payloadSize > (size_t)header.comp_size) {
            err = "Embedded cart blob payload exceeds slot capacity.";
            return false;
        }
        if (sizeof(CartBlobHeader) + payloadSize > blobSize) {
            err = "Embedded cart blob payload exceeds blob size.";
            return false;
        }

        const uint8_t* payload = cart_blob_bin + sizeof(CartBlobHeader);
        size_t offset = 0;
        memcpy(outData.gfx, payload + offset, sizeof(outData.gfx));
        offset += sizeof(outData.gfx);
        memcpy(outData.map, payload + offset, sizeof(outData.map));
        offset += sizeof(outData.map);
        memcpy(outData.sprite_flags, payload + offset, sizeof(outData.sprite_flags));
        offset += sizeof(outData.sprite_flags);
        memcpy(outData.music, payload + offset, sizeof(outData.music));
        offset += sizeof(outData.music);
        memcpy(outData.sfx, payload + offset, sizeof(outData.sfx));
        offset += sizeof(outData.sfx);

        if (offset > payloadSize) {
            err = "Embedded cart blob payload is malformed.";
            return false;
        }

        const size_t luaSize = payloadSize - offset;
        outData.lua_code.assign(reinterpret_cast<const char*>(payload + offset), luaSize);
        outData.lua_code_ptr = nullptr;
        outData.lua_code_size = 0;
        outData.cart_id.clear();
        return true;
    };
#endif

    auto loadFileRaw = [](const char* path) -> std::vector<uint8_t> {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        if (size <= 0) return {};
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer((size_t)size);
        if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return buffer;
        return {};
    };

    auto cartBaseName = [](const std::string& path) -> std::string {
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
    };

    auto renderError = [](Real8VM* vmPtr, const char* title, const char* detail) {
        if (!vmPtr) return;
        vmPtr->gpu.setMenuFont(true);
        vmPtr->gpu.cls(0);
        vmPtr->gpu.rectfill(0, 50, 127, 75, 8);
        vmPtr->gpu.pprint(title, (int)std::strlen(title), 4, 55, 7);
        vmPtr->gpu.pprint(detail, (int)std::strlen(detail), 4, 65, 7);
        vmPtr->gpu.setMenuFont(false);
        vmPtr->show_frame();
    };

    std::unique_ptr<GameData> gameData(new (std::nothrow) GameData());
    if (!gameData) {
        renderError(vm, "ERROR", "OUT OF MEMORY");
        while (aptMainLoop()) {
            host->pollInput();
            if (host->isExitComboHeld()) break;
            gspWaitForVBlank();
        }
        delete vm;
        delete host;
        return 1;
    }

#if defined(REAL8_3DS_EMBED_CART)
    std::string embedErr;
    if (!loadEmbeddedCartBlob(*gameData, embedErr)) {
        renderError(vm, "LOAD ERROR", embedErr.c_str());
        while (aptMainLoop()) {
            host->pollInput();
            if (host->isExitComboHeld()) break;
            gspWaitForVBlank();
        }
        delete vm;
        delete host;
        return 1;
    }

    vm->currentCartPath = "embedded";
    vm->currentGameId = "embedded";
#else
    const char* cartPath = REAL8_3DS_STANDALONE_CART;
    std::vector<uint8_t> fileData = loadFileRaw(cartPath);
    if (fileData.empty()) {
        renderError(vm, "LOAD ERROR", "CART NOT FOUND");
        while (aptMainLoop()) {
            host->pollInput();
            if (host->isExitComboHeld()) break;
            gspWaitForVBlank();
        }
        delete vm;
        delete host;
        return 1;
    }

    if (!Real8CartLoader::LoadFromBuffer(host, fileData, *gameData)) {
        renderError(vm, "LOAD ERROR", "INVALID CART");
        while (aptMainLoop()) {
            host->pollInput();
            if (host->isExitComboHeld()) break;
            gspWaitForVBlank();
        }
        delete vm;
        delete host;
        return 1;
    }

    std::vector<uint8_t>().swap(fileData);

    vm->currentCartPath = cartPath;
    vm->currentGameId = cartBaseName(cartPath);
#endif

    if (!vm->loadGame(*gameData)) {
        const char* errTitle = vm->lastErrorTitle[0] ? vm->lastErrorTitle : "VM ERROR";
        const char* errDetail = vm->lastErrorDetail[0] ? vm->lastErrorDetail : "EXECUTION FAILED";
        renderError(vm, errTitle, errDetail);
        while (aptMainLoop()) {
            host->pollInput();
            if (host->isExitComboHeld()) break;
            gspWaitForVBlank();
        }
        delete vm;
        delete host;
        return 1;
    }

    vm->gpu.pal_reset();
    host->setInterpolation(vm->interpolation);

    host->log("Real-8 3DS Standalone Started.");
    bool running = true;
    bool inMenu = false;
    bool inputLatch = false;
    Real8Gfx::GfxState menuGfxBackup;
    std::vector<std::string> inGameOptions;
    int inGameMenuSelection = 0;
    uint8_t top_screen_fb[Real8VM::RAW_WIDTH][Real8VM::RAW_WIDTH];
    std::memset(top_screen_fb, 0, sizeof(top_screen_fb));

    auto applyPauseCheckerboard = [&](uint8_t (*buffer)[Real8VM::RAW_WIDTH]) {
        for (int y = 0; y < Real8VM::RAW_WIDTH; ++y) {
            for (int x = 0; x < Real8VM::RAW_WIDTH; ++x) {
                if (((x ^ y) & 1) == 0) {
                    if ((buffer[y][x] & 0x0F) != 0) {
                        buffer[y][x] = 0;
                    }
                }
            }
        }
    };

    auto syncMenuInput = [&]() {
        for (int i = 0; i < 8; i++) {
            vm->btn_states[i] = host->getPlayerInput(i);
        }
        vm->btn_mask = vm->btn_states[0];

        for (int p = 0; p < 8; p++) {
            for (int b = 0; b < 7; b++) {
                if (vm->btn_states[p] & (1 << b)) {
                    if (vm->btn_counters[p][b] < 255) vm->btn_counters[p][b]++;
                } else {
                    vm->btn_counters[p][b] = 0;
                }
            }
        }
    };

    auto applyInputLatch = [&]() {
        if (!inputLatch) return;
        if (vm->btn_mask != 0) {
            for (int p = 0; p < 8; p++) {
                for (int b = 0; b < 6; b++) {
                    vm->btn_counters[p][b] = 0;
                }
            }
            vm->btn_mask = 0;
        } else {
            inputLatch = false;
        }
    };

    FrameStats frameStats;
    while (running && aptMainLoop()) {
        frameStats.beginFrame();
        host->pollInput();
        if (host->isExitComboHeld()) {
            running = false;
            break;
        }

        host->crt_filter = vm->crt_filter;
        if (vm->interpolation != host->interpolation) {
            host->setInterpolation(vm->interpolation);
        }

        if (inMenu) {
            vm->isShellUI = true;
            syncMenuInput();
            applyInputLatch();

            Real8Menu::InGameResult result =
                Real8Menu::UpdateInGameMenu(vm, host, inGameOptions, inGameMenuSelection, menuGfxBackup);

            if (result.requestInputLatch) inputLatch = true;

            bool closeMenu = false;
            bool exitApp = false;
            bool reloadGame = false;
            if (result.action == Real8Menu::InGameAction::Resume) {
                closeMenu = true;
            } else if (result.action == Real8Menu::InGameAction::ResetToLoading) {
                closeMenu = true;
                reloadGame = true;
            } else if (result.action == Real8Menu::InGameAction::ExitToBrowser) {
                exitApp = true;
            }

            Real8Menu::RenderInGameMenu(vm, host, inGameOptions, inGameMenuSelection, nullptr);
            vm->show_frame();

            if (reloadGame) {
                if (!vm->loadGame(*gameData)) {
                    const char* errTitle = vm->lastErrorTitle[0] ? vm->lastErrorTitle : "VM ERROR";
                    const char* errDetail = vm->lastErrorDetail[0] ? vm->lastErrorDetail : "EXECUTION FAILED";
                    renderError(vm, errTitle, errDetail);
                    running = false;
                }
            }

            if (closeMenu) {
                inMenu = false;
                vm->clearAltFramebuffer();
            }
            if (exitApp) {
                vm->quit_requested = true;
                running = false;
            }
        } else {
            vm->isShellUI = false;
            vm->runFrame();
            vm->show_frame();

            if (vm->quit_requested || vm->exit_requested) {
                running = false;
                break;
            }

            vm->btn_mask = vm->btn_states[0];
            applyInputLatch();

            if (vm->isMenuPressed()) {
                std::memcpy(top_screen_fb, vm->fb, sizeof(top_screen_fb));
                applyPauseCheckerboard(top_screen_fb);
                vm->setAltFramebuffer(&top_screen_fb[0][0], 128, 128);

                vm->gpu.saveState(menuGfxBackup);
                vm->gpu.reset();
                Real8Menu::BuildInGameMenu(vm, inGameOptions, inGameMenuSelection);
                inMenu = true;
            }
        }

        u64 workEndMs = osGetTime();
        if (!host->isFastForwardHeld()) {
            gspWaitForVBlank();
        }
        frameStats.endFrame(host, workEndMs);
    }

    delete vm;
    delete host;

    return 0;
#else
    Real8Shell *shell = new Real8Shell(host, vm);

    vm->gpu.pal_reset();
    host->setInterpolation(vm->interpolation);

    host->log("Real-8 3DS Port Started.");
    bool running = true;

    FrameStats frameStats;
    while (running && aptMainLoop()) {
        frameStats.beginFrame();
        // Poll input once per frame (pollInput internally avoids duplicate scans in the same ms).
        host->pollInput();
        if (host->isExitComboHeld()) {
            running = false;
            break;
        }

        host->crt_filter = vm->crt_filter;
        if (vm->interpolation != host->interpolation) {
            host->setInterpolation(vm->interpolation);
        }

        // Run exactly one emulation/update step per displayed frame.
        shell->update();
        if (vm->quit_requested) {
            running = false;
            break;
        }

        // Frame pacing: lock to VBlank unless fast-forward is held.
        u64 workEndMs = osGetTime();
        if (!host->isFastForwardHeld()) {
            gspWaitForVBlank();
        }
        frameStats.endFrame(host, workEndMs);
    }

    delete shell;
    delete vm;
    delete host;

    return 0;
#endif
}
