#pragma once

#include <cstdint>
#include <pspctrl.h>

class PspInput
{
public:
    void init();
    void update();
    void clearState();
    uint32_t getMask(int playerIdx) const;
    bool isFastForwardHeld() const;

private:
    uint32_t mask = 0;
    uint32_t buttons = 0;
    int analogX = 128;
    int analogY = 128;
};
