#include "real8_menu.h"
#include "real8_tools.h"

#include <algorithm>
#include <cstring>

namespace {

static const int FONT_WIDTH = 5;
// 3DS: remember the user's skin setting while the in-game menu is open
static bool s_menuSavedShowSkinValid = false;
static bool s_menuSavedShowSkin = false;

int getScreenW(const Real8VM* vm)
{
    return (vm && vm->draw_w() > 0) ? vm->draw_w() : 128;
}

int getScreenH(const Real8VM* vm)
{
    return (vm && vm->draw_h() > 0) ? vm->draw_h() : 128;
}

int getCenteredX(const char* text, int screenW)
{
    int textLenInPixels = (int)std::strlen(text) * FONT_WIDTH;
    return (screenW / 2) - (textLenInPixels / 2);
}

bool isStereoMenuEnabled(const Real8VM* vm)
{
    if (!vm) return false;
    if (!vm->ram) return vm->stereoscopic;

    const uint8_t st_mode = vm->ram[0x5F81] & 0x03;
    const uint8_t st_flags = vm->ram[0x5F80];
    if (st_mode == 1) return (st_flags & 0x01) != 0;
    if (st_mode == 3) return vm->stereoscopic;
    return false;
}

void setStereoMenuEnabled(Real8VM* vm, bool enabled)
{
    if (!vm) return;
    if (vm->ram) {
        const uint8_t st_mode = vm->ram[0x5F81] & 0x03;
        if (st_mode == 1) {
            uint8_t st_flags = vm->ram[0x5F80];
            if (enabled) st_flags |= 0x01;
            else st_flags = (uint8_t)(st_flags & ~0x01);
            vm->ram[0x5F80] = st_flags;
        } else if (enabled) {
            vm->ram[0x5F81] = 3;
        }
    }
    vm->stereoscopic = enabled;
}

bool isRepoSupportedPlatform(const char* platform)
{
    return (std::strcmp(platform, "Windows") == 0) ||
        (std::strcmp(platform, "Linux") == 0) ||
        (std::strcmp(platform, "Switch") == 0) ||
        (std::strcmp(platform, "3DS") == 0);
}

bool isVblankMenuSupported(const Real8VM* vm)
{
    if (!vm) return false;
    const IReal8Host* host = vm->getHost();
    if (!host) return false;
    const char* platform = host->getPlatform();
    return (std::strcmp(platform, "Switch") == 0) ||
        (std::strcmp(platform, "3DS") == 0);
}

struct ScrollWindow {
    int firstVisible = 0;
    int visibleItems = 0;
};

ScrollWindow computeScrollWindow(int selection, int totalItems, int maxVisibleItems)
{
    ScrollWindow window{};
    window.visibleItems = std::min(maxVisibleItems, totalItems);
    if (totalItems <= window.visibleItems) {
        window.firstVisible = 0;
        return window;
    }

    window.firstVisible = selection - (window.visibleItems / 2);
    if (window.firstVisible < 0) window.firstVisible = 0;
    if (window.firstVisible > totalItems - window.visibleItems) {
        window.firstVisible = totalItems - window.visibleItems;
    }
    return window;
}

void drawScrollbar(Real8VM* vm, int trackX0, int trackY0, int trackX1, int trackY1,
                   int visibleItems, int totalItems, int firstVisible)
{
    if (totalItems <= visibleItems) return;

    vm->gpu.rectfill(trackX0, trackY0, trackX1, trackY1, 5);

    int trackH = (trackY1 - trackY0) + 1;
    int thumbH = std::max(3, (trackH * visibleItems) / totalItems);
    int maxThumbOffset = trackH - thumbH;
    int maxFirstVisible = totalItems - visibleItems;
    int thumbOffset = (maxFirstVisible > 0) ? (maxThumbOffset * firstVisible) / maxFirstVisible : 0;

    int thumbY0 = trackY0 + thumbOffset;
    int thumbY1 = thumbY0 + thumbH - 1;
    vm->gpu.rectfill(trackX0, thumbY0, trackX1, thumbY1, 7);
}

void drawMenuFrame(Real8VM* vm, int mx, int my, int mw, int mh, const char* title)
{
    vm->gpu.rectfill(mx, my, mx + mw, my + mh, 0);
    vm->gpu.rect(mx, my, mx + mw, my + mh, 1);
    vm->gpu.rectfill(mx, my, mx + mw, my + 9, 1);

    int screenW = getScreenW(vm);
    vm->gpu.pprint(title, (int)std::strlen(title), getCenteredX(title, screenW), my + 2, 6);
}

} // namespace

namespace Real8Menu {

void BuildInGameMenu(Real8VM* vm, std::vector<std::string>& inGameOptions, int& inGameMenuSelection)
{
    inGameOptions.clear();
    inGameOptions.push_back("CONTINUE");
    inGameOptions.push_back("RESET GAME");

    // Add Custom Items
    for (int i = 1; i <= 5; i++) {
        if (vm->custom_menu_items[i].active) {
            inGameOptions.push_back(vm->custom_menu_items[i].label);
        }
    }

    if (vm->hasState()) inGameOptions.push_back("LOAD STATE");
    inGameOptions.push_back("SAVE STATE");
    inGameOptions.push_back("MUSIC");
    inGameOptions.push_back("SFX");

    // [CHANGED] Keep the label simple; we handle the "ON/OFF" visually in render()
    if (isVblankMenuSupported(vm)) {
        inGameOptions.push_back("SKIP VBLANK");
    }
    inGameOptions.push_back("SHOW FPS");

    // Stereo/anaglyph rendering toggle (moved here from the Settings menu).
    inGameOptions.push_back("STEREO SCR");

    // Display-related toggles (moved here from the Settings menu).
    inGameOptions.push_back("STRETCH SCR");
    inGameOptions.push_back("CRT FILTER");
    inGameOptions.push_back("INTERPOL8");

    inGameOptions.push_back("EXIT");

    inGameMenuSelection = 0;
}

SettingsResult UpdateSettingsMenu(Real8VM* vm, IReal8Host* host, int& menuSelection)
{
    SettingsResult result{};

    const char* platform = host->getPlatform();
    bool repoSupport = isRepoSupportedPlatform(platform);
    bool is3ds = (std::strcmp(platform, "3DS") == 0);
    // Display options (STRETCH/CRT/INTERPOL8) were moved to the in-game pause menu.
    int menuMax = repoSupport ? 5 : 3;

    if (menuSelection > menuMax) menuSelection = menuMax;
    if (menuSelection < 0) menuSelection = 0;

    if (vm->btnp(2)) { menuSelection--; if (menuSelection < 0) menuSelection = menuMax; }
    if (vm->btnp(3)) { menuSelection++; if (menuSelection > menuMax) menuSelection = 0; }

    if (vm->btnp(5)) { // X (Action Button)
        bool changed = false;
        bool listRefresh = false;

        // Helper lambda to handle Skin Toggle Logic
        auto toggleSkin = [&]() {
            vm->showSkin = !vm->showSkin;
            if (vm->showSkin) {
                Real8Tools::LoadSkin(vm, host);
            } else if (std::strcmp(platform, "3DS") != 0) {
                host->clearWallpaper();
            }
            changed = true;
        };

        if (repoSupport) {
            if (is3ds) {
                switch (menuSelection) {
                case 0: vm->showRepoSnap = !vm->showRepoSnap; changed = true; break;
                case 1: toggleSkin(); break; // keep load/clear logic
                case 2: vm->showRepoGames = !vm->showRepoGames; changed = true; listRefresh = true; break;
                case 3: result.openCredits = true; break;
                case 4: vm->quit_requested = true; break;
                case 5: result.back = true; break;
                }
            } else {
                switch (menuSelection) {
                case 0: vm->showRepoSnap = !vm->showRepoSnap; changed = true; break;
                case 1: toggleSkin(); break; // keep load/clear logic
                case 2: vm->showRepoGames = !vm->showRepoGames; changed = true; listRefresh = true; break;
                case 3: result.openCredits = true; break;
                case 4: vm->quit_requested = true; break;
                case 5: result.back = true; break;
                }
            }
        } else {
            switch (menuSelection) {
            case 0: toggleSkin(); break;
            case 1: result.openCredits = true; break;
            case 2: vm->quit_requested = true; break;
            case 3: result.back = true; break;
            }
        }

        if (changed) {
            Real8Tools::SaveSettings(vm, host);
            // Force an update to the Windows Menu UI if running on Windows
            // so the checkmark stays in sync with the Shell
        }

        if (listRefresh) result.refreshList = true;
    }

    if (vm->btnp(4)) result.back = true; // O (Back)

    return result;
}

InGameResult UpdateInGameMenu(Real8VM* vm,
                              IReal8Host* host,
                              std::vector<std::string>& inGameOptions,
                              int& inGameMenuSelection,
                              Real8Gfx::GfxState& menuGfxBackup)
{
    InGameResult result{};

    if (vm->btnp(2)) { inGameMenuSelection--; if (inGameMenuSelection < 0) inGameMenuSelection = (int)inGameOptions.size() - 1; }
    if (vm->btnp(3)) { inGameMenuSelection++; if (inGameMenuSelection >= (int)inGameOptions.size()) inGameMenuSelection = 0; }

    // Volume Adjustment
    if (vm->btnp(0) || vm->btnp(1)) {
        std::string action = inGameOptions[inGameMenuSelection];
        int change = vm->btnp(1) ? 1 : -1;
        if (action == "MUSIC") {
            vm->volume_music = std::max(0, std::min(10, vm->volume_music + change));
            Real8Tools::SaveSettings(vm, host);
        } else if (action == "SFX") {
            vm->volume_sfx = std::max(0, std::min(10, vm->volume_sfx + change));
            Real8Tools::SaveSettings(vm, host);
        }
    }

    auto restoreSkinIfNeeded = [&]() {
        if (std::strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
            vm->showSkin = s_menuSavedShowSkin;
            s_menuSavedShowSkinValid = false;
        }
    };

    if (vm->btnp(5)) { // Action
        std::string action = inGameOptions[inGameMenuSelection];

        if (action == "CONTINUE") {
            restoreSkinIfNeeded();
            vm->gpu.restoreState(menuGfxBackup);
            result.action = InGameAction::Resume;
        }
        else if (action == "RESET GAME") {
            vm->rebootVM();
            restoreSkinIfNeeded();
            result.action = InGameAction::ResetToLoading;
        }
        else if (action == "SAVE STATE") {
            vm->gpu.restoreState(menuGfxBackup);
            vm->saveState();
            vm->gpu.reset();
            RenderMessage(vm, "SYSTEM", "STATE SAVED", 11);
            vm->show_frame();
            BuildInGameMenu(vm, inGameOptions, inGameMenuSelection);
        }
        else if (action == "LOAD STATE") {
            if (vm->loadState()) {
                RenderMessage(vm, "SYSTEM", "STATE LOADED", 12);
                vm->show_frame();
                restoreSkinIfNeeded();
                result.action = InGameAction::Resume;
            } else {
                RenderMessage(vm, "ERROR", "LOAD FAILED", 8);
                vm->show_frame();
            }
        }
        else if (action == "MUSIC") {
            vm->volume_music = (vm->volume_music > 0) ? 0 : 10;
            Real8Tools::SaveSettings(vm, host);
        }
        else if (action == "SFX") {
            vm->volume_sfx = (vm->volume_sfx > 0) ? 0 : 10;
            Real8Tools::SaveSettings(vm, host);
        }
        // Check for substring "SHOW FPS" and refresh menu
        else if (action.find("SHOW FPS") != std::string::npos) {
            vm->showStats = !vm->showStats;
            Real8Tools::SaveSettings(vm, host);
            int savedSel = inGameMenuSelection;
            BuildInGameMenu(vm, inGameOptions, inGameMenuSelection);
            inGameMenuSelection = savedSel;
        }
        else if (action == "SKIP VBLANK") {
            if (host) {
                const bool fastForwardEnabled = host->isFastForwardHeld();
                host->setFastForwardHeld(!fastForwardEnabled);
                Real8Tools::SaveSettings(vm, host);
            }
        }
        else if (action == "STEREO SCR") {
            const bool enabled = isStereoMenuEnabled(vm);
            setStereoMenuEnabled(vm, !enabled);
            Real8Tools::SaveSettings(vm, host);
        }
        else if (action == "STRETCH SCR") {
            vm->stretchScreen = !vm->stretchScreen;
            Real8Tools::SaveSettings(vm, host);
        }
        else if (action == "CRT FILTER") {
            vm->crt_filter = !vm->crt_filter;
            Real8Tools::SaveSettings(vm, host);
        }
        else if (action == "INTERPOL8") {
            vm->interpolation = !vm->interpolation;
            Real8Tools::SaveSettings(vm, host);
        }
        else if (action == "EXIT") {
            vm->forceExit();
            vm->resetInputState();
            restoreSkinIfNeeded();
            result.action = InGameAction::ExitToBrowser;
            result.requestInputLatch = true;
            result.refreshList = true;
        }
        else {
            // Custom Items
            for (int i = 1; i <= 5; i++) {
                if (vm->custom_menu_items[i].active && vm->custom_menu_items[i].label == action) {
                    vm->run_menu_item(i);
                    result.action = InGameAction::Resume;
                    break;
                }
            }
        }
    }

    if (vm->btnp(4)) {
        restoreSkinIfNeeded();
        vm->gpu.restoreState(menuGfxBackup);
        result.action = InGameAction::Resume;
    }

    return result;
}

void RenderSettingsMenu(Real8VM* vm, IReal8Host* host, int menuSelection, const RenderHooks* hooks)
{
    vm->gpu.setMenuFont(true);
    vm->gpu.cls(0);

    if (hooks && hooks->drawBackground) {
        hooks->drawBackground(hooks->user, vm);
    }

    const char* platform = host->getPlatform();
    bool repoSupport = isRepoSupportedPlatform(platform);
    bool is3ds = (std::strcmp(platform, "3DS") == 0);

    static const char* labels_repo_3ds[] = {
        "REPO PREVIEW", "SHOW SKIN", "REPO GAMES",
        "CREDITS", "EXIT REAL8", "BACK"};
    static const char* labels_repo[] = {
        "REPO PREVIEW", "SHOW SKIN", "REPO GAMES",
        "CREDITS", "EXIT REAL8", "BACK"};
    static const char* labels_no_repo[] = {
        "SHOW SKIN",
        "CREDITS", "EXIT REAL8", "BACK"};

    const char** labels = nullptr;
    int itemCount = 0;
    if (repoSupport) {
        labels = is3ds ? labels_repo_3ds : labels_repo;
        itemCount = is3ds ?
            (int)(sizeof(labels_repo_3ds) / sizeof(labels_repo_3ds[0])) :
            (int)(sizeof(labels_repo) / sizeof(labels_repo[0]));
    } else {
        labels = labels_no_repo;
        itemCount = (int)(sizeof(labels_no_repo) / sizeof(labels_no_repo[0]));
    }

    const int maxVisibleItems = 7;
    ScrollWindow window = computeScrollWindow(menuSelection, itemCount, maxVisibleItems);

    // Auto-size menu box based on visible item count
    int screenW = getScreenW(vm);
    int screenH = getScreenH(vm);
    int mw = 107;                       // keeps the old 10..117 horizontal layout
    int mh = (window.visibleItems * 11) + 16;  // same formula as pause menu
    int mx = (screenW - mw) / 2;
    int my = ((screenH - mh) / 2) - (is3ds ? 5 : 0);

    drawMenuFrame(vm, mx, my, mw, mh, "SETTINGS");

    // Settings values
    const char* val_repo_snap = vm->showRepoSnap ? "ON" : "OFF";
    const char* val_skin = vm->showSkin ? "ON" : "OFF";
    const char* val_repo_games = vm->showRepoGames ? "ON" : "OFF";

    // Render only the visible window
    for (int i = 0; i < window.visibleItems; i++) {
        int idx = window.firstVisible + i;
        int y = my + 15 + (i * 11);

        int textX = mx + 13;
        int arrowX = textX - 6;
        int labelCol = (idx == menuSelection) ? 7 : 6;

        if (idx == menuSelection) vm->gpu.pprint(">", 1, arrowX, y, 7);
        vm->gpu.pprint(labels[idx], (int)std::strlen(labels[idx]), textX, y, labelCol);

        // Determine optional value (right-aligned, leaving space for scrollbar)
        const char* valTxt = nullptr;
        bool active = false;

        if (repoSupport) {
            if (idx == 0) { valTxt = val_repo_snap; active = vm->showRepoSnap; }
            else if (idx == 1) { valTxt = val_skin; active = vm->showSkin; }
            else if (idx == 2) { valTxt = val_repo_games; active = vm->showRepoGames; }
        } else {
            if (idx == 0) { valTxt = val_skin; active = vm->showSkin; }
        }

        if (valTxt) {
            int txtW = (int)std::strlen(valTxt) * FONT_WIDTH;
            int valX = (mx + mw) - txtW - 10; // keep clear of border + scrollbar
            vm->gpu.pprint(valTxt, (int)std::strlen(valTxt), valX, y, active ? 11 : 8);
        }
    }

    drawScrollbar(vm, mx + mw - 6, my + 11, mx + mw - 4, my + mh - 4,
                  window.visibleItems, itemCount, window.firstVisible);

    vm->gpu.setMenuFont(false);
}

void RenderInGameMenu(Real8VM* vm,
                      IReal8Host* host,
                      const std::vector<std::string>& inGameOptions,
                      int inGameMenuSelection,
                      const RenderHooks* hooks)
{
    vm->gpu.setMenuFont(true);

    bool is3ds = (std::strcmp(host->getPlatform(), "3DS") == 0);
    if (is3ds) {
        vm->gpu.cls(0);
        if (hooks && hooks->drawBackground) {
            hooks->drawBackground(hooks->user, vm);
        }
    } else {
        // Keep game background on other platforms
        vm->gpu.fillp(0xA5A5);
        int screenW = getScreenW(vm);
        int screenH = getScreenH(vm);
        vm->gpu.rectfill(0, 0, screenW - 1, screenH - 1, 0);
        vm->gpu.fillp(0);
    }

    const int maxVisibleItems = 7;
    ScrollWindow window = computeScrollWindow(inGameMenuSelection, (int)inGameOptions.size(), maxVisibleItems);

    int screenW = getScreenW(vm);
    int screenH = getScreenH(vm);
    int mw = 100;
    int mh = (window.visibleItems * 11) + 16;
    int mx = (screenW - mw) / 2;
    int my = (screenH - mh) / 2 - (is3ds ? 8 : 0);

    drawMenuFrame(vm, mx, my, mw, mh, "PAUSED");

    auto drawRightStatus = [&](const char* status, int oy, int statusCol) {
        int txtW = (int)std::strlen(status) * FONT_WIDTH;
        int statusX = (mx + mw) - txtW - 10;
        vm->gpu.pprint(status, (int)std::strlen(status), statusX, oy, statusCol);
    };

    auto drawVolume = [&](int volume, int oy) {
        if (volume <= 0) {
            drawRightStatus("OFF", oy, 8);
            return;
        }
        for (int b = 0; b < 10; b++) {
            vm->gpu.pprint("|", 1, mx + mw - 45 + (b * 3), oy, (b < volume) ? 11 : 5);
        }
    };

    // Render only the visible window (scrollable when totalItems > maxVisibleItems)
    for (int i = 0; i < window.visibleItems; i++) {
        int idx = window.firstVisible + i;

        int oy = my + 15 + (i * 11);
        int ox = mx + 13;
        int col = (idx == inGameMenuSelection) ? 7 : 6;

        if (idx == inGameMenuSelection) vm->gpu.pprint(">", 1, ox - 6, oy, 7);
        vm->gpu.pprint(inGameOptions[idx].c_str(), inGameOptions[idx].length(), ox, oy, col);

        // Bars for volume (show OFF at 0)
        if (inGameOptions[idx] == "MUSIC") {
            drawVolume(vm->volume_music, oy);
        }
        else if (inGameOptions[idx] == "SFX") {
            drawVolume(vm->volume_sfx, oy);
        }
        else if (inGameOptions[idx] == "SKIP VBLANK") {
            const bool enabled = host ? host->isFastForwardHeld() : false;
            const char* status = enabled ? "ON" : "OFF";
            int statusCol = enabled ? 11 : 8;
            drawRightStatus(status, oy, statusCol);
        }
        // SHOW FPS status (visual ON/OFF)
        else if (inGameOptions[idx] == "SHOW FPS") {
            const char* status = vm->showStats ? "ON" : "OFF";
            int statusCol = vm->showStats ? 11 : 8; // 11=Green, 8=Red
            drawRightStatus(status, oy, statusCol);
        }
        else if (inGameOptions[idx] == "STEREO SCR") {
            const bool enabled = isStereoMenuEnabled(vm);
            const char* status = enabled ? "ON" : "OFF";
            int statusCol = enabled ? 11 : 8;
            drawRightStatus(status, oy, statusCol);
        }
        else if (inGameOptions[idx] == "STRETCH SCR") {
            const char* status = vm->stretchScreen ? "ON" : "OFF";
            int statusCol = vm->stretchScreen ? 11 : 8;
            drawRightStatus(status, oy, statusCol);
        }
        else if (inGameOptions[idx] == "CRT FILTER") {
            const char* status = vm->crt_filter ? "ON" : "OFF";
            int statusCol = vm->crt_filter ? 11 : 8;
            drawRightStatus(status, oy, statusCol);
        }
        else if (inGameOptions[idx] == "INTERPOL8") {
            const char* status = vm->interpolation ? "ON" : "OFF";
            int statusCol = vm->interpolation ? 11 : 8;
            drawRightStatus(status, oy, statusCol);
        }
    }

    drawScrollbar(vm, mx + mw - 6, my + 11, mx + mw - 4, my + mh - 4,
                  window.visibleItems, (int)inGameOptions.size(), window.firstVisible);

    vm->gpu.setMenuFont(false);
}

void RenderMessage(Real8VM* vm, const char* header, const char* msg, int color)
{
    vm->gpu.setMenuFont(true);
    vm->gpu.cls(0);
    int screenW = getScreenW(vm);
    int screenH = getScreenH(vm);
    int boxW = std::min(128, screenW);
    int boxH = 26;
    int boxX = (screenW - boxW) / 2;
    int boxY = (screenH - boxH) / 2;
    vm->gpu.rectfill(boxX, boxY, boxX + boxW - 1, boxY + boxH - 1, color);
    vm->gpu.pprint(header, (int)std::strlen(header), getCenteredX(header, screenW), boxY + 5, 7);
    vm->gpu.pprint(msg, (int)std::strlen(msg), getCenteredX(msg, screenW), boxY + 15, 7);
    vm->gpu.setMenuFont(false);
}

} // namespace Real8Menu

#if !defined(REAL8_3DS_STANDALONE)
#include "real8_shell.h"

void Real8Shell::buildInGameMenu()
{
    Real8Menu::BuildInGameMenu(vm, inGameOptions, inGameMenuSelection);
}

void Real8Shell::updateSettingsMenu()
{
    Real8Menu::SettingsResult result = Real8Menu::UpdateSettingsMenu(vm, host, menuSelection);
    if (result.openCredits) {
        sysState = STATE_CREDITS;
    }
    if (result.back) {
        sysState = STATE_BROWSER;
    }
    if (result.refreshList) {
        refreshGameList();
    }
}

void Real8Shell::updateInGameMenu()
{
    Real8Menu::InGameResult result = Real8Menu::UpdateInGameMenu(vm, host, inGameOptions, inGameMenuSelection, menu_gfx_backup);
    if (result.requestInputLatch) {
        inputLatch = true;
    }
    if (result.refreshList) {
        refreshGameList();
    }
    if (menu_bottom_override_active && host && std::strcmp(host->getPlatform(), "3DS") == 0 &&
        result.action != Real8Menu::InGameAction::None) {
        vm->applyBottomVideoMode(menu_saved_bottom_vmode_req, /*force=*/true);
        menu_bottom_override_active = false;
    }
    switch (result.action) {
    case Real8Menu::InGameAction::Resume:
        sysState = STATE_RUNNING;
        break;
    case Real8Menu::InGameAction::ResetToLoading:
        sysState = STATE_LOADING;
        break;
    case Real8Menu::InGameAction::ExitToBrowser:
        resetModeForShell();
        sysState = STATE_BROWSER;
        break;
    default:
        break;
    }
}

void Real8Shell::renderSettingsMenu()
{
    Real8Menu::RenderHooks hooks{};
    hooks.drawBackground = &Real8Shell::drawStarfieldHook;
    hooks.user = this;
    Real8Menu::RenderSettingsMenu(vm, host, menuSelection, &hooks);
}

void Real8Shell::renderInGameMenu()
{
    Real8Menu::RenderHooks hooks{};
    hooks.drawBackground = &Real8Shell::drawStarfieldHook;
    hooks.user = this;
    Real8Menu::RenderInGameMenu(vm, host, inGameOptions, inGameMenuSelection, &hooks);
}

void Real8Shell::renderMessage(const char* header, std::string msg, int color)
{
    Real8Menu::RenderMessage(vm, header, msg, color);
}

void Real8Shell::drawStarfieldHook(void* user, Real8VM* vm)
{
    (void)vm;
    Real8Shell* shell = static_cast<Real8Shell*>(user);
    shell->drawStarfield();
}

#endif
