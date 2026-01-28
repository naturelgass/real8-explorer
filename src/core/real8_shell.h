#pragma once
#include "real8_vm.h"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <atomic>
#include <thread>

enum ShellState {
    STATE_BOOT,
    STATE_BROWSER,
    STATE_OPTIONS_MENU,
    STATE_PREVIEW_VIEW,
    STATE_SETTINGS,
    STATE_LOADING,
    STATE_RUNNING,
    STATE_INGAME_MENU,
    STATE_ERROR,
    STATE_WIFI_INFO,
    STATE_STORAGE_INFO,
    STATE_CREDITS
};

struct GameEntry {
    std::string displayName;
    std::string path;
    //std::string author; 
    bool isRemote;
    bool isFavorite;
    bool isFolder;
    std::vector<uint8_t> cacheData;

    bool operator<(const GameEntry &other) const {
        if (isFolder != other.isFolder) return isFolder > other.isFolder;
        if (isFavorite != other.isFavorite) return isFavorite > other.isFavorite;
        return displayName < other.displayName;
    }
};

class Real8Shell {
public:
    Real8Shell(IReal8Host* host, Real8VM* vm);
    ~Real8Shell();

    void update();
    void refreshGameList(std::string selectPath = "");

    // 3DS: visual effect when the in-game pause/menu is open (PICO-8 style fillp checkerboard)
    void applyPauseCheckerboardToTop();

private:
    IReal8Host* host;
    Real8VM* vm; 
    ShellState sysState = STATE_BOOT;
    ShellState lastState = STATE_BOOT;
    bool isSwitchPlatform = false;
    bool pendingInitialRefresh = true;
    bool pendingRepoBootCopy = false;

    Real8Gfx::GfxState menu_gfx_backup;
    bool menu_force_draw_bottom = false;
    bool menu_saved_draw_bottom = false;
    bool menu_bottom_override_active = false;
    uint8_t menu_saved_bottom_vmode_req = 0;
    
    // --- State Logic ---
    void updateBrowser();
    void updateOptionsMenu();
    void updateSettingsMenu();
    void updateLoading();
    void updateInGameMenu();
    void buildInGameMenu();
    void buildContextMenu();
    void resetModeForShell();

    // --- Rendering ---
    void renderFileList(bool drawTopPreview = true);
    void renderOptionsMenu();
    void renderSettingsMenu();
    void renderInGameMenu();
    void renderCredits();
    void renderMessage(const char *header, std::string msg, int color);
    
    // --- Graphics/Preview helpers ---
    void drawStarfield();
    static void drawStarfieldHook(void* user, Real8VM* vm);
    void loadPreview(const uint8_t *data, size_t size);
    void clearPreview();
    void drawPreview(int x, int y, bool dim);
    void renderTopPreview3ds(const char *statusText = nullptr);

    // --- Data Management ---
    std::string current_vfs_path = "";
    std::map<std::string, std::vector<GameEntry>> vfs;
    std::vector<GameEntry> gameList;
    std::set<std::string> favorites;
    std::map<std::string, std::vector<uint8_t>> previewCache;
    GameEntry targetGame;
    
    // --- Preview RAM ---
    uint8_t preview_ram[128][128];
    bool has_preview = false;
    std::vector<uint8_t> top_screen_fb;
    int top_screen_w = 0;
    int top_screen_h = 0;

    // --- Selection State ---
    int fileSelection = 0;
    int lastFileSelection = -1;
    int menuSelection = 0;
    int contextSelection = 0;
    int inGameMenuSelection = 0;
    std::string lastPreviewPath;
    
    // --- Containers ---
    std::vector<std::string> contextOptions;
    std::vector<std::string> inGameOptions;

    // --- Helpers ---
    void parseJsonGames();
    void parseJsonToVFS(const std::string &json);
    void loadFavorites();
    void saveFavorites();
    void toggleFavorite(const std::string &path);
    void deleteRemoteGameEntry(const std::string &url);
    bool shouldShowPreviewForEntry(const GameEntry &e) const;
    bool loadPreviewForEntry(GameEntry &e, bool normalMenu, bool allowFetch, bool showFetchMsg);

    struct AsyncDownload {
        std::atomic<bool> active{false};
        std::atomic<bool> done{false};
        std::atomic<bool> success{false};
        std::string url;
        std::string path;
        std::thread worker;
    };

    void startAsyncDownload(AsyncDownload &task, const std::string &url, const std::string &path);
    void updateAsyncDownloads();
    bool isPreviewDownloadActiveFor(const std::string &url) const;

    AsyncDownload repoDownload;
    AsyncDownload previewDownload;
    AsyncDownload gameDownload;
    std::string pendingPreviewUrl;
    bool pendingRepoRefresh = false;
    bool inputLatch = false;
    
    // Background stars logic
    struct Star { float x, y, speed; uint8_t col; };
    std::vector<Star> bg_stars;
    void initStars();
    
    std::string shellErrorMsg;
    std::string errorTitle;
};
