#pragma once

#include "real8_vm.h"
#include <string>
#include <vector>

class IReal8Host;

namespace Real8Menu {

struct SettingsResult {
    bool openCredits = false;
    bool back = false;
    bool refreshList = false;
};

enum class InGameAction {
    None,
    Resume,
    ResetToLoading,
    ExitToBrowser
};

struct InGameResult {
    InGameAction action = InGameAction::None;
    bool requestInputLatch = false;
    bool refreshList = false;
};

using BackgroundDrawFn = void (*)(void* user, Real8VM* vm);

struct RenderHooks {
    BackgroundDrawFn drawBackground = nullptr;
    void* user = nullptr;
};

void BuildInGameMenu(Real8VM* vm, std::vector<std::string>& inGameOptions, int& inGameMenuSelection);
SettingsResult UpdateSettingsMenu(Real8VM* vm, IReal8Host* host, int& menuSelection);
InGameResult UpdateInGameMenu(Real8VM* vm,
                              IReal8Host* host,
                              std::vector<std::string>& inGameOptions,
                              int& inGameMenuSelection,
                              Real8Gfx::GfxState& menuGfxBackup);
void RenderSettingsMenu(Real8VM* vm, IReal8Host* host, int menuSelection, const RenderHooks* hooks);
void RenderInGameMenu(Real8VM* vm,
                      IReal8Host* host,
                      const std::vector<std::string>& inGameOptions,
                      int inGameMenuSelection,
                      const RenderHooks* hooks);
void RenderMessage(Real8VM* vm, const char* header, const char* msg, int color);

inline void RenderMessage(Real8VM* vm, const char* header, const std::string& msg, int color)
{
    RenderMessage(vm, header, msg.c_str(), color);
}

}
