#include "real8_shell.h"
#include "real8_compression.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <memory>
#include <lodePNG.h>
#include "real8_cart.h"
#include "real8_fonts.h"

// --------------------------------------------------------------------------
// STATIC HELPERS
// --------------------------------------------------------------------------

static const int FONT_WIDTH = 5;
static const int SCREEN_CENTER_X = 64;

static int getCenteredX(const char *text)
{
    int textLenInPixels = (int)strlen(text) * FONT_WIDTH;
    return SCREEN_CENTER_X - (textLenInPixels / 2);
}

static void drawMenuTextToBuffer(uint8_t *buffer, int buf_w, int buf_h, const char *text, int x, int y, uint8_t col)
{
    if (!buffer || !text || text[0] == '\0') return;
    std::string p8 = convertUTF8toP8SCII(text);
    int cx = x;
    for (unsigned char ch : p8) {
        const uint8_t *rows = p8_5x6_bits(ch);
        for (int r = 0; r < 6; r++) {
            uint8_t bits = rows[r];
            for (int i = 0; i < 4; i++) {
                if (bits & (0x80 >> i)) {
                    int px = cx + i;
                    int py = y + r;
                    if ((unsigned)px < (unsigned)buf_w && (unsigned)py < (unsigned)buf_h) {
                        buffer[py * buf_w + px] = col;
                    }
                }
            }
        }
        cx += 5;
    }
}

static void ensureTopBufferSize(std::vector<uint8_t> &buffer, int &buf_w, int &buf_h, int w, int h, bool clear)
{
    if (w <= 0 || h <= 0) return;
    const size_t needed = (size_t)w * (size_t)h;
    if (buffer.size() != needed) {
        buffer.assign(needed, 0);
    } else if (clear) {
        std::fill(buffer.begin(), buffer.end(), 0);
    }
    buf_w = w;
    buf_h = h;
}

static bool isRepoSupportedPlatform(const char *platform)
{
    return (strcmp(platform, "Windows") == 0) ||
        (strcmp(platform, "Linux") == 0) ||
        (strcmp(platform, "Switch") == 0) ||
        (strcmp(platform, "3DS") == 0);
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
// The 3DS port uses a dedicated buffer for the top screen while the in-game
// menu is open (STATE_INGAME_MENU). We apply a simple 50% checkerboard darken
// to that frozen frame to make it feel "paused".
// --------------------------------------------------------------------------
void Real8Shell::applyPauseCheckerboardToTop()
{
    if (top_screen_fb.empty() || top_screen_w <= 0 || top_screen_h <= 0) return;
    // Classic checkerboard. fillp(0xA5A5) is essentially alternating pixels,
    // so parity works well as a lightweight approximation.
    for (int y = 0; y < top_screen_h; ++y) {
        for (int x = 0; x < top_screen_w; ++x) {
            // Darken half the pixels by forcing them to color 0.
            // Keep existing black pixels as-is.
            if (((x ^ y) & 1) == 0) {
                uint8_t &pix = top_screen_fb[(size_t)y * (size_t)top_screen_w + (size_t)x];
                if ((pix & 0x0F) != 0) {
                    pix = 0;
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

static std::string trimWhitespace(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::string toUpperAscii(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch >= 'a' && ch <= 'z') s[i] = static_cast<char>(ch - 'a' + 'A');
    }
    return s;
}

static bool recomAllowsPlatform(const std::string &recom, const char *platform)
{
    if (recom.empty() || !platform) return true;
    std::string platformUpper = toUpperAscii(platform);

    size_t start = 0;
    while (start < recom.size()) {
        size_t comma = recom.find(',', start);
        size_t end = (comma == std::string::npos) ? recom.size() : comma;
        std::string token = toUpperAscii(trimWhitespace(recom.substr(start, end - start)));

        if (!token.empty()) {
            if (token == platformUpper) return true;
            if (platformUpper == "WINDOWS" && (token == "WIN" || token == "WIN32" || token == "WIN64")) return true;
            if (platformUpper == "SWITCH" && (token == "NSW" || token == "NINTENDO SWITCH")) return true;
            if (platformUpper == "3DS" && (token == "N3DS" || token == "NINTENDO3DS")) return true;
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
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
    sysState = STATE_BROWSER;
    
    // Clear preview buffer
    memset(preview_ram, 0, sizeof(preview_ram));
    ensureTopBufferSize(top_screen_fb, top_screen_w, top_screen_h, 128, 128, true);
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
    ShellState prevState = lastState;

    // Tell the VM whether we are rendering shell/UI (menus) vs gameplay.
    // Stereo anaglyph should only apply during gameplay.
    vm->isShellUI = (sysState != STATE_RUNNING);

    if (pendingInitialRefresh) {
        refreshGameList();
        pendingInitialRefresh = false;
    }

    if (strcmp(host->getPlatform(), "3DS") == 0) {

        if (sysState == STATE_RUNNING) {
            // Normal gameplay: single framebuffer (both screens show the game)
            vm->clearAltFramebuffer();
            host->clearTopPreviewBlankHint();
        }
        else if (sysState == STATE_INGAME_MENU) {
            // In-game menu: keep the frozen game frame on the TOP screen
            // (top_screen_fb is filled when the menu is opened)
            int w = (top_screen_w > 0) ? top_screen_w : 128;
            int h = (top_screen_h > 0) ? top_screen_h : 128;
            ensureTopBufferSize(top_screen_fb, top_screen_w, top_screen_h, w, h, false);
            vm->setAltFramebuffer(top_screen_fb.data(), w, h);
            host->setTopPreviewBlankHint(false);
        }
        else if (sysState != STATE_BROWSER) {
            // Other menus: top screen uses the dedicated buffer
            ensureTopBufferSize(top_screen_fb, top_screen_w, top_screen_h, 128, 128, true);
            vm->setAltFramebuffer(top_screen_fb.data(), 128, 128);
            host->setTopPreviewBlankHint(true);
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
            bool RepoSupport = isRepoSupportedPlatform(platform);
            bool normalMenu = RepoSupport;
            bool is3ds = (strcmp(platform, "3DS") == 0);
            bool repoPreviewEnabled = RepoSupport && vm->showRepoSnap;

            bool exitPreview = false;
            if (is3ds && !gameList.empty()) {
                int prevSelection = fileSelection;
                if (vm->btnp(2)) { // UP
                    fileSelection--;
                    if (fileSelection < 0) fileSelection = (int)gameList.size() - 1;
                }
                if (vm->btnp(3)) { // DOWN
                    fileSelection++;
                    if (fileSelection >= (int)gameList.size()) fileSelection = 0;
                }
                if (fileSelection != prevSelection) {
                    targetGame = gameList[fileSelection];
                    lastPreviewPath.clear();
                    if (!repoPreviewEnabled) {
                        clearPreview();
                        lastFileSelection = -1;
                        sysState = STATE_BROWSER;
                        exitPreview = true;
                    }
                }
            }

            if (exitPreview) {
                renderFileList();
                vm->show_frame();
                break;
            }

            if (targetGame.path != lastPreviewPath) {
                if (is3ds) {
                    bool hasCachedPreview = false;
                    auto cached = previewCache.find(targetGame.path);
                    if (cached != previewCache.end() && !cached->second.empty()) hasCachedPreview = true;
                    if (!hasCachedPreview) {
                        clearPreview();
                        renderFileList(false);
                        renderTopPreview3ds("LOADING PREVIEW");
                        vm->show_frame();
                    }
                }
                lastPreviewPath = targetGame.path;
                loadPreviewForEntry(targetGame, normalMenu, true, !is3ds);
            }

            if (is3ds) {
                renderFileList(false);
                const char *topStatus = has_preview ? nullptr : "LOADING PREVIEW";
                renderTopPreview3ds(topStatus);
                vm->show_frame();
                vm->gpu.setMenuFont(false);
            } else {
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
            }

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

        case STATE_CREDITS:
            renderCredits();
            vm->show_frame();
            if (vm->btnp(5) || vm->btnp(4) || vm->isMenuPressed()) sysState = STATE_SETTINGS;
            break;

        case STATE_LOADING:
            updateLoading();
            break;

        case STATE_RUNNING:
            if (vm->isMenuPressed()) {

                // 3DS: freeze the current game frame to the TOP screen buffer
                if (strcmp(host->getPlatform(), "3DS") == 0) {
                    const int w = (vm->fb_w > 0) ? vm->fb_w : 128;
                    const int h = (vm->fb_h > 0) ? vm->fb_h : 128;
                    ensureTopBufferSize(top_screen_fb, top_screen_w, top_screen_h, w, h, false);
                    if (vm->fb && vm->fb_w > 0 && vm->fb_h > 0) {
                        const size_t fb_bytes = (size_t)vm->fb_w * (size_t)vm->fb_h;
                        std::memcpy(top_screen_fb.data(), vm->fb, fb_bytes);
                    } else if (!top_screen_fb.empty()) {
                        std::fill(top_screen_fb.begin(), top_screen_fb.end(), 0);
                    }

                    // Apply paused overlay effect (checkerboard like fillp(0xA5A5))
                    applyPauseCheckerboardToTop();

                    vm->setAltFramebuffer(top_screen_fb.data(), top_screen_w, top_screen_h);
                    host->setTopPreviewBlankHint(false);
                }

                vm->gpu.saveState(menu_gfx_backup);
                vm->gpu.reset();
                menu_force_draw_bottom = false;
                if (strcmp(host->getPlatform(), "3DS") == 0 &&
                    vm->bottom_screen_enabled && vm->fb_bottom) {
                    menu_saved_draw_bottom = vm->draw_target_bottom;
                    menu_force_draw_bottom = true;
                    vm->draw_target_bottom = true;
                    vm->gpu.clip(0, 0, vm->draw_w(), vm->draw_h());
                    if (!menu_bottom_override_active) {
                        menu_saved_bottom_vmode_req = vm->bottom_vmode_req;
                        menu_bottom_override_active = true;
                    }
                    vm->applyBottomVideoMode(2, /*force=*/true);
                }
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
                    resetModeForShell();
                    sysState = STATE_BROWSER;
                    refreshGameList();
                }
                if (vm->reset_requested) {
                    // vm->runFrame handles internal Lua reset logic usually,
                    // but if it propagates up here:
                    std::string requestedPath = vm->next_cart_path;
                    vm->rebootVM();
                    if (!requestedPath.empty()) {
                        targetGame = {};
                        targetGame.path = requestedPath;
                        size_t slash = targetGame.path.find_last_of("/\\");
                        targetGame.displayName = (slash == std::string::npos) ? targetGame.path : targetGame.path.substr(slash + 1);
                        targetGame.isRemote = false;
                        targetGame.isFolder = false;
                        targetGame.isFavorite = false;
                    }
                    sysState = STATE_LOADING;
                    vm->reset_requested = false;
                }
            }
            break;

        case STATE_INGAME_MENU:
            updateInGameMenu();
            renderInGameMenu();
            vm->show_frame();
            if (menu_force_draw_bottom && sysState != STATE_INGAME_MENU) {
                vm->draw_target_bottom = menu_saved_draw_bottom;
                vm->gpu.clip(0, 0, vm->draw_w(), vm->draw_h());
                menu_force_draw_bottom = false;
            }
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
                resetModeForShell();
                sysState = STATE_BROWSER;
            }
            break;
        }
    }

    if (sysState == STATE_BROWSER && prevState != STATE_BROWSER) {
        resetModeForShell();
    }
    lastState = sysState;
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

void Real8Shell::resetModeForShell()
{
    if (!vm) return;
    uint8_t mode = 0;
    if (host && std::strcmp(host->getPlatform(), "3DS") == 0) {
        mode = 1;
        vm->applyBottomVideoMode(Real8VM::BOTTOM_VMODE_DEFAULT, /*force=*/true);
    }
    vm->applyVideoMode(mode, /*force=*/true);
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
    const char* platform = host->getPlatform();
    bool RepoSupport = isRepoSupportedPlatform(platform);
    bool is3ds = (strcmp(platform, "3DS") == 0);
    return e.isRemote ? (RepoSupport && vm->showRepoSnap) : true;
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
                if (strcmp(host->getPlatform(), "3DS") == 0) {
                    renderTopPreview3ds("LOADING PREVIEW");
                }
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
    bool RepoSupport = isRepoSupportedPlatform(platform);
    bool is3ds = (strcmp(platform, "3DS") == 0);
    bool normalMenu = RepoSupport;
    auto getParentPath = [](const std::string& path) {
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return std::string();
        return path.substr(0, slash);
    };

    bool repoSnapEnabled = RepoSupport && vm->showRepoSnap;
    static bool lastRepoSnapState = repoSnapEnabled;
    if (repoSnapEnabled != lastRepoSnapState) {
        lastFileSelection = -1; // force preview reload when snaps are re-enabled
        if (gameList.empty() || fileSelection < 0 || fileSelection >= (int)gameList.size() || !shouldShowPreviewForEntry(gameList[fileSelection])) {
            clearPreview();
        }
        lastRepoSnapState = repoSnapEnabled;
    }

    // Allow backing out of empty folders instead of trapping the user
    if (gameList.empty()) {
        if (vm->btnp(4) && !current_vfs_path.empty()) { // O -> Back
            std::string lastFolder = current_vfs_path;
            current_vfs_path = getParentPath(current_vfs_path);
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
            current_vfs_path = getParentPath(current_vfs_path);
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

// --------------------------------------------------------------------------
// RENDERING
// --------------------------------------------------------------------------

void Real8Shell::renderFileList(bool drawTopPreview)
{
    bool is3ds = (strcmp(host->getPlatform(), "3DS") == 0);
    if (is3ds) {
        if (drawTopPreview) renderTopPreview3ds();
    } else {
        vm->clearAltFramebuffer();
    }

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
        if (repoDownload.active && vm->showRepoGames) {
            const char *repoTitle = "REAL-8 EXPLORER";
            const char *repoMsg = "LOADING REPO GAMES";
            vm->gpu.rectfill(0, 52, 127, 76, 1);
            vm->gpu.pprint(repoTitle, strlen(repoTitle), getCenteredX(repoTitle), 56, 7);
            vm->gpu.pprint(repoMsg, strlen(repoMsg), getCenteredX(repoMsg), 66, 7);
        }
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
        int textColor = 6;
        if (e.isFolder) {
            int folderBase = e.isRemote; // ? 9 : 142;
            if(folderBase){
                textColor = isSelected ? 10 : 9;
            } else {
                textColor = isSelected ? 14 : 8;
            }
        } else {
            textColor = isSelected ? 7 : 6;
        }

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
    if (repoDownload.active && vm->showRepoGames) {
        const char *repoTitle = "REAL-8 EXPLORER";
        const char *repoMsg = "LOADING REPO GAMES";
        vm->gpu.rectfill(0, 52, 127, 76, 1);
        vm->gpu.pprint(repoTitle, strlen(repoTitle), getCenteredX(repoTitle), 56, 7);
        vm->gpu.pprint(repoMsg, strlen(repoMsg), getCenteredX(repoMsg), 66, 7);
    }
    vm->gpu.setMenuFont(false);
}

void Real8Shell::renderTopPreview3ds(const char *statusText)
{
    ensureTopBufferSize(top_screen_fb, top_screen_w, top_screen_h, 128, 128, true);
    bool previewBlank = true;
    if (statusText && statusText[0] != '\0') {
        drawMenuTextToBuffer(top_screen_fb.data(), top_screen_w, top_screen_h,
                             statusText, getCenteredX(statusText), 60, 11);
        previewBlank = false;
    } else if (has_preview) {
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                top_screen_fb[(size_t)y * (size_t)top_screen_w + (size_t)x] = preview_ram[y][x] & 0x0F;
            }
        }
        previewBlank = false;
    }
    vm->setAltFramebuffer(top_screen_fb.data(), 128, 128);
    host->setTopPreviewBlankHint(previewBlank);
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
        parseJsonGames();

        auto endsWith = [](const std::string& s, const char* suffix) {
            size_t len = strlen(suffix);
            if (s.length() < len) return false;
            return s.compare(s.length() - len, len, suffix) == 0;
        };
        auto isGameFile = [&](const std::string& s) {
            if (s.find("games.json") != std::string::npos) return false;
            if (s.find("cache.p8") != std::string::npos) return false;
            return endsWith(s, ".p8.png") || endsWith(s, ".p8");
        };
        auto normalizePath = [](std::string p) {
            if (!p.empty() && p[0] == '/') p = p.substr(1);
            for (char &c : p) if (c == '\\') c = '/';
            return p;
        };
        auto ensureFolderEntry = [&](const std::string& folderPath) {
            if (folderPath.empty()) return;
            std::string current;
            size_t pos = 0;
            while (pos < folderPath.size()) {
                size_t next = folderPath.find('/', pos);
                std::string part = (next == std::string::npos) ? folderPath.substr(pos) : folderPath.substr(pos, next - pos);
                if (part.empty()) break;
                std::string parent = current;
                if (!current.empty()) current += "/";
                current += part;

                auto &list = vfs[parent];
                bool exists = false;
                for (const auto &e : list) {
                    if (e.isFolder && e.path == current) { exists = true; break; }
                }
                if (!exists) {
                    GameEntry folder;
                    folder.displayName = part;
                    folder.path = current;
                    folder.isRemote = false;
                    folder.isFolder = true;
                    folder.isFavorite = false;
                    list.push_back(folder);
                }

                if (next == std::string::npos) break;
                pos = next + 1;
            }
        };
        auto addFileEntry = [&](const std::string& relPath) {
            std::string dir;
            std::string name = relPath;
            size_t slash = relPath.find_last_of('/');
            if (slash != std::string::npos) {
                dir = relPath.substr(0, slash);
                name = relPath.substr(slash + 1);
                ensureFolderEntry(dir);
            }

            auto &list = vfs[dir];
            std::string fullPath = "/" + relPath;
            for (const auto &e : list) {
                if (!e.isFolder && e.path == fullPath) return;
            }

            GameEntry e;
            e.displayName = name;
            e.path = fullPath;
            e.isRemote = false;
            e.isFolder = false;
            e.isFavorite = false;
            list.push_back(e);
        };

        for (const auto &raw : files) {
            std::string n = normalizePath(raw);
            if (n.empty()) continue;
            if (!isGameFile(n)) continue;
            addFileEntry(n);
        }

        for (auto e : vfs[""]) {
            e.isFavorite = (favorites.count(e.path) > 0);
            gameList.push_back(e);
        }
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
    const char* platform = host->getPlatform();
    bool RepoSupport = isRepoSupportedPlatform(platform);
    bool is3ds = (strcmp(platform, "3DS") == 0);
    (void)is3ds; // kept for future platform-specific tweaks

    bool repoGamesEnabled = RepoSupport && vm->showRepoGames;
    bool isBootRefresh = pendingInitialRefresh;

    if (isBootRefresh && repoGamesEnabled) {
        pendingRepoBootCopy = true;
    }

    // Goal: do NOT show Repo Games unless we have internet, even if repoGamesEnabled is true.
    // So we only read/parse the cached repo list and do any repo handling when `isConnected` is true.
    const bool isConnected = repoGamesEnabled ? host->getNetworkInfo().connected : false;

    // 1) Cached local JSON (repo mirror) — only when connected (hide repo list offline)
    std::vector<uint8_t> localData;
    if (isConnected) {
        localData = host->loadFile("/gameslist.json");
        if (!localData.empty()) {
            parseJsonToVFS(std::string(localData.begin(), localData.end()));
        }
    }

    // 2) Remote JSON (Repo) — only when repo games are enabled AND connected
    bool hasLocalList = !localData.empty();
    bool allowRemoteHandling = isConnected && repoGamesEnabled && (pendingRepoBootCopy || !hasLocalList);
    bool allowNetworkFetch  = isConnected && repoGamesEnabled && (isBootRefresh || !hasLocalList);

    if (!allowRemoteHandling) {
        return;
    }

    const char *repoPath = "/repo_games.json";

    if (isSwitchPlatform) {
        // Switch: prefer async download, but still allow using an already-downloaded repo file.
        std::vector<uint8_t> remoteData = host->loadFile(repoPath);
        const bool remoteEmpty = remoteData.empty();

        if (!remoteEmpty) {
            bool remoteMatchesLocal = !localData.empty() &&
                localData.size() == remoteData.size() &&
                memcmp(localData.data(), remoteData.data(), localData.size()) == 0;

            if (pendingRepoBootCopy) {
                vfs.clear();
            }

            if (!remoteMatchesLocal || pendingRepoBootCopy) {
                parseJsonToVFS(std::string(remoteData.begin(), remoteData.end()));
            }

            if (pendingRepoBootCopy) {
                host->saveState("/gameslist.json", remoteData.data(), remoteData.size());
                pendingRepoBootCopy = false;
            }
        }

        // Fix: `remoteData` was previously out-of-scope here. Use `remoteEmpty` instead.
        if (allowNetworkFetch && !repoDownload.active && remoteEmpty) {
            startAsyncDownload(repoDownload, vm->currentRepoUrl, repoPath);
        }
    } else if (allowNetworkFetch) {
        // Other platforms: blocking download is fine
        renderMessage("REAL-8 EXPLORER", "LOADING REPO GAMES", 1);
        vm->show_frame();

        if (host->downloadFile(vm->currentRepoUrl.c_str(), repoPath)) {
            std::vector<uint8_t> remoteData = host->loadFile(repoPath);
            if (!remoteData.empty()) {
                bool remoteMatchesLocal = !localData.empty() &&
                    localData.size() == remoteData.size() &&
                    memcmp(localData.data(), remoteData.data(), localData.size()) == 0;

                if (pendingRepoBootCopy) {
                    vfs.clear();
                }

                if (!remoteMatchesLocal || pendingRepoBootCopy) {
                    parseJsonToVFS(std::string(remoteData.begin(), remoteData.end()));
                }

                if (pendingRepoBootCopy) {
                    host->saveState("/gameslist.json", remoteData.data(), remoteData.size());
                    pendingRepoBootCopy = false;
                }
            }
            host->deleteFile(repoPath);
        }
    }
}


void Real8Shell::parseJsonToVFS(const std::string &json)
{
    // Minimal parser to populate vfs map (Same logic as original code)
    const char *platform = host->getPlatform();
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
            std::string recom = json_extract(gJson, "recom");
            
            if (!url.empty() && recomAllowsPlatform(recom, platform)) {
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
    int screen_w = vm ? vm->draw_w() : 128;
    int screen_h = vm ? vm->draw_h() : 128;
    if (screen_w <= 0) screen_w = 128;
    if (screen_h <= 0) screen_h = 128;
    float scale_x = (float)screen_w / 128.0f;
    float scale_y = (float)screen_h / 128.0f;

    for (auto &s : bg_stars) {
        s.x -= s.speed;
        if (s.x < 0) { s.x = 128; s.y = rand() % 128; }
        int px = (int)(s.x * scale_x);
        int py = (int)(s.y * scale_y);
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if (px >= screen_w) px = screen_w - 1;
        if (py >= screen_h) py = screen_h - 1;
        vm->gpu.pset(px, py, s.col);
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
    const char *line2 = "by @natureglass"; 
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
