#include "gba_host.hpp"

#include <gba.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../../core/real8_gfx.h"
#include "../../core/real8_fonts.h"

namespace {
    volatile uint32_t g_vblankTicks = 0;

#ifndef REAL8_GBA_TILEMODE
#define REAL8_GBA_TILEMODE 1
#endif

    volatile uint16_t* const kMgbaDebugEnable = reinterpret_cast<volatile uint16_t*>(0x4FFF780);
    volatile char* const kMgbaDebugString = reinterpret_cast<volatile char*>(0x4FFF600);

#if defined(__GBA__) && !defined(REAL8_GBA_DEBUG_OVERLAY)
#define REAL8_GBA_DEBUG_OVERLAY 0
#endif

#if REAL8_GBA_TILEMODE
    constexpr int kTileSize = 8;
    constexpr int kScreenTiles = 16;
    constexpr int kMapTiles = 32;
    constexpr int kTileXOff = 7;
    constexpr int kTileYOff = 2;
    constexpr int kFirstScreenTile = 1;
    constexpr int kCharBlock = 0;
    constexpr int kScreenBlock = 31;
    static EWRAM_DATA OBJATTR g_oamShadow[128];
#endif

#if defined(__GBA__)
#ifndef REG_DMA3SAD
#define REG_DMA3SAD *(volatile u32*)(0x040000D4)
#define REG_DMA3DAD *(volatile u32*)(0x040000D8)
#define REG_DMA3CNT *(volatile u32*)(0x040000DC)
#endif
#ifndef DMA_ENABLE
#define DMA_ENABLE (1u << 31)
#endif
#ifndef DMA_START_NOW
#define DMA_START_NOW (0u << 28)
#endif
#ifndef DMA_32
#define DMA_32 (1u << 26)
#endif
#ifndef DMA_16
#define DMA_16 (0u << 26)
#endif
#ifndef DMA_SRC_INC
#define DMA_SRC_INC (0u << 23)
#endif
#ifndef DMA_DST_INC
#define DMA_DST_INC (0u << 21)
#endif
#ifndef OBJ_ON
#define OBJ_ON (1u << 12)
#endif
#ifndef OBJ_1D_MAP
#define OBJ_1D_MAP (1u << 6)
#endif
#ifndef ATTR0_COLOR_16
#define ATTR0_COLOR_16 (0u << 13)
#endif
#ifndef ATTR0_SQUARE
#define ATTR0_SQUARE (0u << 14)
#endif
#ifndef ATTR0_WIDE
#define ATTR0_WIDE (1u << 14)
#endif
#ifndef ATTR0_TALL
#define ATTR0_TALL (2u << 14)
#endif
#ifndef ATTR0_HIDE
#define ATTR0_HIDE (1u << 9)
#endif
#ifndef ATTR1_HFLIP
#define ATTR1_HFLIP (1u << 12)
#endif
#ifndef ATTR1_VFLIP
#define ATTR1_VFLIP (1u << 13)
#endif
#ifndef ATTR1_SIZE_8
#define ATTR1_SIZE_8 (0u << 14)
#endif
#ifndef ATTR1_SIZE_16
#define ATTR1_SIZE_16 (1u << 14)
#endif
#ifndef ATTR1_SIZE_32
#define ATTR1_SIZE_32 (2u << 14)
#endif
#ifndef ATTR1_SIZE_64
#define ATTR1_SIZE_64 (3u << 14)
#endif
#ifndef ATTR2_PRIORITY
#define ATTR2_PRIORITY(n) (((n) & 3) << 10)
#endif
#ifndef ATTR2_PALETTE
#define ATTR2_PALETTE(n) (((n) & 15) << 12)
#endif
#ifndef OBJ_VRAM
#define OBJ_VRAM ((u16*)(0x06010000))
#endif
#ifndef OBJ_PALETTE
#define OBJ_PALETTE ((u16*)(0x05000200))
#endif
#ifndef OAM
#define OAM ((OBJATTR*)(0x07000000))
#endif
#ifndef CHAR_BASE_BLOCK
#define CHAR_BASE_BLOCK(n) ((u16*)(0x06000000 + ((n) * 0x4000)))
#endif
#ifndef SCREEN_BASE_BLOCK
#define SCREEN_BASE_BLOCK(n) ((u16*)(0x06000000 + ((n) * 0x800)))
#endif
#ifndef BG_CHAR_BASE
#define BG_CHAR_BASE(n) ((n) << 2)
#endif
#ifndef BG_SCREEN_BASE
#define BG_SCREEN_BASE(n) ((n) << 8)
#endif
#ifndef BG_PRIORITY
#define BG_PRIORITY(n) ((n) & 3)
#endif
#ifndef BG_COLOR_16
#define BG_COLOR_16 0x0000
#endif
#ifndef BG_SIZE_0
#define BG_SIZE_0 0x0000
#endif

    static inline void dma3Copy32(const void* src, void* dst, u32 count) {
        REG_DMA3SAD = (u32)src;
        REG_DMA3DAD = (u32)dst;
        REG_DMA3CNT = count | DMA_32 | DMA_SRC_INC | DMA_DST_INC | DMA_START_NOW | DMA_ENABLE;
    }

    static inline void dma3Copy32Wait(const void* src, void* dst, u32 count) {
        dma3Copy32(src, dst, count);
        while (REG_DMA3CNT & DMA_ENABLE) {
        }
    }

    static inline void dma3Copy16(const void* src, void* dst, u32 count) {
        REG_DMA3SAD = (u32)src;
        REG_DMA3DAD = (u32)dst;
        REG_DMA3CNT = count | DMA_16 | DMA_SRC_INC | DMA_DST_INC | DMA_START_NOW | DMA_ENABLE;
    }
#endif

#if defined(__GBA__)
#if defined(REAL8_GBA_FB_IN_IWRAM)
#define IWRAM_CODE __attribute__((long_call))
#else
#define IWRAM_CODE __attribute__((section(".iwram"), long_call))
#endif
#define EWRAM_DATA __attribute__((section(".ewram")))
#else
#define IWRAM_CODE
#define EWRAM_DATA
#endif

    static void IWRAM_CODE blitFrame(u16* vram, const uint8_t (*framebuffer)[128], int xOff, int yOff) {
        const int stride = 240 / 2;
#if defined(__GBA__)
        const uint8_t* src = &framebuffer[0][0];
        u16* dst = vram + (yOff * stride + (xOff / 2));
        const u32 srcAddr = (u32)src;
        const u32 dstAddr = (u32)dst;
        if (((srcAddr | dstAddr) & 3u) == 0) {
            const u32 count = 128 / 4;
            for (int y = 0; y < 128; ++y) {
                dma3Copy32(src, dst, count);
                src += 128;
                dst += stride;
            }
        } else {
            const u32 count = 128 / 2;
            for (int y = 0; y < 128; ++y) {
                dma3Copy16(src, dst, count);
                src += 128;
                dst += stride;
            }
        }
#else
        for (int y = 0; y < 128; ++y) {
            u16* row16 = vram + ((y + yOff) * stride + (xOff / 2));
            u32* row32 = (u32*)row16;
            const uint8_t* src = framebuffer[y];
            for (int x = 0; x < 128; x += 4) {
                u32 v = (u32)src[x]
                    | ((u32)src[x + 1] << 8)
                    | ((u32)src[x + 2] << 16)
                    | ((u32)src[x + 3] << 24);
                row32[x / 4] = v;
            }
        }
#endif
    }

    void mgbaLog(const char* msg) {
        if (!msg) return;
        *kMgbaDebugEnable = 0xC0DE;
        volatile char* out = kMgbaDebugString;
        while (*msg) {
            *out++ = *msg++;
        }
        *out = '\0';
    }

#if REAL8_GBA_TILEMODE
    static EWRAM_DATA uint8_t g_packLow[256];
    static EWRAM_DATA uint8_t g_packHigh[256];
    static bool g_packLutInit = false;

    static void initPackLut() {
        if (g_packLutInit) return;
        for (int i = 0; i < 256; ++i) {
            g_packLow[i] = (uint8_t)(i & 0x0F);
            g_packHigh[i] = (uint8_t)((i & 0x0F) << 4);
        }
        g_packLutInit = true;
    }
#endif

    static uint32_t mapPicoButtons(uint16_t keys) {
        uint32_t mask = 0;
        if (keys & KEY_LEFT) mask |= (1u << 0);
        if (keys & KEY_RIGHT) mask |= (1u << 1);
        if (keys & KEY_UP) mask |= (1u << 2);
        if (keys & KEY_DOWN) mask |= (1u << 3);
        if (keys & (KEY_B | KEY_L)) mask |= (1u << 4);
        if (keys & (KEY_A | KEY_R)) mask |= (1u << 5);
        if (keys & (KEY_START | KEY_SELECT)) mask |= (1u << 6);
        return mask;
    }
}

GbaHost::GbaHost() {
    initVideo();
#if defined(__GBA__) && REAL8_GBA_ENABLE_AUDIO
    initAudio();
#endif
}

void GbaHost::resetVideo() {
    initVideo();
}

void GbaHost::initVideo() {
#if REAL8_GBA_TILEMODE
    initPackLut();
    tileModeActive = true;
    paletteValid = false;
    tilesPending = false;
    tilesFb = nullptr;
    objCount = 0;
    objSpriteSheet = nullptr;
    objPending = false;
    REG_DISPCNT = MODE_0 | BG0_ON | OBJ_ON | OBJ_1D_MAP;
    REG_BG0CNT = BG_COLOR_16 | BG_SIZE_0 | BG_CHAR_BASE(kCharBlock) | BG_SCREEN_BASE(kScreenBlock) | BG_PRIORITY(1);
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    BG_PALETTE[0] = RGB5(0, 0, 0);
    BG_PALETTE[16] = RGB5(0, 0, 0);

    u16* map = reinterpret_cast<u16*>(SCREEN_BASE_BLOCK(kScreenBlock));
    for (int i = 0; i < kMapTiles * kMapTiles; ++i) {
        map[i] = (1u << 12);
    }

    u16* tiles = reinterpret_cast<u16*>(CHAR_BASE_BLOCK(kCharBlock));
    const int tileCount = kFirstScreenTile + (kScreenTiles * kScreenTiles);
    for (int i = 0; i < tileCount * 16; ++i) {
        tiles[i] = 0;
    }

    for (int ty = 0; ty < kScreenTiles; ++ty) {
        for (int tx = 0; tx < kScreenTiles; ++tx) {
            const int mapIndex = (kTileYOff + ty) * kMapTiles + (kTileXOff + tx);
            const int tileIndex = kFirstScreenTile + (ty * kScreenTiles) + tx;
            map[mapIndex] = (u16)tileIndex;
        }
    }
#else
    tileModeActive = false;
    REG_DISPCNT = MODE_4 | BG2_ON;
    u16* vram = (u16*)VRAM;
    for (int i = 0; i < (240 * 160) / 2; ++i) {
        vram[i] = 0;
    }

    clearBorders();
#endif
}

#if defined(__GBA__) && REAL8_GBA_ENABLE_AUDIO
void GbaHost::initAudio() {
    if (audioInit) return;

    audioRingHead = 0;
    audioRingTail = 0;
    audioRingCount = 0;
    audioFrameIndex = 0;

    REG_SOUNDCNT_X = 0x0080; // master enable
    REG_SOUNDCNT_L = 0;
    REG_SOUNDCNT_H = DSOUNDCTRL_A100 | DSOUNDCTRL_AR | DSOUNDCTRL_AL
                   | DSOUNDCTRL_ATIMER(0) | DSOUNDCTRL_ARESET;
    REG_SOUNDBIAS = 0x0200;

    REG_TM0CNT_H = 0;
    REG_TM0CNT_L = (u16)(65536 - (16777216 / kAudioSampleRate));
    REG_TM0CNT_H = TIMER_START;

    REG_DMA1CNT = 0;
    audioInit = true;
}

void GbaHost::submitAudioFrame() {
    if (!audioInit) return;

    int8_t* out = audioFrames[audioFrameIndex & 1];
    for (int i = 0; i < kAudioFrameSamples; ++i) {
        int16_t s = 0;
        if (audioRingCount > 0) {
            s = audioRing[audioRingTail];
            audioRingTail = (audioRingTail + 1) % kAudioRingSamples;
            audioRingCount--;
        }
        out[i] = (int8_t)(s >> 8);
    }
    audioFrameIndex ^= 1;

    REG_DMA1CNT = 0;
    REG_DMA1SAD = (u32)out;
    REG_DMA1DAD = (u32)&REG_FIFO_A;
    REG_DMA1CNT = DMA_DST_FIXED | DMA_SRC_INC | DMA_REPEAT | DMA_SPECIAL | DMA32
                | DMA_ENABLE | (kAudioFrameSamples / 4);
}
#endif

void GbaHost::waitForVBlank() {
    while (REG_VCOUNT >= 160) {
    }
    while (REG_VCOUNT < 160) {
    }
#if REAL8_GBA_TILEMODE
    if (tileModeActive && tilesPending && tilesFb) {
        blitFrameTiles(tilesFb, tilesX0, tilesY0, tilesX1, tilesY1);
        tilesPending = false;
        tilesFb = nullptr;
    }
    if (tileModeActive) {
        flushSpriteBatch();
    }
#endif
#if defined(__GBA__) && REAL8_GBA_ENABLE_AUDIO
    submitAudioFrame();
#endif
    inputPolled = false;
    ++g_vblankTicks;
}

void GbaHost::setNetworkActive(bool active) {
    (void)active;
}

void GbaHost::setWifiCredentials(const char* ssid, const char* pass) {
    (void)ssid;
    (void)pass;
}

void GbaHost::clearBorders() {
#if REAL8_GBA_TILEMODE
    if (tileModeActive) return;
#endif
    // Keep borders black using palette index 16.
    BG_PALETTE[16] = RGB5(0, 0, 0);
    const int xOff = 56;
    const int yOff = 16;
    fillRect(0, 0, xOff, 160, 16);
    fillRect(xOff + 128, 0, 240 - (xOff + 128), 160, 16);
    fillRect(xOff, 0, 128, yOff, 16);
    fillRect(xOff, yOff + 128, 128, 160 - (yOff + 128), 16);
}

void GbaHost::beginFrame() {
#if REAL8_GBA_TILEMODE
    if (!tileModeActive) return;
    objCount = 0;
    objSpriteSheet = nullptr;
    objPending = false;
#endif
}

bool GbaHost::queueSprite(const uint8_t* spriteSheet, int n, int x, int y, int w, int h, bool fx, bool fy) {
#if REAL8_GBA_TILEMODE
    if (!tileModeActive || !spriteSheet) return false;
    if (w != 1 || h != 1) return false;
    if (objCount >= 128) return false;

    if (objCount == 0) {
        for (int i = 0; i < 128; ++i) {
            g_oamShadow[i].attr0 = ATTR0_HIDE;
            g_oamShadow[i].attr1 = 0;
            g_oamShadow[i].attr2 = 0;
        }
    }

    x += kTileXOff * 8;
    y += kTileYOff * 8;
    if (x <= -8 || x >= 240 || y <= -8 || y >= 160) {
        return true;
    }

    const int idx = 127 - objCount;
    const u16 attr0 = (u16)((y & 0xFF) | ATTR0_COLOR_16 | ATTR0_SQUARE);
    const u16 attr1 = (u16)((x & 0x1FF) | ATTR1_SIZE_8 | (fx ? ATTR1_HFLIP : 0) | (fy ? ATTR1_VFLIP : 0));
    const u16 attr2 = (u16)((n & 0x3FF) | ATTR2_PRIORITY(0) | ATTR2_PALETTE(0));
    g_oamShadow[idx].attr0 = attr0;
    g_oamShadow[idx].attr1 = attr1;
    g_oamShadow[idx].attr2 = attr2;

    objCount++;
    objSpriteSheet = spriteSheet;
    objPending = true;
    return true;
#else
    (void)spriteSheet;
    (void)n;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)fx;
    (void)fy;
    return false;
#endif
}

void GbaHost::cancelSpriteBatch() {
#if REAL8_GBA_TILEMODE
    if (!tileModeActive) return;
    for (int i = 0; i < 128; ++i) {
        g_oamShadow[i].attr0 = ATTR0_HIDE;
        g_oamShadow[i].attr1 = 0;
        g_oamShadow[i].attr2 = 0;
    }
    objCount = 0;
    objSpriteSheet = nullptr;
    objPending = true;
#endif
}

void GbaHost::flushSpriteBatch() {
#if REAL8_GBA_TILEMODE
    if (!objPending) return;
    if (objSpriteSheet) {
        dma3Copy32Wait(objSpriteSheet, OBJ_VRAM, 0x2000 / 4);
    }
    dma3Copy32Wait(g_oamShadow, OAM, sizeof(g_oamShadow) / 4);
    objPending = false;
#endif
}

void IWRAM_CODE GbaHost::blitFrameTiles(const uint8_t (*framebuffer)[128], int x0, int y0, int x1, int y1) {
#if REAL8_GBA_TILEMODE
    if (!framebuffer) return;
    if (x1 < 0 || y1 < 0 || x0 > 127 || y0 > 127) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 127) x1 = 127;
    if (y1 > 127) y1 = 127;

    const int tx0 = x0 >> 3;
    const int ty0 = y0 >> 3;
    const int tx1 = x1 >> 3;
    const int ty1 = y1 >> 3;

    const uint8_t* packLow = g_packLow;
    const uint8_t* packHigh = g_packHigh;
    u16* tileBase = reinterpret_cast<u16*>(CHAR_BASE_BLOCK(kCharBlock));
    for (int ty = ty0; ty <= ty1; ++ty) {
        const int py = ty * kTileSize;
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int px = tx * kTileSize;
            const int tileIndex = kFirstScreenTile + (ty * kScreenTiles) + tx;
            u16* tile = tileBase + (tileIndex * 16);
            alignas(4) u16 packed16[16];
            for (int row = 0; row < kTileSize; ++row) {
                const uint8_t* src = &framebuffer[py + row][px];
                const uint16_t b0 = (uint16_t)(packLow[src[0]] | packHigh[src[1]]);
                const uint16_t b1 = (uint16_t)(packLow[src[2]] | packHigh[src[3]]);
                const uint16_t b2 = (uint16_t)(packLow[src[4]] | packHigh[src[5]]);
                const uint16_t b3 = (uint16_t)(packLow[src[6]] | packHigh[src[7]]);
                packed16[row * 2] = (uint16_t)(b0 | (b1 << 8));
                packed16[row * 2 + 1] = (uint16_t)(b2 | (b3 << 8));
            }
#if defined(__GBA__)
            dma3Copy32Wait(packed16, tile, 8);
#else
            std::memcpy(tile, packed16, sizeof(packed16));
#endif
        }
    }
#else
    (void)framebuffer;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
#endif
}

void GbaHost::flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map) {
#if REAL8_GBA_TILEMODE
    if (tileModeActive) {
        flipScreenDirty(framebuffer, palette_map, 0, 0, 127, 127);
        return;
    }
#endif
    if (!framebuffer) {
        if (debugDirty) drawDebugOverlay();
        return;
    }

    for (int i = 0; i < 16; ++i) {
        uint8_t idx = palette_map ? palette_map[i] : (uint8_t)i;
        if (idx >= 128 && idx <= 143) idx = (uint8_t)(16 + (idx - 128));
        idx &= 0x1F;
        if (!paletteValid || lastPalette[i] != idx) {
            const uint8_t* rgb = Real8Gfx::PALETTE_RGB[idx];
            BG_PALETTE[i] = RGB5(rgb[0] >> 3, rgb[1] >> 3, rgb[2] >> 3);
            lastPalette[i] = idx;
        }
    }
    paletteValid = true;

    u16* vram = (u16*)VRAM;
    const int xOff = 56;
    const int yOff = 16;
    blitFrame(vram, framebuffer, xOff, yOff);

#ifdef REAL8_GBA_DEBUG_DOT
    BG_PALETTE[1] = RGB5(31, 0, 31);
    vram[0] = 0x0101;
#endif

    if (debugDirty) drawDebugOverlay();
}

void GbaHost::flipScreenDirty(uint8_t (*framebuffer)[128], uint8_t *palette_map,
                              int x0, int y0, int x1, int y1) {
#if REAL8_GBA_TILEMODE
    if (tileModeActive) {
        if (!framebuffer) {
            if (debugDirty) drawDebugOverlay();
            return;
        }

        for (int i = 0; i < 16; ++i) {
            uint8_t idx = palette_map ? palette_map[i] : (uint8_t)i;
            if (idx >= 128 && idx <= 143) idx = (uint8_t)(16 + (idx - 128));
            idx &= 0x1F;
            if (!paletteValid || lastPalette[i] != idx) {
                const uint8_t* rgb = Real8Gfx::PALETTE_RGB[idx];
                const u16 color = RGB5(rgb[0] >> 3, rgb[1] >> 3, rgb[2] >> 3);
                BG_PALETTE[i] = color;
                SPRITE_PALETTE[i] = color;
                lastPalette[i] = idx;
            }
        }
        paletteValid = true;

        tilesFb = framebuffer;
        if (!tilesPending) {
            tilesX0 = x0;
            tilesY0 = y0;
            tilesX1 = x1;
            tilesY1 = y1;
            tilesPending = true;
        } else {
            if (x0 < tilesX0) tilesX0 = x0;
            if (y0 < tilesY0) tilesY0 = y0;
            if (x1 > tilesX1) tilesX1 = x1;
            if (y1 > tilesY1) tilesY1 = y1;
        }

        if (debugDirty) drawDebugOverlay();
        return;
    }
#endif
    flipScreen(framebuffer, palette_map);
}

unsigned long GbaHost::getMillis() {
    return (unsigned long)((g_vblankTicks * 1000u) / 60u);
}

void GbaHost::log(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    mgbaLog(buf);
    pushDebugLine(buf);
    drawDebugOverlay();
}

void GbaHost::delayMs(int ms) {
    if (ms <= 0) return;
    int frames = (ms + 15) / 16;
    for (int i = 0; i < frames; ++i) {
        waitForVBlank();
    }
}

std::vector<uint8_t> GbaHost::loadFile(const char* path) {
    (void)path;
    return {};
}

std::vector<std::string> GbaHost::listFiles(const char* ext) {
    (void)ext;
    return {};
}

bool GbaHost::saveState(const char* filename, const uint8_t* data, size_t size) {
    (void)filename;
    (void)data;
    (void)size;
    return false;
}

std::vector<uint8_t> GbaHost::loadState(const char* filename) {
    (void)filename;
    return {};
}

bool GbaHost::hasSaveState(const char* filename) {
    (void)filename;
    return false;
}

void GbaHost::deleteFile(const char* path) {
    (void)path;
}

void GbaHost::getStorageInfo(size_t &used, size_t &total) {
    used = 0;
    total = 0;
}

bool GbaHost::renameGameUI(const char* currentPath) {
    (void)currentPath;
    return false;
}

uint32_t GbaHost::getPlayerInput(int playerIdx) {
    if (playerIdx != 0) return 0;
    if (!inputPolled) {
        pollInput();
    }
    return inputMask;
}

void GbaHost::pollInput() {
    if (inputPolled) return;
    scanKeys();
    keysHeldState = (uint16_t)keysHeld();
    keysDownState = (uint16_t)keysDown();
    inputMask = mapPicoButtons(keysHeldState);
    inputPolled = true;
}

void GbaHost::openGamepadConfigUI() {
}

std::vector<uint8_t> GbaHost::getInputConfigData() {
    return {};
}

void GbaHost::setInputConfigData(const std::vector<uint8_t>& data) {
    (void)data;
}

void GbaHost::pushAudio(const int16_t* samples, int count) {
#if defined(__GBA__) && REAL8_GBA_ENABLE_AUDIO
    if (!audioInit) initAudio();
    if (!samples || count <= 0) {
        audioRingHead = 0;
        audioRingTail = 0;
        audioRingCount = 0;
        return;
    }

    if (count > kAudioRingSamples) {
        samples += (count - kAudioRingSamples);
        count = kAudioRingSamples;
    }

    int freeSpace = kAudioRingSamples - audioRingCount;
    if (count > freeSpace) {
        int drop = count - freeSpace;
        audioRingTail = (audioRingTail + drop) % kAudioRingSamples;
        audioRingCount -= drop;
    }

    for (int i = 0; i < count; ++i) {
        audioRing[audioRingHead] = samples[i];
        audioRingHead = (audioRingHead + 1) % kAudioRingSamples;
    }
    audioRingCount += count;
#else
    (void)samples;
    (void)count;
#endif
}

NetworkInfo GbaHost::getNetworkInfo() {
    return {false, "", "Offline", 0.0f};
}

bool GbaHost::downloadFile(const char* url, const char* savePath) {
    (void)url;
    (void)savePath;
    return false;
}

void GbaHost::takeScreenshot() {
}

void GbaHost::drawWallpaper(const uint8_t* pixels, int w, int h) {
    (void)pixels;
    (void)w;
    (void)h;
}

void GbaHost::clearWallpaper() {
}

void GbaHost::updateOverlay() {
}

void GbaHost::renderDebugOverlay() {
#if defined(__GBA__) && (REAL8_GBA_DEBUG_OVERLAY == 0)
    debugDirty = false;
    return;
#endif
    debugDirty = true;
    drawDebugOverlay();
}

void GbaHost::pushDebugLine(const char* line) {
    if (!line || !line[0]) return;
    snprintf(debugLines[debugLineHead], kDebugLineLen, "%s", line);
    debugLineHead = (debugLineHead + 1) % kDebugLines;
    if (debugLineCount < kDebugLines) ++debugLineCount;
    debugDirty = true;
}

void GbaHost::drawDebugOverlay() {
#if defined(__GBA__) && (REAL8_GBA_DEBUG_OVERLAY == 0)
    debugDirty = false;
    return;
#endif
#if REAL8_GBA_TILEMODE
    if (tileModeActive) {
        debugDirty = false;
        return;
    }
#endif
    if (!debugDirty) return;
    debugDirty = false;

    BG_PALETTE[31] = RGB5(31, 31, 31);

    const int x0 = 0;
    const int y0 = 160 - 7;
    const int w = 240;
    const int h = 7;
    fillRect(x0, y0, w, h, 0);

    if (debugLineCount == 0) return;
    int idx = debugLineHead - 1;
    if (idx < 0) idx += kDebugLines;
    drawText4x6(2, y0 + 1, debugLines[idx], 31);
}

void GbaHost::drawChar4x6(int x, int y, char c, uint8_t color) {
    const uint8_t* rows = p8_4x6_bits(static_cast<uint8_t>(c));
    for (int row = 0; row < 6; ++row) {
        uint8_t bits = rows[row];
        for (int col = 0; col < 4; ++col) {
            if (bits & (0x80u >> col)) {
                putPixel(x + col, y + row, color);
            }
        }
    }
}

void GbaHost::drawText4x6(int x, int y, const char* text, uint8_t color) {
    if (!text) return;
    int cx = x;
    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            y += 7;
            cx = x;
            ++p;
            continue;
        }
        drawChar4x6(cx, y, *p, color);
        cx += 5;
        ++p;
    }
}

void GbaHost::fillRect(int x, int y, int w, int h, uint8_t color) {
    for (int iy = 0; iy < h; ++iy) {
        for (int ix = 0; ix < w; ++ix) {
            putPixel(x + ix, y + iy, color);
        }
    }
}

void GbaHost::putPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 240 || y < 0 || y >= 160) return;
    u16* vram = (u16*)VRAM;
    int idx = (y * 120) + (x >> 1);
    u16 val = vram[idx];
    if (x & 1) val = (u16)((val & 0x00FF) | (color << 8));
    else val = (u16)((val & 0xFF00) | color);
    vram[idx] = val;
}
