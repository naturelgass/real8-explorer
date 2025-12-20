#pragma once
#include <switch.h>
#include <vector>
#include <string>
#include <cstring>

// PICO-8 Button Constants
#define P8_KEY_LEFT 0
#define P8_KEY_RIGHT 1
#define P8_KEY_UP 2
#define P8_KEY_DOWN 3
#define P8_KEY_O 4    // Standard: Button A (East) or B (South)
#define P8_KEY_X 5    // Standard: Button B (South) or A (East)
#define P8_KEY_MENU 6 // Start/Select/Plus/Minus

struct PlayerConfig
{
    int assignedJoystickIndex; 
    u64 btnMap[7];

    PlayerConfig()
    {
        assignedJoystickIndex = -1;
        
        // Default Switch Pro Controller / JoyCon Layout
        btnMap[P8_KEY_LEFT] = HidNpadButton_Left;
        btnMap[P8_KEY_RIGHT] = HidNpadButton_Right;
        btnMap[P8_KEY_UP] = HidNpadButton_Up;
        btnMap[P8_KEY_DOWN] = HidNpadButton_Down;
        
        // PICO-8 'O' is usually the confirm button. 
        // On Nintendo, physical 'A' (Right) is confirm, but 'B' (Down) is often jump/action.
        // We map O -> B (South) and X -> A (East) for comfortable platforming.
        btnMap[P8_KEY_O] = HidNpadButton_B; 
        btnMap[P8_KEY_X] = HidNpadButton_A;
        
        btnMap[P8_KEY_MENU] = HidNpadButton_Plus; // Plus (+) Button
    }
};

class SwitchInput
{
private:
    PlayerConfig configs[8];
    bool hidReady = false;
    PadState pads[8];

    HidNpadIdType getControllerId(int idx) const
    {
        if (idx <= 0) return HidNpadIdType_No1;

        static const HidNpadIdType ids[] = {
            HidNpadIdType_No1,
            HidNpadIdType_No2,
            HidNpadIdType_No3,
            HidNpadIdType_No4,
            HidNpadIdType_No5,
            HidNpadIdType_No6,
            HidNpadIdType_No7,
            HidNpadIdType_No8
        };

        if (idx < (int)(sizeof(ids) / sizeof(ids[0]))) return ids[idx];
        return HidNpadIdType_No1;
    }

public:
    SwitchInput() {}
    ~SwitchInput() { if (hidReady) hidExit(); }

    void init()
    {
        Result rc = hidInitialize();
        if (R_FAILED(rc)) return;
        hidReady = true;

        padConfigureInput(8, HidNpadStyleSet_NpadStandard);

        // Default assignment: Controller 0 -> Player 0
        for(int i=0; i<8; i++) {
            configs[i].assignedJoystickIndex = i;
        }

        padInitialize(&pads[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
        for (int i = 1; i < 8; i++) {
            padInitialize(&pads[i], getControllerId(i));
        }
    }

    void update()
    {
        if (!hidReady) return;
        for (int i = 0; i < 8; i++) {
            padUpdate(&pads[i]);
        }
    }

    void clearState()
    {
        if (!hidReady) return;
        for (int i = 0; i < 8; i++) {
            padUpdate(&pads[i]);
        }
    }
    
    std::vector<uint8_t> serialize() {
        std::vector<uint8_t> data(sizeof(configs));
        memcpy(data.data(), configs, sizeof(configs));
        return data;
    }

    void deserialize(const std::vector<uint8_t>& data) {
        if (data.size() == sizeof(configs)) {
            memcpy(configs, data.data(), sizeof(configs));
        }
    }

    PlayerConfig* getConfig(int playerIdx) {
        if (playerIdx < 0 || playerIdx >= 8) return nullptr;
        return &configs[playerIdx];
    }
    
    uint32_t getMask(int playerIdx)
    {
        if (playerIdx < 0 || playerIdx >= 8) return 0;
        
        uint32_t mask = 0;
        
        if (!hidReady) return 0;
        
        // On Switch, P1 might also be handheld controls (Controller 0)
        int joyIdx = configs[playerIdx].assignedJoystickIndex;
        if (joyIdx < 0 || joyIdx >= 8) return 0;

        u64 held = padGetButtons(&pads[joyIdx]);
        PlayerConfig& cfg = configs[playerIdx];

        const int deadzone = 8000;
        auto applyAxis = [&](int v, int negKey, int posKey) {
            if (v < -deadzone) mask |= (1 << negKey);
            else if (v > deadzone) mask |= (1 << posKey);
        };

        HidAnalogStickState left = padGetStickPos(&pads[joyIdx], 0);
        HidAnalogStickState right = padGetStickPos(&pads[joyIdx], 1);
        applyAxis(left.x, P8_KEY_LEFT, P8_KEY_RIGHT);
        applyAxis(left.y, P8_KEY_DOWN, P8_KEY_UP);
        applyAxis(right.x, P8_KEY_LEFT, P8_KEY_RIGHT);
        applyAxis(right.y, P8_KEY_DOWN, P8_KEY_UP);

        for(int i=0; i<7; i++) {
            if (held & cfg.btnMap[i]) {
                mask |= (1 << i);
            }
        }

        return mask;
    }
};
