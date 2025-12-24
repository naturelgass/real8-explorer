#include <SDL.h>
#include <SDL_syswm.h> 
#include <iostream>
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
    ID_EXT_EXPORT_GFX,
    ID_EXT_EXPORT_MAP,
    ID_EXT_EXPORT_VARS,
    ID_EXT_EXPORT_MUSIC,
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
    
    EnableMenuItem(hMenu, ID_EXT_EXPORT_GFX, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_MAP, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, ID_EXT_EXPORT_MUSIC, MF_BYCOMMAND | (gameRunning ? MF_ENABLED : MF_GRAYED));
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

    SDL_Window *window = SDL_CreateWindow("Real-8 Explorer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_GFX, "Export GFX");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_MAP, "Export MAP");
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_EXPORT_MUSIC, "Export Music Tracks");
    AppendMenu(hExtraMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hExtraMenu, MF_STRING, ID_EXT_REALTIME_MODS, "RealTime Modding");
    AppendMenu(hExtraMenu, MF_STRING, ID_SET_SHOW_CONSOLE, "Debug Console");
    AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hExtraMenu, "Extra");

    SetMenu(hwnd, hMenuBar);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

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
            if (accumulator < FIXED_STEP) {
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
