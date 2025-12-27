#include "psp_input.h"

namespace {
constexpr int kAnalogCenter = 128;
constexpr int kAnalogDeadzone = 24;
}

void PspInput::init()
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    update();
}

void PspInput::update()
{
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);

    buttons = pad.Buttons;
    analogX = pad.Lx;
    analogY = pad.Ly;

    uint32_t m = 0;

    if (buttons & PSP_CTRL_LEFT) m |= (1u << 0);
    if (buttons & PSP_CTRL_RIGHT) m |= (1u << 1);
    if (buttons & PSP_CTRL_UP) m |= (1u << 2);
    if (buttons & PSP_CTRL_DOWN) m |= (1u << 3);

    int dx = analogX - kAnalogCenter;
    int dy = analogY - kAnalogCenter;
    if (dx < -kAnalogDeadzone) m |= (1u << 0);
    if (dx > kAnalogDeadzone) m |= (1u << 1);
    if (dy < -kAnalogDeadzone) m |= (1u << 2);
    if (dy > kAnalogDeadzone) m |= (1u << 3);

    if (buttons & (PSP_CTRL_CROSS | PSP_CTRL_SQUARE | PSP_CTRL_LTRIGGER)) m |= (1u << 4);
    if (buttons & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE | PSP_CTRL_RTRIGGER)) m |= (1u << 5);
    if (buttons & (PSP_CTRL_START | PSP_CTRL_SELECT)) m |= (1u << 6);

    mask = m;
}

void PspInput::clearState()
{
    mask = 0;
    buttons = 0;
}

uint32_t PspInput::getMask(int playerIdx) const
{
    if (playerIdx != 0) return 0;
    return mask;
}

bool PspInput::isFastForwardHeld() const
{
    return (buttons & PSP_CTRL_RTRIGGER) != 0;
}
