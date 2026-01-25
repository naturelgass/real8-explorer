#include <SDL.h>
#include <SDL_syswm.h> 
#include <iostream>
#include <cstdio>
#include <cctype>
#include <vector>
#include <windows.h> 
#include <commdlg.h> 
#include <shlobj.h>

#include "windows_host.hpp"
#include "../../core/real8_vm.h"
#include "../../core/real8_shell.h"
#include "../../core/real8_tools.h"

const int WINDOW_WIDTH = 512;
const int WINDOW_HEIGHT = 512;

// --- MENU CONSTANTS ---
enum MenuID
{
    ID_FILE_LOAD = 101,
    ID_FILE_LOAD_WALLPAPER, 
    ID_FILE_SET_REPO,       
    ID_FILE_EXIT,
    ID_SET_FULLSCREEN,
    ID_OPT_SAVE_STATE,
    ID_OPT_LOAD_STATE,
    ID_SET_INPUT_CONFIG = 120,
    ID_SET_REPO_GAMES,
    ID_SET_SHOW_REPO_SNAP,
    ID_SET_SHOW_FPS,
    ID_SET_SHOW_SKIN,
    ID_SET_MUSIC,
    ID_SET_SFX,
    ID_SET_CRT_FILTER,
    ID_SET_INTERPOLATION,
    ID_SET_STRETCH_SCREEN,
    ID_EXT_EXPORT_LUA,
    ID_EXT_EXPORT_GFX,
    ID_EXT_EXPORT_MAP,
    ID_EXT_EXPORT_VARS,
    ID_EXT_EXPORT_MUSIC,
    ID_EXT_EXPORT_GAMECARD,
    ID_EXT_REALTIME_MODS,
    ID_SET_SHOW_CONSOLE
};

std::string OpenFileDialog(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "PICO-8 Carts\0*.p8;*.png\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

std::string OpenImageDialog(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Images\0*.png\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; 
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

static std::string GetLoadedCartBaseName(Real8VM* vm)
{
    if (!vm) return "cart";
    std::string name = (!vm->currentCartPath.empty()) ? vm->currentCartPath : vm->currentGameId;
    if (name.empty()) return "cart";
    size_t lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);
    size_t lastDot = name.find_last_of('.');
    if (lastDot != std::string::npos) name = name.substr(0, lastDot);
    if (name.empty()) name = "cart";
    return name;
}

std::string SaveLuaCartDialog(HWND hwnd, Real8VM* vm)
{
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    std::string suggested = GetLoadedCartBaseName(vm); // + ".p8";
#if defined(_MSC_VER)
    strncpy_s(szFile, suggested.c_str(), _TRUNCATE);
#else
    std::snprintf(szFile, sizeof(szFile), "%s", suggested.c_str());
#endif
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "PICO-8 Text Cart\0*.p8\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Export LUA (p8)";
    ofn.lpstrDefExt = "p8";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

std::string BrowseFolder(HWND hwnd);

static std::string JoinPath(const std::string& dir, const std::string& file)
{
    if (dir.empty()) return file;
    char last = dir.back();
    if (last == '\\' || last == '/') return dir + file;
    return dir + "\\" + file;
}

static bool EndsWithNoCase(const std::string& value, const std::string& suffix)
{
    if (value.size() < suffix.size()) return false;
    size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = (char)tolower((unsigned char)value[offset + i]);
        char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

static std::string SanitizeFileName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = (unsigned char)c;
        if (uc < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) out = "cart";
    return out;
}

static std::string GetDlgItemTextString(HWND hWnd, int id)
{
    int len = GetWindowTextLengthA(GetDlgItem(hWnd, id));
    std::string text(len, '\0');
    if (len > 0) {
        GetWindowTextA(GetDlgItem(hWnd, id), text.data(), len + 1);
    }
    return text;
}

struct GamecardDialogState {
    Real8VM* vm = nullptr;
    WindowsHost* host = nullptr;
    std::string defaultTitle;
    bool exported = false;
    HFONT font = NULL;
};

static const int kCartTemplateResourceId = 201;

enum {
    ID_GC_TITLE = 9101,
    ID_GC_AUTHOR,
    ID_GC_COVER,
    ID_GC_BROWSE,
    ID_GC_RESET,
    ID_GC_EXPORT
};

static bool LoadEmbeddedResource(int resourceId, std::vector<uint8_t>& out)
{
    HRSRC r = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!r) return false;
    DWORD size = SizeofResource(NULL, r);
    if (size == 0) return false;
    HGLOBAL h = LoadResource(NULL, r);
    if (!h) return false;
    void* p = LockResource(h);
    if (!p) return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(p);
    out.assign(bytes, bytes + size);
    return true;
}

static LRESULT CALLBACK GamecardDialogProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    GamecardDialogState* state = (GamecardDialogState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (message) {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        state = (GamecardDialogState*)cs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)state);

        state->font = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        const int pad = 10;
        const int labelW = 140;
        const int editH = 22;
        const int editW = 260;
        const int btnW = 80;
        int y = pad;

        CreateWindow("STATIC", "Game Title:", WS_CHILD | WS_VISIBLE,
                     pad, y, labelW, editH, hWnd, NULL, NULL, NULL);
        HWND hTitleEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                         pad, y + editH + 2, editW + btnW + pad, editH, hWnd, (HMENU)(UINT_PTR)ID_GC_TITLE, NULL, NULL);
        y += editH + 10 + editH;

        CreateWindow("STATIC", "Publisher / Author:", WS_CHILD | WS_VISIBLE,
                     pad, y, labelW + 40, editH, hWnd, NULL, NULL, NULL);
        HWND hAuthorEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          pad, y + editH + 2, editW + btnW + pad, editH, hWnd, (HMENU)(UINT_PTR)ID_GC_AUTHOR, NULL, NULL);
        y += editH + 10 + editH;

        CreateWindow("STATIC", "Cover Art:", WS_CHILD | WS_VISIBLE,
                     pad, y, labelW, editH, hWnd, NULL, NULL, NULL);
        HWND hCoverEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                         pad, y + editH + 2, editW, editH, hWnd, (HMENU)(UINT_PTR)ID_GC_COVER, NULL, NULL);
        HWND hBrowseBtn = CreateWindow("BUTTON", "Browse", WS_CHILD | WS_VISIBLE,
                                       pad + editW + pad, y + editH + 2, btnW, editH, hWnd, (HMENU)(UINT_PTR)ID_GC_BROWSE, NULL, NULL);
        y += editH + 18 + editH;

        HWND hResetBtn = CreateWindow("BUTTON", "Reset", WS_CHILD | WS_VISIBLE,
                                      pad, y, btnW, editH + 4, hWnd, (HMENU)(UINT_PTR)ID_GC_RESET, NULL, NULL);
        HWND hExportBtn = CreateWindow("BUTTON", "Export", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       pad + editW + pad, y, btnW, editH + 4, hWnd, (HMENU)(UINT_PTR)ID_GC_EXPORT, NULL, NULL);

        if (state && !state->defaultTitle.empty()) {
            SetWindowTextA(hTitleEdit, state->defaultTitle.c_str());
            SendMessage(hTitleEdit, EM_SETSEL, 0, -1);
        }

        if (state && state->font) {
            SendMessage(hTitleEdit, WM_SETFONT, (WPARAM)state->font, TRUE);
            SendMessage(hAuthorEdit, WM_SETFONT, (WPARAM)state->font, TRUE);
            SendMessage(hCoverEdit, WM_SETFONT, (WPARAM)state->font, TRUE);
            SendMessage(hBrowseBtn, WM_SETFONT, (WPARAM)state->font, TRUE);
            SendMessage(hResetBtn, WM_SETFONT, (WPARAM)state->font, TRUE);
            SendMessage(hExportBtn, WM_SETFONT, (WPARAM)state->font, TRUE);
        }

        SetFocus(hTitleEdit);
        return 0;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (!state) return 0;

        if (id == ID_GC_BROWSE) {
            std::string path = OpenImageDialog(hWnd);
            if (!path.empty()) {
                SetDlgItemTextA(hWnd, ID_GC_COVER, path.c_str());
            }
            return 0;
        }
        if (id == ID_GC_RESET) {
            SetDlgItemTextA(hWnd, ID_GC_TITLE, "");
            SetDlgItemTextA(hWnd, ID_GC_AUTHOR, "");
            SetDlgItemTextA(hWnd, ID_GC_COVER, "");
            return 0;
        }
        if (id == ID_GC_EXPORT) {
            std::string title = GetDlgItemTextString(hWnd, ID_GC_TITLE);
            std::string author = GetDlgItemTextString(hWnd, ID_GC_AUTHOR);
            std::string cover = GetDlgItemTextString(hWnd, ID_GC_COVER);

            if (title.empty()) {
                MessageBoxA(hWnd, "Please enter a Game Title.", "Missing Title", MB_OK | MB_ICONWARNING);
                return 0;
            }

            std::string folder = BrowseFolder(hWnd);
            if (folder.empty()) return 0;

            std::string safeTitle = SanitizeFileName(title);
            if (safeTitle.empty()) safeTitle = "cart";
            std::string fileName = safeTitle;
            if (!EndsWithNoCase(fileName, ".p8.png")) fileName += ".p8.png";

            std::string outputPath = JoinPath(folder, fileName);
            std::vector<uint8_t> templatePng;
            if (!LoadEmbeddedResource(kCartTemplateResourceId, templatePng)) {
                MessageBoxA(hWnd, "Embedded cart template not found.", "Missing Template", MB_OK | MB_ICONERROR);
                return 0;
            }

            bool ok = Real8Tools::ExportGamecard(state->vm, state->host, outputPath, title, author, cover, templatePng);
            if (!ok) {
                MessageBoxA(hWnd, "Export failed. Check logs.txt for details.", "Export Failed", MB_OK | MB_ICONERROR);
                return 0;
            }

            state->exported = true;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (state && state->font) {
            DeleteObject(state->font);
            state->font = NULL;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static bool ShowGamecardExportDialog(HWND parent, Real8VM* vm, WindowsHost* host)
{
    const char* className = "Real8GamecardExport";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = GamecardDialogProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClass(&wc);

    GamecardDialogState state;
    state.vm = vm;
    state.host = host;
    state.defaultTitle = GetLoadedCartBaseName(vm);

    HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, className, "Export Gamecard",
                               WS_VISIBLE | WS_SYSMENU | WS_CAPTION,
                               300, 300, 400, 260,
                               parent, NULL, GetModuleHandle(NULL), (LPVOID)&state);
    if (!hWnd) {
        UnregisterClass(className, GetModuleHandle(NULL));
        return false;
    }

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hWnd)) {
        if (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    UnregisterClass(className, GetModuleHandle(NULL));
    return state.exported;
}


void ToggleFullscreen(SDL_Window *window, HWND hwnd, HMENU hMenu)
{
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(window, 0);
        SetMenu(hwnd, hMenu);
    } else {
        SetMenu(hwnd, NULL);
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

// Checks Shell state indirectly via VM ID or assumes 'gameRunning' if ID is set
void UpdateMenuState(HMENU hMenu, Real8VM *vm, SDL_Window *window, WindowsHost* host)
{
    CheckMenuItem(hMenu, ID_SET_SHOW_FPS, vm->showStats ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_SHOW_SKIN, vm->showSkin ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_CRT_FILTER, vm->crt_filter ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_SHOW_REPO_SNAP, vm->showRepoSnap ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_STRETCH_SCREEN, vm->stretchScreen ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_MUSIC, (vm->volume_music > 0) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_SFX,   (vm->volume_sfx > 0)   ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_SHOW_CONSOLE, host->isConsoleOpen() ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_EXT_REALTIME_MODS, host->isRealtimeModWindowOpen() ? MF_CHECKED : MF_UNCHECKED);

    bool isFS = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP);
    CheckMenuItem(hMenu, ID_SET_FULLSCREEN, isFS ? MF_CHECKED : MF_UNCHECKED);

    CheckMenuItem(hMenu, ID_SET_INTERPOLATION, vm->interpolation ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_SET_REPO_GAMES, vm->showRepoGames ? MF_CHECKED : MF_UNCHECKED);

    // Check ID instead of sysState
    bool gameRunning = (!vm->currentGameId.empty());
    
    EnableMenuItem(hMenu, ID_OPT_SAVE_STATE, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_OPT_LOAD_STATE, MF_BYCOMMAND | (vm->hasState() ? MF_ENABLED : MF_GRAYED));
    
    EnableMenuItem(hMenu, ID_EXT_EXPORT_LUA, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_GFX, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_MAP, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_MUSIC, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_GAMECARD, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_REALTIME_MODS, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
}

// Helper to bridge Native Menu Load -> Shell
// Since we can't easily access 'sysState' (private), we force a VM reset request which the Shell picks up.
void LoadGameViaShell(Real8VM *vm, SDL_Window *window, HMENU hMenu, const std::string &path, WindowsHost* host)
{
    if (path.empty()) return;
    
    // Set the path the VM should load next
    vm->currentCartPath = path;
    vm->next_cart_path = path;
    
    // Populate currentGameId immediately so Menu Logic sees a "Running Game"
    size_t lastSlash = path.find_last_of("/\\");
    vm->currentGameId = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);

    // Tell Shell (via VM signal) that a reset/load is requested
    vm->reset_requested = true;

    // Clear Host Audio Buffer to prevent lag on load
    host->pushAudio(nullptr, 0);

    // Update UI immediately (optimistic)
    UpdateMenuState(hMenu, vm, window, host);
}

std::string BrowseFolder(HWND hwnd)
{
    char path[MAX_PATH];
    BROWSEINFO bi = { 0 };
    bi.lpszTitle = "Select Export Destination";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    bi.hwndOwner = hwnd;
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl != 0) {
        SHGetPathFromIDList(pidl, path);
        IMalloc * imalloc = 0;
        if (SUCCEEDED(SHGetMalloc(&imalloc))) { imalloc->Free(pidl); imalloc->Release(); }
        return std::string(path);
    }
    return "";
}

// 1. Add this function BEFORE main()
LONG WINAPI Real8CrashHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
    std::ofstream crashLog;
    crashLog.open("logs.txt", std::ios::out | std::ios::app); // Append to existing log
    
    if (crashLog.is_open()) {
        crashLog << "\n\n!!! CRITICAL CRASH DETECTED !!!\n";
        crashLog << "-----------------------------\n";
        
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        crashLog << "Exception Code: 0x" << std::hex << code << std::dec << "\n";
        
        if (code == EXCEPTION_ACCESS_VIOLATION) {
            crashLog << "Type: ACCESS VIOLATION (Segmentation Fault)\n";
            // Check if read or write
            if (pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
                auto type = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
                auto addr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
                crashLog << "Attempted to " << (type ? "WRITE" : "READ") << " address: 0x" << std::hex << addr << "\n";
            }
        }
        else if (code == EXCEPTION_STACK_OVERFLOW) {
            crashLog << "Type: STACK OVERFLOW (Infinite recursion or huge allocation)\n";
        }
        else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
            crashLog << "Type: DIVIDE BY ZERO\n";
        }
        
        crashLog << "-----------------------------\n";
        crashLog << "Last Cart Path: " << real8_get_last_cart_path() << "\n";
        crashLog << "Last Lua Phase: " << real8_get_last_lua_phase() << "\n";
        crashLog << "Last API Call: " << real8_get_last_api_call() << "\n";
        crashLog << "Last Lua Line: " << real8_get_last_lua_line() << "\n";
        crashLog << "Last Lua Source: " << real8_get_last_lua_source() << "\n";
        crashLog << "Please share this file with the developer.\n";
        crashLog.close();
    }

    // Show a popup so you know it happened
    MessageBoxA(NULL, "The emulator has crashed!\nCheck logs.txt for details.", "Real-8 Crash", MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER; // Kill the process safely
}

int main(int argc, char *argv[])
{
    // INSTALL CRASH HANDLER
    SetUnhandledExceptionFilter(Real8CrashHandler);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) return 1;

    std::string title = std::string(IReal8Host::REAL8_APPNAME) + " v" + IReal8Host::REAL8_VERSION + " by @natureglass";
    SDL_Window *window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return 1;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;

    // --- MENU CREATION (Same as before) ---
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hOptMenu = CreatePopupMenu();
    HMENU hSetMenu = CreatePopupMenu();
    HMENU hEffectMenu = CreatePopupMenu();
    HMENU hExtraMenu = CreatePopupMenu();

    AppendMenu(hFileMenu, MF_STRING, ID_FILE_LOAD, "Load Game");
    AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFileMenu, MF_STRING, ID_FILE_LOAD_WALLPAPER, "Load Wallpaper"); 
    AppendMenu(hFileMenu, MF_STRING, ID_FILE_SET_REPO, "Set Repo Path");       
    AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFileMenu, MF_STRING, ID_FILE_EXIT, "Exit");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, "File");

    AppendMenu(hOptMenu, MF_STRING, ID_SET_FULLSCREEN, "Fullscreen");
    AppendMenu(hOptMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hOptMenu, MF_STRING, ID_OPT_SAVE_STATE, "Save State");
    AppendMenu(hOptMenu, MF_STRING, ID_OPT_LOAD_STATE, "Load State");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hOptMenu, "Options");

    AppendMenu(hSetMenu, MF_STRING, ID_SET_INPUT_CONFIG, "Remap keyboard");
    AppendMenu(hSetMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSetMenu, MF_STRING, ID_SET_REPO_GAMES, "Show Repo Games");
    AppendMenu(hSetMenu, MF_STRING, ID_SET_SHOW_REPO_SNAP, "Show Repo Snap");
    AppendMenu(hSetMenu, MF_STRING, ID_SET_SHOW_FPS, "Show FPS");
    AppendMenu(hSetMenu, MF_STRING, ID_SET_SHOW_SKIN, "Show Skin");
    AppendMenu(hSetMenu, MF_STRING, ID_SET_STRETCH_SCREEN, "Stretch Screen");
    AppendMenu(hSetMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSetMenu, MF_STRING, ID_SET_MUSIC, "Music");
    AppendMenu(hSetMenu, MF_STRING, ID_SET_SFX, "SFX");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hSetMenu, "Settings");

    AppendMenu(hEffectMenu, MF_STRING, ID_SET_CRT_FILTER, "CRT Filter");
    AppendMenu(hEffectMenu, MF_STRING, ID_SET_INTERPOLATION, "Interpolation");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hEffectMenu, "Effects");

    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_LUA, "Export LUA");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_GFX, "Export GFX");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_MAP, "Export MAP");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_MUSIC, "Export Music Tracks");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_GAMECARD, "Export Gamecard");
    AppendMenu(hExtraMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_REALTIME_MODS, "RealTime Modding");
    AppendMenu(hExtraMenu, MF_STRING, ID_SET_SHOW_CONSOLE, "Debug Console");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hExtraMenu, "Extra");

    SetMenu(hwnd, hMenuBar);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

    // Ensure the client area is 1:1 (512x512) despite the menu bar.
    {
        RECT clientRect{};
        if (GetClientRect(hwnd, &clientRect)) {
            int clientW = clientRect.right - clientRect.left;
            int clientH = clientRect.bottom - clientRect.top;
            int winW = 0, winH = 0;
            SDL_GetWindowSize(window, &winW, &winH);
            int newW = WINDOW_WIDTH + (winW - clientW);
            int newH = WINDOW_HEIGHT + (winH - clientH);
            SDL_SetWindowSize(window, newW, newH);
        }
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Instantiate VM then Shell
    WindowsHost *host = new WindowsHost(renderer, window);
    Real8VM *vm = new Real8VM(host);
    Real8Shell *shell = new Real8Shell(host, vm); // [NEW]

    host->debugVMRef = vm;

    if (!vm->initMemory()) return 1;

    // Initialize RAM Palette mappings. 
    // Without this, RAM is 0, so 0x5F10 (Screen Palette) is all 0s (Black).
    vm->gpu.pal_reset(); 

    host->setInterpolation(vm->interpolation);
    UpdateMenuState(hMenuBar, vm, window, host);

    SDL_StartTextInput();

    bool running = true;
    SDL_Event event;

    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 last = 0;
    double deltaTime = 0;
    double accumulator = 0.0;
    const double FIXED_STEP = 1.0 / 60.0; 
    
    try {
        while (running)
        {
            last = now;
            now = SDL_GetPerformanceCounter();
            deltaTime = (double)((now - last)) / SDL_GetPerformanceFrequency();
            if (deltaTime > 0.25) deltaTime = 0.25;
            accumulator += deltaTime;

            // 1. Process Input
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT) running = false;
                else if (event.type == SDL_DROPFILE) {
                    char *droppedFile = event.drop.file;
                    LoadGameViaShell(vm, window, hMenuBar, std::string(droppedFile), host);
                    SDL_free(droppedFile);
                }
                else if (event.type == SDL_TEXTINPUT) {
                    const char *text = event.text.text;
                    if (text) {
                        for (size_t i = 0; text[i] != '\0'; ++i) {
                            vm->key_queue.push_back(std::string(1, text[i]));
                        }
                    }
                }
                else if (event.type == SDL_MOUSEWHEEL) {
                    int delta = event.wheel.y;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
                    if (delta != 0) vm->mouse_wheel_event = delta;
                }
                else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                            ToggleFullscreen(window, hwnd, hMenuBar);
                            UpdateMenuState(hMenuBar, vm, window, host);
                        }
                    }
                    else if (event.key.keysym.sym == SDLK_F11) {
                        ToggleFullscreen(window, hwnd, hMenuBar);
                        UpdateMenuState(hMenuBar, vm, window, host);
                    }
                    else if (event.key.keysym.sym == SDLK_F12) { 
                        host->takeScreenshot();
                        vm->gpu.renderMessage("SYSTEM", "SCREENSHOT SAVED", 6); 
                        vm->show_frame();                                  
                    }
                }
                else if (event.type == SDL_SYSWMEVENT) {
                    if (event.syswm.msg->msg.win.msg == WM_COMMAND) {
                        int menuID = LOWORD(event.syswm.msg->msg.win.wParam);
                        // ... inside WM_COMMAND case ...
                        switch (menuID) {
                            // Use Shell Loader
                            case ID_FILE_LOAD: 
                            {
                                std::string path = OpenFileDialog(hwnd);
                                if (!path.empty()) {
                                    LoadGameViaShell(vm, window, hMenuBar, path, host);
                                    SetForegroundWindow(hwnd);
                                    SetFocus(hwnd);
                                }
                                break;
                            }
                            
                            case ID_EXT_REALTIME_MODS:
                                host->openRealtimeModWindow();
                                break;

                            case ID_SET_SHOW_CONSOLE: 
                                host->setConsoleState(!host->isConsoleOpen()); 
                                break;

                            case ID_FILE_LOAD_WALLPAPER: {
                                std::string p = OpenImageDialog(hwnd);
                                if (!p.empty() && host->importWallpaper(p)) { 
                                    vm->showSkin = true; 
                                    Real8Tools::LoadSkin(vm, host);     
                                    Real8Tools::SaveSettings(vm, host); 
                                    UpdateMenuState(hMenuBar, vm, window, host); 
                                }
                                SetForegroundWindow(hwnd);
                                SetFocus(hwnd);
                                break;
                            }

                            case ID_FILE_SET_REPO: 
                                // ShowRepoConfigBox updates vm->currentRepoUrl by reference if ID_BTN_SAVE is clicked
                                if(host->ShowRepoConfigBox(vm->currentRepoUrl, IReal8Host::DEFAULT_GAMES_REPOSITORY)) { 
                                    
                                    // Persist the new URL to ./data/config/gamesrepo.txt
                                    host->saveRepoUrlToFile(vm->currentRepoUrl);
                                    
                                    Real8Tools::SaveSettings(vm, host); 
                                    if(vm->showRepoGames) shell->update(); 
                                } 
                                break;

                            case ID_FILE_EXIT: 
                                running = false; 
                                break;

                            case ID_OPT_SAVE_STATE: 
                                vm->saveState(); 
                                break;

                            case ID_OPT_LOAD_STATE: 
                                vm->loadState(); 
                                break;

                            case ID_SET_FULLSCREEN: 
                                ToggleFullscreen(window, hwnd, hMenuBar); 
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_INPUT_CONFIG: 
                                host->openGamepadConfigUI(); 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                break;

                            case ID_SET_SHOW_REPO_SNAP: 
                                vm->showRepoSnap = !vm->showRepoSnap; 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;


                            case ID_SET_SHOW_FPS: 
                                vm->showStats = !vm->showStats; 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_SHOW_SKIN: 
                                vm->showSkin = !vm->showSkin; 
                                if(vm->showSkin) Real8Tools::LoadSkin(vm, host); // FIXED
                                else host->clearWallpaper(); 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_STRETCH_SCREEN:
                                vm->stretchScreen = !vm->stretchScreen;
                                Real8Tools::SaveSettings(vm, host);
                                UpdateMenuState(hMenuBar, vm, window, host);
                                break;

                            case ID_SET_CRT_FILTER: 
                                vm->crt_filter = !vm->crt_filter;
                                // Apply immediately to host so the next frame catches it
                                host->crt_filter = vm->crt_filter; 
                                Real8Tools::SaveSettings(vm, host);
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_INTERPOLATION: 
                                vm->interpolation = !vm->interpolation; 
                                host->setInterpolation(vm->interpolation); 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_MUSIC: 
                                vm->volume_music = (vm->volume_music > 0) ? 0 : 10; 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_SFX: 
                                vm->volume_sfx = (vm->volume_sfx > 0) ? 0 : 10; 
                                Real8Tools::SaveSettings(vm, host);     // FIXED
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                break;

                            case ID_SET_REPO_GAMES: 
                                vm->showRepoGames = !vm->showRepoGames; 
                                Real8Tools::SaveSettings(vm, host);
                                UpdateMenuState(hMenuBar, vm, window, host); 
                                shell->refreshGameList();
                                break;

                            case ID_EXT_EXPORT_LUA:
                                if(!vm->currentGameId.empty()) {
                                    std::string f = SaveLuaCartDialog(hwnd, vm);
                                    if(!f.empty()) Real8Tools::ExportLUA(vm, host, f);
                                }
                                break;

                            case ID_EXT_EXPORT_GFX: 
                                if(!vm->currentGameId.empty()) { 
                                    std::string f=BrowseFolder(hwnd); 
                                    if(!f.empty()) Real8Tools::ExportGFX(vm, host, f); // FIXED
                                } 
                                break;

                            case ID_EXT_EXPORT_MAP: 
                                if(!vm->currentGameId.empty()) { 
                                    std::string f=BrowseFolder(hwnd); 
                                    if(!f.empty()) Real8Tools::ExportMAP(vm, host, f); // FIXED
                                } 
                                break;

                            case ID_EXT_EXPORT_MUSIC: 
                                if(!vm->currentGameId.empty()) { 
                                    std::string f=BrowseFolder(hwnd); 
                                    if(!f.empty()) Real8Tools::ExportMusic(vm, host, f); // FIXED
                                } 
                                break;
                            case ID_EXT_EXPORT_GAMECARD:
                                if (!vm->currentGameId.empty()) {
                                    bool wasPaused = vm->debug.paused;
                                    vm->debug.paused = true;
                                    vm->debug.step_mode = false;
                                    ShowGamecardExportDialog(hwnd, vm, host);
                                    if (!wasPaused) vm->debug.paused = false;
                                }
                                break;
                        }
                    }
                }
            }

            UpdateMenuState(hMenuBar, vm, window, host);
            // Allow CRT filter even in Shell/Desktop mode for immediate feedback
            host->crt_filter = vm->crt_filter;
            if (vm->interpolation != host->interpolation) {
                host->setInterpolation(vm->interpolation);
            }

            // 2. Fixed Timestep Logic
            while (accumulator >= FIXED_STEP)
            {
                // Run Shell Update instead of VM directly
                shell->update();
                if (vm->quit_requested) {
                    running = false;
                    break;
                }
                host->applyRealtimeMods();
                accumulator -= FIXED_STEP;
            }

            // 3. Render Loop (Managed by VM/Shell internals, called during update)
            // If in a "Paused" state or Menu where update() doesn't render heavily, we might want to force a flip here?
            // But generally, shell->update() calls vm->show_frame().
            
            // Minor optimization: Sleep if we processed frames fast
            if (accumulator < FIXED_STEP && !host->isFastForwardHeld()) {
                SDL_Delay(1);
            }
        }
    }
    catch (const std::exception& e) {
        // This catches std::vector errors, bad_alloc, etc.
        std::ofstream f("logs.txt", std::ios::app);
        f << "\n[C++ EXCEPTION] " << e.what() << "\n";
        f.close();
        MessageBoxA(NULL, e.what(), "C++ Runtime Error", MB_OK | MB_ICONERROR);
    }

    delete shell; // [NEW] Delete shell first
    delete vm;
    delete host;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
