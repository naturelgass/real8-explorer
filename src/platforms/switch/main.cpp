#include <SDL.h>
#include <switch.h> // libnx
#include <iostream>

#include "switch_host.hpp"
#include "../../core/real8_vm.h"

#if defined(REAL8_SWITCH_STANDALONE)
#include "../../core/real8_menu.h"
#include "../../core/real8_cart.h"
#if defined(REAL8_SWITCH_EMBED_CART)
#include "cart_blob.h"
#include "cart_blob_bin.h"
#endif
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#else
#include "../../core/real8_shell.h"
#endif

// Switch Screen Dimensions
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;

static volatile bool g_requestInputReset = false;

static void AppletHookCallback(AppletHookType hook, void* param)
{
    (void)param;
    switch (hook) {
        case AppletHookType_OnResume:
        case AppletHookType_OnFocusState:
            g_requestInputReset = true;
            break;
        default:
            break;
    }
}

#if defined(REAL8_SWITCH_STANDALONE)
#ifndef REAL8_SWITCH_STANDALONE_CART
#define REAL8_SWITCH_STANDALONE_CART "romfs:/game.p8.png"
#endif

namespace {
std::vector<uint8_t> loadFileRaw(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer((size_t)size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return buffer;
    return {};
}

std::string cartBaseName(const std::string& path)
{
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

void renderError(Real8VM* vmPtr, const char* title, const char* detail)
{
    if (!vmPtr) return;
    vmPtr->gpu.setMenuFont(true);
    vmPtr->gpu.cls(0);
    vmPtr->gpu.rectfill(0, 50, 127, 75, 8);
    vmPtr->gpu.pprint(title, (int)std::strlen(title), 4, 55, 7);
    vmPtr->gpu.pprint(detail, (int)std::strlen(detail), 4, 65, 7);
    vmPtr->gpu.setMenuFont(false);
    vmPtr->show_frame();
}

#if defined(REAL8_SWITCH_EMBED_CART)
bool loadEmbeddedCartBlob(GameData& outData, uint32_t& outFlags, std::string& err)
{
    if (!cart_blob_bin || cart_blob_bin_size < sizeof(CartBlobHeader)) {
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
    if (sizeof(CartBlobHeader) + payloadSize > cart_blob_bin_size) {
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
    outFlags = header.flags;
    return true;
}

void applyEmbeddedFlags(Real8VM* vmPtr, SwitchHost* hostPtr, uint32_t flags)
{
    if (!vmPtr || !hostPtr) return;
    vmPtr->stretchScreen = (flags & CART_BLOB_FLAG_STRETCH) != 0;
    vmPtr->crt_filter = (flags & CART_BLOB_FLAG_CRTFILTER) != 0;
    vmPtr->interpolation = (flags & CART_BLOB_FLAG_INTERPOL8) != 0;
    hostPtr->setInterpolation(vmPtr->interpolation);
}
#endif
} // namespace
#endif

int main(int argc, char *argv[])
{
    // Receive HOME/sleep notifications while still getting focus callbacks.
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    AppletHookCookie appletHookCookie;
    appletHook(&appletHookCookie, AppletHookCallback, nullptr);

    // 1. Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        // Fallback logging using libnx if SDL fails
        consoleInit(NULL);
        printf("SDL Init Failed: %s\n", SDL_GetError());
        while(appletMainLoop()) {
            consoleUpdate(NULL);
        }
        return 1;
    }

    // 2. Create Window
    SDL_Window *window = SDL_CreateWindow(
        "Real-8 VM (Switch)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN); // Fullscreen is implied on Switch

    if (!window) return 1;

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return 1;

    // 3. Initialize Host and VM
    SwitchHost *host = new SwitchHost(renderer);
    PadState exitPad;
    padInitialize(&exitPad, HidNpadIdType_No1, HidNpadIdType_Handheld);
    Real8VM *vm = new Real8VM(host);
    host->debugVMRef = vm;

    if (!vm->initMemory()) {
        // Handle init failure
        delete vm;
        delete host;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if defined(REAL8_SWITCH_STANDALONE)
    auto waitForExit = [&]() {
        while (appletMainLoop()) {
            padUpdate(&exitPad);
            if (padGetButtonsDown(&exitPad) & HidNpadButton_Minus) {
                break;
            }
            svcSleepThread(1000000LL);
        }
    };

    GameData gameData;
    bool loadedCart = false;
    uint32_t embeddedFlags = 0;

#if defined(REAL8_SWITCH_EMBED_CART)
    {
        std::string embedErr;
        if (!loadEmbeddedCartBlob(gameData, embeddedFlags, embedErr)) {
            renderError(vm, "LOAD ERROR", embedErr.c_str());
            waitForExit();
            delete vm;
            delete host;
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        loadedCart = true;
        vm->currentCartPath = "embedded";
        vm->currentGameId = "embedded";
        applyEmbeddedFlags(vm, host, embeddedFlags);
    }
#endif

    if (!loadedCart) {
        const char* cartPath = REAL8_SWITCH_STANDALONE_CART;
        std::vector<uint8_t> fileData = loadFileRaw(cartPath);
        if (fileData.empty()) {
            renderError(vm, "LOAD ERROR", "CART NOT FOUND");
            waitForExit();
            delete vm;
            delete host;
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        if (!Real8CartLoader::LoadFromBuffer(host, fileData, gameData)) {
            renderError(vm, "LOAD ERROR", "INVALID CART");
            waitForExit();
            delete vm;
            delete host;
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        std::vector<uint8_t>().swap(fileData);
        vm->currentCartPath = cartPath;
        vm->currentGameId = cartBaseName(cartPath);
    }

    if (!vm->loadGame(gameData)) {
        const char* errTitle = vm->lastErrorTitle[0] ? vm->lastErrorTitle : "VM ERROR";
        const char* errDetail = vm->lastErrorDetail[0] ? vm->lastErrorDetail : "EXECUTION FAILED";
        renderError(vm, errTitle, errDetail);
        waitForExit();
        delete vm;
        delete host;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool inMenu = false;
    bool inputLatch = false;
    Real8Gfx::GfxState menuGfxBackup;
    std::vector<std::string> inGameOptions;
    int inGameMenuSelection = 0;

    auto syncMenuInput = [&]() {
        for (int i = 0; i < 8; i++) {
            vm->btn_states[i] = host->getPlayerInput(i);
        }
        vm->btn_mask = vm->btn_states[0];

        for (int p = 0; p < 8; p++) {
            for (int b = 0; b < 6; b++) {
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

    auto reloadGame = [&]() -> bool {
        if (!vm->loadGame(gameData)) {
            const char* errTitle = vm->lastErrorTitle[0] ? vm->lastErrorTitle : "VM ERROR";
            const char* errDetail = vm->lastErrorDetail[0] ? vm->lastErrorDetail : "EXECUTION FAILED";
            renderError(vm, errTitle, errDetail);
            waitForExit();
            return false;
        }
        return true;
    };
#else
    Real8Shell *shell = new Real8Shell(host, vm);
#endif

    vm->gpu.pal_reset();

    // Load initial settings
    host->setInterpolation(vm->interpolation);

    host->log(
#if defined(REAL8_SWITCH_STANDALONE)
        "Real-8 Switch Standalone Started."
#else
        "Real-8 Switch Port Started."
#endif
    );

    bool running = true;
    bool resetInputAfterResume = false;

    u64 now = armGetSystemTick();
    u64 last = now;
    const u64 tickFreq = armGetSystemTickFreq();
    double accumulator = 0.0;
    const double FIXED_STEP = 1.0 / 60.0;

    // 4. Main Loop
    while (running && appletMainLoop())
    {
        last = now;
        now = armGetSystemTick();
        double deltaTime = 0.0;
        if (tickFreq > 0) {
            deltaTime = (double)(now - last) / (double)tickFreq;
        }
        if (deltaTime > 0.25) deltaTime = 0.25;
        accumulator += deltaTime;

        padUpdate(&exitPad);
        if (padGetButtonsDown(&exitPad) & HidNpadButton_Minus) {
            running = false;
        }

        if (g_requestInputReset) {
            resetInputAfterResume = true;
            g_requestInputReset = false;
        }

        if (resetInputAfterResume) {
            vm->resetInputState();
            host->clearInputState();
            resetInputAfterResume = false;
        }

        // Keep host visual flags in sync with VM options
        host->crt_filter = vm->crt_filter;
        if (vm->interpolation != host->interpolation) {
            host->setInterpolation(vm->interpolation);
        }

        while (accumulator >= FIXED_STEP) {
#if defined(REAL8_SWITCH_STANDALONE)
            if (inMenu) {
                vm->isShellUI = true;
                host->pollInput();
                syncMenuInput();
                applyInputLatch();

                Real8Menu::InGameResult result =
                    Real8Menu::UpdateInGameMenu(vm, host, inGameOptions, inGameMenuSelection, menuGfxBackup);

                if (result.requestInputLatch) inputLatch = true;

                bool closeMenu = false;
                bool exitApp = false;
                bool reload = false;
                if (result.action == Real8Menu::InGameAction::Resume) {
                    closeMenu = true;
                } else if (result.action == Real8Menu::InGameAction::ResetToLoading) {
                    closeMenu = true;
                    reload = true;
                } else if (result.action == Real8Menu::InGameAction::ExitToBrowser) {
                    exitApp = true;
                }

                Real8Menu::RenderInGameMenu(vm, host, inGameOptions, inGameMenuSelection, nullptr);
                vm->show_frame();

                if (reload) {
                    if (!reloadGame()) {
                        running = false;
                        break;
                    }
                }
                if (closeMenu) {
                    inMenu = false;
                    vm->clearAltFramebuffer();
                }
                if (exitApp) {
                    vm->quit_requested = true;
                    running = false;
                    break;
                }
            } else {
                vm->isShellUI = false;
                vm->runFrame();
                vm->show_frame();
                if (vm->reset_requested) {
                    vm->reset_requested = false;
                    if (!reloadGame()) {
                        running = false;
                        break;
                    }
                }
                if (vm->quit_requested || vm->exit_requested) {
                    running = false;
                    break;
                }

                vm->btn_mask = vm->btn_states[0];
                applyInputLatch();

                if (vm->isMenuPressed()) {
                    vm->gpu.saveState(menuGfxBackup);
                    vm->gpu.reset();
                    Real8Menu::BuildInGameMenu(vm, inGameOptions, inGameMenuSelection);
                    inMenu = true;
                }
            }
#else
            shell->update();
            if (vm->quit_requested) {
                running = false;
                break;
            }
#endif
            accumulator -= FIXED_STEP;
        }

        if (accumulator < FIXED_STEP && !host->isFastForwardHeld()) {
            svcSleepThread(1000000LL);
        }
    }

    // Cleanup
#if !defined(REAL8_SWITCH_STANDALONE)
    delete shell;
#endif
    delete vm;
    delete host;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    appletUnhook(&appletHookCookie);

    return 0;
}
