#include <SDL.h>
#include <exception>
#include <string>

#include "linux_host.hpp"
#include "../../core/real8_vm.h"
#include "../../core/real8_shell.h"
#include "../../core/real8_tools.h"

const int WINDOW_WIDTH = 512;
const int WINDOW_HEIGHT = 512;

static void ToggleFullscreen(SDL_Window *window)
{
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(window, 0);
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

static void QueueCartLoad(Real8VM *vm, LinuxHost *host, const std::string &path)
{
    if (!vm || path.empty()) return;

    vm->currentCartPath = path;
    vm->next_cart_path = path;

    size_t lastSlash = path.find_last_of("/\\");
    vm->currentGameId = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);

    vm->reset_requested = true;

    if (host) host->pushAudio(nullptr, 0);
}

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) return 1;

    SDL_Window *window = SDL_CreateWindow(
        "Real-8 Explorer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) return 1;

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!renderer) return 1;

    LinuxHost *host = new LinuxHost(renderer);
    Real8VM *vm = new Real8VM(host);
    Real8Shell *shell = new Real8Shell(host, vm);
    host->debugVMRef = vm;

    if (!vm->initMemory()) return 1;

    vm->gpu.pal_reset();
    host->setInterpolation(vm->interpolation);

    if (argc > 1 && argv[1]) {
        QueueCartLoad(vm, host, std::string(argv[1]));
    }

    SDL_StartTextInput();

    bool running = true;
    SDL_Event event;

    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 last = 0;
    double deltaTime = 0.0;
    double accumulator = 0.0;
    const double FIXED_STEP = 1.0 / 60.0;

    try {
        while (running)
        {
            last = now;
            now = SDL_GetPerformanceCounter();
            deltaTime = (double)(now - last) / SDL_GetPerformanceFrequency();
            if (deltaTime > 0.25) deltaTime = 0.25;
            accumulator += deltaTime;

            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_DROPFILE) {
                    char *droppedFile = event.drop.file;
                    QueueCartLoad(vm, host, std::string(droppedFile));
                    SDL_free(droppedFile);
                } else if (event.type == SDL_TEXTINPUT) {
                    const char *text = event.text.text;
                    if (text) {
                        for (size_t i = 0; text[i] != '\0'; ++i) {
                            vm->key_queue.push_back(std::string(1, text[i]));
                        }
                    }
                } else if (event.type == SDL_MOUSEWHEEL) {
                    int delta = event.wheel.y;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
                    if (delta != 0) vm->mouse_wheel_event = delta;
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                            ToggleFullscreen(window);
                        }
                    } else if (event.key.keysym.sym == SDLK_F11) {
                        ToggleFullscreen(window);
                    } else if (event.key.keysym.sym == SDLK_F12) {
                        host->takeScreenshot();
                        vm->gpu.renderMessage("SYSTEM", "SCREENSHOT SAVED", 6);
                        vm->show_frame();
                    }
                }
            }

            host->crt_filter = vm->crt_filter;
            if (vm->interpolation != host->interpolation) {
                host->setInterpolation(vm->interpolation);
            }

            while (accumulator >= FIXED_STEP)
            {
                shell->update();
                if (vm->quit_requested) {
                    running = false;
                    break;
                }
                accumulator -= FIXED_STEP;
            }

            if (accumulator < FIXED_STEP) {
                SDL_Delay(1);
            }
        }
    }
    catch (const std::exception &e) {
        host->log("[C++ EXCEPTION] %s", e.what());
    }

    SDL_StopTextInput();

    delete shell;
    delete vm;
    delete host;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
