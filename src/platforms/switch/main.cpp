#include <SDL.h>
#include <switch.h> // libnx
#include <iostream>

#include "switch_host.hpp"
#include "../../core/real8_vm.h"
#include "../../core/real8_shell.h"

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

    Real8Shell *shell = new Real8Shell(host, vm);

    vm->gpu.pal_reset();

    // Load initial settings
    host->setInterpolation(vm->interpolation);

    host->log("Real-8 Switch Port Started.");

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
            shell->update();
            if (vm->quit_requested) {
                running = false;
                break;
            }
            accumulator -= FIXED_STEP;
        }

        if (accumulator < FIXED_STEP) {
            svcSleepThread(1000000LL);
        }
    }

    // Cleanup
    delete shell;
    delete vm;
    delete host;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    appletUnhook(&appletHookCookie);

    return 0;
}
