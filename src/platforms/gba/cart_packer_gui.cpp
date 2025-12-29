#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <process.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    const int kPadding = 12;
    const int kButtonWidth = 110;
    const int kButtonHeight = 28;

    const int kIdBrowseMake = 1001;
    const int kIdBrowseDevkit = 1002;
    const int kIdBrowseGba = 1003;
    const int kIdBrowseCart = 1004;
    const int kIdGenerate = 1005;
    const int kIdSpinner = 1006;

    const UINT kIdSpinnerTimer = 2001;
    const UINT kMsgBuildDone = WM_APP + 1;

    HWND g_makeEdit = nullptr;
    HWND g_devkitEdit = nullptr;
    HWND g_gbaEdit = nullptr;
    HWND g_cartEdit = nullptr;
    HWND g_generateButton = nullptr;
    HWND g_spinner = nullptr;
    HWND g_browseMakeButton = nullptr;
    HWND g_browseDevkitButton = nullptr;
    HWND g_browseGbaButton = nullptr;
    HWND g_browseCartButton = nullptr;

    char g_makePath[MAX_PATH] = "";
    char g_devkitProPath[MAX_PATH] = "";
    char g_gbaDir[MAX_PATH] = "";
    char g_cartPath[MAX_PATH] = "";
    char g_iniPath[MAX_PATH] = "";

    bool g_building = false;
    HANDLE g_buildThread = nullptr;

    struct BuildParams {
        HWND hwnd;
        std::string makePath;
        std::string devkitArmPath;
        std::string devkitProPath;
        std::string cartPath;
        std::string gbaDir;
        std::string outputPath;
        std::string logPath;
    };

    struct BuildResult {
        bool success;
        std::string message;
    };

    static bool endsWithIgnoreCase(const char* str, const char* suffix) {
        if (!str || !suffix) return false;
        size_t len = strlen(str);
        size_t sufLen = strlen(suffix);
        if (sufLen > len) return false;
        const char* a = str + (len - sufLen);
        for (size_t i = 0; i < sufLen; ++i) {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)suffix[i])) {
                return false;
            }
        }
        return true;
    }

    static bool startsWithIgnoreCase(const char* str, const char* prefix) {
        if (!str || !prefix) return false;
        size_t len = strlen(prefix);
        for (size_t i = 0; i < len; ++i) {
            if (!str[i]) return false;
            if (std::tolower((unsigned char)str[i]) != std::tolower((unsigned char)prefix[i])) {
                return false;
            }
        }
        return true;
    }

    static void updateGenerateEnabled() {
        bool ready = g_makePath[0] != '\0' && g_devkitProPath[0] != '\0' && g_gbaDir[0] != '\0' && g_cartPath[0] != '\0';
        EnableWindow(g_generateButton, ready);
    }

    static bool buildOutputPath(const char* input, char* out, size_t outSize) {
        if (!input || !*input || !out || outSize == 0) return false;

        const char* lastSlash = strrchr(input, '\\');
        const char* lastFwd = strrchr(input, '/');
        const char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;

        size_t dirLen = sep ? (size_t)(sep - input + 1) : 0;
        const char* name = sep ? sep + 1 : input;
        size_t nameLen = strlen(name);

        size_t baseLen = nameLen;
        if (endsWithIgnoreCase(name, ".p8.png")) {
            baseLen = nameLen - 7;
        } else if (endsWithIgnoreCase(name, ".png")) {
            baseLen = nameLen - 4;
        } else {
            const char* dot = strrchr(name, '.');
            if (dot) baseLen = (size_t)(dot - name);
        }

        const char* outExt = ".gba";
        size_t outExtLen = strlen(outExt);

        if (dirLen + baseLen + outExtLen + 1 > outSize) return false;
        if (dirLen > 0) memcpy(out, input, dirLen);
        memcpy(out + dirLen, name, baseLen);
        memcpy(out + dirLen + baseLen, outExt, outExtLen);
        out[dirLen + baseLen + outExtLen] = '\0';
        return true;
    }

    static bool getExeDir(char* out, size_t outSize) {
        if (!out || outSize == 0) return false;
        char path[MAX_PATH] = "";
        DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return false;
        char* lastSlash = strrchr(path, '\\');
        char* lastFwd = strrchr(path, '/');
        char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;
        if (!sep) return false;
        *sep = '\0';
        if (strlen(path) + 1 > outSize) return false;
        strcpy(out, path);
        return true;
    }

    static bool getDirFromPath(const char* path, char* out, size_t outSize) {
        if (!path || !*path || !out || outSize == 0) return false;
        const char* lastSlash = strrchr(path, '\\');
        const char* lastFwd = strrchr(path, '/');
        const char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;
        if (!sep) return false;
        size_t len = (size_t)(sep - path);
        if (len + 1 > outSize) return false;
        memcpy(out, path, len);
        out[len] = '\0';
        return true;
    }

    static bool fileExists(const char* path) {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
    }

    static bool dirExists(const char* path) {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
    }

    static bool buildPath(const char* dir, const char* file, char* out, size_t outSize) {
        if (!dir || !file || !out || outSize == 0) return false;
        size_t dirLen = strlen(dir);
        size_t fileLen = strlen(file);
        bool needsSlash = (dirLen > 0 && dir[dirLen - 1] != '\\' && dir[dirLen - 1] != '/');
        size_t total = dirLen + (needsSlash ? 1 : 0) + fileLen + 1;
        if (total > outSize) return false;
        memcpy(out, dir, dirLen);
        size_t offset = dirLen;
        if (needsSlash) out[offset++] = '\\';
        memcpy(out + offset, file, fileLen);
        out[offset + fileLen] = '\0';
        return true;
    }

    static bool deriveMsysRootFromDir(const char* dir, char* out, size_t outSize) {
        if (!dir || !*dir || !out || outSize == 0) return false;
        const char* suffixes[] = {
            "\\mingw64\\bin",
            "/mingw64/bin",
            "\\mingw32\\bin",
            "/mingw32/bin",
            "\\usr\\bin",
            "/usr/bin"
        };
        for (const char* suffix : suffixes) {
            if (endsWithIgnoreCase(dir, suffix)) {
                size_t dirLen = strlen(dir);
                size_t sufLen = strlen(suffix);
                size_t rootLen = dirLen - sufLen;
                while (rootLen > 0 && (dir[rootLen - 1] == '\\' || dir[rootLen - 1] == '/')) {
                    rootLen--;
                }
                if (rootLen == 0 || rootLen + 1 > outSize) return false;
                memcpy(out, dir, rootLen);
                out[rootLen] = '\0';
                return true;
            }
        }
        return false;
    }

    static void appendPathEntry(std::string& path, const char* entry) {
        if (!entry || !*entry) return;
        if (!path.empty() && path.back() != ';') {
            path.push_back(';');
        }
        path += entry;
    }

    static std::string formatWin32Error(DWORD err) {
        char buffer[256] = "";
        DWORD len = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            err,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer,
            (DWORD)sizeof(buffer),
            nullptr);
        if (len == 0) {
            return "Unknown error.";
        }
        while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) {
            buffer[--len] = '\0';
        }
        return buffer;
    }

    static bool ensureDirExists(const char* path) {
        if (!path || !*path) return false;
        if (dirExists(path)) return true;
        if (CreateDirectoryA(path, nullptr)) return true;
        DWORD err = GetLastError();
        return err == ERROR_ALREADY_EXISTS;
    }

    static bool buildDriveTmpFromPath(const char* path, char* outWin, size_t outWinSize, char* outPosix, size_t outPosixSize) {
        if (!path || !outWin || outWinSize == 0) return false;
        if (strlen(path) < 2 || path[1] != ':') return false;
        char drive = path[0];
        if (!((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z'))) return false;
        char upper = (char)std::toupper((unsigned char)drive);
        char lower = (char)std::tolower((unsigned char)drive);
        int written = snprintf(outWin, outWinSize, "%c:\\tmp", upper);
        if (written <= 0 || (size_t)written >= outWinSize) return false;
        if (outPosix && outPosixSize > 0) {
            int posixWritten = snprintf(outPosix, outPosixSize, "/%c/tmp", lower);
            if (posixWritten <= 0 || (size_t)posixWritten >= outPosixSize) {
                outPosix[0] = '\0';
            }
        }
        return true;
    }

    static bool getIniPath(char* out, size_t outSize) {
        if (!out || outSize == 0) return false;
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;
        if (!buildPath(exeDir, "pico2gba.ini", out, outSize)) return false;
        return true;
    }

    static void loadIniValue(const char* key, char* out, size_t outSize) {
        if (!g_iniPath[0] || !key || !out || outSize == 0) return;
        GetPrivateProfileStringA("Paths", key, "", out, (DWORD)outSize, g_iniPath);
    }

    static void saveIniValue(const char* key, const char* value) {
        if (!g_iniPath[0] || !key) return;
        WritePrivateProfileStringA("Paths", key, value ? value : "", g_iniPath);
    }

    static bool hasMakefile(const char* dir) {
        char path[MAX_PATH] = "";
        if (!buildPath(dir, "Makefile", path, sizeof(path))) return false;
        return fileExists(path);
    }

    static bool getGbaDir(char* out, size_t outSize) {
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir))) return false;
        if (hasMakefile(exeDir)) {
            if (strlen(exeDir) + 1 > outSize) return false;
            strcpy(out, exeDir);
            return true;
        }

        char parent[MAX_PATH] = "";
        strcpy(parent, exeDir);
        char* lastSlash = strrchr(parent, '\\');
        char* lastFwd = strrchr(parent, '/');
        char* sep = lastSlash;
        if (!sep || (lastFwd && lastFwd > sep)) sep = lastFwd;
        if (sep) {
            *sep = '\0';
            if (hasMakefile(parent)) {
                if (strlen(parent) + 1 > outSize) return false;
                strcpy(out, parent);
                return true;
            }
        }
        return false;
    }

    static bool deriveDevkitPro(const char* devkitArmPath, char* out, size_t outSize) {
        if (!devkitArmPath || !*devkitArmPath || !out || outSize == 0) return false;
        size_t len = strlen(devkitArmPath);
        while (len > 0 && (devkitArmPath[len - 1] == '\\' || devkitArmPath[len - 1] == '/')) {
            len--;
        }
        if (len < 9) return false;
        std::string trimmed(devkitArmPath, len);
        if (!endsWithIgnoreCase(trimmed.c_str(), "devkitarm")) return false;
        size_t newLen = len - 9;
        if (newLen == 0 || newLen + 1 > outSize) return false;
        memcpy(out, trimmed.c_str(), newLen);
        if (out[newLen - 1] == '\\' || out[newLen - 1] == '/') {
            out[newLen - 1] = '\0';
        } else {
            out[newLen] = '\0';
        }
        return true;
    }

    static bool deriveDevkitArmFromPro(const char* devkitProPath, char* out, size_t outSize) {
        if (!devkitProPath || !*devkitProPath || !out || outSize == 0) return false;
        if (!buildPath(devkitProPath, "devkitARM", out, outSize)) return false;
        return true;
    }

    static bool runMakeCommand(const char* gbaDir, const char* makePath, const char* devkitArmPath, const char* devkitProPath, const char* inputPath, const char* logPath, std::string& err) {
        if (!makePath || !*makePath) return false;
        if (!devkitArmPath || !*devkitArmPath) return false;

        std::string args = "V=1 rom CART_PNG=\"";
        args += inputPath;
        args += "\"";

        char makeDir[MAX_PATH] = "";
        if (!getDirFromPath(makePath, makeDir, sizeof(makeDir))) {
            err = "Failed to locate the make executable folder.";
            return false;
        }

        char msysRoot[MAX_PATH] = "";
        char msysUsrBin[MAX_PATH] = "";
        char msysTmp[MAX_PATH] = "";
        if (deriveMsysRootFromDir(makeDir, msysRoot, sizeof(msysRoot))) {
            buildPath(msysRoot, "usr\\bin", msysUsrBin, sizeof(msysUsrBin));
            buildPath(msysRoot, "tmp", msysTmp, sizeof(msysTmp));
        }

        char devkitArmBin[MAX_PATH] = "";
        if (buildPath(devkitArmPath, "bin", devkitArmBin, sizeof(devkitArmBin)) && !dirExists(devkitArmBin)) {
            devkitArmBin[0] = '\0';
        }
        char devkitToolsBin[MAX_PATH] = "";
        if (devkitProPath && devkitProPath[0]) {
            if (buildPath(devkitProPath, "tools\\bin", devkitToolsBin, sizeof(devkitToolsBin)) && !dirExists(devkitToolsBin)) {
                devkitToolsBin[0] = '\0';
            }
        }

        char tempDirWin[MAX_PATH] = "";
        char exeDir[MAX_PATH] = "";
        char exeTmp[MAX_PATH] = "";
        if (getExeDir(exeDir, sizeof(exeDir)) && buildPath(exeDir, "tmp", exeTmp, sizeof(exeTmp)) && ensureDirExists(exeTmp)) {
            snprintf(tempDirWin, sizeof(tempDirWin), "%s", exeTmp);
        } else {
            char cTmp[MAX_PATH] = "C:\\tmp";
            char driveTmpWin[MAX_PATH] = "";
            if (ensureDirExists(cTmp)) {
                snprintf(tempDirWin, sizeof(tempDirWin), "%s", cTmp);
            } else if (buildDriveTmpFromPath(gbaDir, driveTmpWin, sizeof(driveTmpWin), nullptr, 0) &&
                       ensureDirExists(driveTmpWin)) {
                snprintf(tempDirWin, sizeof(tempDirWin), "%s", driveTmpWin);
            } else if (msysTmp[0] && ensureDirExists(msysTmp)) {
                snprintf(tempDirWin, sizeof(tempDirWin), "%s", msysTmp);
            } else {
                DWORD tempLen = GetTempPathA((DWORD)sizeof(tempDirWin), tempDirWin);
                if (tempLen == 0 || tempLen >= sizeof(tempDirWin)) {
                    tempDirWin[0] = '\0';
                }
            }
        }

        std::string existingPath;
        std::vector<std::string> envVars;
        LPCH env = GetEnvironmentStringsA();
        if (env) {
            for (LPCH p = env; *p; ) {
                size_t len = strlen(p);
                if (startsWithIgnoreCase(p, "PATH=")) {
                    existingPath = p + 5;
                } else if (!startsWithIgnoreCase(p, "DEVKITARM=") && !startsWithIgnoreCase(p, "DEVKITPRO=") &&
                           !startsWithIgnoreCase(p, "SHELL=") && !startsWithIgnoreCase(p, "TMP=") &&
                           !startsWithIgnoreCase(p, "TEMP=") && !startsWithIgnoreCase(p, "TMPDIR=")) {
                    envVars.emplace_back(p, len);
                }
                p += len + 1;
            }
            FreeEnvironmentStringsA(env);
        }

        std::string newPath;
        if (msysUsrBin[0] && dirExists(msysUsrBin)) {
            appendPathEntry(newPath, msysUsrBin);
        }
        if (makeDir[0] && dirExists(makeDir)) {
            appendPathEntry(newPath, makeDir);
        }
        if (devkitArmBin[0] && dirExists(devkitArmBin)) {
            appendPathEntry(newPath, devkitArmBin);
        }
        if (devkitToolsBin[0] && dirExists(devkitToolsBin)) {
            appendPathEntry(newPath, devkitToolsBin);
        }
        if (!existingPath.empty()) {
            appendPathEntry(newPath, existingPath.c_str());
        }

        envVars.emplace_back(std::string("DEVKITARM=") + devkitArmPath);
        if (devkitProPath && devkitProPath[0]) {
            envVars.emplace_back(std::string("DEVKITPRO=") + devkitProPath);
        }
        if (!newPath.empty()) {
            envVars.emplace_back(std::string("PATH=") + newPath);
        }
        if (tempDirWin[0]) {
            envVars.emplace_back(std::string("TMP=") + tempDirWin);
            envVars.emplace_back(std::string("TEMP=") + tempDirWin);
            envVars.emplace_back(std::string("TMPDIR=") + tempDirWin);
        }
        envVars.emplace_back("MSYS2_ENV_CONV_EXCL=TMP;TEMP;TMPDIR");
        envVars.emplace_back("REAL8_HOST_CMD=1");
        envVars.emplace_back("SHELL=cmd.exe");

        std::vector<char> envBlock;
        for (const auto& var : envVars) {
            envBlock.insert(envBlock.end(), var.begin(), var.end());
            envBlock.push_back('\0');
        }
        envBlock.push_back('\0');

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE logHandle = INVALID_HANDLE_VALUE;
        if (logPath && *logPath) {
            logHandle = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (logHandle == INVALID_HANDLE_VALUE) {
                DWORD logErr = GetLastError();
                err = "Failed to open log file. ";
                err += formatWin32Error(logErr);
                return false;
            }
            const char* header = "pico2gba build log\r\n";
            DWORD written = 0;
            WriteFile(logHandle, header, (DWORD)strlen(header), &written, nullptr);
        }

        HANDLE stdIn = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE nullIn = INVALID_HANDLE_VALUE;
        if (stdIn == INVALID_HANDLE_VALUE || stdIn == nullptr) {
            nullIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (nullIn == INVALID_HANDLE_VALUE) {
                DWORD logErr = GetLastError();
                err = "Failed to open NUL for stdin. ";
                err += formatWin32Error(logErr);
                if (logHandle != INVALID_HANDLE_VALUE) {
                    CloseHandle(logHandle);
                }
                return false;
            }
            stdIn = nullIn;
        }

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = (logHandle != INVALID_HANDLE_VALUE) ? logHandle : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = (logHandle != INVALID_HANDLE_VALUE) ? logHandle : GetStdHandle(STD_ERROR_HANDLE);
        si.hStdInput = stdIn;
        PROCESS_INFORMATION pi = {};

        std::string cmd = "\"";
        cmd += makePath;
        cmd += "\" ";
        cmd += args;

        std::vector<char> cmdLine(cmd.begin(), cmd.end());
        cmdLine.push_back('\0');

        BOOL ok = CreateProcessA(
            nullptr,
            cmdLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            envBlock.data(),
            gbaDir,
            &si,
            &pi);

        if (!ok) {
            DWORD lastErr = GetLastError();
            std::string cmd = "cmd.exe /C \"\"";
            cmd += makePath;
            cmd += "\" ";
            cmd += args;
            cmd += "\"";

            std::vector<char> cmdLineShell(cmd.begin(), cmd.end());
            cmdLineShell.push_back('\0');

            ok = CreateProcessA(
                nullptr,
                cmdLineShell.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                envBlock.data(),
                gbaDir,
                &si,
                &pi);

            if (!ok) {
                DWORD secondErr = GetLastError();
                std::string details = formatWin32Error(secondErr);
                char msg[128] = "";
                snprintf(msg, sizeof(msg), "Failed to launch make (error %lu).", (unsigned long)secondErr);
                err = msg;
                err += " ";
                err += details;
                if (lastErr != secondErr) {
                    err += " (initial error ";
                    err += std::to_string((unsigned long)lastErr);
                    err += ").";
                }
                if (logHandle != INVALID_HANDLE_VALUE) {
                    CloseHandle(logHandle);
                }
                if (nullIn != INVALID_HANDLE_VALUE) {
                    CloseHandle(nullIn);
                }
                return false;
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (logHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(logHandle);
        }
        if (nullIn != INVALID_HANDLE_VALUE) {
            CloseHandle(nullIn);
        }

        if (exitCode != 0) {
            err = "Build failed. Check the log for details.";
            if (logPath && *logPath) {
                err += "\n";
                err += logPath;
            }
            return false;
        }

        return true;
    }

    static bool runMake(const char* gbaDir, const char* makePath, const char* devkitArmPath, const char* devkitProPath, const char* inputPath, const char* logPath, std::string& err) {
        if (runMakeCommand(gbaDir, makePath, devkitArmPath, devkitProPath, inputPath, logPath, err)) return true;
        if (err.empty()) {
            err = "Failed to launch make. Check the selected path and permissions.";
        }
        return false;
    }

    static bool copyBuiltRom(const char* gbaDir, const char* outputPath, std::string& err) {
        char builtPath[MAX_PATH] = "";
        if (!buildPath(gbaDir, "REAL8_GBA.gba", builtPath, sizeof(builtPath))) {
            err = "Failed to locate build output.";
            return false;
        }
        if (!fileExists(builtPath)) {
            err = "Build output not found.";
            return false;
        }
        if (_stricmp(builtPath, outputPath) == 0) {
            return true;
        }
        if (!MoveFileExA(builtPath, outputPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            DWORD moveErr = GetLastError();
            err = "Failed to move output file. ";
            err += formatWin32Error(moveErr);
            return false;
        }
        return true;
    }

    static void deleteCartBlob(const char* gbaDir) {
        char blobPath[MAX_PATH] = "";
        if (buildPath(gbaDir, "cart_blob.bin", blobPath, sizeof(blobPath))) {
            DeleteFileA(blobPath);
        }
    }

    static void setSelectedMake(const char* path) {
        if (!path) return;
        snprintf(g_makePath, sizeof(g_makePath), "%s", path);
        SetWindowTextA(g_makeEdit, g_makePath);
        saveIniValue("make", g_makePath);
        updateGenerateEnabled();
    }

    static void setSelectedDevkitPro(const char* path) {
        if (!path) return;
        snprintf(g_devkitProPath, sizeof(g_devkitProPath), "%s", path);
        SetWindowTextA(g_devkitEdit, g_devkitProPath);
        saveIniValue("devkitpro", g_devkitProPath);
        updateGenerateEnabled();
    }

    static void setSelectedGbaDir(const char* path) {
        if (!path) return;
        snprintf(g_gbaDir, sizeof(g_gbaDir), "%s", path);
        SetWindowTextA(g_gbaEdit, g_gbaDir);
        saveIniValue("gbadir", g_gbaDir);
        updateGenerateEnabled();
    }

    static void setSelectedCart(const char* path) {
        if (!path) return;
        snprintf(g_cartPath, sizeof(g_cartPath), "%s", path);
        SetWindowTextA(g_cartEdit, g_cartPath);
        saveIniValue("cart", g_cartPath);
        updateGenerateEnabled();
    }

    static void showMessage(const char* text, UINT flags) {
        MessageBoxA(nullptr, text, "Pico2GBA", flags);
    }

    static void startSpinner(HWND hwnd) {
        if (g_spinner) {
            SetWindowTextA(g_spinner, "|");
            ShowWindow(g_spinner, SW_SHOW);
        }
        g_building = true;
        SetTimer(hwnd, kIdSpinnerTimer, 100, nullptr);
    }

    static void stopSpinner(HWND hwnd) {
        KillTimer(hwnd, kIdSpinnerTimer);
        g_building = false;
        if (g_spinner) {
            SetWindowTextA(g_spinner, "");
        }
    }

    static unsigned __stdcall buildThreadProc(void* param) {
        BuildParams* params = static_cast<BuildParams*>(param);
        BuildResult* result = new BuildResult();
        std::string err;

        deleteCartBlob(params->gbaDir.c_str());
        if (!runMake(params->gbaDir.c_str(),
                     params->makePath.c_str(),
                     params->devkitArmPath.c_str(),
                     params->devkitProPath.c_str(),
                     params->cartPath.c_str(),
                     params->logPath.c_str(),
                     err)) {
            result->success = false;
            result->message = err;
        } else if (!copyBuiltRom(params->gbaDir.c_str(), params->outputPath.c_str(), err)) {
            result->success = false;
            result->message = err;
        } else {
            result->success = true;
            result->message = "Generated:\n";
            result->message += params->outputPath;
        }

        PostMessageA(params->hwnd, kMsgBuildDone, result->success ? 1 : 0, reinterpret_cast<LPARAM>(result));
        delete params;
        return 0;
    }

    static void applyDefaultPaths() {
        bool devkitInvalid = !g_devkitProPath[0] || !dirExists(g_devkitProPath);
        if (!devkitInvalid && g_devkitProPath[0] == '/') {
            devkitInvalid = true;
        }
        if (!devkitInvalid && startsWithIgnoreCase(g_devkitProPath, "/opt/devkitpro")) {
            devkitInvalid = true;
        }
        if (devkitInvalid) {
            setSelectedDevkitPro("C:\\devkitPro");
        }
        if (!g_makePath[0]) {
            setSelectedMake("C:\\msys64official\\mingw64\\bin\\mingw32-make.exe");
        }
        if (!g_gbaDir[0]) {
            char gbaDir[MAX_PATH] = "";
            if (getGbaDir(gbaDir, sizeof(gbaDir))) {
                setSelectedGbaDir(gbaDir);
            }
        }
    }

    static void handleBrowseMake(HWND owner) {
        char filePath[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = sizeof(filePath);
        ofn.lpstrFilter = "Make Executable (*.exe)\0*.exe\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (GetOpenFileNameA(&ofn)) {
            setSelectedMake(filePath);
        }
    }

    static void handleBrowseDevkitPro(HWND owner) {
        BROWSEINFOA bi = {};
        bi.hwndOwner = owner;
        bi.lpszTitle = "Select devkitPro folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            char folder[MAX_PATH] = "";
            if (SHGetPathFromIDListA(pidl, folder)) {
                setSelectedDevkitPro(folder);
            }
            CoTaskMemFree(pidl);
        }
    }

    static void handleBrowseGbaDir(HWND owner) {
        BROWSEINFOA bi = {};
        bi.hwndOwner = owner;
        bi.lpszTitle = "Select GBA Makefile folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            char folder[MAX_PATH] = "";
            if (SHGetPathFromIDListA(pidl, folder)) {
                if (!hasMakefile(folder)) {
                    showMessage("Makefile not found in the selected folder.", MB_ICONWARNING | MB_OK);
                } else {
                    setSelectedGbaDir(folder);
                }
            }
            CoTaskMemFree(pidl);
        }
    }

    static void handleBrowseCart(HWND owner) {
        char filePath[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = sizeof(filePath);
        ofn.lpstrFilter = "PICO-8 Cart (*.p8.png)\0*.p8.png\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (GetOpenFileNameA(&ofn)) {
            setSelectedCart(filePath);
        }
    }

    static void setBusy(bool busy) {
        EnableWindow(g_browseMakeButton, !busy);
        EnableWindow(g_browseDevkitButton, !busy);
        EnableWindow(g_browseGbaButton, !busy);
        EnableWindow(g_browseCartButton, !busy);
        EnableWindow(g_generateButton, !busy && g_makePath[0] != '\0' && g_devkitProPath[0] != '\0' && g_gbaDir[0] != '\0' && g_cartPath[0] != '\0');
        SetWindowTextA(g_generateButton, busy ? "Generating..." : "Generate");
    }

    static void handleGenerate(HWND hwnd) {
        if (!g_makePath[0] || !fileExists(g_makePath)) {
            showMessage("Select a valid mingw32-make.exe first.", MB_ICONWARNING | MB_OK);
            return;
        }

        if (!g_devkitProPath[0] || !dirExists(g_devkitProPath)) {
            showMessage("Select a valid devkitPro folder first.", MB_ICONWARNING | MB_OK);
            return;
        }

        if (!g_cartPath[0] || !fileExists(g_cartPath)) {
            showMessage("Select a .p8.png cart first.", MB_ICONWARNING | MB_OK);
            return;
        }

        if (!g_gbaDir[0] || !dirExists(g_gbaDir) || !hasMakefile(g_gbaDir)) {
            showMessage("Select a valid GBA Makefile folder first.", MB_ICONWARNING | MB_OK);
            return;
        }

        char gbaDir[MAX_PATH] = "";
        snprintf(gbaDir, sizeof(gbaDir), "%s", g_gbaDir);

        char outputPath[MAX_PATH] = "";
        if (!buildOutputPath(g_cartPath, outputPath, sizeof(outputPath))) {
            showMessage("Failed to build output path.", MB_ICONERROR | MB_OK);
            return;
        }

        char devkitProPath[MAX_PATH] = "";
        char devkitArmPath[MAX_PATH] = "";
        snprintf(devkitProPath, sizeof(devkitProPath), "%s", g_devkitProPath);
        if (!devkitProPath[0]) {
            DWORD len = GetEnvironmentVariableA("DEVKITPRO", devkitProPath, sizeof(devkitProPath));
            if (len == 0 || len >= sizeof(devkitProPath)) {
                char envDevkitArm[MAX_PATH] = "";
                DWORD armLen = GetEnvironmentVariableA("DEVKITARM", envDevkitArm, sizeof(envDevkitArm));
                if (armLen > 0 && armLen < sizeof(envDevkitArm)) {
                    if (!deriveDevkitPro(envDevkitArm, devkitProPath, sizeof(devkitProPath))) {
                        showMessage("DEVKITPRO could not be determined. Select the devkitPro folder.", MB_ICONERROR | MB_OK);
                        return;
                    }
                } else {
                    showMessage("DEVKITPRO could not be determined. Select the devkitPro folder.", MB_ICONERROR | MB_OK);
                    return;
                }
            }
        }

        if (!deriveDevkitArmFromPro(devkitProPath, devkitArmPath, sizeof(devkitArmPath)) || !dirExists(devkitArmPath)) {
            showMessage("devkitARM was not found inside the selected devkitPro folder.", MB_ICONERROR | MB_OK);
            return;
        }

        char logPath[MAX_PATH] = "";
        char exeDir[MAX_PATH] = "";
        if (!getExeDir(exeDir, sizeof(exeDir)) || !buildPath(exeDir, "pico2gba_build.log", logPath, sizeof(logPath))) {
            showMessage("Failed to prepare log file path.", MB_ICONERROR | MB_OK);
            return;
        }
        if (g_building) {
            showMessage("Build already in progress.", MB_ICONWARNING | MB_OK);
            return;
        }

        BuildParams* params = new BuildParams();
        params->hwnd = hwnd;
        params->makePath = g_makePath;
        params->devkitArmPath = devkitArmPath;
        params->devkitProPath = devkitProPath;
        params->cartPath = g_cartPath;
        params->gbaDir = gbaDir;
        params->outputPath = outputPath;
        params->logPath = logPath;

        setBusy(true);
        startSpinner(hwnd);

        uintptr_t thread = _beginthreadex(nullptr, 0, buildThreadProc, params, 0, nullptr);
        if (thread == 0) {
            stopSpinner(hwnd);
            setBusy(false);
            showMessage("Failed to start build thread.", MB_ICONERROR | MB_OK);
            delete params;
            return;
        }
        g_buildThread = reinterpret_cast<HANDLE>(thread);
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE: {
            RECT rect{};
            GetClientRect(hwnd, &rect);

            CreateWindowExA(
                0,
                "STATIC",
                "Step 1: Select mingw32-make.exe",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                kPadding,
                rect.right - (kPadding * 2),
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_makeEdit = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                kPadding,
                kPadding + 18,
                rect.right - (kPadding * 2) - (kButtonWidth + 10),
                24,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_browseMakeButton = CreateWindowExA(
                0,
                "BUTTON",
                "Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                rect.right - kPadding - kButtonWidth,
                kPadding + 18,
                kButtonWidth,
                24,
                hwnd,
                (HMENU)kIdBrowseMake,
                GetModuleHandleA(nullptr),
                nullptr);

            CreateWindowExA(
                0,
                "STATIC",
                "Step 2: Select devkitPro folder",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                kPadding + 52,
                rect.right - (kPadding * 2),
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_devkitEdit = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                kPadding,
                kPadding + 70,
                rect.right - (kPadding * 2) - (kButtonWidth + 10),
                24,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_browseDevkitButton = CreateWindowExA(
                0,
                "BUTTON",
                "Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                rect.right - kPadding - kButtonWidth,
                kPadding + 70,
                kButtonWidth,
                24,
                hwnd,
                (HMENU)kIdBrowseDevkit,
                GetModuleHandleA(nullptr),
                nullptr);

            CreateWindowExA(
                0,
                "STATIC",
                "Step 3: Select GBA Makefile location",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                kPadding + 104,
                rect.right - (kPadding * 2),
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_gbaEdit = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                kPadding,
                kPadding + 122,
                rect.right - (kPadding * 2) - (kButtonWidth + 10),
                24,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_browseGbaButton = CreateWindowExA(
                0,
                "BUTTON",
                "Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                rect.right - kPadding - kButtonWidth,
                kPadding + 122,
                kButtonWidth,
                24,
                hwnd,
                (HMENU)kIdBrowseGba,
                GetModuleHandleA(nullptr),
                nullptr);

            CreateWindowExA(
                0,
                "STATIC",
                "Step 4: Select .p8.png cart",
                WS_CHILD | WS_VISIBLE,
                kPadding,
                kPadding + 156,
                rect.right - (kPadding * 2),
                16,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_cartEdit = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                kPadding,
                kPadding + 174,
                rect.right - (kPadding * 2) - (kButtonWidth + 10),
                24,
                hwnd,
                nullptr,
                GetModuleHandleA(nullptr),
                nullptr);

            g_browseCartButton = CreateWindowExA(
                0,
                "BUTTON",
                "Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                rect.right - kPadding - kButtonWidth,
                kPadding + 174,
                kButtonWidth,
                24,
                hwnd,
                (HMENU)kIdBrowseCart,
                GetModuleHandleA(nullptr),
                nullptr);

            g_generateButton = CreateWindowExA(
                0,
                "BUTTON",
                "Generate",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                kPadding,
                kPadding + 212,
                kButtonWidth,
                kButtonHeight,
                hwnd,
                (HMENU)kIdGenerate,
                GetModuleHandleA(nullptr),
                nullptr);

            g_spinner = CreateWindowExA(
                0,
                "STATIC",
                "",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                kPadding + kButtonWidth + 8,
                kPadding + 212,
                24,
                kButtonHeight,
                hwnd,
                (HMENU)kIdSpinner,
                GetModuleHandleA(nullptr),
                nullptr);

            if (getIniPath(g_iniPath, sizeof(g_iniPath))) {
                char iniMake[MAX_PATH] = "";
                char iniDevkit[MAX_PATH] = "";
                char iniGba[MAX_PATH] = "";
                char iniCart[MAX_PATH] = "";
                loadIniValue("make", iniMake, sizeof(iniMake));
                loadIniValue("devkitpro", iniDevkit, sizeof(iniDevkit));
                loadIniValue("gbadir", iniGba, sizeof(iniGba));
                loadIniValue("cart", iniCart, sizeof(iniCart));
                if (iniMake[0]) setSelectedMake(iniMake);
                if (!iniDevkit[0]) {
                    char legacyDevkit[MAX_PATH] = "";
                    loadIniValue("devkitarm", legacyDevkit, sizeof(legacyDevkit));
                    if (legacyDevkit[0]) {
                        char derived[MAX_PATH] = "";
                        if (deriveDevkitPro(legacyDevkit, derived, sizeof(derived))) {
                            setSelectedDevkitPro(derived);
                        } else {
                            setSelectedDevkitPro(legacyDevkit);
                        }
                    }
                } else {
                    setSelectedDevkitPro(iniDevkit);
                }
                if (iniGba[0]) setSelectedGbaDir(iniGba);
                if (iniCart[0]) setSelectedCart(iniCart);
            }

            if (!g_devkitProPath[0]) {
                char envDevkitPro[MAX_PATH] = "";
                DWORD envLen = GetEnvironmentVariableA("DEVKITPRO", envDevkitPro, sizeof(envDevkitPro));
                if (envLen > 0 && envLen < sizeof(envDevkitPro)) {
                    setSelectedDevkitPro(envDevkitPro);
                } else {
                    char envDevkitArm[MAX_PATH] = "";
                    DWORD armLen = GetEnvironmentVariableA("DEVKITARM", envDevkitArm, sizeof(envDevkitArm));
                    if (armLen > 0 && armLen < sizeof(envDevkitArm)) {
                        char derived[MAX_PATH] = "";
                        if (deriveDevkitPro(envDevkitArm, derived, sizeof(derived))) {
                            setSelectedDevkitPro(derived);
                        }
                    }
                }
            }

            applyDefaultPaths();

            updateGenerateEnabled();
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kIdBrowseMake) {
                handleBrowseMake(hwnd);
            } else if (id == kIdBrowseDevkit) {
                handleBrowseDevkitPro(hwnd);
            } else if (id == kIdBrowseGba) {
                handleBrowseGbaDir(hwnd);
            } else if (id == kIdBrowseCart) {
                handleBrowseCart(hwnd);
            } else if (id == kIdGenerate) {
                handleGenerate(hwnd);
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == kIdSpinnerTimer && g_building && g_spinner) {
                static const char* frames = "|/-\\";
                static int frame = 0;
                char text[2] = { frames[frame], '\0' };
                frame = (frame + 1) % 4;
                SetWindowTextA(g_spinner, text);
            }
            break;
        }
        case kMsgBuildDone: {
            BuildResult* result = reinterpret_cast<BuildResult*>(lParam);
            stopSpinner(hwnd);
            setBusy(false);
            if (result) {
                showMessage(result->message.c_str(), wParam ? (MB_ICONINFORMATION | MB_OK) : (MB_ICONERROR | MB_OK));
                delete result;
            }
            if (g_buildThread) {
                CloseHandle(g_buildThread);
                g_buildThread = nullptr;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        return 0;
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmdShow) {
    const char* className = "Pico2GbaWindow";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        0,
        className,
        "Pico2GBA",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        312,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
