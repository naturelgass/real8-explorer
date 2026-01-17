#pragma once

#include "../../hal/real8_host.h"
#include "../../core/real8_vm.h" // Ensure this path is correct for your project structure
#include "../../core/real8_tools.h"
#include "windows_input.hpp"

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <map>
#include <cstdlib>
#include <cctype>
#include <shlobj.h>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <urlmon.h>
#include <commctrl.h>
#include <fstream>
#include <windowsx.h>
#include <numeric>


#include <wininet.h>
#include <netlistmgr.h>
#include <objbase.h>
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
namespace fs = std::filesystem;

// IDs for the Repo Dialog
#define ID_BTN_SAVE 301
#define ID_BTN_RESET 302
#define ID_EDIT_URL 303

// IDs for the Custom Console
#define ID_CONSOLE_EDIT 401
#define ID_BTN_CLEAR_LOG 403
#define ID_BTN_COPY_LOG 404
#define ID_BTN_RESUME    406
#define ID_BTN_STEP      407
#define ID_CHK_CLEAR_ON_STEP 409
#define ID_BTN_EXIT_GAME 410

// IDs for the Real-Time Modding window
#define ID_MOD_CHECK_BASE 5000
#define ID_MOD_EDIT_BASE 6000
#define ID_BTN_REFRESH_VARS 6200
#define ID_MOD_FAV_BASE 7000
#define ID_MOD_AUTO_TIMER 7100
#define ID_MOD_EXPORT_VARS 7200
#define ID_MOD_SEND_COMMAND 7250
#define ID_CMD_PROMPT_EDIT 7300
#define ID_CMD_PROMPT_SEND 7301
#define ID_CMD_PROMPT_CANCEL 7302

class WindowsHost : public IReal8Host
{
private:
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *wallpaperTex;
    SDL_AudioDeviceID audioDevice;
    WindowsInput input;
    SDL_Window* sdlWindow; 

    std::ofstream logFile;

    std::vector<uint32_t> screenBuffer;
    int screenW = 128;
    int screenH = 128;
    int defaultWindowW = 0;
    int defaultWindowH = 0;
    std::vector<uint32_t> wallBuffer;
    int wallW = 0, wallH = 0;
    fs::path rootPath;

    // --- CONSOLE VARIABLES ---
    HWND hConsoleWnd = NULL;
    HWND hLogEdit = NULL;
    HBRUSH hConsoleBrush = NULL;
    bool isConsoleActive = false;

    // --- REAL-TIME MODDING VARIABLES ---
    struct ModEntryRow {
        HWND checkbox = NULL;
        HWND edit = NULL;
        HWND favoriteCheck = NULL;
        Real8Tools::StaticVarEntry::Type type = Real8Tools::StaticVarEntry::Type::Number;
        std::string name;
        std::string value;
        bool locked = false;
        bool dirty = false;
        bool favorite = false;
    };
    HWND hModWnd = NULL;
    HFONT hModFont = NULL;
    bool isModWindowActive = false;
    int modScrollOffset = 0;
    int modContentHeight = 0;
    bool modAutoRefreshPaused = true;
    std::vector<ModEntryRow> modEntries;
    std::string modTrackedGameId;
    HMENU hModMenuBar = NULL;
    HMENU hModActionsMenu = NULL;
    //void rebuildRealtimeModList();
    //void layoutRealtimeModControls();
    //void handleRealtimeScroll(int delta);
    
    // Flags
    bool optShowLuaErrors = true; 
    bool optPauseLogs = false; 
    bool optClearOnStep = false; 

    // Internal buffer for the Repo Dialog
    static char* s_repoBuffer;
    static const char* s_defaultRepoUrl; 

    // Bridge print(...) during "Inject LUA code" so output also reaches the host log.
    static int LuaCmdPrintBridge(lua_State *L)
    {
        WindowsHost* host = (WindowsHost*)lua_touserdata(L, lua_upvalueindex(1));
        int nargs = lua_gettop(L);

        std::string assembled;
        for (int i = 1; i <= nargs; ++i) {
            size_t len = 0;
            const char* s = luaL_tolstring(L, i, &len); // pushes string on stack
            if (i > 1) assembled += "\t";
            if (s && len > 0) assembled.append(s, len);
            lua_pop(L, 1); // remove luaL_tolstring result
        }

        if (host) {
            host->log("[CMD PRINT] %s", assembled.c_str());
        }

        // Forward to original print so on-screen output stays intact.
        lua_getglobal(L, "__real8_cmd_orig_print");
        if (lua_isfunction(L, -1)) {
            lua_insert(L, 1); // move original print below arguments
            lua_call(L, nargs, 0);
            return 0;
        }
        lua_pop(L, 1);
        return 0;
    }

    // --- CONSOLE WNDPROC ---
    static LRESULT CALLBACK ConsoleWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowsHost* host = (WindowsHost*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        
        // Do NOT use 'static' here. The VM can be deleted and recreated, 
        // or deleted on exit while this window still processes a final WM_TIMER message.
        // We must fetch the fresh pointer from the host every time.
        Real8VM* linkedVM = (host) ? host->debugVMRef : nullptr;

        switch (message)
        {
        case WM_CREATE:
        {
            HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                     FIXED_PITCH | FF_MODERN, "Consolas");

            // 1. Log View (Read Only)
            HWND hEdit = CreateWindowEx(0, "EDIT", "", 
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                0, 0, 0, 0, hWnd, (HMENU)(UINT_PTR)ID_CONSOLE_EDIT, GetModuleHandle(NULL), NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            CreateWindow("BUTTON", "Clear on Step", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0,0,0,0, hWnd, (HMENU)(UINT_PTR)ID_CHK_CLEAR_ON_STEP, NULL, NULL);

            // 3. Controls - Buttons
            CreateWindow("BUTTON", "Clear", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0,0,0,0, hWnd, (HMENU)(UINT_PTR)ID_BTN_CLEAR_LOG, NULL, NULL);
            
            // Force Exit created as DISABLED, but WM_TIMER will enable it immediately if Lua is running
            CreateWindow("BUTTON", "Force Exit", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, 0,0,0,0, hWnd, (HMENU)(UINT_PTR)ID_BTN_EXIT_GAME, NULL, NULL);

            CreateWindow("BUTTON", "Pause (F5)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, 0,0,0,0, hWnd, (HMENU)(UINT_PTR)ID_BTN_RESUME, NULL, NULL);
            CreateWindow("BUTTON", "Step (F10)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, 0,0,0,0, hWnd, (HMENU)(UINT_PTR)ID_BTN_STEP, NULL, NULL);

            SetTimer(hWnd, 1, 100, NULL);
            return 0;
        }

        case WM_TIMER:
        {
            if (linkedVM) {
                // [CHANGED] Check if Lua is active rather than checking for a specific Game ID.
                // This allows you to Pause or Force Exit even in the Shell or during boot.
                bool isLuaActive = (linkedVM->getLuaState() != nullptr);
                bool isPaused = linkedVM->debug.paused;

                // 1. Update PAUSE Button
                HWND hBtnPR = GetDlgItem(hWnd, ID_BTN_RESUME);
                EnableWindow(hBtnPR, isLuaActive);

                char currentLabel[32];
                GetWindowText(hBtnPR, currentLabel, 32);
                const char* targetLabel = isPaused ? "Resume (F5)" : "Pause (F5)";
                if (strcmp(currentLabel, targetLabel) != 0) {
                    SetWindowText(hBtnPR, targetLabel);
                }

                // 2. Update STEP Button (Only enabled when PAUSED)
                EnableWindow(GetDlgItem(hWnd, ID_BTN_STEP), isLuaActive && isPaused);
                
                // 3. Update FORCE EXIT Button
                // Now enabled whenever Lua is active, not just when a cart ID is present.
                EnableWindow(GetDlgItem(hWnd, ID_BTN_EXIT_GAME), isLuaActive);
            } 
            else {
                EnableWindow(GetDlgItem(hWnd, ID_BTN_RESUME), FALSE);
                EnableWindow(GetDlgItem(hWnd, ID_BTN_STEP), FALSE);
                EnableWindow(GetDlgItem(hWnd, ID_BTN_EXIT_GAME), FALSE);
            }
            return 0;
        }

        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            int rowH = 24; 
            int pad = 5;
            int yBot = h - rowH - pad;
            int currentX = pad;
            
            int btnW = 90;
            MoveWindow(GetDlgItem(hWnd, ID_BTN_RESUME), currentX, yBot, btnW, rowH, TRUE);
            currentX += btnW + pad;

            MoveWindow(GetDlgItem(hWnd, ID_BTN_STEP), currentX, yBot, btnW, rowH, TRUE);
            currentX += btnW + pad; 
            
            int chkW = 105; 
            MoveWindow(GetDlgItem(hWnd, ID_CHK_CLEAR_ON_STEP), currentX, yBot, chkW, rowH, TRUE);
            currentX += chkW + pad;

            int clearW = 60;
            int xClear = w - clearW - pad;
            MoveWindow(GetDlgItem(hWnd, ID_BTN_CLEAR_LOG), xClear, yBot, clearW, rowH, TRUE);

            int exitW = 80; 
            int xExit = xClear - exitW - pad;
            MoveWindow(GetDlgItem(hWnd, ID_BTN_EXIT_GAME), xExit, yBot, exitW, rowH, TRUE);

            int hLog = yBot - pad - pad; 
            if (hLog < 0) hLog = 0;
            
            MoveWindow(GetDlgItem(hWnd, ID_CONSOLE_EDIT), pad, pad, w - 2*pad, hLog, TRUE);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            HDC hdc = (HDC)wParam;
            HWND hCtl = (HWND)lParam;
            if (hCtl == GetDlgItem(hWnd, ID_CONSOLE_EDIT)) {
                SetTextColor(hdc, RGB(0, 255, 0)); 
                SetBkColor(hdc, RGB(0, 0, 0));     
                SetBkMode(hdc, OPAQUE);
                if (host && host->hConsoleBrush) return (LRESULT)host->hConsoleBrush;
                return (LRESULT)GetStockObject(BLACK_BRUSH);
            }
            break;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            
            if (id == ID_CHK_CLEAR_ON_STEP && host) {
                host->optClearOnStep = (IsDlgButtonChecked(hWnd, ID_CHK_CLEAR_ON_STEP) == BST_CHECKED);
            }
            else if (id == ID_BTN_CLEAR_LOG) {
                SetDlgItemText(hWnd, ID_CONSOLE_EDIT, "");
            }
            else if (id == ID_BTN_EXIT_GAME && linkedVM) { 
                if (MessageBox(hWnd, "Are you sure you want to stop the game?", "Confirm Exit", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    linkedVM->exit_requested = true;

                    if (linkedVM->debug.paused) {
                        linkedVM->debug.paused = false;
                        linkedVM->debug.step_mode = false;
                    }
                    
                    if (host) host->log("[DBG] Force Exit requested.");
                }
            }
            else if (id == ID_BTN_RESUME && linkedVM) {
                if (linkedVM->debug.paused) {
                    linkedVM->debug.step_mode = false;
                    linkedVM->debug.paused = false;
                    host->log("[DBG] Resumed.");
                } else {
                    linkedVM->debug.togglePause();
                    // Update UI immediately to prevent lag in button state
                    EnableWindow(GetDlgItem(hWnd, ID_BTN_STEP), TRUE);
                    SetWindowText(GetDlgItem(hWnd, ID_BTN_RESUME), "Resume (F5)");
                }
            }
            else if (id == ID_BTN_STEP && linkedVM) {
                if (host && host->optClearOnStep) {
                    SetDlgItemText(hWnd, ID_CONSOLE_EDIT, "");
                }
                linkedVM->debug.step();
            }
            return 0;
        }

        case WM_DESTROY: 
            linkedVM = nullptr; 
            break;

        case WM_CLOSE:
            if (linkedVM && linkedVM->debug.paused) {
                linkedVM->debug.paused = false;
                linkedVM->debug.step_mode = false;
                if (host) host->log("[DBG] Console closed. Resuming execution.");
            }
            ShowWindow(hWnd, SW_HIDE);
            if (host) host->setConsoleState(false);
            return 0;
        }
        
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    // --- REAL-TIME MODDING WNDPROC ---
    static LRESULT CALLBACK RealtimeModWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowsHost* host = (WindowsHost*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!host && message == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            host = (WindowsHost*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)host);
        }
        if (!host) return DefWindowProc(hWnd, message, wParam, lParam);

        switch (message)
        {
        case WM_CREATE:
            host->stopModAutoRefresh();
            host->rebuildRealtimeModList();
            return 0;

        case WM_SIZE:
            host->layoutRealtimeModControls();
            return 0;

        case WM_MOUSEWHEEL:
            host->handleRealtimeScroll(-(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * 20);
            return 0;

        case WM_VSCROLL:
            if (LOWORD(wParam) == SB_THUMBTRACK || LOWORD(wParam) == SB_THUMBPOSITION) {
                int pos = HIWORD(wParam);
                host->modScrollOffset = pos;
                host->layoutRealtimeModControls();
            } else if (LOWORD(wParam) == SB_LINEUP) {
                host->handleRealtimeScroll(-20);
            } else if (LOWORD(wParam) == SB_LINEDOWN) {
                host->handleRealtimeScroll(20);
            }
            return 0;

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id == ID_MOD_EXPORT_VARS) {
                host->exportFavoriteVars();
                return 0;
            }
            if (id == ID_MOD_SEND_COMMAND) {
                host->openCommandPrompt();
                return 0;
            }
            if (id >= ID_MOD_CHECK_BASE && id < ID_MOD_EDIT_BASE) {
                int idx = id - ID_MOD_CHECK_BASE;
                if (idx >= 0 && idx < (int)host->modEntries.size()) {
                    bool checked = (SendMessage(host->modEntries[idx].checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    host->modEntries[idx].locked = checked;
                    host->modEntries[idx].dirty = true;
                    EnableWindow(host->modEntries[idx].edit, !checked);
                }
                return 0;
            }
            if (id >= ID_MOD_EDIT_BASE && id < ID_BTN_REFRESH_VARS) {
                int idx = id - ID_MOD_EDIT_BASE;
                if (idx >= 0 && idx < (int)host->modEntries.size()) {
                    char buf[256] = {0};
                    GetWindowText(host->modEntries[idx].edit, buf, 255);
                    host->modEntries[idx].value = buf;
                    host->modEntries[idx].dirty = true;
                }
                return 0;
            }
            if (id >= ID_MOD_FAV_BASE && id < ID_MOD_FAV_BASE + 1000) {
                int idx = id - ID_MOD_FAV_BASE;
                if (idx >= 0 && idx < (int)host->modEntries.size()) {
                    bool isFav = (SendMessage(host->modEntries[idx].favoriteCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    host->modEntries[idx].favorite = isFav;
                    host->layoutRealtimeModControls();
                }
                return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == ID_MOD_AUTO_TIMER) {
                host->rebuildRealtimeModList();
                return 0;
            }
            break;

        case WM_SETFOCUS:
        case WM_ACTIVATE:
            if (message == WM_SETFOCUS || LOWORD(wParam) != WA_INACTIVE) {
                host->stopModAutoRefresh();
            } else {
                host->startModAutoRefresh();
            }
            return 0;

        case WM_KILLFOCUS:
            host->startModAutoRefresh();
            return 0;

        case WM_CLOSE:
            ShowWindow(hWnd, SW_HIDE);
            host->isModWindowActive = false;
            host->stopModAutoRefresh();
            return 0;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

public:
    Real8VM* debugVMRef = nullptr;

    // Real-time modding helpers
    //void openRealtimeModWindow();
    //void applyRealtimeMods();
    bool isRealtimeModWindowOpen() const { return isModWindowActive; }

    // --- Helper to process typed commands ---
    void ProcessDebugCommand(const char* cmd) {
        if (!debugVMRef) return;
        std::string s(cmd);
        log("> %s", cmd);

        std::stringstream ss(s);
        std::string action;
        ss >> action;

        if (action == "b" || action == "break") {
            int line;
            if (ss >> line) {
                debugVMRef->debug.addBreakpoint(line);
            } else {
                log("Usage: b <line_number>");
            }
        }
        else if (action == "cb" || action == "clearbreak") {
            int line;
            if (ss >> line) debugVMRef->debug.removeBreakpoint(line);
            else debugVMRef->debug.clearBreakpoints();
        }
        else if (action == "p" || action == "print") {
            std::string var;
            ss >> var;
            std::string val = debugVMRef->debug.inspectVariable(debugVMRef->getLuaState(), var);
            log("%s = %s", var.c_str(), val.c_str());
        }
        else if (action == "m" || action == "mem") {
            int addr, len=16;
            ss >> std::hex >> addr;
            if (!(ss >> std::dec >> len)) len = 16;
            std::string dump = debugVMRef->debug.dumpMemory(addr, len);
            log("%s", dump.c_str());
        }
        else if (action == "poke") {
            int addr, val;
            ss >> std::hex >> addr >> val;
            debugVMRef->debug.poke(addr, (uint8_t)val);
        }
        else {
            log("Unknown command. Try: b, p, m, poke, cb");
        }
    }

    void waitForDebugEvent() override {
        // 1. Keep SDL Internal Events alive (Prevents Main Window "Not Responding")
        SDL_PumpEvents(); 

        // 2. Process Console UI Events (Keeps Debugger Buttons clickable)
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // 3. Sleep to save CPU
        SDL_Delay(10);
    }

    // To enable the "Enter" key, we need to subclass the Input Edit Control.
    // Add this static WNDPROC:
    static WNDPROC wpOrigEdit;
    static LRESULT APIENTRY EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
            // Retrieve text
            char buf[256];
            GetWindowText(hwnd, buf, 256);
            SetWindowText(hwnd, ""); // Clear
            
            // Dispatch to Host (via user data stored in parent)
            HWND hParent = GetParent(hwnd);
            WindowsHost* host = (WindowsHost*)GetWindowLongPtr(hParent, GWLP_USERDATA);
            if(host) host->ProcessDebugCommand(buf);
            
            return 0;
        }
        return CallWindowProc(wpOrigEdit, hwnd, uMsg, wParam, lParam);
    }

    // --- HELPER: Validate JSON Structure ---
    static bool ValidateRepoJSON(const std::string& filePath) {
        std::ifstream f(filePath);
        if (!f.is_open()) return false;
        
        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string content = buffer.str();
        
        // 1. Basic JSON Syntax Check (Must start with [ or {)
        // Skip whitespace
        size_t start = content.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return false;
        if (content[start] != '[' && content[start] != '{') return false;

        // 2. Real-8 Structure Check (Heuristic)
        // The parser expects specific keys. We check if at least one entry exists.
        bool hasUrl = (content.find("\"url\"") != std::string::npos);
        bool hasName = (content.find("\"name\"") != std::string::npos);
        
        // If it doesn't have a name or a url key, it's likely not a valid games list
        if (!hasUrl && !hasName) return false;

        return true;
    }

    // --- HELPER: Logic to check URL validity ---
    static bool CheckAndValidateURL(HWND hWnd, const char* url) {
        std::string sUrl = url;

        // 1. Check Extension
        if (sUrl.length() < 5 || sUrl.substr(sUrl.length() - 5) != ".json") {
            MessageBox(hWnd, "The URL must end with '.json'", "Invalid Extension", MB_OK | MB_ICONERROR);
            return false;
        }

        // 2. Download to Temp to Check Structure
        char tempPath[MAX_PATH];
        GetTempPath(MAX_PATH, tempPath);
        std::string tempFile = std::string(tempPath) + "real8_validate.json";

        // Visual feedback (Cursor)
        HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

        // Use standard URLDownloadToFile (blocking)
        // Delete previous temp file if exists to avoid caching issues
        DeleteFile(tempFile.c_str()); 
        
        HRESULT hr = URLDownloadToFileA(NULL, sUrl.c_str(), tempFile.c_str(), 0, NULL);
        
        SetCursor(hOldCursor);

        if (hr != S_OK) {
            MessageBox(hWnd, "Could not connect to the provided URL.\nPlease check your internet connection and the link.", "Connection Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        // 3. Validate Content
        if (!ValidateRepoJSON(tempFile)) {
            MessageBox(hWnd, "The file was downloaded but the structure is incorrect.\nIt must be a valid JSON Game List containing 'name' and 'url' fields.", "Invalid JSON Structure", MB_OK | MB_ICONERROR);
            DeleteFile(tempFile.c_str());
            return false;
        }

        DeleteFile(tempFile.c_str());
        return true;
    }

    // ... (Existing Helpers: ResolveVirtualPath, etc.) ...
    std::string resolveVirtualPath(const char *filename)
    {
        std::string fname = filename;
        if (!fname.empty() && fname[0] == '/')
            fname = fname.substr(1);

        fs::path targetDir;

        // Check for Saves
        if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".sav")
        {
            targetDir = rootPath / "saves";
        }
        // Added "gamesrepo.txt" to the config folder whitelist
        else if (fname == "config.dat" || fname == "wallpaper.png" || fname == "favorites.txt" || fname == "gameslist.json" || fname == "gamesrepo.txt")
        {
            targetDir = rootPath / "config";
        }
        else
        {
            targetDir = rootPath;
        }

        if (!fs::exists(targetDir))
            fs::create_directories(targetDir);
        return (targetDir / fname).string();
    }

    std::string getRepoUrlFromFile() override
    {
        // This will now resolve to ./data/config/gamesrepo.txt
        std::string path = resolveVirtualPath("gamesrepo.txt");
        
        if (!fs::exists(path)) return ""; 

        std::ifstream file(path);
        if (file.is_open()) {
            std::string url;
            std::getline(file, url);
            
            // Trim whitespace
            const char* ws = " \t\n\r\f\v";
            url.erase(url.find_last_not_of(ws) + 1);
            url.erase(0, url.find_first_not_of(ws));

            if (!url.empty()) return url;
        }
        return "";
    }

    void saveRepoUrlToFile(const std::string& url) override
    {
        // This will now resolve to ./data/config/gamesrepo.txt
        std::string path = resolveVirtualPath("gamesrepo.txt");
        
        std::ofstream file(path, std::ios::trunc); // Overwrite mode
        if (file.is_open()) {
            file << url;
        }
    }

    int getModeWindowScale(int mode) const
    {
        switch (mode) {
        case 1: return 3;
        case 2: return 2;
        case 3: return 1;
        default: return 1;
        }
    }

    void calculateGameRect(int winW, int winH, SDL_Rect *outRect, float *outScale)
    {
        int padding = 0;

        int availW = winW - (padding * 2);
        int availH = winH - (padding * 2);

        if (availW < 1) availW = 1;
        if (availH < 1) availH = 1;

        bool stretch = (debugVMRef && debugVMRef->stretchScreen);
        int gameW = (debugVMRef && debugVMRef->fb_w > 0) ? debugVMRef->fb_w : 128;
        int gameH = (debugVMRef && debugVMRef->fb_h > 0) ? debugVMRef->fb_h : 128;
        if (stretch) {
            outRect->x = padding;
            outRect->y = padding;
            outRect->w = availW;
            outRect->h = availH;
            if (outScale) *outScale = (float)availW / (float)gameW;
        } else {
            float scale = std::min((float)availW / (float)gameW, (float)availH / (float)gameH);
            int drawW = (int)((float)gameW * scale);
            int drawH = (int)((float)gameH * scale);

            outRect->x = (winW - drawW) / 2;
            outRect->y = (winH - drawH) / 2;
            outRect->w = drawW;
            outRect->h = drawH;

            if (outScale) *outScale = scale;
        }
    }

    static LRESULT CALLBACK InputBoxWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        static char *outputBuffer = nullptr;
        switch (message)
        {
        case WM_CREATE:
            CreateWindow("STATIC", "Enter new filename:", WS_VISIBLE | WS_CHILD, 10, 10, 260, 20, hWnd, NULL, NULL, NULL);
            SetFocus(CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 260, 20, hWnd, (HMENU)101, NULL, NULL));
            CreateWindow("BUTTON", "OK", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 100, 70, 80, 25, hWnd, (HMENU)(UINT_PTR)IDOK, NULL, NULL);
            CreateWindow("BUTTON", "Cancel", WS_VISIBLE | WS_CHILD, 190, 70, 80, 25, hWnd, (HMENU)(UINT_PTR)IDCANCEL, NULL, NULL);
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                HWND hEdit = GetDlgItem(hWnd, 101);
                int len = GetWindowTextLength(hEdit);
                if (len > 0 && outputBuffer) GetWindowText(hEdit, outputBuffer, 255);
                EndDialog(hWnd, IDOK);
                DestroyWindow(hWnd);
            }
            else if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hWnd, IDCANCEL);
                DestroyWindow(hWnd);
            }
            break;
        case WM_CLOSE:
            EndDialog(hWnd, IDCANCEL);
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }

    // --- CUSTOM DIALOG PROC FOR REPO SETTINGS ---
    static LRESULT CALLBACK RepoBoxWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            // Retrieve params passed in CreateWindowEx
            CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
            const char* current = (const char*)pCreate->lpCreateParams;

            CreateWindow("STATIC", "Repository URL:", WS_VISIBLE | WS_CHILD, 10, 10, 360, 20, hWnd, NULL, NULL, NULL);
            
            HWND hEdit = CreateWindow("EDIT", current, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 
                                      10, 35, 360, 20, hWnd, (HMENU)(UINT_PTR)ID_EDIT_URL, NULL, NULL);

            CreateWindow("BUTTON", "Reset", WS_VISIBLE | WS_CHILD, 10, 70, 80, 25, hWnd, (HMENU)(UINT_PTR)ID_BTN_RESET, NULL, NULL);
            CreateWindow("BUTTON", "Save", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 200, 70, 100, 25, hWnd, (HMENU)(UINT_PTR)ID_BTN_SAVE, NULL, NULL);
            CreateWindow("BUTTON", "Cancel", WS_VISIBLE | WS_CHILD, 310, 70, 60, 25, hWnd, (HMENU)(UINT_PTR)IDCANCEL, NULL, NULL);
            break;
        }
        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id == ID_BTN_SAVE) {
                HWND hEdit = GetDlgItem(hWnd, ID_EDIT_URL);
                int len = GetWindowTextLength(hEdit);
                
                char tempBuf[512] = {0};
                if (len > 0) GetWindowText(hEdit, tempBuf, 512);

                // --- NEW VALIDATION LOGIC ---
                if (CheckAndValidateURL(hWnd, tempBuf)) {
                    // Only save to the output buffer if validation passes
                    if (s_repoBuffer) {
                        strcpy_s(s_repoBuffer, 512, tempBuf);
                    }
                    EndDialog(hWnd, IDOK);
                    DestroyWindow(hWnd);
                }
                // If validation failed, CheckAndValidateURL showed a messagebox 
                // and we do NOT close the window, allowing the user to fix it.
            }
            else if (id == ID_BTN_RESET) {
                // Reset text box to default constant
                if (s_defaultRepoUrl) {
                    SetDlgItemText(hWnd, ID_EDIT_URL, s_defaultRepoUrl);
                }
            }
            else if (id == IDCANCEL) {
                EndDialog(hWnd, IDCANCEL);
                DestroyWindow(hWnd);
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hWnd, IDCANCEL);
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }

    bool ShowInputBox(std::string &outName, const std::string &defaultName)
    {
        const char *className = "Real8InputBox";
        WNDCLASS wc = {0};
        wc.lpfnWndProc = InputBoxWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = className;
        RegisterClass(&wc);

        HWND hParent = GetActiveWindow();
        HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, className, "Rename File", WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 300, 300, 300, 140, hParent, NULL, GetModuleHandle(NULL), NULL);
        if (!hWnd) return false;

        HWND hEdit = GetDlgItem(hWnd, 101);
        SetWindowText(hEdit, defaultName.c_str());
        SendMessage(hEdit, EM_SETSEL, 0, -1);

        char buffer[256] = {0};
        EnableWindow(hParent, FALSE);

        bool result = false;
        MSG msg;
        while (IsWindow(hWnd)) {
            if (GetMessage(&msg, NULL, 0, 0)) {
                if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) SendMessage(hWnd, WM_COMMAND, IDOK, 0);
                else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) SendMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
                if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == IDOK) {
                    GetWindowText(GetDlgItem(hWnd, 101), buffer, 255);
                    outName = std::string(buffer);
                    result = (!outName.empty());
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        UnregisterClass(className, GetModuleHandle(NULL));
        return result;
    }

    static SDL_Scancode GetScancodeFromWinKey(WPARAM wParam)
    {
        SDL_Keycode keycode = SDLK_UNKNOWN;
        switch (wParam) {
            case VK_LEFT: keycode = SDLK_LEFT; break;
            case VK_RIGHT: keycode = SDLK_RIGHT; break;
            case VK_UP: keycode = SDLK_UP; break;
            case VK_DOWN: keycode = SDLK_DOWN; break;
            case VK_RETURN: keycode = SDLK_RETURN; break;
            case VK_SPACE: keycode = SDLK_SPACE; break;
            case VK_TAB: keycode = SDLK_TAB; break;
            case VK_BACK: keycode = SDLK_BACKSPACE; break;
            case VK_DELETE: keycode = SDLK_DELETE; break;
            case VK_HOME: keycode = SDLK_HOME; break;
            case VK_END: keycode = SDLK_END; break;
            case VK_PRIOR: keycode = SDLK_PAGEUP; break;
            case VK_NEXT: keycode = SDLK_PAGEDOWN; break;
            case VK_INSERT: keycode = SDLK_INSERT; break;
            case VK_SHIFT: keycode = SDLK_LSHIFT; break;
            case VK_CONTROL: keycode = SDLK_LCTRL; break;
            case VK_MENU: keycode = SDLK_LALT; break;
            case VK_F1: keycode = SDLK_F1; break;
            case VK_F2: keycode = SDLK_F2; break;
            case VK_F3: keycode = SDLK_F3; break;
            case VK_F4: keycode = SDLK_F4; break;
            case VK_F5: keycode = SDLK_F5; break;
            case VK_F6: keycode = SDLK_F6; break;
            case VK_F7: keycode = SDLK_F7; break;
            case VK_F8: keycode = SDLK_F8; break;
            case VK_F9: keycode = SDLK_F9; break;
            case VK_F10: keycode = SDLK_F10; break;
            case VK_F11: keycode = SDLK_F11; break;
            case VK_F12: keycode = SDLK_F12; break;
            default:
                UINT ch = MapVirtualKey((UINT)wParam, MAPVK_VK_TO_CHAR);
                if (ch != 0) {
                    if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
                    keycode = (SDL_Keycode)ch;
                }
                break;
        }

        if (keycode == SDLK_UNKNOWN) return SDL_SCANCODE_UNKNOWN;
        return SDL_GetScancodeFromKey(keycode);
    }

    // --- Modern Remap keyboard Configuration Dialog ---
    static LRESULT CALLBACK ConfigWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        static WindowsInput* inputRef = nullptr;
        static int selectedPlayer = 0;
        static HWND hCombo, hBtnRemap, hBtnDone, hStatusLabel;
        static HFONT hFont;

        const int ID_COMBO = 201;
        const int ID_BTN_REMAP = 202;
        const int ID_LBL_STATUS = 203;

        switch (message)
        {
        case WM_CREATE:
        {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            inputRef = (WindowsInput*)pCreate->lpCreateParams;

            // 1. Create Font
            hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            int margin = 20;
            int winW = 380;
            int y = margin;

            // Label: Player Selection
            HWND hLbl = CreateWindow("STATIC", "Select Player Slot:", WS_VISIBLE | WS_CHILD, 
                margin, y, winW - (margin * 2), 20, hWnd, NULL, NULL, NULL);
            SendMessage(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 25;

            // ComboBox
            hCombo = CreateWindow("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 
                margin, y, winW - (margin * 2), 200, hWnd, (HMENU)(UINT_PTR)ID_COMBO, NULL, NULL);
            SendMessage(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            for(int i=0; i<8; i++) {
                char buf[32]; sprintf(buf, "Player %d", i+1);
                SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
            }
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            y += 40;

            // Separator
            CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ, 
                margin, y, winW - (margin * 2), 2, hWnd, NULL, NULL, NULL);
            y += 15;

            // --- CHANGED: Increased Height from 40 to 80 ---
            hStatusLabel = CreateWindow("STATIC", "Ready to configure.", WS_VISIBLE | WS_CHILD | SS_CENTER, 
                margin, y, winW - (margin * 2), 60, hWnd, (HMENU)(UINT_PTR)ID_LBL_STATUS, NULL, NULL);
            SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Push subsequent elements down by 85px (80 height + 5 padding)
            y += 65; 

            // Remap Button
            hBtnRemap = CreateWindow("BUTTON", "Start Button Mapping", WS_VISIBLE | WS_CHILD, 
                margin, y, winW - (margin * 2), 35, hWnd, (HMENU)(UINT_PTR)ID_BTN_REMAP, NULL, NULL);
            SendMessage(hBtnRemap, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 45;

            // Done Button
            hBtnDone  = CreateWindow("BUTTON", "Done", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
                winW - 100 - margin, y + 10, 100, 30, hWnd, (HMENU)(UINT_PTR)IDOK, NULL, NULL);
            SendMessage(hBtnDone, WM_SETFONT, (WPARAM)hFont, TRUE);

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            SetBkColor(hdcStatic, GetSysColor(COLOR_WINDOW));
            SetBkMode(hdcStatic, OPAQUE);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == ID_COMBO && code == CBN_SELCHANGE) {
                selectedPlayer = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            }
            else if (id == ID_BTN_REMAP) {
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

                PlayerConfig* cfg = inputRef->getConfig(selectedPlayer);
                const char* actions[] = {"LEFT", "RIGHT", "UP", "DOWN", "O (A)", "X (B)", "MENU"};
                
                EnableWindow(hBtnRemap, FALSE);
                EnableWindow(hBtnDone, FALSE);
                EnableWindow(hCombo, FALSE);

                for(int i=0; i<7; i++) {
                    if (!IsWindow(hWnd)) break;

                    char buf[128]; 
                    // Added extra newline for spacing
                    sprintf(buf, "Press Keyboard Button for:\n\n>>> %s <<<", actions[i]);
                    SetWindowText(hStatusLabel, buf);
                    UpdateWindow(hStatusLabel);

                    bool mapped = false;
                    
                    SDL_PumpEvents();
                    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

                    while(!mapped) {
                        MSG msg;
                        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                                if (msg.wParam == VK_ESCAPE) {
                                    mapped = true; 
                                } else {
                                    SDL_Scancode scancode = GetScancodeFromWinKey(msg.wParam);
                                    if (scancode != SDL_SCANCODE_UNKNOWN) {
                                        cfg->assignedJoystickIndex = -1;
                                        cfg->keyMap[i] = scancode;
                                        mapped = true;
                                    }
                                }
                            }
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                            if (msg.message == WM_QUIT) {
                                mapped = true; i = 7; 
                            }
                        }

                        if (!IsWindow(hWnd)) { mapped = true; i = 7; break; }

                        SDL_PumpEvents(); 
                        SDL_Event e;
                        while(SDL_PollEvent(&e)) {
                            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                                cfg->assignedJoystickIndex = selectedPlayer; 
                                cfg->btnMap[i] = (SDL_GameControllerButton)e.cbutton.button;
                                mapped = true;
                            } 
                            else if (e.type == SDL_QUIT) {
                                mapped = true; i = 7;
                            }
                        }
                        SDL_Delay(10);
                    }
                    SDL_Delay(200);
                }

                if (IsWindow(hWnd)) {
                    SetWindowText(hStatusLabel, "Mapping Complete!");
                    MessageBox(hWnd, "Controller mapped successfully.", "Success", MB_OK | MB_ICONINFORMATION);
                    
                    SetWindowText(hStatusLabel, "Ready to configure.");
                    EnableWindow(hBtnRemap, TRUE);
                    EnableWindow(hBtnDone, TRUE);
                    EnableWindow(hCombo, TRUE);
                    SetFocus(hBtnDone);
                }

                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
            } 
            else if (id == IDOK) {
                EndDialog(hWnd, IDOK);
                DestroyWindow(hWnd); 
                return 0;
            }
            else if (id == IDCANCEL) {
                EndDialog(hWnd, IDCANCEL);
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        } 
        case WM_DESTROY:
            if (hFont) DeleteObject(hFont);
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }

    // --- INJECT LUA PROMPT ---
    static LRESULT CALLBACK CommandPromptWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowsHost* host = (WindowsHost*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!host && message == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            host = (WindowsHost*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)host);
        }
        HFONT hFont = (HFONT)GetProp(hWnd, "CmdFont");

        switch (message)
        {
        case WM_CREATE:
        {
            hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            SetProp(hWnd, "CmdFont", hFont);

            HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN,
                0, 0, 0, 0, hWnd, (HMENU)(UINT_PTR)ID_CMD_PROMPT_EDIT, NULL, NULL);
            HWND hSend = CreateWindow("BUTTON", "Send",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, hWnd, (HMENU)(UINT_PTR)ID_CMD_PROMPT_SEND, NULL, NULL);
            HWND hCancel = CreateWindow("BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, hWnd, (HMENU)(UINT_PTR)ID_CMD_PROMPT_CANCEL, NULL, NULL);

            if (hFont) {
                SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hSend, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
            return 0;
        }

        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            int pad = 10;
            int rowH = 28;
            int btnW = 80;

            MoveWindow(GetDlgItem(hWnd, ID_CMD_PROMPT_EDIT), pad, pad, w - (2 * pad), h - rowH - (3 * pad), TRUE);
            MoveWindow(GetDlgItem(hWnd, ID_CMD_PROMPT_CANCEL), w - pad - (2 * btnW + pad), h - rowH - pad, btnW, rowH, TRUE);
            MoveWindow(GetDlgItem(hWnd, ID_CMD_PROMPT_SEND), w - pad - btnW, h - rowH - pad, btnW, rowH, TRUE);
            return 0;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id == ID_CMD_PROMPT_SEND) {
                HWND hEdit = GetDlgItem(hWnd, ID_CMD_PROMPT_EDIT);
                int len = GetWindowTextLengthA(hEdit);
                std::string buf(len, '\0');
                GetWindowTextA(hEdit, buf.data(), len + 1);
                if (len > 0) buf.resize(len);
                if (host) host->executeLuaCommand(buf);
                DestroyWindow(hWnd);
                return 0;
            }
            if (id == ID_CMD_PROMPT_CANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (hFont) {
                DeleteObject(hFont);
                RemoveProp(hWnd, "CmdFont");
            }
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

public:
    bool crt_filter = false;
    bool interpolation = false;

    const char *getPlatform() override { return "Windows"; }

    std::string getClipboardText() override {
        if (SDL_HasClipboardText()) {
            char* text = SDL_GetClipboardText();
            if (text) {
                std::string result(text);
                SDL_free(text);
                return result;
            }
        }
        return "";
    }

    WindowsHost(SDL_Renderer *r, SDL_Window* win) 
        : renderer(r), texture(nullptr), wallpaperTex(nullptr), sdlWindow(win)
    {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
        if (sdlWindow) {
            SDL_GetWindowSize(sdlWindow, &defaultWindowW, &defaultWindowH);
        }
        screenBuffer.resize((size_t)screenW * (size_t)screenH);

        // OPEN LOG FILE (Overwrites previous log on startup)
        logFile.open("logs.txt", std::ios::out | std::ios::trunc);
        if (logFile.is_open()) {
            logFile << "=== REAL-8 SESSION STARTED ===\n";
            logFile.flush();
        }

        input.init();
        initAudio();
        rootPath = fs::current_path() / "data";
        if (!fs::exists(rootPath)) fs::create_directory(rootPath);
        fs::path modsPath = rootPath / "mods";
        if (!fs::exists(modsPath)) fs::create_directory(modsPath);
        setInterpolation(false);
        initConsoleWindow();
    }

    // --- Custom Console Initialization ---
    void initConsoleWindow()
    {
        const char* className = "Real8DebugConsole";
        WNDCLASS wc = {0};
        wc.lpfnWndProc = ConsoleWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = className;
        RegisterClass(&wc);

        hConsoleBrush = CreateSolidBrush(RGB(0, 0, 0));
        hConsoleWnd = CreateWindowEx(0, className, "Real-8 Debug Console",
            WS_OVERLAPPEDWINDOW, 
            CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, 
            NULL, NULL, GetModuleHandle(NULL), NULL);

        // Store pointer to this class so static WndProc can access 'optShowLuaErrors'
        SetWindowLongPtr(hConsoleWnd, GWLP_USERDATA, (LONG_PTR)this);
        
        hLogEdit = GetDlgItem(hConsoleWnd, ID_CONSOLE_EDIT);
    }

    // --- UPDATED: setConsoleState ---
    void setConsoleState(bool active) 
    { 
        // Prevent redundant updates
        if (isConsoleActive == active) return;

        isConsoleActive = active; 
        
        if (hConsoleWnd && IsWindow(hConsoleWnd)) {
            if (active) {
                ShowWindow(hConsoleWnd, SW_SHOW);
                SetForegroundWindow(hConsoleWnd);
            } else {
                ShowWindow(hConsoleWnd, SW_HIDE);
            }
        }
    }

    bool isConsoleOpen() override { return isConsoleActive; }

    void openRealtimeModWindow()
    {
        if (!debugVMRef || !debugVMRef->getLuaState()) {
            log("[MOD] No running game or Lua state to inspect.");
            return;
        }

        const char* className = "Real8RealtimeMods";
        static bool registered = false;
        if (!registered) {
            WNDCLASS wc = {0};
            wc.lpfnWndProc = RealtimeModWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = className;
            RegisterClass(&wc);
            registered = true;
        }

        if (!hModFont) {
            hModFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        }

        if (!hModWnd || !IsWindow(hModWnd)) {
            hModWnd = CreateWindowEx(0, className, "Real-8 RealTime Modding",
                WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                CW_USEDEFAULT, CW_USEDEFAULT, 520, 420,
                NULL, NULL, GetModuleHandle(NULL), (LPVOID)this);

            if (hModWnd) {
                SetWindowLongPtr(hModWnd, GWLP_USERDATA, (LONG_PTR)this);
            }
        }

        if (!hModWnd) return;

        if (!hModMenuBar) {
            hModMenuBar = CreateMenu();
            hModActionsMenu = CreatePopupMenu();
            AppendMenu(hModActionsMenu, MF_STRING, ID_MOD_EXPORT_VARS, "Export Patch");
            AppendMenu(hModActionsMenu, MF_STRING, ID_MOD_SEND_COMMAND, "Inject LUA code");
            AppendMenu(hModMenuBar, MF_POPUP, (UINT_PTR)hModActionsMenu, "Actions");
        }
        SetMenu(hModWnd, hModMenuBar);

        ShowWindow(hModWnd, SW_SHOW);
        SetForegroundWindow(hModWnd);
        isModWindowActive = true;
        modScrollOffset = 0;
        stopModAutoRefresh(); // start in paused state while focused
        setModWindowTitle();
        rebuildRealtimeModList();
    }

    void rebuildRealtimeModList()
    {
        if (!hModWnd) return;

        std::string currentId = debugVMRef ? debugVMRef->currentGameId : "";
        std::map<std::string, ModEntryRow> previous;
        for (auto &entry : modEntries) {
            if (!modTrackedGameId.empty() && modTrackedGameId == currentId) {
                previous[entry.name] = entry;
            }
            if (entry.checkbox) DestroyWindow(entry.checkbox);
            if (entry.edit) DestroyWindow(entry.edit);
            if (entry.favoriteCheck) DestroyWindow(entry.favoriteCheck);
        }
        modEntries.clear();

        if (!debugVMRef || !debugVMRef->getLuaState()) {
            modTrackedGameId.clear();
            modContentHeight = 0;
            layoutRealtimeModControls();
            return;
        }

        modTrackedGameId = currentId;

        std::vector<Real8Tools::StaticVarEntry> vars = Real8Tools::CollectStaticVars(debugVMRef);
        if (vars.empty()) {
            log("[MOD] No static Lua variables found.");
        }
        for (size_t i = 0; i < vars.size(); ++i) {
            ModEntryRow row;
            row.name = vars[i].name;
            row.value = vars[i].value;
            row.type = vars[i].type;

            auto it = previous.find(row.name);
            if (it != previous.end()) {
                row.locked = it->second.locked;
                row.favorite = it->second.favorite;
                // Preserve locked edits; otherwise show live values
                if (row.locked) {
                    if (!it->second.value.empty()) row.value = it->second.value;
                    row.dirty = true; // keep applying locked values
                } else {
                    row.value = vars[i].value; // show latest live value
                    row.dirty = false;
                }
            }

            row.checkbox = CreateWindow("BUTTON", row.name.c_str(),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                0, 0, 100, 20, hModWnd, (HMENU)(UINT_PTR)(ID_MOD_CHECK_BASE + i), NULL, NULL);

            row.edit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", row.value.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 120, 22, hModWnd, (HMENU)(UINT_PTR)(ID_MOD_EDIT_BASE + i), NULL, NULL);

            row.favoriteCheck = CreateWindow("BUTTON", "Favorite",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                0, 0, 80, 20, hModWnd, (HMENU)(UINT_PTR)(ID_MOD_FAV_BASE + i), NULL, NULL);

            if (hModFont) {
                SendMessage(row.checkbox, WM_SETFONT, (WPARAM)hModFont, TRUE);
                SendMessage(row.edit, WM_SETFONT, (WPARAM)hModFont, TRUE);
                SendMessage(row.favoriteCheck, WM_SETFONT, (WPARAM)hModFont, TRUE);
            }

            SendMessage(row.checkbox, BM_SETCHECK, row.locked ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(row.edit, !row.locked);
            SendMessage(row.favoriteCheck, BM_SETCHECK, row.favorite ? BST_CHECKED : BST_UNCHECKED, 0);

            modEntries.push_back(row);
        }

        int pad = 10;
        int rowH = 24;
        modContentHeight = pad + (int)modEntries.size() * (rowH + 6);
        layoutRealtimeModControls();
    }

    void setModWindowTitle()
    {
        if (!hModWnd) return;
        const char* state = modAutoRefreshPaused ? "Auto-refresh paused" : "Auto-refresh active";
        std::string title = std::string("Real-8 RealTime Modding - ") + state;
        SetWindowText(hModWnd, title.c_str());
    }

    void stopModAutoRefresh()
    {
        modAutoRefreshPaused = true;
        if (hModWnd) KillTimer(hModWnd, ID_MOD_AUTO_TIMER);
        setModWindowTitle();
    }

    void startModAutoRefresh()
    {
        modAutoRefreshPaused = false;
        if (hModWnd) SetTimer(hModWnd, ID_MOD_AUTO_TIMER, 3000, NULL);
        setModWindowTitle();
    }

    void layoutRealtimeModControls()
    {
        if (!hModWnd) return;

        RECT rc;
        GetClientRect(hModWnd, &rc);

        int pad = 10;
        int rowH = 24;
        int editW = 170;
        int favW = 90;
        int usableHeight = rc.bottom - (pad * 2);
        int checkW = rc.right - (pad * 2) - editW - favW - 16;
        if (checkW < 80) checkW = 80;
        int startY = pad - modScrollOffset;

        std::vector<size_t> order(modEntries.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return modEntries[a].favorite > modEntries[b].favorite;
        });

        for (size_t displayIdx = 0; displayIdx < order.size(); ++displayIdx) {
            auto &entry = modEntries[order[displayIdx]];
            int rowY = startY + (int)displayIdx * (rowH + 6);
            MoveWindow(entry.checkbox, pad, rowY, checkW, rowH, TRUE);
            MoveWindow(entry.edit, pad + checkW + 8, rowY, editW, rowH, TRUE);
            MoveWindow(entry.favoriteCheck, pad + checkW + 8 + editW + 8, rowY, favW, rowH, TRUE);
        }

        int maxScroll = std::max(0, modContentHeight - usableHeight);
        if (modScrollOffset > maxScroll) modScrollOffset = maxScroll;

        SCROLLINFO si = {0};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = modContentHeight;
        si.nPage = usableHeight;
        si.nPos = modScrollOffset;
        SetScrollInfo(hModWnd, SB_VERT, &si, TRUE);
    }

    void handleRealtimeScroll(int delta)
    {
        if (!hModWnd) return;
        RECT rc;
        GetClientRect(hModWnd, &rc);
        int pad = 10;
        int refreshH = 24;
        int usableHeight = rc.bottom - refreshH - (pad * 2);
        int maxScroll = std::max(0, modContentHeight - usableHeight);

        modScrollOffset += delta;
        if (modScrollOffset < 0) modScrollOffset = 0;
        if (modScrollOffset > maxScroll) modScrollOffset = maxScroll;
        layoutRealtimeModControls();
    }

    std::string chooseExportFolder(HWND owner)
    {
        char path[MAX_PATH];
        BROWSEINFO bi = {0};
        bi.lpszTitle = "Select Export Destination";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
        bi.hwndOwner = owner;
        LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
        if (pidl != 0) {
            SHGetPathFromIDList(pidl, path);
            IMalloc * imalloc = 0;
            if (SUCCEEDED(SHGetMalloc(&imalloc))) { imalloc->Free(pidl); imalloc->Release(); }
            return std::string(path);
        }
        return "";
    }

    void exportFavoriteVars()
    {
        if (!debugVMRef || !debugVMRef->getLuaState()) {
            log("[EXPORT] No running game to export.");
            return;
        }

        std::vector<Real8Tools::StaticVarEntry> favorites;
        for (const auto &entry : modEntries) {
            if (!entry.favorite) continue;
            Real8Tools::StaticVarEntry fav;
            fav.name = entry.name;
            fav.value = entry.value;
            fav.type = entry.type;
            favorites.push_back(fav);
        }

        if (favorites.empty()) {
            log("[EXPORT] No favorites selected to export.");
            return;
        }

        std::string folder = chooseExportFolder(hModWnd);
        if (folder.empty()) return;

        Real8Tools::ExportStaticVars(debugVMRef, this, folder, favorites);
    }

    void openCommandPrompt()
    {
        if (!debugVMRef || !debugVMRef->getLuaState()) {
            log("[MOD] No running Lua state to send commands.");
            return;
        }

        const char* className = "Real8CommandPrompt";
        static bool registered = false;
        if (!registered) {
            WNDCLASS wc = {0};
            wc.lpfnWndProc = CommandPromptWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = className;
            RegisterClass(&wc);
            registered = true;
        }

        HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, className, "Inject LUA code",
            WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 420, 200,
            hModWnd ? hModWnd : NULL, NULL, GetModuleHandle(NULL), (LPVOID)this);

        if (!hWnd) return;

        EnableWindow(hModWnd, FALSE);
        MSG msg;
        while (IsWindow(hWnd)) {
            if (GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        EnableWindow(hModWnd, TRUE);
        SetForegroundWindow(hModWnd);
    }

    void executeLuaCommand(const std::string& cmd)
    {
        if (!debugVMRef || !debugVMRef->getLuaState()) {
            log("[MOD] No Lua state available to execute commands.");
            return;
        }
        if (cmd.empty()) {
            log("[MOD] Command is empty.");
            return;
        }

        lua_State* L = debugVMRef->getLuaState();

        // Temporarily override print so Send Command output also reaches the host console.
        lua_getglobal(L, "print");
        lua_setglobal(L, "__real8_cmd_orig_print");
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, LuaCmdPrintBridge, 1);
        lua_setglobal(L, "print");

        int result = luaL_loadstring(L, cmd.c_str());
        if (result == LUA_OK) {
            result = lua_pcall(L, 0, 0, 0);
        }

        // Restore original print regardless of success or failure.
        lua_getglobal(L, "__real8_cmd_orig_print");
        if (lua_isfunction(L, -1)) {
            lua_setglobal(L, "print");
        } else {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_setglobal(L, "print");
        }
        lua_pushnil(L);
        lua_setglobal(L, "__real8_cmd_orig_print");

        if (result != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            log("[MOD] Lua Error: %s", err ? err : "(unknown)");
            lua_pop(L, 1);
            return;
        }

        log("Command Executed: %s", cmd.c_str());
    }

    void applyRealtimeMods()
    {
        if (!debugVMRef || !debugVMRef->getLuaState()) return;
        lua_State* L = debugVMRef->getLuaState();
        if (!modTrackedGameId.empty() && modTrackedGameId != debugVMRef->currentGameId) return;

        for (auto &entry : modEntries) {
            if (!entry.locked && !entry.dirty) continue;

            switch (entry.type) {
                case Real8Tools::StaticVarEntry::Type::Number:
                {
                    double v = strtod(entry.value.c_str(), nullptr);
                    lua_pushnumber(L, v);
                    break;
                }
                case Real8Tools::StaticVarEntry::Type::Boolean:
                {
                    std::string lower = entry.value;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    bool v = (lower == "true" || lower == "1" || lower == "yes");
                    lua_pushboolean(L, v);
                    break;
                }
                case Real8Tools::StaticVarEntry::Type::String:
                default:
                    lua_pushstring(L, entry.value.c_str());
                    break;
            }

            lua_setglobal(L, entry.name.c_str());
            entry.dirty = entry.locked;
        }
    }

    // --- UPDATED: log function ---
    void log(const char *fmt, ...) override
    {
        const int BUF_SIZE = 4096;
        char buffer[BUF_SIZE];
        
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, BUF_SIZE - 4, fmt, args); 
        va_end(args);

        // A. Filter logic (same as before)
        std::string msg(buffer);
        bool isLuaError = (msg.find("[LUA ERROR]") != std::string::npos);
        if (isLuaError && !optShowLuaErrors) return;

        // B. Write to Standard Console
        printf("%s\n", buffer);

        // C. Write to FILE (Flush immediately so we don't lose data on crash)
        if (logFile.is_open()) {
            logFile << buffer << "\n";
            logFile.flush(); 
        }

        // D. Write to GUI Console
        if (hLogEdit && !optPauseLogs) {
            strcat_s(buffer, BUF_SIZE, "\r\n"); 
            int len = GetWindowTextLength(hLogEdit);
            if (len > 30000) SetWindowText(hLogEdit, ""); // Prevent overflow
            SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)buffer);
        }
    }
    
    uint32_t getPlayerInput(int playerIdx) override { return input.getMask(playerIdx); }
    void pollInput() override { input.update(); }
    void clearInputState() override { input.clearState(); }
    bool isKeyDownScancode(int scancode) override
    {
        if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return false;
        const Uint8 *state = SDL_GetKeyboardState(NULL);
        return state && state[scancode];
    }
    std::vector<uint8_t> getInputConfigData() override { return input.serialize(); }
    void setInputConfigData(const std::vector<uint8_t>& data) override { input.deserialize(data); }

    void openGamepadConfigUI() override
    {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(sdlWindow, &wmInfo);
        HWND hParent = wmInfo.info.win.window;

        const char* className = "Real8GamepadConfig";
        WNDCLASS wc = {0};
        wc.lpfnWndProc = ConfigWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = className;
        RegisterClass(&wc);

        // --- CHANGED: Increased Window Height from 280 to 350 ---
        HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, className, "Remap keyboard",
                       WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 
                       300, 300, 
                       400, 300, // <-- New Height
                       hParent, NULL, GetModuleHandle(NULL), (LPVOID)&input);
                       
        EnableWindow(hParent, FALSE);
        MSG msg;
        while (IsWindow(hWnd)) {
            if (GetMessage(&msg, NULL, 0, 0)) {
                 TranslateMessage(&msg);
                 DispatchMessage(&msg);
            }
        }
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        UnregisterClass(className, GetModuleHandle(NULL));
    }

    void setInterpolation(bool active)
    {
        this->interpolation = active; 
        // Force texture destruction. This ensures flipScreen() recreates it 
        // with the correct flags immediately on the next frame.
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    void onFramebufferResize(int fb_w, int fb_h) override
    {
        if (!sdlWindow) return;
        const int mode = (debugVMRef ? debugVMRef->r8_vmode_cur : 0);
        if (mode == 0) {
            if (defaultWindowW > 0 && defaultWindowH > 0) {
                SDL_SetWindowSize(sdlWindow, defaultWindowW, defaultWindowH);
            }
        } else {
            const int scale = getModeWindowScale(mode);
            SDL_SetWindowSize(sdlWindow, fb_w * scale, fb_h * scale);
        }
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    ~WindowsHost() { 
        if (logFile.is_open()) logFile.close();
        if (wallpaperTex) SDL_DestroyTexture(wallpaperTex); 
        if (texture) SDL_DestroyTexture(texture);
        if (hConsoleWnd) DestroyWindow(hConsoleWnd);
        if (hConsoleBrush) DeleteObject(hConsoleBrush);
        if (hModWnd) DestroyWindow(hModWnd);
        if (hModFont) DeleteObject(hModFont);
        if (hModMenuBar) DestroyMenu(hModMenuBar);
    }

    void initAudio()
    {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        
        want.freq = 22050;       // PICO-8 standard
        want.format = AUDIO_S16SYS;
        want.channels = 1;       // Mono
        want.samples = 1024;     // Internal buffer size
        want.callback = NULL;    // We use SDL_QueueAudio

        // Change 'SDL_AUDIO_ALLOW_FORMAT_CHANGE' to 0.
        // This forces SDL to emulate 22050Hz S16 even if the hardware is 48000Hz Float.
        audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

        if (audioDevice != 0) {
            SDL_PauseAudioDevice(audioDevice, 0); // Unpause immediately
        }
    }

    // In windows_host.hpp

    void pushAudio(const int16_t *samples, int count) override
    {
        if (audioDevice == 0 || samples == nullptr || count == 0) return;

        // --- AUDIO SYNC FIX ---
        // Don't drop samples. If the queue is full, we WAIT.
        // This syncs the game loop to the audio hardware, preventing pops/clicks.
        
        const Uint32 TARGET_QUEUE_BYTES = 1024 * sizeof(int16_t); // ~46ms latency target
        const Uint32 MAX_WAIT_CYCLES = 500; // Timeout safety

        Uint32 queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
        int safety = 0;

        // If we are too far ahead, sleep briefly to let the audio card catch up
        while (queuedBytes > TARGET_QUEUE_BYTES && safety < MAX_WAIT_CYCLES) {
            SDL_Delay(1); 
            queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
            safety++;
        }
        
        SDL_QueueAudio(audioDevice, samples, count * sizeof(int16_t));
    }

    void drawWallpaper(const uint8_t *pixels, int w, int h) override
    {
        if (!pixels) return;
        if (w != wallW || h != wallH || !wallpaperTex) {
            if (wallpaperTex) SDL_DestroyTexture(wallpaperTex);
            wallpaperTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            wallW = w; wallH = h;
            wallBuffer.resize(w * h);
        }
        const uint8_t *p = pixels;
        for (int i = 0; i < w * h; i++) {
            wallBuffer[i] = (255 << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
            p += 4;
        }
        SDL_UpdateTexture(wallpaperTex, NULL, wallBuffer.data(), w * sizeof(uint32_t));
    }

    void clearWallpaper() override
    {
        if (wallpaperTex) { SDL_DestroyTexture(wallpaperTex); wallpaperTex = nullptr; }
    }

    void updateOverlay() override {}

    void flipScreen(const uint8_t *framebuffer, int fb_w, int fb_h, uint8_t *palette_map) override
    {
        if (!framebuffer || fb_w <= 0 || fb_h <= 0) return;

        uint32_t paletteLUT[16];
        for (int i = 0; i < 16; i++) {
            uint8_t p8ID = palette_map ? palette_map[i] : (uint8_t)i;
            const uint8_t *rgb;
            if (p8ID < 16) rgb = Real8Gfx::PALETTE_RGB[p8ID];
            else if (p8ID >= 128 && p8ID < 144) rgb = Real8Gfx::PALETTE_RGB[p8ID - 128 + 16];
            else rgb = Real8Gfx::PALETTE_RGB[p8ID & 0x0F];
            paletteLUT[i] = (255u << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        }

        if (screenW != fb_w || screenH != fb_h) {
            screenW = fb_w;
            screenH = fb_h;
            screenBuffer.resize((size_t)screenW * (size_t)screenH);
        }

        for (int y = 0; y < fb_h; y++) {
            const uint8_t *src_row = framebuffer + (y * fb_w);
            uint32_t *dst_row = screenBuffer.data() + (y * fb_w);
            for (int x = 0; x < fb_w; x++) {
                dst_row[x] = paletteLUT[src_row[x] & 0x0F];
            }
        }

        SDL_RenderClear(renderer);

        int outputW, outputH;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);

        int texW = 0, texH = 0;
        if (!texture || SDL_QueryTexture(texture, NULL, NULL, &texW, &texH) != 0 || texW != fb_w || texH != fb_h) {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fb_w, fb_h);
        }

        const int mode = (debugVMRef ? debugVMRef->r8_vmode_cur : 0);
        SDL_SetTextureScaleMode(texture, (mode == 0 && interpolation) ? SDL_ScaleModeBest : SDL_ScaleModeNearest);

        SDL_UpdateTexture(texture, NULL, screenBuffer.data(), fb_w * sizeof(uint32_t));

        SDL_Rect srcRect = {0, 0, fb_w, fb_h};
        // Draw Wallpaper
        if (wallpaperTex && wallW > 0 && wallH > 0) {
            float scaleW = (float)outputW / (float)wallW;
            float scaleH = (float)outputH / (float)wallH;
            float scale = (scaleW > scaleH) ? scaleW : scaleH;

            int drawW = (int)(wallW * scale);
            int drawH = (int)(wallH * scale);

            SDL_Rect wallRect;
            wallRect.x = (outputW - drawW) / 2;
            wallRect.y = (outputH - drawH) / 2;
            wallRect.w = drawW;
            wallRect.h = drawH;

            SDL_RenderCopy(renderer, wallpaperTex, NULL, &wallRect);
        }

        SDL_Rect dstRect;
        float scale = 1.0f;
        calculateGameRect(outputW, outputH, &dstRect, &scale);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);

        if (mode == 0 && crt_filter) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
            for (int y = dstRect.y; y < dstRect.y + dstRect.h; y += 2) {
                SDL_RenderDrawLine(renderer, dstRect.x, y, dstRect.x + dstRect.w, y);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        SDL_RenderPresent(renderer);
    }

    unsigned long getMillis() override { return SDL_GetTicks(); }
    void delayMs(int ms) override { SDL_Delay(ms); }

    std::vector<uint8_t> loadFile(const char *path) override
    {
        std::string fullPath = resolveVirtualPath(path);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    std::vector<std::string> listFiles(const char *ext) override
    {
        std::vector<std::string> results;
        if (!fs::exists(rootPath)) return results;
        for (const auto &entry : fs::directory_iterator(rootPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (strlen(ext) == 0 || filename.find(ext) != std::string::npos) {
                    results.push_back("/" + filename);
                }
            }
        }
        return results;
    }

    bool saveState(const char *filename, const uint8_t *data, size_t size) override
    {
        std::string fullPath = resolveVirtualPath(filename);
        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) return false;
        file.write((const char *)data, size);
        return true;
    }

    std::vector<uint8_t> loadState(const char *filename) override
    {
        std::string fullPath = resolveVirtualPath(filename);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read((char *)buffer.data(), size)) return buffer;
        return {};
    }

    bool hasSaveState(const char *filename) override { return fs::exists(resolveVirtualPath(filename)); }
    void deleteFile(const char *path) override { fs::remove(resolveVirtualPath(path)); }

    void getStorageInfo(size_t &used, size_t &total) override { used = 0; total = 1024 * 1024 * 1024; }

    bool renameGameUI(const char *currentPath) override
    {
        std::string fullPath = resolveVirtualPath(currentPath);
        fs::path p(fullPath);
        if (!fs::exists(p)) return false;
        std::string stem = p.stem().string();
        std::string newName;
        if (ShowInputBox(newName, stem)) {
            if (newName.empty() || newName == stem) return false;
            if (newName.find(p.extension().string()) == std::string::npos) newName += p.extension().string();
            fs::path newP = p.parent_path() / newName;
            try { fs::rename(p, newP); return true; }
            catch (fs::filesystem_error &e) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rename Error", e.what(), NULL); return false; }
        }
        return false;
    }


    NetworkInfo getNetworkInfo() override
    {
        // Cache the result briefly to avoid expensive connectivity checks every frame.
        using namespace std::chrono;
        static steady_clock::time_point lastCheck{};
        static bool lastConnected = false;

        auto now = steady_clock::now();
        if (lastCheck.time_since_epoch().count() == 0 || (now - lastCheck) > seconds(2))
        {
            bool connected = false;

            // Prefer Network List Manager (checks actual internet connectivity),
            // fall back to WinINet if NLM isn't available.
            VARIANT_BOOL isConnected = VARIANT_FALSE;
            HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            bool didInit = (hrInit == S_OK || hrInit == S_FALSE);

            INetworkListManager *pNLM = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                                          IID_INetworkListManager, (void **)&pNLM);
            if (SUCCEEDED(hr) && pNLM)
            {
                if (SUCCEEDED(pNLM->get_IsConnectedToInternet(&isConnected)))
                    connected = (isConnected == VARIANT_TRUE);
                pNLM->Release();
            }
            else
            {
                DWORD flags = 0;
                connected = (InternetGetConnectedState(&flags, 0) == TRUE);
            }

            if (didInit) CoUninitialize();

            lastConnected = connected;
            lastCheck = now;
        }

        return {lastConnected, "127.0.0.1", "DESKTOP MODE", 0.0f};
    }
    void setWifiCredentials(const char *ssid, const char *pass) override {}
    void setNetworkActive(bool active) override {}

    bool downloadFile(const char *url, const char *savePath) override
    {
        std::string fullPath = resolveVirtualPath(savePath);
        HRESULT hr = URLDownloadToFileA(NULL, url, fullPath.c_str(), 0, NULL);
        return (hr == S_OK);
    }

    MouseState getMouseState() override
    {
        int x, y, w, h;
        Uint32 buttons = SDL_GetMouseState(&x, &y);
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Rect gameRect;
        float scale;
        calculateGameRect(w, h, &gameRect, &scale);
        int relX = x - gameRect.x;
        int relY = y - gameRect.y;
        MouseState ms;
        bool stretch = (debugVMRef && debugVMRef->stretchScreen);
        int gameW = (debugVMRef && debugVMRef->fb_w > 0) ? debugVMRef->fb_w : 128;
        int gameH = (debugVMRef && debugVMRef->fb_h > 0) ? debugVMRef->fb_h : 128;
        float scaleX = stretch ? ((float)gameRect.w / (float)gameW) : scale;
        float scaleY = stretch ? ((float)gameRect.h / (float)gameH) : scale;
        if (scaleX <= 0.0f) scaleX = 1.0f;
        if (scaleY <= 0.0f) scaleY = 1.0f;
        ms.x = (int)(relX / scaleX);
        ms.y = (int)(relY / scaleY);
        if (ms.x < 0) ms.x = 0; if (ms.x >= gameW) ms.x = gameW - 1;
        if (ms.y < 0) ms.y = 0; if (ms.y >= gameH) ms.y = gameH - 1;
        ms.btn = 0;
        if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ms.btn |= 1;
        if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) ms.btn |= 2;
        if (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ms.btn |= 4;
        return ms;
    }

    void takeScreenshot() override
    {
        char path[MAX_PATH];
        std::string finalDir;
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, 0, path))) {
            finalDir = std::string(path) + "\\Real8 Screenshots";
        } else {
            finalDir = (rootPath / "screenshots").string();
        }
        if (!fs::exists(finalDir)) fs::create_directories(finalDir);

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << finalDir << "\\screenshot_" << std::put_time(std::localtime(&now_c), "%Y-%m-%d_%H-%M-%S") << ".bmp";
        std::string fullPath = ss.str();

        const int capW = (screenW > 0) ? screenW : 128;
        const int capH = (screenH > 0) ? screenH : 128;
        SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
            (void *)screenBuffer.data(),
            capW, capH,
            32,
            capW * 4,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
        );

        if (surface) {
            if (SDL_SaveBMP(surface, fullPath.c_str()) == 0) log("[SYSTEM] Screenshot saved: %s", fullPath.c_str());
            else log("[ERROR] Failed to save screenshot: %s", SDL_GetError());
            SDL_FreeSurface(surface);
        }
    }

    // --- Wallpaper Import ---
    bool importWallpaper(const std::string& sourcePath) {
        try {
            std::string destStr = resolveVirtualPath("/wallpaper.png");
            fs::path dest(destStr);
            fs::path src(sourcePath);
            
            // Overwrite existing wallpaper
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
            return true;
        } catch (std::exception& e) {
            log("Wallpaper Import Error: %s", e.what());
            return false;
        }
    }

    // --- Show Repo Dialog ---
    // [UPDATE THIS METHOD in windows_host.hpp]
    bool ShowRepoConfigBox(std::string &ioUrl, const std::string &defaultUrl)
    {
        const char *className = "Real8RepoBox";
        WNDCLASS wc = {0};
        wc.lpfnWndProc = RepoBoxWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = className;
        RegisterClass(&wc);

        HWND hParent = GetActiveWindow();
        
        // Setup static buffers before creating window
        char buffer[512] = {0};
        s_repoBuffer = buffer;
        s_defaultRepoUrl = defaultUrl.c_str();

        // Check file existence/content immediately
        std::string storedUrl = getRepoUrlFromFile();
        
        // If file exists and has content, use it. 
        // Otherwise, fall back to the in-memory 'ioUrl' passed to the function.
        std::string currentText = storedUrl.empty() ? ioUrl : storedUrl;

        // Pass 'currentText' to the window creation so it populates the Edit Box
        HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, className, "Configure Repository", 
                                   WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 
                                   300, 300, 400, 145, 
                                   hParent, NULL, GetModuleHandle(NULL), (LPVOID)currentText.c_str());
        
        if (!hWnd) {
            s_repoBuffer = nullptr;
            s_defaultRepoUrl = nullptr;
            return false;
        }

        EnableWindow(hParent, FALSE);
        bool result = false;
        MSG msg;
        while (IsWindow(hWnd)) {
            if (GetMessage(&msg, NULL, 0, 0)) {
                if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) SendMessage(hWnd, WM_COMMAND, ID_BTN_SAVE, 0);
                else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) SendMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
                
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        
        // If buffer was modified (Save clicked), update the output string
        if (strlen(buffer) > 0) {
            ioUrl = std::string(buffer);
            result = true;
        }

        // Cleanup
        s_repoBuffer = nullptr;
        s_defaultRepoUrl = nullptr;

        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        UnregisterClass(className, GetModuleHandle(NULL));
        return result;
    }
};

// Define static members
WNDPROC WindowsHost::wpOrigEdit = nullptr;
char* WindowsHost::s_repoBuffer = nullptr;
const char* WindowsHost::s_defaultRepoUrl = nullptr;
