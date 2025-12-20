#pragma once
#include <SDL.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#define P8_KEY_LEFT 0
#define P8_KEY_RIGHT 1
#define P8_KEY_UP 2
#define P8_KEY_DOWN 3
#define P8_KEY_O 4
#define P8_KEY_X 5
#define P8_KEY_MENU 6

// POD Struct for saving to disk
struct PlayerConfig
{
    int assignedJoystickIndex; // -1 = None/Keyboard (P1 only), 0+ = SDL Joystick Index
    SDL_GameControllerButton btnMap[7]; // Map PICO-8 action -> SDL Button
    SDL_Scancode keyMap[7]; // Map PICO-8 action -> SDL Scancode (keyboard)

    PlayerConfig()
    {
        assignedJoystickIndex = -1;
        // Default Xbox/Generic layout
        btnMap[P8_KEY_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        btnMap[P8_KEY_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        btnMap[P8_KEY_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP;
        btnMap[P8_KEY_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        btnMap[P8_KEY_O] = SDL_CONTROLLER_BUTTON_A;      // Z/C
        btnMap[P8_KEY_X] = SDL_CONTROLLER_BUTTON_B;      // X/V
        btnMap[P8_KEY_MENU] = SDL_CONTROLLER_BUTTON_START; // Enter/Esc
        for (int i = 0; i < 7; i++) keyMap[i] = SDL_SCANCODE_UNKNOWN;
    }
};

class WindowsInput
{
private:
    std::vector<SDL_GameController *> controllers;
    PlayerConfig configs[8];

public:
    WindowsInput() {}

    void init()
    {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
            return;
        scanControllers();
        
        // Default assignment: Controller 0 -> Player 0, etc.
        for(int i=0; i<8; i++) {
            configs[i].assignedJoystickIndex = i;
        }
    }

    void scanControllers()
    {
        // Close existing
        for (auto *c : controllers)
        {
            if (c) SDL_GameControllerClose(c);
        }
        controllers.clear();

        int num = SDL_NumJoysticks();
        for (int i = 0; i < num; i++)
        {
            if (SDL_IsGameController(i))
            {
                SDL_GameController *pad = SDL_GameControllerOpen(i);
                if (pad)
                {
                    controllers.push_back(pad);
                }
                else
                {
                    controllers.push_back(nullptr); // Placeholder to keep indices aligned
                }
            }
        }
    }

    void update()
    {
        SDL_PumpEvents();
    }

    void clearState()
    {
        SDL_PumpEvents();
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_ResetKeyboard();
#endif
        SDL_FlushEvents(SDL_KEYDOWN, SDL_KEYUP);
        SDL_FlushEvent(SDL_TEXTINPUT);
        SDL_FlushEvent(SDL_TEXTEDITING);
    }
    
    // --- Serialization Helpers ---
    std::vector<uint8_t> serialize() {
        std::vector<uint8_t> data;
        data.resize(sizeof(configs));
        memcpy(data.data(), configs, sizeof(configs));
        return data;
    }

    void deserialize(const std::vector<uint8_t>& data) {
        if (data.size() == sizeof(configs)) {
            memcpy(configs, data.data(), sizeof(configs));
            return;
        }

        struct LegacyPlayerConfig {
            int assignedJoystickIndex;
            SDL_GameControllerButton btnMap[7];
        };

        if (data.size() == sizeof(LegacyPlayerConfig) * 8) {
            const LegacyPlayerConfig* legacy = reinterpret_cast<const LegacyPlayerConfig*>(data.data());
            for (int i = 0; i < 8; i++) {
                configs[i].assignedJoystickIndex = legacy[i].assignedJoystickIndex;
                memcpy(configs[i].btnMap, legacy[i].btnMap, sizeof(configs[i].btnMap));
                for (int j = 0; j < 7; j++) configs[i].keyMap[j] = SDL_SCANCODE_UNKNOWN;
            }
        }
    }

    // --- Configuration Logic ---
    
    PlayerConfig* getConfig(int playerIdx) {
        if (playerIdx < 0 || playerIdx >= 8) return nullptr;
        return &configs[playerIdx];
    }
    
    // Returns bitmask for a specific player
    uint32_t getMask(int playerIdx)
    {
        if (playerIdx < 0 || playerIdx >= 8) return 0;
        
        uint32_t mask = 0;
        const Uint8 *state = SDL_GetKeyboardState(NULL);
        
        // P1 Fallback to Keyboard if no controller is explicitly overriding, 
        // OR if P1 is set to Keyboard explicitly (index -1)
        if (playerIdx == 0 || configs[playerIdx].assignedJoystickIndex == -1) 
        {
            // Standard PICO-8 Layout
            PlayerConfig& cfg = configs[playerIdx];
            if (cfg.keyMap[P8_KEY_LEFT] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_LEFT]]) mask |= (1 << P8_KEY_LEFT);
            } else if (state[SDL_SCANCODE_LEFT]) {
                mask |= (1 << P8_KEY_LEFT);
            }

            if (cfg.keyMap[P8_KEY_RIGHT] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_RIGHT]]) mask |= (1 << P8_KEY_RIGHT);
            } else if (state[SDL_SCANCODE_RIGHT]) {
                mask |= (1 << P8_KEY_RIGHT);
            }

            if (cfg.keyMap[P8_KEY_UP] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_UP]]) mask |= (1 << P8_KEY_UP);
            } else if (state[SDL_SCANCODE_UP]) {
                mask |= (1 << P8_KEY_UP);
            }

            if (cfg.keyMap[P8_KEY_DOWN] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_DOWN]]) mask |= (1 << P8_KEY_DOWN);
            } else if (state[SDL_SCANCODE_DOWN]) {
                mask |= (1 << P8_KEY_DOWN);
            }

            if (cfg.keyMap[P8_KEY_O] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_O]]) mask |= (1 << P8_KEY_O);
            } else if (state[SDL_SCANCODE_Z] || state[SDL_SCANCODE_C] || state[SDL_SCANCODE_N]) {
                mask |= (1 << P8_KEY_O);
            }

            if (cfg.keyMap[P8_KEY_X] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_X]]) mask |= (1 << P8_KEY_X);
            } else if (state[SDL_SCANCODE_X] || state[SDL_SCANCODE_V] || state[SDL_SCANCODE_M]) {
                mask |= (1 << P8_KEY_X);
            }

            if (cfg.keyMap[P8_KEY_MENU] != SDL_SCANCODE_UNKNOWN) {
                if (state[cfg.keyMap[P8_KEY_MENU]]) mask |= (1 << P8_KEY_MENU);
            } else if (state[SDL_SCANCODE_RETURN] || state[SDL_SCANCODE_P]) {
                mask |= (1 << P8_KEY_MENU);
            }
        }
        
        // Controller Input
        int joyIdx = configs[playerIdx].assignedJoystickIndex;
        if (joyIdx >= 0 && joyIdx < (int)controllers.size() && controllers[joyIdx])
        {
            SDL_GameController* pad = controllers[joyIdx];
            PlayerConfig& cfg = configs[playerIdx];
            
            for(int i=0; i<7; i++) {
                if (SDL_GameControllerGetButton(pad, cfg.btnMap[i])) {
                    mask |= (1 << i);
                }
            }
        }

        return mask;
    }
};
