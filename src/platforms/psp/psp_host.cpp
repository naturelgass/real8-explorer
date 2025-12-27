#include "psp_host.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <malloc.h>

#ifndef REAL8_PSP_ENABLE_LOG
#define REAL8_PSP_ENABLE_LOG 1
#endif

extern "C" {
extern const unsigned char _binary_wallpaper_png_start[];
extern const unsigned char _binary_wallpaper_png_end[];
}

namespace {
constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr int kBufferW = 512;

constexpr int kGameW = 128;
constexpr int kGameH = 128;

constexpr int kAudioSampleRate = 22050;
constexpr int kAudioOutSamples = 1024;
constexpr int kAudioRingSamples = kAudioOutSamples * 8;
constexpr int kPbpEntryCount = 8;
constexpr int kPbpPic1Index = 4;

alignas(16) unsigned int g_cmdList[262144];

struct PbpHeader {
    char magic[4];
    uint32_t version;
    uint32_t offsets[kPbpEntryCount];
};

uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t packPsp565(uint8_t r, uint8_t g, uint8_t b)
{
    // PSP GU_PSM_5650 expects BGR ordering.
    return packRgb565(b, g, r);
}

int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

bool ensureDir(const std::string &path)
{
    SceUID dir = sceIoDopen(path.c_str());
    if (dir >= 0) {
        sceIoDclose(dir);
        return true;
    }

    int rc = sceIoMkdir(path.c_str(), 0777);
    if (rc >= 0) return true;

    dir = sceIoDopen(path.c_str());
    if (dir >= 0) {
        sceIoDclose(dir);
        return true;
    }
    return false;
}

bool fileExistsNonEmpty(const std::string &path)
{
    SceIoStat st;
    if (sceIoGetstat(path.c_str(), &st) < 0) return false;
    return st.st_size > 0;
}

bool writeFile(const std::string &path, const void *data, size_t size)
{
    if (!data || size == 0) return false;
    SceUID out = sceIoOpen(path.c_str(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) return false;

    const uint8_t *bytes = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    bool ok = true;

    while (remaining > 0) {
        uint32_t toWrite = remaining > 16 * 1024 ? 16 * 1024 : (uint32_t)remaining;
        int written = sceIoWrite(out, bytes, toWrite);
        if (written != (int)toWrite) { ok = false; break; }
        bytes += toWrite;
        remaining -= toWrite;
    }

    sceIoClose(out);
    if (!ok) {
        sceIoRemove(path.c_str());
    }
    return ok;
}

bool copyFile(const std::string &srcPath, const std::string &dstPath)
{
    SceUID in = sceIoOpen(srcPath.c_str(), PSP_O_RDONLY, 0);
    if (in < 0) return false;

    SceUID out = sceIoOpen(dstPath.c_str(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        return false;
    }

    static const int kChunk = 16 * 1024;
    char buf[kChunk];
    bool ok = true;
    while (true) {
        int rd = sceIoRead(in, buf, sizeof(buf));
        if (rd == 0) break;
        if (rd < 0) { ok = false; break; }
        int wr = sceIoWrite(out, buf, rd);
        if (wr != rd) { ok = false; break; }
    }

    sceIoClose(out);
    sceIoClose(in);

    if (!ok) {
        sceIoRemove(dstPath.c_str());
    }
    return ok;
}

bool extractPbpEntry(const std::string &pbpPath, int entryIndex, const std::string &outPath)
{
    SceUID in = sceIoOpen(pbpPath.c_str(), PSP_O_RDONLY, 0);
    if (in < 0) return false;

    PbpHeader header{};
    int readHeader = sceIoRead(in, &header, sizeof(header));
    if (readHeader != (int)sizeof(header)) {
        sceIoClose(in);
        return false;
    }

    const char kMagic[4] = {0, 'P', 'B', 'P'};
    if (memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) {
        sceIoClose(in);
        return false;
    }
    if (entryIndex < 0 || entryIndex >= kPbpEntryCount) {
        sceIoClose(in);
        return false;
    }

    SceOff fileSize = sceIoLseek(in, 0, PSP_SEEK_END);
    if (fileSize <= 0) {
        sceIoClose(in);
        return false;
    }

    uint32_t start = header.offsets[entryIndex];
    uint32_t end = (entryIndex + 1 < kPbpEntryCount)
        ? header.offsets[entryIndex + 1]
        : (uint32_t)fileSize;

    if (start < sizeof(PbpHeader) || end <= start || end > (uint32_t)fileSize) {
        sceIoClose(in);
        return false;
    }

    if (sceIoLseek(in, (SceOff)start, PSP_SEEK_SET) < 0) {
        sceIoClose(in);
        return false;
    }

    SceUID out = sceIoOpen(outPath.c_str(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        return false;
    }

    static const uint32_t kChunk = 16 * 1024;
    char buf[kChunk];
    uint32_t remaining = end - start;
    bool ok = true;
    while (remaining > 0) {
        uint32_t toRead = remaining < kChunk ? remaining : kChunk;
        int got = sceIoRead(in, buf, toRead);
        if (got <= 0) { ok = false; break; }
        int wr = sceIoWrite(out, buf, got);
        if (wr != got) { ok = false; break; }
        remaining -= (uint32_t)got;
    }
    sceIoClose(out);
    sceIoClose(in);

    if (!ok) {
        sceIoRemove(outPath.c_str());
        return false;
    }
    return true;
}
} // namespace

PspHost::PspHost()
{
    initGu();

    input.init();

    rootPath = "ms0:/PSP/GAME/REAL8";
    ensureDir(rootPath);
    ensureDir(rootPath + "/config");
    ensureDir(rootPath + "/saves");
    ensureDir(rootPath + "/mods");
    ensureDir(rootPath + "/screenshots");
    seedWallpaperFromPbp();

    gameTexture = (uint16_t*)memalign(16, kGameW * kGameH * sizeof(uint16_t));
    if (gameTexture) {
        memset(gameTexture, 0, kGameW * kGameH * sizeof(uint16_t));
    }

    initAudio();
}

PspHost::~PspHost()
{
    shutdownAudio();
    clearWallpaper();

    if (gameTexture) {
        free(gameTexture);
        gameTexture = nullptr;
    }

    sceGuTerm();
}

void PspHost::initGu()
{
    sceGuInit();

    sceGuStart(GU_DIRECT, g_cmdList);
    sceGuDrawBuffer(GU_PSM_5650, (void*)0, kBufferW);
    sceGuDispBuffer(kScreenW, kScreenH, (void*)(kBufferW * kScreenH * 2), kBufferW);
    sceGuDepthBuffer((void*)(kBufferW * kScreenH * 2 * 2), kBufferW);
    sceGuOffset(2048 - (kScreenW / 2), 2048 - (kScreenH / 2));
    sceGuViewport(2048, 2048, kScreenW, kScreenH);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, kScreenW, kScreenH);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    sceGuClearColor(0x00000000);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void PspHost::initAudio()
{
    outputSampleRate = 44100; // PSP user-mode output is effectively 44.1kHz; we resample in software.


    audioChannel = sceAudioChReserve(-1, kAudioOutSamples, PSP_AUDIO_FORMAT_STEREO);
    if (audioChannel < 0) {
        audioChannel = -1;
        return;
    }

    audioRing.assign(kAudioRingSamples, 0);
    ringHead = ringTail = ringCount = 0;
    memset(&audioMutex, 0, sizeof(audioMutex));
    int mrc = sceKernelCreateLwMutex(&audioMutex, "real8_audio", 0, 1, NULL);
    audioMutexInit = (mrc >= 0);
    audioRunning = true;

    audioThreadId = sceKernelCreateThread("real8_audio", audioThreadEntry, 0x12, 0x10000, 0, NULL);
    if (audioThreadId >= 0) {
        PspHost *self = this;
        sceKernelStartThread(audioThreadId, sizeof(self), &self);
    } else {
        audioRunning = false;
    }
}

void PspHost::shutdownAudio()
{
    if (audioRunning) {
        audioRunning = false;
        if (audioThreadId >= 0) {
            sceKernelWaitThreadEnd(audioThreadId, NULL);
            sceKernelDeleteThread(audioThreadId);
            audioThreadId = -1;
        }
    }

    if (audioChannel >= 0) {
        sceAudioChRelease(audioChannel);
        audioChannel = -1;
    }

    if (audioMutexInit) {
        sceKernelDeleteLwMutex(&audioMutex);
        audioMutexInit = false;
    }
}

void PspHost::seedWallpaperFromPbp()
{
    const std::string wallpaperPath = rootPath + "/config/wallpaper.png";
    if (fileExistsNonEmpty(wallpaperPath)) return;

    const unsigned char *start = _binary_wallpaper_png_start;
    const unsigned char *end = _binary_wallpaper_png_end;
    size_t size = (size_t)(end - start);
    if (size > 0 && writeFile(wallpaperPath, start, size)) {
        log("[PSP] Seeded wallpaper.png from embedded data.");
        return;
    }

    const std::string pbpPath = rootPath + "/EBOOT.PBP";
    if (extractPbpEntry(pbpPath, kPbpPic1Index, wallpaperPath)) {
        log("[PSP] Seeded wallpaper.png from EBOOT.PBP.");
        return;
    }

    const std::string pic1Path = rootPath + "/PIC1.PNG";
    if (copyFile(pic1Path, wallpaperPath)) {
        log("[PSP] Seeded wallpaper.png from PIC1.PNG.");
    } else {
        log("[PSP] Failed to seed wallpaper.png (EBOOT.PBP/PIC1.PNG).");
    }
}

void PspHost::resetAudioFifo()
{
    if (audioMutexInit) sceKernelLockLwMutex(&audioMutex, 1, NULL);
    ringHead = ringTail = ringCount = 0;
    if (audioMutexInit) sceKernelUnlockLwMutex(&audioMutex, 1);
}

int PspHost::audioThreadEntry(SceSize args, void *argp)
{
    (void)args;
    if (!argp) return 0;

    PspHost *self = *reinterpret_cast<PspHost**>(argp);
    if (!self) return 0;

    return self->audioThread();
}

int PspHost::audioThread()
{
    alignas(16) int16_t stereo[kAudioOutSamples * 2];
    int16_t mono[kAudioOutSamples];

    while (audioRunning) {
        int outRate = outputSampleRate;
        if (outRate <= 0) outRate = kAudioSampleRate;

        int needMono = (kAudioOutSamples * kAudioSampleRate + outRate - 1) / outRate;
        if (needMono < 1) needMono = 1;
        if (needMono > kAudioOutSamples) needMono = kAudioOutSamples;

        int toRead = 0;

        if (audioMutexInit) sceKernelLockLwMutex(&audioMutex, 1, NULL);
        size_t available = ringCount;
        toRead = (int)std::min(available, (size_t)needMono);
        for (int i = 0; i < toRead; ++i) {
            mono[i] = audioRing[ringTail];
            ringTail = (ringTail + 1) % audioRing.size();
        }
        ringCount -= (size_t)toRead;
        if (audioMutexInit) sceKernelUnlockLwMutex(&audioMutex, 1);

        for (int i = toRead; i < needMono; ++i) {
            mono[i] = 0;
        }

        if (outRate == kAudioSampleRate) {
            for (int i = 0; i < kAudioOutSamples; ++i) {
                int16_t s = (i < needMono) ? mono[i] : 0;
                stereo[i * 2 + 0] = s;
                stereo[i * 2 + 1] = s;
            }
        } else {
            for (int i = 0; i < kAudioOutSamples; ++i) {
                int src = (i * kAudioSampleRate) / outRate;
                if (src < 0) src = 0;
                if (src >= needMono) src = needMono - 1;
                int16_t s = mono[src];
                stereo[i * 2 + 0] = s;
                stereo[i * 2 + 1] = s;
            }
        }

        if (audioChannel >= 0) {
            sceAudioOutputBlocking(audioChannel, PSP_AUDIO_VOLUME_MAX, stereo);
        } else {
            sceKernelDelayThread(1000);
        }
    }

    return 0;
}

void PspHost::setInterpolation(bool active)
{
    interpolation = active;
}

void PspHost::calculateGameRect(float &outX, float &outY, float &outW, float &outH, float &outScale) const
{
    if (!debugVMRef || !debugVMRef->stretchScreen) {
        float maxScale = std::min((float)kScreenW / (float)kGameW, (float)kScreenH / (float)kGameH);
        float scale = maxScale;
        float intScale = floorf(maxScale);
        if (intScale < 1.0f) intScale = maxScale;
        scale = intScale;
        outScale = scale;
        outW = (float)kGameW * scale;
        outH = (float)kGameH * scale;
    } else {
        outW = (float)kGameW * 3.0f;
        outH = (float)kGameH * 2.0f;
        outScale = outH / (float)kGameH;
    }
    outX = ((float)kScreenW - outW) * 0.5f;
    outY = ((float)kScreenH - outH) * 0.5f;
}

void PspHost::flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map)
{
    if (!gameTexture || !framebuffer || !palette_map) return;

    uint16_t paletteLUT[16];
    for (int i = 0; i < 16; ++i) {
        uint8_t p8ID = palette_map[i];
        const uint8_t *rgb = nullptr;
        if (p8ID < 16) rgb = Real8Gfx::PALETTE_RGB[p8ID];
        else if (p8ID >= 128 && p8ID < 144) rgb = Real8Gfx::PALETTE_RGB[p8ID - 128 + 16];
        else rgb = Real8Gfx::PALETTE_RGB[p8ID & 0x0F];

        paletteLUT[i] = packPsp565(rgb[0], rgb[1], rgb[2]);
    }

    int idx = 0;
    for (int y = 0; y < kGameH; ++y) {
        for (int x = 0; x < kGameW; ++x) {
            gameTexture[idx++] = paletteLUT[framebuffer[y][x] & 0x0F];
        }
    }
    sceKernelDcacheWritebackRange(gameTexture, kGameW * kGameH * sizeof(uint16_t));

    float drawX = 0.0f, drawY = 0.0f, drawW = 0.0f, drawH = 0.0f, scale = 1.0f;
    calculateGameRect(drawX, drawY, drawW, drawH, scale);

    sceGuStart(GU_DIRECT, g_cmdList);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    if (wallTexture && wallW > 0 && wallH > 0) {
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
        sceGuTexImage(0, wallTexW, wallTexH, wallTexW, wallTexture);
        sceGuTexScale(1.0f / (float)wallTexW, 1.0f / (float)wallTexH);
        sceGuTexOffset(0.0f, 0.0f);

        struct Vertex {
            float u, v;
            float x, y, z;
        };
        Vertex *verts = (Vertex*)sceGuGetMemory(2 * sizeof(Vertex));
        verts[0].u = 0.0f;
        verts[0].v = 0.0f;
        verts[0].x = 0.0f;
        verts[0].y = 0.0f;
        verts[0].z = 0.0f;

        verts[1].u = (float)wallW;
        verts[1].v = (float)wallH;
        verts[1].x = (float)kScreenW;
        verts[1].y = (float)kScreenH;
        verts[1].z = 0.0f;

        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                       2, 0, verts);
    }

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
    sceGuTexFilter(interpolation ? GU_LINEAR : GU_NEAREST,
                   interpolation ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexImage(0, kGameW, kGameH, kGameW, gameTexture);
    sceGuTexScale(1.0f / (float)kGameW, 1.0f / (float)kGameH);
    sceGuTexOffset(0.0f, 0.0f);

    {
        struct Vertex {
            float u, v;
            float x, y, z;
        };
        Vertex *verts = (Vertex*)sceGuGetMemory(2 * sizeof(Vertex));
        verts[0].u = 0.0f;
        verts[0].v = 0.0f;
        verts[0].x = drawX;
        verts[0].y = drawY;
        verts[0].z = 0.0f;

        verts[1].u = (float)kGameW;
        verts[1].v = (float)kGameH;
        verts[1].x = drawX + drawW;
        verts[1].y = drawY + drawH;
        verts[1].z = 0.0f;

        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                       2, 0, verts);
    }

    if (crt_filter) {
        sceGuDisable(GU_TEXTURE_2D);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuColor(0x40000000);

        int y0 = (int)drawY;
        int y1 = (int)(drawY + drawH);
        for (int y = y0; y < y1; y += 2) {
            struct Vertex {
                float x, y, z;
            };
            Vertex *verts = (Vertex*)sceGuGetMemory(2 * sizeof(Vertex));
            verts[0].x = drawX;
            verts[0].y = (float)y;
            verts[0].z = 0.0f;
            verts[1].x = drawX + drawW;
            verts[1].y = (float)(y + 1);
            verts[1].z = 0.0f;

            sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, verts);
        }

        sceGuDisable(GU_BLEND);
        sceGuEnable(GU_TEXTURE_2D);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
}

unsigned long PspHost::getMillis()
{
    return (unsigned long)(sceKernelGetSystemTimeWide() / 1000ULL);
}

void PspHost::log(const char *fmt, ...)
{
#if REAL8_PSP_ENABLE_LOG
    const int BUF_SIZE = 2048;
    char buffer[BUF_SIZE];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, BUF_SIZE - 1, fmt, args);
    va_end(args);

    printf("%s\n", buffer);
#else
    (void)fmt;
#endif
}

void PspHost::delayMs(int ms)
{
    if (ms <= 0) return;
    sceKernelDelayThread(ms * 1000);
}

bool PspHost::isFastForwardHeld()
{
    return input.isFastForwardHeld();
}

std::string PspHost::resolveVirtualPath(const char *filename)
{
    std::string fname = filename ? filename : "";
    if (!fname.empty() && (fname[0] == '/' || fname[0] == '\\')) {
        fname = fname.substr(1);
    }

    std::string targetDir = rootPath;

    if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".sav") {
        targetDir = rootPath + "/saves";
    } else if (fname == "config.dat" || fname == "wallpaper.png" ||
               fname == "favorites.txt" || fname == "gameslist.json" ||
               fname == "gamesrepo.txt") {
        targetDir = rootPath + "/config";
    }

    ensureDir(targetDir);
    return targetDir + "/" + fname;
}

std::vector<uint8_t> PspHost::loadFile(const char *path)
{
    std::string fullPath = resolveVirtualPath(path);
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer((size_t)size);
    if (file.read((char*)buffer.data(), size)) return buffer;
    return {};
}

std::vector<std::string> PspHost::listFiles(const char *ext)
{
    std::vector<std::string> results;
    SceUID dir = sceIoDopen(rootPath.c_str());
    if (dir < 0) return results;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    while (sceIoDread(dir, &ent) > 0) {
        if (ent.d_name[0] != '\0') {
            if (ent.d_stat.st_attr & FIO_SO_IFDIR) {
                memset(&ent, 0, sizeof(ent));
                continue;
            }
            std::string name = ent.d_name;
            if (!ext || ext[0] == '\0' || name.find(ext) != std::string::npos) {
                results.push_back("/" + name);
            }
        }
        memset(&ent, 0, sizeof(ent));
    }

    sceIoDclose(dir);
    return results;
}

bool PspHost::saveState(const char *filename, const uint8_t *data, size_t size)
{
    std::string fullPath = resolveVirtualPath(filename);
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write((const char*)data, size);
    return true;
}

std::vector<uint8_t> PspHost::loadState(const char *filename)
{
    return loadFile(filename);
}

bool PspHost::hasSaveState(const char *filename)
{
    std::string fullPath = resolveVirtualPath(filename);
    SceIoStat st;
    return sceIoGetstat(fullPath.c_str(), &st) >= 0;
}

void PspHost::deleteFile(const char *path)
{
    std::string fullPath = resolveVirtualPath(path);
    sceIoRemove(fullPath.c_str());
}

void PspHost::getStorageInfo(size_t &used, size_t &total)
{
    used = 0;
    total = 1024ULL * 1024ULL * 1024ULL;
}

bool PspHost::renameGameUI(const char *currentPath)
{
    (void)currentPath;
    return false;
}

uint32_t PspHost::getPlayerInput(int playerIdx)
{
    return input.getMask(playerIdx);
}

void PspHost::pollInput()
{
    input.update();
}

void PspHost::clearInputState()
{
    input.clearState();
}

std::vector<uint8_t> PspHost::getInputConfigData()
{
    return {};
}

void PspHost::setInputConfigData(const std::vector<uint8_t>& data)
{
    (void)data;
}

void PspHost::openGamepadConfigUI()
{
    log("[PSP] External gamepad config UI not supported.");
}

void PspHost::pushAudio(const int16_t *samples, int count)
{
    if (!audioRunning || audioChannel < 0) return;

    if (!samples || count <= 0) {
        resetAudioFifo();
        return;
    }

    if (audioMutexInit) sceKernelLockLwMutex(&audioMutex, 1, NULL);

    size_t capacity = audioRing.size();
    if ((size_t)count > capacity) {
        samples += (count - (int)capacity);
        count = (int)capacity;
    }

    size_t freeSpace = capacity - ringCount;
    if ((size_t)count > freeSpace) {
        size_t drop = (size_t)count - freeSpace;
        ringTail = (ringTail + drop) % capacity;
        ringCount -= drop;
    }

    for (int i = 0; i < count; ++i) {
        audioRing[ringHead] = samples[i];
        ringHead = (ringHead + 1) % capacity;
    }
    ringCount += (size_t)count;

    if (audioMutexInit) sceKernelUnlockLwMutex(&audioMutex, 1);
}

NetworkInfo PspHost::getNetworkInfo()
{
    return {false, "", "OFFLINE", 0.0f};
}

bool PspHost::downloadFile(const char *url, const char *savePath)
{
    (void)url;
    (void)savePath;
    return false;
}

void PspHost::setNetworkActive(bool active)
{
    (void)active;
}

void PspHost::setWifiCredentials(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
}

void PspHost::takeScreenshot()
{
    log("[PSP] Screenshot not supported.");
}

void PspHost::drawWallpaper(const uint8_t *pixels, int w, int h)
{
    if (!pixels || w <= 0 || h <= 0) return;

    int texW = nextPow2(w);
    int texH = nextPow2(h);

    if (!wallTexture || w != wallW || h != wallH || texW != wallTexW || texH != wallTexH) {
        if (wallTexture) {
            free(wallTexture);
            wallTexture = nullptr;
        }

        wallTexture = (uint16_t*)memalign(16, texW * texH * sizeof(uint16_t));
        if (!wallTexture) return;

        wallW = w;
        wallH = h;
        wallTexW = texW;
        wallTexH = texH;
    }

    for (int y = 0; y < wallTexH; ++y) {
        for (int x = 0; x < wallTexW; ++x) {
            uint16_t out = 0;
            if (x < wallW && y < wallH) {
                const uint8_t *p = pixels + ((y * wallW + x) * 4);
                out = packPsp565(p[0], p[1], p[2]);
            }
            wallTexture[y * wallTexW + x] = out;
        }
    }

    sceKernelDcacheWritebackRange(wallTexture, wallTexW * wallTexH * sizeof(uint16_t));
}

void PspHost::clearWallpaper()
{
    if (wallTexture) {
        free(wallTexture);
        wallTexture = nullptr;
    }
    wallW = 0;
    wallH = 0;
    wallTexW = 0;
    wallTexH = 0;
}

void PspHost::updateOverlay()
{
}
