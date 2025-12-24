#include "real8_shell.h"
#include "real8_compression.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <memory>
#include <lodePNG.h>
#include "real8_cart.h"
#include "real8_tools.h"

// --------------------------------------------------------------------------
// STATIC HELPERS
// --------------------------------------------------------------------------

static const int FONT_WIDTH = 5;
static const int SCREEN_CENTER_X = 64;

// 3DS: remember the user's skin setting while the in-game menu is open
static bool s_menuSavedShowSkinValid = false;
static bool s_menuSavedShowSkin = false;

static int getCenteredX(const char *text)
{
    int textLenInPixels = (int)strlen(text) * FONT_WIDTH;
    return SCREEN_CENTER_X - (textLenInPixels / 2);
}

static std::string formatBytes(size_t bytes)
{
    char buf[32];
    if (bytes < 1024)
        snprintf(buf, 32, "%d B", (int)bytes);
    else if (bytes < (1024 * 1024))
        snprintf(buf, 32, "%.2f KB", bytes / 1024.0);
    else
        snprintf(buf, 32, "%.2f MB", bytes / 1024.0 / 1024.0);
    return std::string(buf);
}

// --------------------------------------------------------------------------
// 3DS: Pause overlay effect (PICO-8-ish fillp(0xA5A5) checkerboard)
//
// The 3DS port uses a dedicated 128x128 buffer for the top screen while the
// in-game menu is open (STATE_INGAME_MENU). We apply a simple 50% checkerboard
// darken to that frozen frame to make it feel "paused".
// --------------------------------------------------------------------------
void Real8Shell::applyPauseCheckerboardToTop()
{
    // Classic checkerboard. fillp(0xA5A5) is essentially alternating pixels,
    // so parity works well as a lightweight approximation.
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            // Darken half the pixels by forcing them to color 0.
            // Keep existing black pixels as-is.
            if (((x ^ y) & 1) == 0) {
                if ((top_screen_fb[y][x] & 0x0F) != 0) {
                    top_screen_fb[y][x] = 0;
                }
            }
        }
    }
}

static void drawWrapped(IReal8Host* host, Real8VM* vm, const std::string& text, int x, int y, int color, int maxChars, int maxLines)
{
    std::string s = text;
    int line = 0;
    size_t pos = 0;

    while (pos < s.size() && line < maxLines) {
        // handle explicit newlines
        size_t nl = s.find('\n', pos);
        size_t end = (nl == std::string::npos) ? s.size() : nl;

        while (pos < end && line < maxLines) {
            size_t len = std::min((size_t)maxChars, end - pos);
            // try break on space
            size_t cut = len;
            if (pos + len < end) {
                size_t sp = s.rfind(' ', pos + len);
                if (sp != std::string::npos && sp > pos) cut = sp - pos;
            }
            std::string chunk = s.substr(pos, cut);
            while (!chunk.empty() && chunk[0] == ' ') chunk.erase(chunk.begin());

            vm->gpu.pprint(chunk.c_str(), chunk.size(), x, y + (line * 8), color);
            line++;

            pos += cut;
            while (pos < end && s[pos] == ' ') pos++;
        }

        if (nl != std::string::npos) pos = nl + 1;
    }
}

// Helper to extract value by key from a JSON object string
static std::string json_extract(const std::string &obj, const std::string &key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = obj.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = obj.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return "";

    size_t quoteStart = obj.find('\"', colonPos);
    if (quoteStart == std::string::npos) return "";

    size_t quoteEnd = obj.find('\"', quoteStart + 1);
    if (quoteEnd == std::string::npos) return "";

    return obj.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

// Helper to match RGB to PICO-8 Palette
static uint8_t find_closest_p8_color(uint8_t r, uint8_t g, uint8_t b)
{
    int min_dist = 1000000;
    uint8_t best_idx = 0;

    // Standard 0-15
    for (int i = 0; i < 16; i++)
    {
        int dr = r - Real8Gfx::PALETTE_RGB[i][0];
        int dg = g - Real8Gfx::PALETTE_RGB[i][1];
        int db = b - Real8Gfx::PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = i; }
    }
    // Extended 16-31 (Mapped to 128+)
    for (int i = 16; i < 32; i++)
    {
        int dr = r - Real8Gfx::PALETTE_RGB[i][0];
        int dg = g - Real8Gfx::PALETTE_RGB[i][1];
        int db = b - Real8Gfx::PALETTE_RGB[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) { min_dist = dist; best_idx = 128 + (i - 16); }
    }
    return best_idx;
}

// --------------------------------------------------------------------------
// SHELL IMPLEMENTATION
// --------------------------------------------------------------------------

Real8Shell::Real8Shell(IReal8Host* h, Real8VM* v) : host(h), vm(v)
{
    isSwitchPlatform = (strcmp(host->getPlatform(), "Switch") == 0);
    initStars();
    refreshGameList();
    sysState = STATE_BROWSER;
    
    // Clear preview buffer
    memset(preview_ram, 0, sizeof(preview_ram));
    memset(top_screen_fb, 0, sizeof(top_screen_fb));
}

Real8Shell::~Real8Shell()
{
    if (repoDownload.worker.joinable()) repoDownload.worker.join();
    if (previewDownload.worker.joinable()) previewDownload.worker.join();
    if (gameDownload.worker.joinable()) gameDownload.worker.join();
    // Host and VM are owned by Main/Libretro, not Shell
}

void Real8Shell::update()
{
    // 1. Poll Hardware
    host->pollInput();
    updateAsyncDownloads();

    if (strcmp(host->getPlatform(), "3DS") == 0) {

        if (sysState == STATE_RUNNING) {
            // Normal gameplay: single framebuffer (both screens show the game)
            vm->clearAltFramebuffer();
        }
        else if (sysState == STATE_INGAME_MENU) {
            // In-game menu: keep the frozen game frame on the TOP screen
            // (top_screen_fb is filled when the menu is opened)
            vm->setAltFramebuffer(top_screen_fb);
        }
        else if (sysState != STATE_BROWSER) {
            // Other menus: top screen uses the dedicated buffer
            memset(top_screen_fb, 0, sizeof(top_screen_fb));
            vm->setAltFramebuffer(top_screen_fb);
        }
    }



    // 2. Sync Input to VM
    // [FIX] Only perform manual sync if the VM is NOT running.
    // When STATE_RUNNING, vm->runFrame() now handles input and counters internally.
    // Doing it here too causes double-counting (breaking btnp) and overwrites history.
    if (sysState != STATE_RUNNING) {
        for(int i=0; i<8; i++) {
            vm->btn_states[i] = host->getPlayerInput(i);
        }
        vm->btn_mask = vm->btn_states[0];

        // Update Counters for btnp() logic
        for (int p = 0; p < 8; p++) {
            for (int b = 0; b < 7; b++) { 
                if (vm->btn_states[p] & (1 << b)) {
                    if (vm->btn_counters[p][b] < 255) vm->btn_counters[p][b]++; 
                } else {
                    vm->btn_counters[p][b] = 0;
                }
            }
        }
    }
    else {
        // [FIX] When running, we still need btn_mask valid for the isMenuPressed() check below.
        // We use the state from the previous frame, which is fine (1 frame latency).
        vm->btn_mask = vm->btn_states[0];
    }
    
    if (inputLatch) {
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
    }

    // Host-driven loads (e.g., Windows menu/drag & drop) while not already in a game
    if (!vm->next_cart_path.empty() && sysState != STATE_RUNNING && sysState != STATE_LOADING) {
        targetGame = {};
        targetGame.path = vm->next_cart_path;
        size_t slash = targetGame.path.find_last_of("/\\");
        targetGame.displayName = (slash == std::string::npos) ? targetGame.path : targetGame.path.substr(slash + 1);
        targetGame.isRemote = false;
        targetGame.isFolder = false;
        targetGame.isFavorite = false;

        sysState = STATE_LOADING;
        vm->reset_requested = false; // Shell will drive the load now
    }

    // 3. State Machine
    switch (sysState) {

        case STATE_BOOT:
            sysState = STATE_BROWSER;
            break;

        case STATE_BROWSER:
            updateBrowser();
            renderFileList();
            vm->show_frame();
            break;

        case STATE_OPTIONS_MENU:
            updateOptionsMenu();
            renderOptionsMenu();
            vm->show_frame();
            break;

        case STATE_PREVIEW_VIEW: {
            const char* platform = host->getPlatform();
            bool normalMenu =
                (strcmp(platform, "Windows") == 0) ||
                (strcmp(platform, "3DS") == 0) ||
                (strcmp(platform, "Switch")  == 0);

            if (targetGame.path != lastPreviewPath) {
                lastPreviewPath = targetGame.path;
                loadPreviewForEntry(targetGame, normalMenu, true, true);
            }

            vm->gpu.cls(0);
            vm->gpu.setMenuFont(true);

            bool previewPending = isSwitchPlatform && isPreviewDownloadActiveFor(targetGame.path);
            if (has_preview) {
                drawPreview(0, 0, false);
            } else if (previewPending) {
                const char *fetching = "FETCHING PREVIEW";
                vm->gpu.pprint(fetching, strlen(fetching), getCenteredX(fetching), 60, 11);
            } else {
                const char *noprevMsg = "NO PREVIEW DATA";
                vm->gpu.pprint(noprevMsg, strlen(noprevMsg), getCenteredX(noprevMsg), 60, 8);
            }

            vm->gpu.rectfill(0, 118, 128, 128, 1);
            const char *pressxMsg = isSwitchPlatform ? "PRESS A TO START" : "PRESS X TO START";
            vm->gpu.pprint(pressxMsg, strlen(pressxMsg), getCenteredX(pressxMsg), 120, 7);    
            vm->show_frame();

            vm->gpu.setMenuFont(false);

            if (vm->btnp(5)) {
                vm->resetInputState();
                sysState = STATE_LOADING; // X
            }
            if (vm->btnp(0) || vm->btnp(4)) {
                vm->resetInputState();
                inputLatch = true;
                sysState = STATE_BROWSER; // Left or O
            }
            break;
        }

        case STATE_SETTINGS:
            updateSettingsMenu();
            renderSettingsMenu();
            vm->show_frame();
            break;

        case STATE_STORAGE_INFO:
            renderStorageView();
            vm->show_frame();
            if (vm->btnp(5) || vm->isMenuPressed()) sysState = STATE_SETTINGS;
            break;

        case STATE_CREDITS:
            renderCredits();
            vm->show_frame();
            if (vm->btnp(5) || vm->btnp(4) || vm->isMenuPressed()) sysState = STATE_SETTINGS;
            break;
        case STATE_WIFI_INFO:
        {
            NetworkInfo net = host->getNetworkInfo();
            drawWifiScreen(net.connected ? "CONNECTED" : "DISCONNECTED",
                        net.ip, net.statusMsg, net.transferProgress);
            vm->show_frame();
            if (vm->isMenuPressed() || vm->btnp(4)) sysState = STATE_SETTINGS;
            break;
        }

        case STATE_LOADING:
            updateLoading();
            break;

        case STATE_RUNNING:
            if (vm->isMenuPressed()) {

                // 3DS: freeze the current game frame to the TOP screen buffer
                if (strcmp(host->getPlatform(), "3DS") == 0) {
                    memcpy(top_screen_fb, vm->fb, sizeof(top_screen_fb));

                    // Apply paused overlay effect (checkerboard like fillp(0xA5A5))
                    applyPauseCheckerboardToTop();

                    vm->setAltFramebuffer(top_screen_fb);
                }

                vm->gpu.saveState(menu_gfx_backup);
                vm->gpu.reset();
                buildInGameMenu();
                sysState = STATE_INGAME_MENU;
            }
            else {
                vm->runFrame();
                vm->show_frame();

                // Check if Game requested exit/reset
                if (vm->exit_requested) {
                    vm->exit_requested = false;
                    vm->forceExit();
                    vm->resetInputState();
                    sysState = STATE_BROWSER;
                    refreshGameList();
                }
                if (vm->reset_requested) {
                    // vm->runFrame handles internal Lua reset logic usually,
                    // but if it propagates up here:
                    vm->rebootVM();
                    if (!vm->next_cart_path.empty()) {
                        targetGame.path = vm->next_cart_path;
                        sysState = STATE_LOADING;
                    } else {
                        // Reload current
                        sysState = STATE_LOADING;
                    }
                    vm->reset_requested = false;
                }
            }
            break;

        case STATE_INGAME_MENU:
            updateInGameMenu();
            renderInGameMenu();
            vm->show_frame();
            break;

        case STATE_ERROR: {
            vm->gpu.setMenuFont(true);
            vm->gpu.cls(0);

            vm->gpu.rectfill(0, 12, 127, 30, 8);
            vm->gpu.pprint(errorTitle.c_str(), errorTitle.size(), getCenteredX(errorTitle.c_str()), 16, 7);

            std::string detail = shellErrorMsg;
            if (vm->hasLastError) {
                // Prefer VM detail if available
                errorTitle = vm->lastErrorTitle;
                detail = vm->lastErrorDetail;
            }

            drawWrapped(host, vm, detail, 4, 38, 7, 24, 8);

            vm->gpu.pprint("B OR X TO GO BACK", 16, getCenteredX("B OR X TO GO BACK"), 118, 6);
            vm->gpu.setMenuFont(false);

            vm->show_frame();

            if (vm->btnp(4) || vm->btnp(5)) {
                vm->forceExit();
                vm->resetInputState();
                sysState = STATE_BROWSER;
            }
            break;
        }
    }

}

void Real8Shell::startAsyncDownload(AsyncDownload &task, const std::string &url, const std::string &path)
{
    if (task.active) return;
    if (task.worker.joinable()) task.worker.join();

    task.url = url;
    task.path = path;
    task.done = false;
    task.success = false;
    task.active = true;

    std::string urlCopy = url;
    std::string pathCopy = path;
    task.worker = std::thread([this, &task, urlCopy, pathCopy]() {
        bool ok = host->downloadFile(urlCopy.c_str(), pathCopy.c_str());
        task.success = ok;
        task.done = true;
        task.active = false;
    });
}

bool Real8Shell::isPreviewDownloadActiveFor(const std::string &url) const
{
    return previewDownload.active && previewDownload.url == url;
}

void Real8Shell::updateAsyncDownloads()
{
    if (!isSwitchPlatform) return;

    if (repoDownload.done) {
        if (repoDownload.worker.joinable()) repoDownload.worker.join();
        bool ok = repoDownload.success;
        repoDownload.done = false;
        repoDownload.success = false;
        if (ok && vm->showRepoGames) {
            pendingRepoRefresh = true;
        }
    }

    if (previewDownload.done) {
        if (previewDownload.worker.joinable()) previewDownload.worker.join();
        bool ok = previewDownload.success;
        previewDownload.done = false;
        previewDownload.success = false;

        if (ok) {
            std::vector<uint8_t> data = host->loadFile(previewDownload.path.c_str());
            if (!data.empty()) {
                previewCache[previewDownload.url] = data;
                if (fileSelection >= 0 && fileSelection < (int)gameList.size() &&
                    gameList[fileSelection].path == previewDownload.url) {
                    loadPreview(data.data(), data.size());
                }
            }
        }
        host->deleteFile(previewDownload.path.c_str());

        if (!pendingPreviewUrl.empty() && !previewDownload.active) {
            std::string nextUrl = pendingPreviewUrl;
            pendingPreviewUrl.clear();
            startAsyncDownload(previewDownload, nextUrl, "/temp_preview.png");
        }
    }

    if (pendingRepoRefresh) {
        if (sysState != STATE_RUNNING && sysState != STATE_LOADING && sysState != STATE_INGAME_MENU) {
            refreshGameList();
            pendingRepoRefresh = false;
        }
    }
}

// --------------------------------------------------------------------------
// STATE UPDATES
// --------------------------------------------------------------------------

bool Real8Shell::shouldShowPreviewForEntry(const GameEntry &e) const
{
    if (e.isFolder) return false;
    return e.isRemote ? vm->showRepoSnap : true;
}

bool Real8Shell::loadPreviewForEntry(GameEntry &e, bool normalMenu, bool allowFetch, bool showFetchMsg)
{
    if (e.isFolder) {
        clearPreview();
        return false;
    }

    auto cached = previewCache.find(e.path);
    if (cached != previewCache.end() && !cached->second.empty()) {
        loadPreview(cached->second.data(), cached->second.size());
        return has_preview;
    }

    if (!allowFetch) {
        clearPreview();
        return false;
    }

    std::vector<uint8_t> data;
    if (!e.isRemote) {
        data = host->loadFile(e.path.c_str());
    } else if (normalMenu) {
        if (!e.cacheData.empty()) {
            data = e.cacheData;
        } else if (isSwitchPlatform) {
            if (!previewDownload.active) {
                startAsyncDownload(previewDownload, e.path, "/temp_preview.png");
            } else if (previewDownload.url != e.path) {
                pendingPreviewUrl = e.path;
            }
            clearPreview();
            return false;
        } else {
            std::string tempPath = "/temp_preview.png";
            if (showFetchMsg) {
                vm->gpu.setMenuFont(true);
                vm->gpu.rectfill(0, 120, 128, 128, 1);
                std::string fetchMsg = "FETCHING GAME";
                vm->gpu.pprint(fetchMsg.c_str(), fetchMsg.length(), getCenteredX(fetchMsg.c_str()), 121, 6);
                vm->show_frame();
                vm->gpu.setMenuFont(false);
            }

            if (host->downloadFile(e.path.c_str(), tempPath.c_str())) {
                data = host->loadFile(tempPath.c_str());
                host->deleteFile(tempPath.c_str());
            }
        }
    }

    if (!data.empty()) {
        auto &preview = previewCache[e.path];
        preview = std::move(data);
        loadPreview(preview.data(), preview.size());
        e.cacheData = preview;
        return has_preview;
    }

    clearPreview();
    return false;
}

void Real8Shell::updateBrowser()
{
    const char* platform = host->getPlatform();
    bool normalMenu =
        (strcmp(platform, "Windows") == 0) ||
        (strcmp(platform, "3DS") == 0) ||
        (strcmp(platform, "Switch")  == 0);

    static bool lastRepoSnapState = vm->showRepoSnap;
    if (vm->showRepoSnap != lastRepoSnapState) {
        lastFileSelection = -1; // force preview reload when snaps are re-enabled
        if (gameList.empty() || fileSelection < 0 || fileSelection >= (int)gameList.size() || !shouldShowPreviewForEntry(gameList[fileSelection])) {
            clearPreview();
        }
        lastRepoSnapState = vm->showRepoSnap;
    }

    // Allow backing out of empty folders instead of trapping the user
    if (gameList.empty()) {
        if (vm->btnp(4) && !current_vfs_path.empty()) { // O -> Back
            std::string lastFolder = current_vfs_path;
            current_vfs_path = "";
            refreshGameList(lastFolder);
            return;
        }
        if (vm->isMenuPressed()) {
            menuSelection = 0;
            sysState = STATE_SETTINGS;
        }
        return;
    }

    // Navigation
    if (vm->btnp(2)) { // UP
        fileSelection--;
        if (fileSelection < 0) fileSelection = gameList.size() - 1;
    }
    if (vm->btnp(3)) { // DOWN
        fileSelection++;
        if (fileSelection >= gameList.size()) fileSelection = 0;
    }

    // Preview Loader
    if (fileSelection != lastFileSelection) {
        lastFileSelection = fileSelection;
        GameEntry &e = gameList[fileSelection];

        // Skip preview loading when snaps are hidden or the entry is a directory
        bool shouldLoadPreview = shouldShowPreviewForEntry(e);
        loadPreviewForEntry(e, normalMenu, shouldLoadPreview, true);
    }

    // Interactions
    if (vm->btnp(0) && !gameList[fileSelection].isFolder) { // LEFT -> Options
        targetGame = gameList[fileSelection];
        buildContextMenu();
        sysState = STATE_OPTIONS_MENU;
    }

    if (vm->btnp(1) && !gameList[fileSelection].isFolder) { // RIGHT -> Preview
        targetGame = gameList[fileSelection];
        lastPreviewPath.clear();
        sysState = STATE_PREVIEW_VIEW;
    }

    if (vm->btnp(5)) { // X -> Select
        targetGame = gameList[fileSelection];
        if (targetGame.isFolder) {
            current_vfs_path = targetGame.path; 
            refreshGameList();
        } else {
            sysState = STATE_LOADING;
        }
    }

    if (vm->btnp(4)) { // O -> Back
        if (current_vfs_path != "") {
            std::string lastFolder = current_vfs_path;
            current_vfs_path = "";
            refreshGameList(lastFolder); 
        }
    }

    if (vm->isMenuPressed()) {
        menuSelection = 0;
        sysState = STATE_SETTINGS;
    }
}

void Real8Shell::buildContextMenu()
{
    contextOptions.clear();
    contextOptions.push_back("LAUNCH");
    contextOptions.push_back(targetGame.isFavorite ? "UNFAVORITE" : "FAVORITE");

    if (targetGame.isRemote) {
        contextOptions.push_back("DOWNLOAD");
    } else {
        contextOptions.push_back("RENAME"); 
        contextOptions.push_back("DELETE");
    }
    contextOptions.push_back("BACK");
    contextSelection = 0;
}

void Real8Shell::updateOptionsMenu()
{
    if (vm->btnp(2)) { contextSelection--; if (contextSelection < 0) contextSelection = contextOptions.size() - 1; }
    if (vm->btnp(3)) { contextSelection++; if (contextSelection >= contextOptions.size()) contextSelection = 0; }
    
    if (vm->btnp(4)) sysState = STATE_BROWSER; // Back

    if (vm->btnp(5)) { // Action
        std::string action = contextOptions[contextSelection];

        if (action == "LAUNCH") sysState = STATE_LOADING;
        else if (action == "FAVORITE" || action == "UNFAVORITE") {
            toggleFavorite(targetGame.path);
            refreshGameList();
            sysState = STATE_BROWSER;
        }
        else if (action == "PREVIEW") {
            lastPreviewPath.clear();
            sysState = STATE_PREVIEW_VIEW;
        }
        else if (action == "RENAME") {
            if (host->renameGameUI(targetGame.path.c_str())) {
                refreshGameList();
                sysState = STATE_BROWSER;
            }
        }
        else if (action == "DELETE") {
            if (targetGame.isRemote) deleteRemoteGameEntry(targetGame.path);
            else host->deleteFile(targetGame.path.c_str());
            refreshGameList();
            sysState = STATE_BROWSER;
        }
        else if (action == "DOWNLOAD") {
            renderMessage("DOWNLOADING", "PLEASE WAIT...", 1);
            vm->show_frame();
            std::string filename = targetGame.path.substr(targetGame.path.find_last_of('/') + 1);
            if (host->downloadFile(targetGame.path.c_str(), ("/" + filename).c_str())) {
                refreshGameList();
                sysState = STATE_BROWSER;
            } else {
                shellErrorMsg = "DOWNLOAD FAILED";
                errorTitle = "ERROR";
                sysState = STATE_ERROR;
            }
        }
        else if (action == "BACK") sysState = STATE_BROWSER;
    }
}

void Real8Shell::updateSettingsMenu()
{   
    const char* platform = host->getPlatform();
    bool normalMenu =
        (strcmp(platform, "Windows") == 0) ||
        (strcmp(platform, "3DS") == 0) ||
        (strcmp(platform, "Switch")  == 0);

    int menuMax = normalMenu ? 8 : 7; 

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
            } else if (strcmp(platform, "3DS") != 0) {
                host->clearWallpaper();
            }
            changed = true;
        };

        if (normalMenu) {
            // Windows indices
            switch (menuSelection) {
            case 0: vm->showRepoSnap = !vm->showRepoSnap; changed = true; break;
            case 1: toggleSkin(); break; // [FIXED] Now calls load/clear logic
            case 2: vm->showRepoGames = !vm->showRepoGames; changed = true; listRefresh = true; break;
            case 3: vm->stretchScreen = !vm->stretchScreen; changed = true; break;
            case 4:
                vm->crt_filter = !vm->crt_filter;
                changed = true;
                break;
            case 5:
                vm->interpolation = !vm->interpolation;
                changed = true;
                break;
            case 6: sysState = STATE_CREDITS; break;
            case 7: vm->quit_requested = true; break;
            case 8: sysState = STATE_BROWSER; break;
            }
        } else {
            // Standard indices
            switch (menuSelection) {
            case 0: sysState = STATE_STORAGE_INFO; break;
            case 1: vm->showRepoSnap = !vm->showRepoSnap; changed = true; break;
            case 2: toggleSkin(); break; // [FIXED] Now calls load/clear logic
            case 3: vm->showRepoGames = !vm->showRepoGames; changed = true; listRefresh = true; break;
            case 4: vm->stretchScreen = !vm->stretchScreen; changed = true; break;
            case 5: sysState = STATE_WIFI_INFO; break;
            case 6: sysState = STATE_CREDITS; break;
            case 7: sysState = STATE_BROWSER; break;
            }
        }
        
        if (changed) {
            Real8Tools::SaveSettings(vm, host);
            // Force an update to the Windows Menu UI if running on Windows
            // so the checkmark stays in sync with the Shell
        }
        
        if (listRefresh) refreshGameList();
    }
    if (vm->btnp(4)) sysState = STATE_BROWSER; // O (Back)
}

void Real8Shell::updateLoading()
{
    vm->gpu.cls(0);

    // Preserve the originally selected cart path so we don't lose it if we download to cache.
    std::string sourcePath = targetGame.path;

    if (targetGame.isRemote) {
        renderMessage("DOWNLOADING", "FETCHING...", 1);
        vm->show_frame();
        const char *cachePath = "/cache.p8.png";
        if (isSwitchPlatform) {
            if (!gameDownload.active && !gameDownload.done) {
                startAsyncDownload(gameDownload, targetGame.path, cachePath);
            }
            if (!gameDownload.done) return;

            if (gameDownload.worker.joinable()) gameDownload.worker.join();
            bool ok = gameDownload.success;
            gameDownload.done = false;
            gameDownload.success = false;

            if (ok) {
                targetGame.path = cachePath;
            } else {
                shellErrorMsg = "FETCH FAILED";
                errorTitle = "ERROR";
                sysState = STATE_ERROR;
                return;
            }
        } else {
            if (host->downloadFile(targetGame.path.c_str(), cachePath)) {
                targetGame.path = cachePath;
            } else {
                shellErrorMsg = "FETCH FAILED";
                errorTitle = "ERROR";
                sysState = STATE_ERROR;
                return;
            }
        }
    }

    // 3DS: Free network buffers BEFORE we parse/load the cart.
    if (strcmp(host->getPlatform(), "3DS") == 0) {
        host->setNetworkActive(false);

        // Release menu/preview caches before parsing/compiling Lua (reduces peak heap).
        clearPreview();
        previewCache.clear();
        pendingPreviewUrl.clear();
        for (auto &e : gameList) e.cacheData.clear();
    }

    // Keep host menu items in sync by tracking the active game id
    vm->currentCartPath = sourcePath;
    vm->next_cart_path = targetGame.path;
    std::string gameId = targetGame.displayName.empty() ? sourcePath : targetGame.displayName;
    size_t lastSlash = gameId.find_last_of("/\\");
    if (lastSlash != std::string::npos) gameId = gameId.substr(lastSlash + 1);
    if (gameId.empty()) gameId = "cart";
    vm->currentGameId = gameId;

    renderMessage("LOADING", targetGame.displayName, 12);
    vm->show_frame();

    // 1. Load File Raw Data
    std::vector<uint8_t> fileData = host->loadFile(targetGame.path.c_str());

    if (fileData.empty()) {
        errorTitle = "LOAD ERROR";
        shellErrorMsg = "FILE NOT FOUND";
        sysState = STATE_ERROR;
        return;
    }

    // ---------------------------------------------------------
    // Allocate GameData on the HEAP, not the Stack
    // ---------------------------------------------------------
    
    // Using unique_ptr automatically handles 'delete' when it goes out of scope.
    // This moves the ~17KB payload to the Heap, preventing Stack Overflow.
    
    //auto gameData = std::make_unique<GameData>();

    std::unique_ptr<GameData> gameData(new (std::nothrow) GameData());
    if (!gameData) {
        errorTitle = "OUT OF MEMORY";
        shellErrorMsg = "HEAP TOO LOW";
        sysState = STATE_ERROR;
        return;
    }

    // Pass the dereferenced pointer (*gameData) to the loader
    bool parseSuccess = Real8CartLoader::LoadFromBuffer(host, fileData, *gameData);

    if (parseSuccess) {

        // Free raw cart buffer before Lua compile (peak-memory point).
        std::vector<uint8_t>().swap(fileData);

        // Pass the dereferenced pointer (*gameData) to the VM
        if (vm->loadGame(*gameData)) { 
            host->setNetworkActive(false);
            vm->resetInputState();
            sysState = STATE_RUNNING;
        } else {
            errorTitle = "VM ERROR";
            shellErrorMsg = "EXECUTION FAILED";
            // If VM set specifics, STATE_ERROR renderer will show them
            sysState = STATE_ERROR;
        }
    } else {
        errorTitle = "LOAD ERROR";
        shellErrorMsg = "INVALID CART FORMAT";
        sysState = STATE_ERROR;
    }
    // ---------------------------------------------------------
    // gameData is automatically freed here
    // ---------------------------------------------------------
}

void Real8Shell::buildInGameMenu()
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
    inGameOptions.push_back("SHOW FPS");

    inGameOptions.push_back("EXIT");

    inGameMenuSelection = 0;
}

void Real8Shell::updateInGameMenu()
{
    if (vm->btnp(2)) { inGameMenuSelection--; if (inGameMenuSelection < 0) inGameMenuSelection = inGameOptions.size() - 1; }
    if (vm->btnp(3)) { inGameMenuSelection++; if (inGameMenuSelection >= inGameOptions.size()) inGameMenuSelection = 0; }

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

    if (vm->btnp(5)) { // Action
        std::string action = inGameOptions[inGameMenuSelection];

        if (action == "CONTINUE") {

            if (strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
                vm->showSkin = s_menuSavedShowSkin;
                s_menuSavedShowSkinValid = false;
            }

            vm->gpu.restoreState(menu_gfx_backup);
            sysState = STATE_RUNNING;
        }
        else if (action == "RESET GAME") {
            vm->rebootVM();

            if (strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
                vm->showSkin = s_menuSavedShowSkin;
                s_menuSavedShowSkinValid = false;
            }

            sysState = STATE_LOADING;
        }
        else if (action == "SAVE STATE") {
            vm->gpu.restoreState(menu_gfx_backup);
            vm->saveState();
            vm->gpu.reset();
            renderMessage("SYSTEM", "STATE SAVED", 11);
            vm->show_frame();
            buildInGameMenu();
        }
        else if (action == "LOAD STATE") {
            if (vm->loadState()) {
                renderMessage("SYSTEM", "STATE LOADED", 12);
                vm->show_frame();
                if (strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
                    vm->showSkin = s_menuSavedShowSkin;
                    s_menuSavedShowSkinValid = false;
                }
                sysState = STATE_RUNNING;
            } else {
                renderMessage("ERROR", "LOAD FAILED", 8);
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
            buildInGameMenu(); 
            inGameMenuSelection = savedSel;
        }
        else if (action == "EXIT") {
            vm->forceExit();
            vm->resetInputState();
            inputLatch = true;

            if (strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
                vm->showSkin = s_menuSavedShowSkin;
                s_menuSavedShowSkinValid = false;
            }

            sysState = STATE_BROWSER;
            refreshGameList();
        }
        else {
             // Custom Items
             for (int i = 1; i <= 5; i++) {
                 if (vm->custom_menu_items[i].active && vm->custom_menu_items[i].label == action) {
                     vm->run_menu_item(i);
                     sysState = STATE_RUNNING;
                     break;
                 }
             }
        }
    }

    if (vm->btnp(4)) {

        if (strcmp(host->getPlatform(), "3DS") == 0 && s_menuSavedShowSkinValid) {
            vm->showSkin = s_menuSavedShowSkin;
            s_menuSavedShowSkinValid = false;
        }

        vm->gpu.restoreState(menu_gfx_backup);
        sysState = STATE_RUNNING;
    }

}

// --------------------------------------------------------------------------
// RENDERING
// --------------------------------------------------------------------------

void Real8Shell::renderFileList()
{
    bool is3ds = (strcmp(host->getPlatform(), "3DS") == 0);
    if (is3ds) renderTopPreview3ds();
    else vm->clearAltFramebuffer();

    vm->gpu.setMenuFont(true);
    vm->gpu.cls(0);
    bool snapEnabled = (!is3ds && !gameList.empty() && fileSelection >= 0 && fileSelection < (int)gameList.size() &&
                        shouldShowPreviewForEntry(gameList[fileSelection]));
    if (snapEnabled && has_preview) drawPreview(0, 0, true);
    else drawStarfield();

    // Header
    vm->gpu.rectfill(0, 0, 127, 8, 1);
    std::string titleStr = (current_vfs_path == "") ? IReal8Host::REAL8_APPNAME : current_vfs_path;
    vm->gpu.pprint(titleStr.c_str(), titleStr.length(), getCenteredX(titleStr.c_str()), 2, 6);

    if (gameList.empty()) {
        const char *notitle = "EMPTY FOLDER";
        vm->gpu.pprint(notitle, strlen(notitle), getCenteredX(notitle), 50, 8);
        return;
    }

    int items = 11;
    int page_start = (fileSelection / items) * items;

    for (int i = 0; i < items; i++) {
        int idx = page_start + i;
        if (idx >= (int)gameList.size()) break;
        int y = (is3ds ? 15 : 18) + (i * 9);

        GameEntry &e = gameList[idx];
        bool isSelected = (idx == fileSelection);
        int textColor = e.isFolder ? (isSelected ? 10 : 9) : (isSelected ? 7 : 6);

        if (isSelected) {
            vm->gpu.rectfill(2, y - 2, 125, y + 6, 5);
            vm->gpu.pprint(">", 1, 5, y, 7);
        }

        std::string name = e.displayName;
        if (name.length() >= 22) name = name.substr(0, 22);
        std::string display = (e.isFavorite ? "* " : "") + name;
        vm->gpu.pprint(display.c_str(), display.length(), 11, y, textColor);
    }

    // Footer
    /*
    if (fileSelection >= 0 && fileSelection < (int)gameList.size()) {
        GameEntry &sel = gameList[fileSelection];
        if (sel.isRemote && !sel.author.empty()) {
            vm->gpu.rectfill(0, 120, 127, 127, 1);
            std::string authText = "By " + sel.author;
            vm->gpu.pprint(authText.c_str(), authText.length(), getCenteredX(authText.c_str()), 121, 6);
        }
    }
    */
    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderTopPreview3ds()
{
    memset(top_screen_fb, 0, sizeof(top_screen_fb));
    if (has_preview) {
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                top_screen_fb[y][x] = preview_ram[y][x] & 0x0F;
            }
        }
    }
    vm->setAltFramebuffer(top_screen_fb);
}

void Real8Shell::renderOptionsMenu()
{
    vm->gpu.setMenuFont(true);
    vm->gpu.cls(0);
    drawStarfield();

    vm->gpu.rectfill(10, 20, 117, 97, 0); 
    vm->gpu.rect(10, 20, 117, 97, 1);    
    vm->gpu.rectfill(10, 20, 117, 29, 1);

    const char *title = "GAME OPTIONS";
    vm->gpu.pprint(title, strlen(title), getCenteredX(title), 22, 6);

    int startY = 37;
    for (int i = 0; i < (int)contextOptions.size(); i++) {
        int y = startY + (i * 12);
        int color = (i == contextSelection) ? 7 : 6;
        if (i == contextSelection) vm->gpu.pprint(">", 1, 17, y, 7);
        vm->gpu.pprint(contextOptions[i].c_str(), contextOptions[i].length(), 25, y, color);
    }
    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderSettingsMenu()
{
    vm->gpu.setMenuFont(true);
    // Use vm->gpu for graphics calls
    vm->gpu.cls(0);
    
    // drawStarfield is a member of Real8Shell, not gpu
    drawStarfield();

    // Graphics primitives accessed via vm->gpu
    vm->gpu.rectfill(10, 5, 117, 110, 0); 
    vm->gpu.rect(10, 5, 117, 110, 1);
    vm->gpu.rectfill(10, 5, 117, 14, 1);

    const char *title = "SETTINGS";
    // pprint accessed via vm->gpu
    vm->gpu.pprint(title, (int)strlen(title), getCenteredX(title), 7, 6);

    const char* platform = host->getPlatform();
    bool normalMenu =
        (strcmp(platform, "Windows") == 0) ||
        (strcmp(platform, "3DS") == 0) ||
        (strcmp(platform, "Switch")  == 0);

    static const char *labels_win[] = {
        "REPO PREVIEW", "SHOW SKIN", "REPO GAMES", "STRETCH SCREEN",
        "CRT FILTER", "INTERPOLATION", "CREDITS", "EXIT REAL8", "BACK"};
        
    static const char *labels_std[] = {
        "STORAGE INFO", "REPO PREVIEW", "SHOW SKIN", "REPO GAMES", "STRETCH SCREEN", "WIFI STATUS", "CREDITS", "BACK"};

    const char **labels = normalMenu ? labels_win : labels_std;
    int itemCount = normalMenu ? 9 : 8; 

    // Settings variables are inside vm structure
    const char *val_repo_snap = vm->showRepoSnap ? "ON" : "OFF";
    const char *val_skin = vm->showSkin ? "ON" : "OFF";
    const char *val_stretch = vm->stretchScreen ? "ON" : "OFF";
    const char *val_crt = vm->crt_filter ? "ON" : "OFF";
    const char *val_interp = vm->interpolation ? "ON" : "OFF";
    NetworkInfo net = {};
    const char *val_wifi = nullptr;
    if (!normalMenu) {
        net = host->getNetworkInfo();
        val_wifi = net.connected ? "CONN" : "DISC";
    }

    for (int i = 0; i < itemCount; i++)
    {
        int y = 20 + (i * 10);
        
        // menuSelection is a member of Real8Shell
        if (i == menuSelection)
            vm->gpu.pprint(">", 1, 15, y, 7);
            
        vm->gpu.pprint(labels[i], (int)strlen(labels[i]), 22, y, (i == menuSelection) ? 7 : 6);

        // Lambda must capture 'this' or 'vm' to use gpu functions
        auto drawVal = [&](const char *txt, bool active)
        {
            vm->gpu.pprint(txt, (int)strlen(txt), 95, y, active ? 11 : 8);
        };

        if (normalMenu)
        {
            if (i == 0) drawVal(val_repo_snap, vm->showRepoSnap);
            if (i == 1) drawVal(val_skin, vm->showSkin);
            if (i == 2) drawVal(vm->showRepoGames ? "ON" : "OFF", vm->showRepoGames);
            if (i == 3) drawVal(val_stretch, vm->stretchScreen);
            if (i == 4) drawVal(val_crt, vm->crt_filter);
            if (i == 5) drawVal(val_interp, vm->interpolation);
        }
        else
        {
            if (i == 1) drawVal(val_repo_snap, vm->showRepoSnap);
            if (i == 2) drawVal(val_skin, vm->showSkin);
            if (i == 3) drawVal(vm->showRepoGames ? "ON" : "OFF", vm->showRepoGames); 
            if (i == 4) drawVal(val_stretch, vm->stretchScreen);
            if (i == 5) drawVal(val_wifi, net.connected);
        }
    }
    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderInGameMenu()
{

    vm->gpu.setMenuFont(true);
    bool is3ds = (strcmp(host->getPlatform(), "3DS") == 0);
    if (is3ds) {
        vm->gpu.cls(0);
        drawStarfield();
    } else {
        // Keep game background on other platforms
        vm->gpu.fillp(0xA5A5);
        vm->gpu.rectfill(0, 0, 128, 128, 0);
        vm->gpu.fillp(0);
    }

    int mw = 100;
    int mh = (inGameOptions.size() * 11) + 16;
    int mx = (128 - mw) / 2;
    int my = (128 - mh) / 2 - (is3ds ? 8 : 0);

    vm->gpu.rectfill(mx, my, mx + mw, my + mh, 0);
    vm->gpu.rect(mx, my, mx + mw, my + mh, 1);
    vm->gpu.rectfill(mx, my, mx + mw, my + 9, 1);

    const char *title = "PAUSED";
    vm->gpu.pprint(title, strlen(title), getCenteredX(title), my + 2, 6);

    for (int i = 0; i < (int)inGameOptions.size(); i++) {
        int oy = my + 15 + (i * 11);
        int ox = mx + 13;
        int col = (i == inGameMenuSelection) ? 7 : 6;

        if (i == inGameMenuSelection) vm->gpu.pprint(">", 1, ox - 6, oy, 7);
        vm->gpu.pprint(inGameOptions[i].c_str(), inGameOptions[i].length(), ox, oy, col);
        
        // Bars for volume
        if (inGameOptions[i] == "MUSIC") {
             for(int b=0; b<10; b++) vm->gpu.pprint("|", 1, mx+mw-45+(b*3), oy, (b < vm->volume_music)?11:5);
        }
        else if (inGameOptions[i] == "SFX") {
             for(int b=0; b<10; b++) vm->gpu.pprint("|", 1, mx+mw-45+(b*3), oy, (b < vm->volume_sfx)?11:5);
        }
        // [NEW] Logic for SHOW FPS Status
        else if (inGameOptions[i] == "SHOW FPS") {
             const char* status = vm->showStats ? "ON" : "OFF";
             int statusCol = vm->showStats ? 11 : 8; // 11=Green, 8=Red
             
             // Calculate alignment: Right Edge (mx+mw) - Padding(10) - TextWidth(len*5)
             int txtW = strlen(status) * 5; 
             int statusX = (mx + mw) - txtW - 10;
             
             vm->gpu.pprint(status, strlen(status), statusX, oy, statusCol);
        }
    }

    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderMessage(const char *header, std::string msg, int color)
{
    vm->gpu.setMenuFont(true);
    vm->gpu.cls(0);
    vm->gpu.rectfill(0, 50, 127, 75, color);
    vm->gpu.pprint(header, strlen(header), getCenteredX(header), 55, 7);
    vm->gpu.pprint(msg.c_str(), msg.length(), getCenteredX(msg.c_str()), 65, 7);
    vm->gpu.setMenuFont(false);
}

void Real8Shell::drawWifiScreen(const std::string &ssid, const std::string &ip, const std::string &status, float progress)
{
    vm->gpu.setMenuFont(true);
    vm->gpu.camera(0, 0);
    vm->gpu.clip(0, 0, 128, 128);
    vm->gpu.cls(0);
    vm->gpu.rectfill(0, 0, 127, 9, 1);
    
    const char *title = "WIFI TRANSFER";
    vm->gpu.pprint(title, strlen(title), getCenteredX(title), 2, 6);
    vm->gpu.pprint("IP ADDRESS:", 11, 4, 45, 6);
    vm->gpu.pprint(ip.c_str(), ip.length(), 4, 53, 12);
    
    vm->gpu.rect(10, 80, 118, 90, 6);
    if (progress > 0) vm->gpu.rectfill(12, 82, 12 + (int)(104 * progress), 88, 8);
    
    vm->gpu.pprint(status.c_str(), status.length(), getCenteredX(status.c_str()), 70, 7);
    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderStorageView() { /* Implementation identical to previous, using vm-> */ }

// --------------------------------------------------------------------------
// DATA & HELPERS
// --------------------------------------------------------------------------

void Real8Shell::refreshGameList(std::string selectPath)
{
    std::string previousPath = selectPath;
    if (previousPath.empty() && !gameList.empty() && fileSelection >= 0 && fileSelection < gameList.size()) {
        previousPath = gameList[fileSelection].path;
    }

    gameList.clear();
    loadFavorites();
    lastFileSelection = -1;

    if (current_vfs_path == "") {
        // Root: Scan Local Files
        std::vector<std::string> files = host->listFiles(""); 
        for (const auto &n : files) {
            bool isGame = false;
            if (n.length() > 3 && n.substr(n.length() - 3) == ".p8") isGame = true;
            if (n.length() > 7 && n.substr(n.length() - 7) == ".p8.png") isGame = true;
            if (n.find("games.json") != std::string::npos) isGame = false;
            if (n.find("cache.p8") != std::string::npos) isGame = false;

            if (isGame) {
                GameEntry e;
                e.displayName = (n[0] == '/') ? n.substr(1) : n;
                e.path = (n[0] == '/') ? n : "/" + n;
                e.isRemote = false;
                e.isFolder = false;
                e.isFavorite = (favorites.count(e.path) > 0);
                gameList.push_back(e);
            }
        }
        parseJsonGames();
        for(auto &f : vfs[""]) gameList.push_back(f);
    } 
    else {
        // Subfolder
        if (vfs.count(current_vfs_path)) {
            for(auto e : vfs[current_vfs_path]) {
                e.isFavorite = (favorites.count(e.path) > 0);
                gameList.push_back(e);
            }
        }
    }

    std::sort(gameList.begin(), gameList.end());

    fileSelection = 0;
    if (!previousPath.empty()) {
        for (size_t i = 0; i < gameList.size(); i++) {
            if (gameList[i].path == previousPath) { fileSelection = (int)i; break; }
        }
    }
}

void Real8Shell::parseJsonGames()
{
    vfs.clear();

    // 1. Local JSON (always load so local lists remain visible)
    std::vector<uint8_t> localData = host->loadFile("/gameslist.json");
    if (!localData.empty()) {
        parseJsonToVFS(std::string(localData.begin(), localData.end()));
    }

    // 2. Remote JSON (Repo) — only when repo games are enabled
    if (vm->showRepoGames) {
        const char *repoPath = "/repo_games.json";
        if (isSwitchPlatform) {
            std::vector<uint8_t> remoteData = host->loadFile(repoPath);
            if (!remoteData.empty()) {
                parseJsonToVFS(std::string(remoteData.begin(), remoteData.end()));
            }
            if (!repoDownload.active && remoteData.empty()) {
                startAsyncDownload(repoDownload, vm->currentRepoUrl, repoPath);
            }
        } else {
            if (host->downloadFile(vm->currentRepoUrl.c_str(), repoPath)) {
                std::vector<uint8_t> remoteData = host->loadFile(repoPath);
                if (!remoteData.empty()) parseJsonToVFS(std::string(remoteData.begin(), remoteData.end()));
                host->deleteFile(repoPath);
            }
        }
    }
}

void Real8Shell::parseJsonToVFS(const std::string &json)
{
    // Minimal parser to populate vfs map (Same logic as original code)
    size_t pos = 0;
    while(true) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;
        
        size_t keyStart = json.find('"', objStart);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = json.find('"', keyStart + 1);
        std::string folderName = json.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        bool folderExists = false;
        for(auto &existing : vfs[""]) if(existing.path == folderName) folderExists = true;

        if (!folderExists) {
            GameEntry folder;
            folder.displayName = folderName;
            folder.path = folderName;
            folder.isFolder = true;
            folder.isRemote = true; 
            vfs[""].push_back(folder);
        }

        size_t arrayStart = json.find('[', keyEnd);
        if (arrayStart == std::string::npos) break;
        
        size_t arrayEnd = arrayStart + 1;
        int depth = 1;
        while (depth > 0 && arrayEnd < json.length()) {
            if (json[arrayEnd] == '[') depth++;
            if (json[arrayEnd] == ']') depth--;
            arrayEnd++;
        }
        
        std::string gameArrayStr = json.substr(arrayStart, arrayEnd - arrayStart);
        size_t gPos = 0;
        while(true) {
            size_t gObjStart = gameArrayStr.find('{', gPos);
            if (gObjStart == std::string::npos) break;
            size_t gObjEnd = gameArrayStr.find('}', gObjStart);
            if (gObjEnd == std::string::npos) break;
            
            std::string gJson = gameArrayStr.substr(gObjStart, gObjEnd - gObjStart + 1);
            std::string name = json_extract(gJson, "name");
            //std::string author = json_extract(gJson, "author");
            std::string url = json_extract(gJson, "url");
            
            if (!url.empty()) {
                GameEntry e;
                e.displayName = name.empty() ? url : name;
                //e.author = author;
                e.path = url;
                e.isRemote = true;
                e.isFolder = false;
                vfs[folderName].push_back(e);
            }
            gPos = gObjEnd + 1;
        }
        pos = arrayEnd;
    }
}

// --------------------------------------------------------------------------
// PREVIEW & GRAPHICS HELPERS
// --------------------------------------------------------------------------

void Real8Shell::loadPreview(const uint8_t *data, size_t size)
{
    unsigned w, h;
    unsigned char *image = nullptr;
    unsigned error = lodepng_decode32(&image, &w, &h, data, size);

    if (error || !image) { if (image) free(image); clearPreview(); return; }

    memset(preview_ram, 0, sizeof(preview_ram));
    int src_offset_x = 16;
    int src_offset_y = 25;

    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            int sx = src_offset_x + x;
            int sy = src_offset_y + y;
            if (sx < w && sy < h) {
                size_t idx = (sy * w + sx) * 4;
                if (image[idx + 3] < 128) preview_ram[y][x] = 0;
                else preview_ram[y][x] = find_closest_p8_color(image[idx], image[idx+1], image[idx+2]);
            }
        }
    }
    free(image);
    has_preview = true;
}

void Real8Shell::clearPreview() { has_preview = false; memset(preview_ram, 0, sizeof(preview_ram)); }

static const uint8_t DIM_MAP[16] = { 0, 0, 1, 1, 2, 1, 5, 6, 2, 4, 9, 3, 1, 1, 2, 4 };

void Real8Shell::drawPreview(int x, int y, bool dim)
{
    if (!has_preview) return;
    for (int py = 0; py < 128; py++) {
        for (int px = 0; px < 128; px++) {
            uint8_t col = preview_ram[py][px];
            if (dim) col = DIM_MAP[col & 0x0F];
            vm->gpu.pset(x + px, y + py, col);
        }
    }
}

void Real8Shell::initStars()
{
    bg_stars.clear();
    for (int i = 0; i < 50; ++i) {
        Star s;
        s.x = rand() % 128;
        s.y = rand() % 128;
        s.speed = 0.2f + ((rand() % 100) / 100.0f);
        s.col = (rand() % 2 == 0) ? 1 : 5;
        bg_stars.push_back(s);
    }
}

void Real8Shell::drawStarfield()
{
    for (auto &s : bg_stars) {
        s.x -= s.speed;
        if (s.x < 0) { s.x = 128; s.y = rand() % 128; }
        vm->gpu.pset((int)s.x, (int)s.y, s.col);
    }
}

// Remove the old Real8VM::renderCredits implementation entirely 
// and replace it with this:

void Real8Shell::renderCredits()
{
    // 1. Access graphics via vm->gpu
    vm->gpu.cls(0);
    
    // 2. Call the shell's starfield method
    drawStarfield();

    // 3. Use the new setter we added to Real8Gfx
    vm->gpu.setMenuFont(true);

    // --- Draw Box ---
    int w = 110;
    int h = 65;
    int x = (128 - w) / 2;
    int y = (128 - h) / 2;

    vm->gpu.rectfill(x, y, x + w, y + h, 1);  // Dark Blue Background
    vm->gpu.rect(x, y, x + w, y + h, 12);     // Light Blue Border
    vm->gpu.rectfill(x, y, x + w, y + 9, 12); // Header Bar

    // --- Render Text ---

    // Header
    const char *title = "CREDITS";
    // getCenteredX is a static helper in this file, so we can call it directly
    vm->gpu.pprint(title, strlen(title), getCenteredX(title), y + 2, 7);

    int textY = y + 18;

    // Line 1
    const char *line1 = "REAL-8 EXPLORER";
    vm->gpu.pprint(line1, strlen(line1), getCenteredX(line1), textY, 6);

    // Line 2
    textY += 12;
    // \211 is the P8SCII code for the Copyright symbol
    const char *line2 = "2026 by @natureglass"; 
    vm->gpu.pprint(line2, strlen(line2), getCenteredX(line2), textY, 7);

    // Line 3
    textY += 8;
    const char *line3 = "Alex Daskalakis";
    vm->gpu.pprint(line3, strlen(line3), getCenteredX(line3), textY, 7);

    // Line 4
    textY += 14;
    // host is a member of Real8Shell, so we can access it directly
    std::string line4 = std::string("Ver ") + IReal8Host::REAL8_VERSION + " for " + host->getPlatform();
    vm->gpu.pprint(line4.c_str(), line4.length(), getCenteredX(line4.c_str()), textY, 11);

    // Restore font
    vm->gpu.setMenuFont(false);
}

// --------------------------------------------------------------------------
// PERSISTENCE HELPERS
// --------------------------------------------------------------------------

void Real8Shell::loadFavorites()
{
    favorites.clear();
    std::vector<uint8_t> data = host->loadFile("/favorites.txt");
    if (data.empty()) return;
    std::stringstream ss(std::string(data.begin(), data.end()));
    std::string line;
    while(std::getline(ss, line)) {
        if(!line.empty()) favorites.insert(line);
    }
}

void Real8Shell::saveFavorites()
{
    std::string out;
    for (const auto &path : favorites) out += path + "\n";
    host->saveState("/favorites.txt", (const uint8_t *)out.c_str(), out.length());
}

void Real8Shell::toggleFavorite(const std::string &path)
{
    if (favorites.count(path)) favorites.erase(path); else favorites.insert(path);
    saveFavorites();
}

void Real8Shell::deleteRemoteGameEntry(const std::string &targetUrl)
{
    // Minimal JSON deletion logic...
    // In production, better to parse, remove from object, and re-serialize.
    // For now, we assume this is handled or we rely on redownloading games.json.
}
