#pragma once

#include <3ds.h>
#include <3ds/services/soc.h>
#include <3ds/services/sslc.h>
#include <curl/curl.h>

#if defined(__has_include)
  #if __has_include(<3ds/services/ac.h>)
    #include <3ds/services/ac.h>
    #define REAL8_HAS_ACU 1
  #else
    #define REAL8_HAS_ACU 0
  #endif

  #if __has_include(<3ds/services/sslc.h>)
    #include <3ds/services/sslc.h>
    #define REAL8_HAS_SSLC 1
  #else
    #define REAL8_HAS_SSLC 0
  #endif
#else
  #include <3ds/services/ac.h>
  #include <3ds/services/sslc.h>
  #define REAL8_HAS_ACU 1
  #define REAL8_HAS_SSLC 1
#endif


#include <citro3d.h>
#include <citro2d.h>

#include "real8_host.h"
#include "real8_vm.h"
#include "real8_gfx.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

#ifndef REAL8_BOTTOM_NOBACK
#define REAL8_BOTTOM_NOBACK 0
#endif

namespace {
    const int kTopWidth = 400;
    const int kTopHeight = 240;
    const int kBottomWidth = 320;
    const int kBottomHeight = 240;
    const int kPicoWidth = 128;
    const int kPicoHeight = 128;
    const int kBottomGameSize = 256;
    const int kBottomPad = 5;
    const int kBottomVisibleHeight = kBottomHeight - (kBottomPad * 2);
    const int kBottomCropPx = kBottomGameSize - kBottomVisibleHeight;
    const int kBottomScale = kBottomGameSize / kPicoWidth;

    const int kSampleRate = 22050;
    const int kSamplesPerBuffer = 1024;
    const int kNumAudioBuffers = 2;
    // Citro3D clear color is 0xRRGGBBAA (like CLEAR_COLOR 0x68B0D8FF in examples)
    const u32 kClearColor = 0x000000FF; // black, fully opaque

    // --- GPU palette / indexed texture support ---
    // NOTE: The 3DS PICA200 does not have a programmable fragment shader stage.
    // The fastest way to do "palette lookup" is to use the GPU's native paletted texture formats (PAL4/PAL8) + TLUT.
    // This eliminates the per-pixel CPU color conversion (index -> RGB565) during normal rendering.
    #if defined(GPU_PAL8) && defined(GX_TRANSFER_FMT_I8)
        #define REAL8_3DS_HAS_PAL8_TLUT 1
    #else
        #define REAL8_3DS_HAS_PAL8_TLUT 0
    #endif

    // If enabled, keep the game textures CPU-accessible and write them directly in swizzled (tiled) order.
    // This avoids per-frame GX display transfers, which can stall the CPU on Old3DS.
    #ifndef REAL8_3DS_DIRECT_TEX_UPDATE
    #define REAL8_3DS_DIRECT_TEX_UPDATE 1
    #endif


    bool ensureDir(const std::string& path) {
        // If it already exists and is a directory, opendir will succeed
        if (DIR* d = opendir(path.c_str())) {
            closedir(d);
            return true;
        }

        // Try to create it
        if (mkdir(path.c_str(), 0777) == 0) return true;

        // Some setups fail mkdir if it already exists; re-check
        if (DIR* d = opendir(path.c_str())) {
            closedir(d);
            return true;
        }

        return false;
    }

    int nextPow2(int v) {
        int p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    uint16_t packBgr565(uint8_t r, uint8_t g, uint8_t b) {
        // 3DS textures expect BGR ordering for RGB565 uploads.
        return (uint16_t)(((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3));
    }

    uint32_t packAbgr8888(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        // Match 3DS RGBA8 texture byte order (ABGR in memory).
        return (uint32_t)((r << 24) | (g << 16) | (b << 8) | a);
    }

    // 3DS textures are stored as 8x8 tiles, with pixels inside each tile in Morton (Z-order) layout.
    // Addressing: tileIndex = (y>>3)*tilesPerRow + (x>>3) (row-major in tiles)
    //             within   = morton(x&7, y&7)
    // See GBATEK's '3DS Video Texture Swizzling'.
    static inline const u8* mortonLut64() {
        static u8 lut[64];
        static bool inited = false;
        if (!inited) {
            for (u32 y = 0; y < 8; ++y) {
                for (u32 x = 0; x < 8; ++x) {
                    u32 m = 0;
                    for (u32 i = 0; i < 3; ++i) {
                        m |= ((x >> i) & 1u) << (2u*i);
                        m |= ((y >> i) & 1u) << (2u*i + 1u);
                    }
                    lut[(y<<3) | x] = (u8)m;
                }
            }
            inited = true;
        }
        return lut;
    }

    static inline void swizzleCopyPal8_128(const u8* srcLinear128, u8* dstTiled, bool maskLowNibble) {
        const u8* mort = mortonLut64();
        constexpr int W = kPicoWidth;
        constexpr int H = kPicoHeight;
        constexpr int tilesX = W / 8;
        // 8-bit texel: 64 bytes per 8x8 tile
        for (int ty = 0; ty < H; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = 0; tx < W; tx += 8) {
                const int tileX = tx >> 3;
                u8* dstTile = dstTiled + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear128 + (ty + y) * W + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 v = srcRow[x];
                        if (maskLowNibble) v &= 0x0F;
                        dstTile[mort[(y<<3) | x]] = v;
                    }
                }
            }
        }
    }
    static inline void swizzleCopyRgb565FromIdx_128(const u8* srcLinear128, u16* dstTiled565, const u16* pal565) {
        const u8* mort = mortonLut64();
        constexpr int W = kPicoWidth;
        constexpr int H = kPicoHeight;
        constexpr int tilesX = W / 8;
        // 16-bit texel: 64 u16 per 8x8 tile
        for (int ty = 0; ty < H; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = 0; tx < W; tx += 8) {
                const int tileX = tx >> 3;
                u16* dstTile = dstTiled565 + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear128 + (ty + y) * W + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 c = srcRow[x] & 0x0F;
                        dstTile[mort[(y<<3) | x]] = pal565[c];
                    }
                }
            }
        }
    }

    void buildGameRect(bool stretch, bool hasWallpaper, int screenW, int screenH,
                   int &outX, int &outY, int &outW, int &outH, float &outScale) {
        // We only want the wallpaper visible on the *sides* of the top screen.
        // So we apply padding horizontally, but keep full height (no top/bottom padding).
        int padX = 0;
        int padY = 0;
        if (hasWallpaper) padX = stretch ? 10 : 20;

        int availW = screenW - (padX * 2);
        int availH = screenH - (padY * 2);
        if (availW < 1) availW = 1;
        if (availH < 1) availH = 1;

        if (stretch) {
            // Force 3x width (128 * 3 = 384) instead of filling the whole top screen width.
            int targetW = kPicoWidth * 3; // 384
            if (targetW > screenW) targetW = screenW;

            outW = targetW;
            outX = (screenW - outW) / 2;

            // Keep full height (no top/bottom padding)
            outY = padY;
            outH = screenH - (padY * 2);
            if (outH < 1) outH = 1;

            outScale = (float)outW / (float)kPicoWidth;
            return;
        }

        float scale = std::min((float)availW / (float)kPicoWidth,
                            (float)availH / (float)kPicoHeight);

        outW = (int)((float)kPicoWidth * scale);
        outH = (int)((float)kPicoHeight * scale);
        outX = (screenW - outW) / 2;
        outY = (screenH - outH) / 2;
        outScale = scale;
    }

    bool readConfigFlags2(const std::vector<uint8_t>& data, uint8_t& outFlags2) {
        if (data.size() < 6) return false;
        uint32_t inputSize = 0;
        memcpy(&inputSize, &data[1], 4);
        size_t offset = 5 + inputSize;
        if (data.size() <= offset) return false;
        outFlags2 = data[offset];
        return true;
    }

    bool writeBmp24(const std::string &path, const uint32_t *pixels) {
        const int width = kPicoWidth;
        const int height = kPicoHeight;
        const int rowSize = width * 3;
        const int imageSize = rowSize * height;
        const int fileSize = 14 + 40 + imageSize;

        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) return false;

        uint8_t fileHeader[14] = {
            'B','M',
            (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
            0,0,0,0,
            54,0,0,0
        };
        uint8_t infoHeader[40] = {
            40,0,0,0,
            (uint8_t)(width), (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
            (uint8_t)(height), (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
            1,0,
            24,0,
            0,0,0,0,
            (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
            0,0,0,0,
            0,0,0,0,
            0,0,0,0,
            0,0,0,0
        };

        out.write((char*)fileHeader, sizeof(fileHeader));
        out.write((char*)infoHeader, sizeof(infoHeader));

        for (int y = height - 1; y >= 0; --y) {
            const uint32_t *row = pixels + (y * width);
            for (int x = 0; x < width; ++x) {
                uint32_t c = row[x];
                uint8_t b = (uint8_t)(c & 0xFF);
                uint8_t g = (uint8_t)((c >> 8) & 0xFF);
                uint8_t r = (uint8_t)((c >> 16) & 0xFF);
                out.put((char)b);
                out.put((char)g);
                out.put((char)r);
            }
        }

        return true;
    }

    struct CurlWriteState {
        FILE *file = nullptr;
        size_t total = 0;
        bool error = false;
    };

    size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
        CurlWriteState *state = static_cast<CurlWriteState*>(userdata);
        if (!state || !state->file) return 0;
        const size_t bytes = size * nmemb;
        if (bytes == 0) return 0;
        const size_t written = fwrite(ptr, 1, bytes, state->file);
        state->total += written;
        if (written != bytes) {
            state->error = true;
            return 0;
        }
        return written;
    }
}

class ThreeDSHost : public IReal8Host
{
private:
    C3D_Tex *gameTex = nullptr;
    Tex3DS_SubTexture *gameSubtex = nullptr;
    C2D_Image gameImage;
    Tex3DS_SubTexture *gameSubtexBottom = nullptr;
    C2D_Image gameImageBottom;
    C3D_Tex *gameTexTop = nullptr;
    Tex3DS_SubTexture *gameSubtexTop = nullptr;
    C2D_Image gameImageTop;
    // Right-eye top screen texture for stereoscopic 3D
    C3D_Tex *gameTexTopR = nullptr;
    Tex3DS_SubTexture *gameSubtexTopR = nullptr;
    C2D_Image gameImageTopR;
    C3D_Tex *wallpaperTex = nullptr;
    Tex3DS_SubTexture *wallpaperSubtex = nullptr;
    C2D_Image wallpaperImage;
    C3D_RenderTarget *topTarget = nullptr;
    C3D_RenderTarget *topTargetR = nullptr; // Right-eye target (3D mode)
    C3D_RenderTarget *bottomTarget = nullptr;

    // --- Game framebuffer upload path ---
    // Legacy (CPU conversion): indices -> RGB565 into linear buffers, then DMA to tiled VRAM texture.
    u16 *pixelBuffer565Top = nullptr;
    u16 *pixelBuffer565Bottom = nullptr;
    size_t pixelBufferSize = 0;

    // GPU palette path: upload 8-bit indices, GPU does palette lookup (PAL8 + TLUT).
    u8 *indexBufferTop = nullptr;
    u8 *indexBufferBottom = nullptr;
    size_t indexBufferSize = 0;

    // Runtime switch: true when PAL8+TLUT is available and initialized successfully.
    bool useGpuPalette = false;

#if REAL8_3DS_HAS_PAL8_TLUT
    C3D_Tlut gameTlut;
    alignas(0x10) u16 tlutData[256] = {};
    u16 lastPalette565[16] = {};
    bool tlutReady = false;
#endif

    uint32_t screenBuffer32[kPicoWidth * kPicoHeight];
    u32 *wallpaperBuffer = nullptr;
    size_t wallpaperBufferSize = 0;
    int wallW = 0;
    int wallH = 0;
    int wallTexW = 0;
    int wallTexH = 0;

    ndspWaveBuf waveBuf[kNumAudioBuffers];
    int16_t *audioBuffer = nullptr;
    int audioBufIndex = 0;
    int audioBufPos = 0;
    bool audioReady = false;

    bool networkReady = false;
    bool acReady = false;
    bool sslcReady = false;
    bool curlReady = false;

    u32 *socBuffer = nullptr;
    bool socBufferOwned = false;


    u32 m_keysDown = 0;
    u32 m_keysHeld = 0;
    u64 m_lastInputPollMs = 0;
    int lastTouchX = 0;
    int lastTouchY = 0;
    uint8_t lastTouchBtn = 0;

    std::string rootPath = "sdmc:/real8";

    // ---- Audio FIFO (mono) to avoid blocking the emulation thread ----
    static const size_t kAudioFifoSamples = kSamplesPerBuffer * kNumAudioBuffers * 4; // mono samples
    int16_t* audioFifo = nullptr;
    size_t audioFifoHead = 0;   // write pos
    size_t audioFifoTail = 0;   // read pos
    size_t audioFifoCount = 0;  // number of valid mono samples in fifo
    int nextWaveToSubmit = 0;

    inline void audioFifoReset() {
        audioFifoHead = audioFifoTail = audioFifoCount = 0;
    }

    inline size_t audioFifoFree() const {
        return kAudioFifoSamples - audioFifoCount;
    }

    inline void audioFifoWriteMono(const int16_t* src, int count) {
        if (!audioFifo || count <= 0) return;

        // Never block: if overflow, drop newest samples.
        size_t freeSpace = audioFifoFree();
        if ((size_t)count > freeSpace) {
            count = (int)freeSpace;
            if (count <= 0) return;
        }

        for (int i = 0; i < count; ++i) {
            audioFifo[audioFifoHead] = src[i];
            audioFifoHead = (audioFifoHead + 1) % kAudioFifoSamples;
        }
        audioFifoCount += (size_t)count;
    }

    inline bool waveBufIsBusy(int idx) const {
        return waveBuf[idx].status == NDSP_WBUF_QUEUED || waveBuf[idx].status == NDSP_WBUF_PLAYING;
    }

    void pumpAudio() {
        if (!audioReady || !audioFifo) return;

        // Try to submit as many full buffers as possible without blocking.
        for (int attempts = 0; attempts < kNumAudioBuffers; ++attempts) {
            int idx = nextWaveToSubmit;

            if (waveBufIsBusy(idx)) break;
            if (audioFifoCount < (size_t)kSamplesPerBuffer) break;

            int16_t* dst = (int16_t*)waveBuf[idx].data_pcm16; // interleaved stereo

            // Fill one NDSP buffer worth of stereo frames from mono FIFO.
            for (int i = 0; i < kSamplesPerBuffer; ++i) {
                int16_t s = audioFifo[audioFifoTail];
                audioFifoTail = (audioFifoTail + 1) % kAudioFifoSamples;
                dst[i * 2 + 0] = s;
                dst[i * 2 + 1] = s;
            }
            audioFifoCount -= (size_t)kSamplesPerBuffer;

            submitAudioBuffer(idx);
            nextWaveToSubmit = (nextWaveToSubmit + 1) % kNumAudioBuffers;
        }
    }


    void initGfx() {
        gfxInitDefault();
        C3D_Init(0x10000);
        C2D_Init(256);
        C2D_Prepare();

        // 2D-only: disable depth testing once (no need to set it every frame).
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

        topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        topTargetR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
        bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

        gameTex = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
#if REAL8_3DS_HAS_PAL8_TLUT
        // 8-bit indexed texture; palette supplied via TLUT (no per-pixel CPU conversion).
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTex, kPicoWidth, kPicoHeight, GPU_PAL8);
    #else
        C3D_TexInitVRAM(gameTex, kPicoWidth, kPicoHeight, GPU_PAL8);
    #endif
#else
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTex, kPicoWidth, kPicoHeight, GPU_RGB565);
    #else
        C3D_TexInitVRAM(gameTex, kPicoWidth, kPicoHeight, GPU_RGB565);
    #endif
#endif
        C3D_TexSetFilter(gameTex, GPU_NEAREST, GPU_NEAREST);

        gameSubtex = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        gameSubtex->width = kPicoWidth;
        gameSubtex->height = kPicoHeight;
        gameSubtex->left = 0.0f;
        gameSubtex->top = 1.0f;
        gameSubtex->right = 1.0f;
        gameSubtex->bottom = 0.0f;

        gameImage.tex = gameTex;
        gameImage.subtex = gameSubtex;

        gameSubtexBottom = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        const int bottomCropSrc = kBottomCropPx / kBottomScale;
        const int bottomSrcHeight = kPicoHeight - bottomCropSrc;
        gameSubtexBottom->width = kPicoWidth;
        gameSubtexBottom->height = bottomSrcHeight;
        gameSubtexBottom->left = 0.0f;
        gameSubtexBottom->top = 1.0f;
        gameSubtexBottom->right = 1.0f;
        gameSubtexBottom->bottom = 1.0f - ((float)bottomSrcHeight / (float)kPicoHeight);

        gameImageBottom.tex = gameTex;
        gameImageBottom.subtex = gameSubtexBottom;

        gameTexTop = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
#if REAL8_3DS_HAS_PAL8_TLUT
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTexTop, kPicoWidth, kPicoHeight, GPU_PAL8);
    #else
        C3D_TexInitVRAM(gameTexTop, kPicoWidth, kPicoHeight, GPU_PAL8);
    #endif
#else
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTexTop, kPicoWidth, kPicoHeight, GPU_RGB565);
    #else
        C3D_TexInitVRAM(gameTexTop, kPicoWidth, kPicoHeight, GPU_RGB565);
    #endif
#endif
        C3D_TexSetFilter(gameTexTop, GPU_NEAREST, GPU_NEAREST);

        gameSubtexTop = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        gameSubtexTop->width = kPicoWidth;
        gameSubtexTop->height = kPicoHeight;
        gameSubtexTop->left = 0.0f;
        gameSubtexTop->top = 1.0f;
        gameSubtexTop->right = 1.0f;
        gameSubtexTop->bottom = 0.0f;

        gameImageTop.tex = gameTexTop;
        gameImageTop.subtex = gameSubtexTop;


        // Right-eye top texture for stereoscopic 3D
        gameTexTopR = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
#if REAL8_3DS_HAS_PAL8_TLUT
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTexTopR, kPicoWidth, kPicoHeight, GPU_PAL8);
    #else
        C3D_TexInitVRAM(gameTexTopR, kPicoWidth, kPicoHeight, GPU_PAL8);
    #endif
#else
    #if REAL8_3DS_DIRECT_TEX_UPDATE
        C3D_TexInit(gameTexTopR, kPicoWidth, kPicoHeight, GPU_RGB565);
    #else
        C3D_TexInitVRAM(gameTexTopR, kPicoWidth, kPicoHeight, GPU_RGB565);
    #endif
#endif
        C3D_TexSetFilter(gameTexTopR, GPU_NEAREST, GPU_NEAREST);

        gameSubtexTopR = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        *gameSubtexTopR = *gameSubtexTop; // same UVs/dimensions as the left-eye texture
        gameImageTopR.tex = gameTexTopR;
        gameImageTopR.subtex = gameSubtexTopR;

        // Allocate legacy RGB565 upload buffers (used when GPU palette isn't available, and for safety fallback).
        pixelBufferSize = (size_t)(kPicoWidth * kPicoHeight * sizeof(u16));
        pixelBuffer565Top = (u16*)linearAlloc(pixelBufferSize);
        pixelBuffer565Bottom = (u16*)linearAlloc(pixelBufferSize);

#if REAL8_3DS_HAS_PAL8_TLUT
        indexBufferSize = (size_t)(kPicoWidth * kPicoHeight);
        indexBufferTop = (u8*)linearAlloc(indexBufferSize);
        indexBufferBottom = (u8*)linearAlloc(indexBufferSize);

        // Initialize a 256-entry TLUT (we only use entries 0-15, but 256 keeps it simple).
        memset(tlutData, 0, sizeof(tlutData));
        memset(lastPalette565, 0xFF, sizeof(lastPalette565)); // force first upload
        tlutReady = C3D_TlutInit(&gameTlut, 256, GPU_RGB565);
        if (tlutReady) {
            C3D_TlutLoad(&gameTlut, tlutData);
            useGpuPalette = true;
        } else {            // TLUT init failed. Re-initialize textures back to RGB565 so the legacy path stays correct.
            useGpuPalette = false;

            C3D_TexDelete(gameTex);
#if REAL8_3DS_DIRECT_TEX_UPDATE
            C3D_TexInit(gameTex, kPicoWidth, kPicoHeight, GPU_RGB565);
#else
            C3D_TexInitVRAM(gameTex, kPicoWidth, kPicoHeight, GPU_RGB565);
#endif
            C3D_TexSetFilter(gameTex, GPU_NEAREST, GPU_NEAREST);

            C3D_TexDelete(gameTexTop);
#if REAL8_3DS_DIRECT_TEX_UPDATE
            C3D_TexInit(gameTexTop, kPicoWidth, kPicoHeight, GPU_RGB565);
#else
            C3D_TexInitVRAM(gameTexTop, kPicoWidth, kPicoHeight, GPU_RGB565);
#endif
            C3D_TexSetFilter(gameTexTop, GPU_NEAREST, GPU_NEAREST);

            C3D_TexDelete(gameTexTopR);
#if REAL8_3DS_DIRECT_TEX_UPDATE
            C3D_TexInit(gameTexTopR, kPicoWidth, kPicoHeight, GPU_RGB565);
#else
            C3D_TexInitVRAM(gameTexTopR, kPicoWidth, kPicoHeight, GPU_RGB565);
#endif
            C3D_TexSetFilter(gameTexTopR, GPU_NEAREST, GPU_NEAREST);
        }
#else
        useGpuPalette = false;
#endif

    }

    void initAudio() {
        // ndspInit() relies on the DSP firmware component. libctru looks for it at:
        //   sdmc:/3ds/dspfirm.cdc
        // and (depending on build) may also load from RomFS.
        //
        // Robust behaviour:
        //  1) Try ndspInit().
        //  2) If it fails and sdmc:/3ds/dspfirm.cdc is missing, try to copy romfs:/dspfirm.cdc to sdmc:/3ds/.
        //  3) Retry ndspInit() once and log a clear error if it still fails.

        auto copyFile = [&](const char* src, const char* dst) -> bool {
            FILE* in = fopen(src, "rb");
            if (!in) return false;

            FILE* out = fopen(dst, "wb");
            if (!out) { fclose(in); return false; }

            static const size_t kChunk = 0x4000;
            uint8_t buf[kChunk];
            bool ok = true;

            while (true) {
                size_t rd = fread(buf, 1, kChunk, in);
                if (rd == 0) break;
                if (fwrite(buf, 1, rd, out) != rd) { ok = false; break; }
            }

            fclose(out);
            fclose(in);
            return ok;
        };

        auto tryInit = [&]() -> Result { return ndspInit(); };

        Result rc = tryInit();
        if (R_FAILED(rc)) {
            log("[3DS][AUDIO] ndspInit failed: 0x%08lX", (unsigned long)rc);

            const char* kSdDspPath  = "sdmc:/3ds/dspfirm.cdc";
            const char* kSdDspDir   = "sdmc:/3ds";
            const char* kRomfsDspPath = "romfs:/dspfirm.cdc";

            // If the SD DSP firmware is missing, try to provision it from RomFS (CIA builds often bundle it).
            if (access(kSdDspPath, F_OK) != 0) {
                ensureDir(kSdDspDir);

                // Only attempt the copy if the RomFS file exists.
                FILE* test = fopen(kRomfsDspPath, "rb");
                if (test) {
                    fclose(test);
                    log("[3DS][AUDIO] Installing DSP firmware from RomFS...");
                    if (copyFile(kRomfsDspPath, kSdDspPath)) {
                        log("[3DS][AUDIO] DSP firmware installed to sdmc:/3ds/dspfirm.cdc. Retrying ndspInit...");
                        rc = tryInit();
                    } else {
                        log("[3DS][AUDIO] Failed to copy romfs:/dspfirm.cdc to sdmc:/3ds/dspfirm.cdc");
                    }
                }
            }

            if (R_FAILED(rc)) {
                log("[3DS][AUDIO] Audio disabled. Ensure DSP firmware exists at sdmc:/3ds/dspfirm.cdc "
                    "(many users install it via the DSP1 homebrew / 3ds.hacks.guide finalizing step), "
                    "or bundle romfs:/dspfirm.cdc in your build.");
                audioReady = false;
                return;
            }
        }

        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        ndspChnSetRate(0, kSampleRate);
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

        float mix[12];
        memset(mix, 0, sizeof(mix));
        mix[0] = 1.0f;
        mix[1] = 1.0f;
        ndspChnSetMix(0, mix);

        size_t totalFrames = (size_t)kSamplesPerBuffer * (size_t)kNumAudioBuffers;
        size_t totalI16 = totalFrames * 2; // stereo interleaved
        audioBuffer = (int16_t*)linearAlloc(totalI16 * sizeof(int16_t));
        if (!audioBuffer) {
            log("[3DS][AUDIO] linearAlloc failed for audio buffer");
            ndspExit();
            return;
        }
        memset(audioBuffer, 0, totalI16 * sizeof(int16_t));

        memset(waveBuf, 0, sizeof(waveBuf));
        for (int i = 0; i < kNumAudioBuffers; ++i) {
            waveBuf[i].data_vaddr = audioBuffer + (i * kSamplesPerBuffer * 2);
            waveBuf[i].data_pcm16 = (int16_t*)waveBuf[i].data_vaddr;
            waveBuf[i].nsamples   = kSamplesPerBuffer;
        }

        // Allocate FIFO in normal heap (don’t burn linear RAM).
        audioFifo = (int16_t*)malloc(kAudioFifoSamples * sizeof(int16_t));
        if (audioFifo) memset(audioFifo, 0, kAudioFifoSamples * sizeof(int16_t));
        audioFifoReset();
        nextWaveToSubmit = 0;

        audioReady = true;
    }


    void syncBottomWallpaperFromConfig() {
        uint8_t flags2 = 0;
        std::vector<uint8_t> data = loadFile("/config.dat");
        if (!readConfigFlags2(data, flags2)) return;
        bottomWallpaperVisible = (flags2 & (1 << 1)) == 0;
    }

    void initFs() {
        ensureDir(rootPath);
        ensureDir(rootPath + "/config");
        ensureDir(rootPath + "/saves");
        ensureDir(rootPath + "/mods");
        ensureDir(rootPath + "/screenshots");

        // Init RomFS once
        bool romfsReady = R_SUCCEEDED(romfsInit());
        if (romfsReady) {
            auto copyFromRomfs = [&](const std::string& romName, const std::string& dstName, bool overwrite) {
                const std::string dst = rootPath + "/config/" + dstName;

                if (!overwrite && access(dst.c_str(), F_OK) == 0) return;

                std::ifstream in(("romfs:/" + romName).c_str(), std::ios::binary);
                if (!in.is_open()) return;

                std::ofstream out(dst.c_str(), std::ios::binary | std::ios::trunc);
                if (!out.is_open()) return;

                out << in.rdbuf();
            };

            copyFromRomfs("config.dat", "config.dat", false);
            copyFromRomfs("wallpaper.png", "wallpaper.png", true);
            copyFromRomfs("gamesrepo.txt", "gamesrepo.txt", false);
        }
        syncBottomWallpaperFromConfig();
    }

    void initNetwork() {
        if (networkReady) return;

        // 1) Bring up ACU so we can check Wi-Fi state.
        Result rc = acInit();
        if (R_SUCCEEDED(rc)) {
            acReady = true;

            // Wait a short time for Wi-Fi to be actually up (handles reconnects / toggles).
            u32 wifi = 0;
            for (int i = 0; i < 25; ++i) { // ~5 seconds total
                ACU_GetWifiStatus(&wifi);
                if (wifi != 0) break;
                svcSleepThread(200 * 1000 * 1000LL); // 200ms
            }
            if (wifi == 0) {
                // Don’t hard-fail here; user might enable Wi-Fi later and we also re-check on demand.
                log("[3DS][NET] Wi-Fi not connected (ACU_GetWifiStatus=0). Requests may fail until Wi-Fi is enabled.");
            }
        } else {
            log("[3DS][NET] acInit failed: 0x%08lX", (u32)rc);
            acReady = false;
        }

        // 2) SOC buffer: use normal heap (more robust than linear memory).
        const size_t kSocBufferSize = 0x100000;
        if (!socBuffer) {
            socBuffer = (u32*)memalign(0x1000, kSocBufferSize);
            if (!socBuffer) {
                log("[3DS][NET] Failed to allocate SOC buffer.");
                return;
            }
            socBufferOwned = true;
            memset(socBuffer, 0, kSocBufferSize);
        }

        rc = socInit(socBuffer, kSocBufferSize);
        if (R_FAILED(rc)) {
            // If SOC is already initialized by something else, continuing can still work in practice,
            // but we log it so you can see what's going on.
            log("[3DS][NET] socInit failed: 0x%08lX", (u32)rc);
            if (socBufferOwned) {
                free(socBuffer);
                socBuffer = nullptr;
                socBufferOwned = false;
            }
            return;
        }

        // 3) SSL (helps with HTTPS stability on some setups)
        rc = sslcInit(0);
        if (R_SUCCEEDED(rc)) {
            sslcReady = true;
        } else {
            log("[3DS][NET] sslcInit failed: 0x%08lX (continuing)", (u32)rc);
            sslcReady = false;
            // Not fatal; HTTP-only endpoints can still work.
        }

        // 4) libcurl (3ds-curl)
        CURLcode curlRc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlRc != CURLE_OK) {
            log("[3DS][NET] curl_global_init failed: %d", (int)curlRc);
            if (sslcReady) { sslcExit(); sslcReady = false; }
            socExit();
            if (socBufferOwned) {
                free(socBuffer);
                socBuffer = nullptr;
                socBufferOwned = false;
            }
            return;
        }
        curlReady = true;

        networkReady = true;
    }

    void shutdownAudio() {
        if (!audioReady) return;
        ndspExit();

        if (audioBuffer) { linearFree(audioBuffer); audioBuffer = nullptr; }
        if (audioFifo)   { free(audioFifo); audioFifo = nullptr; }

        audioReady = false;
    }

    void shutdownNetwork() {
        if (!networkReady) return;

        if (curlReady) {
            curl_global_cleanup();
            curlReady = false;
        }

        if (sslcReady) {
            sslcExit();
            sslcReady = false;
        }

        socExit();

        if (acReady) {
            acExit();
            acReady = false;
        }

        if (socBuffer && socBufferOwned) {
            free(socBuffer);
            socBuffer = nullptr;
            socBufferOwned = false;
        }

        networkReady = false;
    }


    void shutdownGfx() {
        if (pixelBuffer565Top) {
            linearFree(pixelBuffer565Top);
            pixelBuffer565Top = nullptr;
        }
        if (pixelBuffer565Bottom) {
            linearFree(pixelBuffer565Bottom);
            pixelBuffer565Bottom = nullptr;
        }

#if REAL8_3DS_HAS_PAL8_TLUT
        if (indexBufferTop) {
            linearFree(indexBufferTop);
            indexBufferTop = nullptr;
        }
        if (indexBufferBottom) {
            linearFree(indexBufferBottom);
            indexBufferBottom = nullptr;
        }
        indexBufferSize = 0;

        if (tlutReady) {
            C3D_TlutDelete(&gameTlut);
            tlutReady = false;
        }
#endif
        if (wallpaperBuffer) {
            linearFree(wallpaperBuffer);
            wallpaperBuffer = nullptr;
        }
        if (wallpaperTex) {
            C3D_TexDelete(wallpaperTex);
            linearFree(wallpaperTex);
            wallpaperTex = nullptr;
        }
        if (wallpaperSubtex) {
            linearFree(wallpaperSubtex);
            wallpaperSubtex = nullptr;
        }
        if (gameTex) {
            C3D_TexDelete(gameTex);
            linearFree(gameTex);
            gameTex = nullptr;
        }
        if (gameSubtex) {
            linearFree(gameSubtex);
            gameSubtex = nullptr;
        }
        if (gameSubtexBottom) {
            linearFree(gameSubtexBottom);
            gameSubtexBottom = nullptr;
        }
        if (gameTexTop) {
            C3D_TexDelete(gameTexTop);
            linearFree(gameTexTop);
            gameTexTop = nullptr;
        }
        if (gameSubtexTop) {
            linearFree(gameSubtexTop);
            gameSubtexTop = nullptr;
        }

if (gameTexTopR) {
    C3D_TexDelete(gameTexTopR);
    linearFree(gameTexTopR);
    gameTexTopR = nullptr;
}
if (gameSubtexTopR) {
    linearFree(gameSubtexTopR);
    gameSubtexTopR = nullptr;
}
        C2D_Fini();
        C3D_Fini();
        gfxExit();
    }

    std::string resolveVirtualPath(const char *filename) {
        std::string fname = filename ? filename : "";
        if (!fname.empty() && fname[0] == '/') fname = fname.substr(1);

        std::string targetDir = rootPath;
        if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".sav") {
            targetDir = rootPath + "/saves";
        } else if (fname == "config.dat" || fname == "wallpaper.png" || fname == "favorites.txt" ||
                   fname == "gameslist.json" || fname == "gamesrepo.txt") {
            targetDir = rootPath + "/config";
        }

        ensureDir(targetDir);
        return targetDir + "/" + fname;
    }

    void submitAudioBuffer(int bufIndex) {
        int16_t *buf = (int16_t*)waveBuf[bufIndex].data_pcm16;
        DSP_FlushDataCache(buf, kSamplesPerBuffer * 2 * sizeof(int16_t));
        ndspChnWaveBufAdd(0, &waveBuf[bufIndex]);
    }

    void drawScanlines(int x, int y, int w, int h, float z) {
        if (w <= 0 || h <= 0) return;
        const u32 color = C2D_Color32(0, 0, 0, 80);
        for (int yy = 0; yy < h; yy += 2) {
            C2D_DrawRectSolid((float)x, (float)(y + yy), z, (float)w, 1.0f, color);
        }
    }

    #if REAL8_3DS_HAS_PAL8_TLUT
        inline void updateGpuPaletteIfNeeded(const u16* paletteLUT565) {
            if (!useGpuPalette || !tlutReady) return;
            if (memcmp(lastPalette565, paletteLUT565, 16 * sizeof(u16)) == 0) return;
    
            memcpy(lastPalette565, paletteLUT565, 16 * sizeof(u16));
            memcpy(tlutData, paletteLUT565, 16 * sizeof(u16));
            memset(tlutData + 16, 0, (256 - 16) * sizeof(u16));
    
            // Ensure the TLUT source is coherent before loading.
            GSPGPU_FlushDataCache(tlutData, sizeof(tlutData));
            C3D_TlutLoad(&gameTlut, tlutData);
        }
    
        // Fast path: upload 8-bit indices and let the GPU do the palette lookup via TLUT.
        void blitFrameToTexture(uint8_t (*framebuffer)[128], C3D_Tex *destTex,
                                const uint16_t *paletteLUT565, const uint32_t *paletteLUT32,
                                bool updateScreenshot, u8 *destIndexBuffer) {
            // 1) Optional screenshot capture (rare): keep CPU conversion only when needed.
            if (updateScreenshot) {
                int idx = 0;
                for (int y = 0; y < kPicoHeight; ++y) {
                    for (int x = 0; x < kPicoWidth; ++x) {
                        uint8_t col = framebuffer[y][x] & 0x0F;
                        screenBuffer32[idx++] = paletteLUT32[col];
                    }
                }
            }
            // 2) Update GPU palette if it changed this frame.
            updateGpuPaletteIfNeeded(paletteLUT565);

#if REAL8_3DS_DIRECT_TEX_UPDATE
            // 3) Write indices directly into the CPU-accessible texture in swizzled (tiled) order.
            swizzleCopyPal8_128((const u8*)framebuffer, (u8*)destTex->data, /*maskLowNibble=*/true);
            GSPGPU_FlushDataCache(destTex->data, (size_t)(kPicoWidth * kPicoHeight));
            (void)destIndexBuffer;
#else
            // 3) Copy indices linearly then use GX display transfer to swizzle into VRAM.
            memcpy(destIndexBuffer, framebuffer, (size_t)(kPicoWidth * kPicoHeight));
            GSPGPU_FlushDataCache(destIndexBuffer, indexBufferSize);
            C3D_SyncDisplayTransfer(
                (u32*)destIndexBuffer, GX_BUFFER_DIM(kPicoWidth, kPicoHeight),
                (u32*)destTex->data, GX_BUFFER_DIM(kPicoWidth, kPicoHeight),
                (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(1) |
                 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_I8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_I8) |
                 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
            );
#endif
        }
    #endif // REAL8_3DS_HAS_PAL8_TLUT
    
        // Legacy path: CPU converts indices -> RGB565 and uploads a 16-bit texture.
        void blitFrameToTexture(uint8_t (*framebuffer)[128], C3D_Tex *destTex,
                                const uint16_t *paletteLUT565, const uint32_t *paletteLUT32,
                                bool updateScreenshot, u16 *destBuffer565) {
#if REAL8_3DS_DIRECT_TEX_UPDATE
            // Write RGB565 directly into the CPU-accessible texture in swizzled (tiled) order.
            // Screenshot conversion (rare) stays linear for simplicity.
            if (updateScreenshot) {
                int idx = 0;
                for (int y = 0; y < kPicoHeight; ++y) {
                    for (int x = 0; x < kPicoWidth; ++x) {
                        uint8_t col = framebuffer[y][x] & 0x0F;
                        screenBuffer32[idx++] = paletteLUT32[col];
                    }
                }
            }
            // Swizzled write for the texture.
            const u8* mort = mortonLut64();
            constexpr int W = kPicoWidth;
            constexpr int H = kPicoHeight;
            constexpr int tilesX = W / 8;
            u16* dst = (u16*)destTex->data;
            for (int ty = 0; ty < H; ty += 8) {
                const int tileY = ty >> 3;
                for (int tx = 0; tx < W; tx += 8) {
                    const int tileX = tx >> 3;
                    u16* dstTile = dst + (tileY * tilesX + tileX) * 64;
                    for (int y = 0; y < 8; ++y) {
                        const u8* srcRow = &framebuffer[ty + y][tx];
                        for (int x = 0; x < 8; ++x) {
                            u8 c = srcRow[x] & 0x0F;
                            dstTile[mort[(y<<3) | x]] = paletteLUT565[c];
                        }
                    }
                }
            }
            GSPGPU_FlushDataCache(destTex->data, (size_t)(kPicoWidth * kPicoHeight * sizeof(u16)));
#else
            int idx = 0;
            for (int y = 0; y < kPicoHeight; ++y) {
                for (int x = 0; x < kPicoWidth; ++x) {
                    uint8_t col = framebuffer[y][x] & 0x0F;
                    uint16_t rgb565 = paletteLUT565[col];
                    destBuffer565[idx] = rgb565;
                    if (updateScreenshot) {
                        screenBuffer32[idx] = paletteLUT32[col];
                    }
                    idx++;
                }
            }
    
            GSPGPU_FlushDataCache(destBuffer565, pixelBufferSize);
            C3D_SyncDisplayTransfer(
                (u32*)destBuffer565, GX_BUFFER_DIM(kPicoWidth, kPicoHeight),
                (u32*)destTex->data, GX_BUFFER_DIM(kPicoWidth, kPicoHeight),
                (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
            );
#endif
        }

    // When browsing directories, the shell may give a dedicated top buffer.
    // If no preview exists, that top buffer is usually cleared to 0 (all pixels 0).
    // In that case we should not draw the top preview texture, so wallpaper/skin stays visible.
    bool isBlankTopPreview(uint8_t (*topbuffer)[128], uint8_t (*bottombuffer)[128]) const {
        // Normal gameplay path uses the same framebuffer for both (flipScreen() calls flipScreens(fb,fb,...))
        // Never treat that as "missing preview".
        if (topbuffer == bottombuffer) return false;

        for (int y = 0; y < kPicoHeight; ++y) {
            for (int x = 0; x < kPicoWidth; ++x) {
                if ((topbuffer[y][x] & 0x0F) != 0) return false;
            }
        }
        return true;
    }

public:
    Real8VM *debugVMRef = nullptr;
    bool crt_filter = false;
    bool interpolation = false;
    bool bottomWallpaperVisible = (REAL8_BOTTOM_NOBACK == 0);

    bool bottomStaticValid = false;
    bool lastInGameSingleScreen = false;

    ThreeDSHost() {
        initGfx();

        // IMPORTANT (audio robustness):
        // ndspInit() may need RomFS mounted when DSP firmware is bundled with the app (romfs:/dspfirm.cdc).
        // Initializing FS (and RomFS) first lets audio work out-of-the-box on installs without sdmc:/3ds/dspfirm.cdc.
        initFs();
        initAudio();

        initNetwork();
    }

    ~ThreeDSHost() {
        shutdownAudio();
        shutdownNetwork();
        shutdownGfx();
    }

    const char *getPlatform() override { return "3DS"; }
    std::string getClipboardText() override { return ""; }

    // Raw 3DS key state helpers (used by main loop / host controls)
    u32 getKeysHeldRaw() const { return m_keysHeld; }
    u32 getKeysDownRaw() const { return m_keysDown; }
    bool isExitComboHeld() const { return (m_keysHeld & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT); }

    void setInterpolation(bool active) {
        interpolation = active;
        if (gameTexTop) {
            GPU_TEXTURE_FILTER_PARAM filter = active ? GPU_LINEAR : GPU_NEAREST;
            C3D_TexSetFilter(gameTexTop, filter, filter);
        }
        if (gameTexTopR) {
            GPU_TEXTURE_FILTER_PARAM filter = active ? GPU_LINEAR : GPU_NEAREST;
            C3D_TexSetFilter(gameTexTopR, filter, filter);
        }
        if (gameTex) {
            C3D_TexSetFilter(gameTex, GPU_NEAREST, GPU_NEAREST);
        }
    }

    void waitForDebugEvent() override {
        gspWaitForVBlank();
        svcSleepThread(1000000LL);
    }

    void setNetworkActive(bool active) override {
        if (active && !networkReady) initNetwork();
        if (!active && networkReady) shutdownNetwork();
    }
    void setWifiCredentials(const char *ssid, const char *pass) override {}

    std::string getRepoUrlFromFile() override {
        std::string path = resolveVirtualPath("gamesrepo.txt");
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::string url;
        std::getline(file, url);
        const char *ws = " \t\n\r\f\v";
        url.erase(url.find_last_not_of(ws) + 1);
        url.erase(0, url.find_first_not_of(ws));
        return url;
    }

    void saveRepoUrlToFile(const std::string &url) override {
        std::string path = resolveVirtualPath("gamesrepo.txt");
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) file << url;
    }

    void flipScreen(uint8_t (*framebuffer)[128], uint8_t *palette_map) override {
        flipScreens(framebuffer, framebuffer, palette_map);
    }

    void flipScreens(uint8_t (*topbuffer)[128], uint8_t (*bottombuffer)[128], uint8_t *palette_map) override {
        uint16_t paletteLUT565[16];
        uint32_t paletteLUT32[16];

        for (int i = 0; i < 16; ++i) {
            uint8_t p8ID = palette_map[i];
            const uint8_t *rgb;
            if (p8ID < 16) rgb = Real8Gfx::PALETTE_RGB[p8ID];
            else if (p8ID >= 128 && p8ID < 144) rgb = Real8Gfx::PALETTE_RGB[p8ID - 128 + 16];
            else rgb = Real8Gfx::PALETTE_RGB[p8ID & 0x0F];

            uint8_t r = rgb[0];
            uint8_t g = rgb[1];
            uint8_t b = rgb[2];

            // Swap Red and Blue for the game palette path
            uint8_t tmp = r;
            r = b;
            b = tmp;

            paletteLUT565[i] = packBgr565(r, g, b);
            paletteLUT32[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }

        const bool inGameSingleScreen = (topbuffer == bottombuffer);

        if (inGameSingleScreen != lastInGameSingleScreen) {
            bottomStaticValid = false;
            lastInGameSingleScreen = inGameSingleScreen;
        }

        bool topPreviewBlank = false;
        if (!inGameSingleScreen && topbuffer) {
            topPreviewBlank = isBlankTopPreview(topbuffer, bottombuffer);
        }


        // Stereoscopic 3D (top screen): if enabled in the VM and the 3D slider is up,
        // render separate left/right eye images from vm->stereo_layers using bucket-based parallax.
        const float stereoSlider = osGet3DSliderState();
        const bool stereoActive =
            (inGameSingleScreen &&
             debugVMRef && debugVMRef->stereoscopic && !debugVMRef->isShellUI &&
             debugVMRef->stereo_layers != nullptr &&
             topTargetR != nullptr && gameTexTopR != nullptr &&
             stereoSlider > 0.01f);

        // When 3D is disabled, the system duplicates the left framebuffer to the right.
        gfxSet3D(stereoActive);

        if (stereoActive) {
            static uint8_t eyeL[128][128];
            static uint8_t eyeR[128][128];
            static uint8_t zL[128 * 128];
            static uint8_t zR[128 * 128];

            std::memset(eyeL, 0, sizeof(eyeL));
            std::memset(eyeR, 0, sizeof(eyeR));
            std::memset(zL, 0, sizeof(zL));
            std::memset(zR, 0, sizeof(zR));

            constexpr float kPxPerBucket = 1.0f; // 1 source pixel per bucket at max slider
            for (int li = 0; li < Real8VM::STEREO_LAYER_COUNT; ++li) {
                const int bucket = li - Real8VM::STEREO_BUCKET_BIAS;
                int shift = (int)lroundf((float)bucket * stereoSlider * kPxPerBucket);
                if (shift < -Real8VM::STEREO_BUCKET_MAX) shift = -Real8VM::STEREO_BUCKET_MAX;
                if (shift >  Real8VM::STEREO_BUCKET_MAX) shift =  Real8VM::STEREO_BUCKET_MAX;
                const uint8_t zval = (uint8_t)(bucket < 0 ? -bucket : bucket); // |bucket| in 0..7

                for (int y = 0; y < kPicoHeight; ++y) {
                    const uint8_t* src_row = debugVMRef->stereo_layers[li * kPicoHeight + y];
                    for (int x = 0; x < kPicoWidth; ++x) {
                        uint8_t src = src_row[x];
                        if (src == 0xFF) continue;
                        src &= 0x0F;

                        const int lx = x + shift;
                        if ((unsigned)lx < (unsigned)kPicoWidth) {
                            const int i = y * kPicoWidth + lx;
                            if (zval >= zL[i]) { zL[i] = zval; eyeL[y][lx] = src; }
                        }

                        const int rx = x - shift;
                        if ((unsigned)rx < (unsigned)kPicoWidth) {
                            const int i = y * kPicoWidth + rx;
                            if (zval >= zR[i]) { zR[i] = zval; eyeR[y][rx] = src; }
                        }
                    }
                }
            }

#if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                // Use separate CPU buffers for the two uploads to avoid any chance of overlap.
                blitFrameToTexture(eyeL, gameTexTop,  paletteLUT565, paletteLUT32, true,  indexBufferTop);
                blitFrameToTexture(eyeR, gameTexTopR, paletteLUT565, paletteLUT32, false, indexBufferBottom);
            } else
#endif
            {
                blitFrameToTexture(eyeL, gameTexTop,  paletteLUT565, paletteLUT32, true,  pixelBuffer565Top);
                blitFrameToTexture(eyeR, gameTexTopR, paletteLUT565, paletteLUT32, false, pixelBuffer565Bottom);
            }
        } else {

        if (inGameSingleScreen) {
            // Update top texture and screenshot buffer from the game framebuffer.
            #if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                blitFrameToTexture(topbuffer, gameTexTop, paletteLUT565, paletteLUT32, true, indexBufferTop);
            } else
#endif
            {
                blitFrameToTexture(topbuffer, gameTexTop, paletteLUT565, paletteLUT32, true, pixelBuffer565Top);
            }
        } else {
            // Normal: top preview + bottom UI
            if (!topPreviewBlank) {
                #if REAL8_3DS_HAS_PAL8_TLUT
                if (useGpuPalette) {
                    blitFrameToTexture(topbuffer, gameTexTop, paletteLUT565, paletteLUT32, false, indexBufferTop);
                } else
#endif
                {
                    blitFrameToTexture(topbuffer, gameTexTop, paletteLUT565, paletteLUT32, false, pixelBuffer565Top);
                }
            }
            #if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                blitFrameToTexture(bottombuffer, gameTex, paletteLUT565, paletteLUT32, true, indexBufferBottom);
            } else
#endif
            {
                blitFrameToTexture(bottombuffer, gameTex, paletteLUT565, paletteLUT32, true, pixelBuffer565Bottom);
            }
        }

        
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        int tx, ty, tw, th;
        float tscale;
        bool hasWallpaper = (wallpaperTex != nullptr);
        bool topStretch = debugVMRef && debugVMRef->stretchScreen;
        bool topHasWallpaper = hasWallpaper && (!debugVMRef || (debugVMRef->showSkin && !topStretch));
        bool bottomHasWallpaper = hasWallpaper && bottomWallpaperVisible;
        buildGameRect(topStretch, topHasWallpaper, kTopWidth, kTopHeight, tx, ty, tw, th, tscale);

        auto drawTopScene = [&](C3D_RenderTarget* tgt, const C2D_Image& img) {
            C3D_RenderTargetClear(tgt, C3D_CLEAR_ALL, kClearColor, 0);
            C2D_SceneBegin(tgt);

            if (topHasWallpaper && wallW > 0 && wallH > 0) {
            float scaleW = (float)kTopWidth / (float)wallW;
            float scaleH = (float)kTopHeight / (float)wallH;
            float scale = (scaleW > scaleH) ? scaleW : scaleH;
            float drawW = (float)wallW * scale;
            float drawH = (float)wallH * scale;
            float dstX = ((float)kTopWidth - drawW) * 0.5f;
            float dstY = ((float)kTopHeight - drawH) * 0.5f;
            C2D_DrawImageAt(wallpaperImage, dstX, dstY, 0.0f, nullptr, scale, scale);
        }

            float tscaleX = (float)tw / (float)kPicoWidth;

        // Use the "2x (minus 16px)" vertical mapping whenever the final height is the full
        // top-screen height (240). This keeps the game full-height even when the skin/wallpaper
        // is enabled, so the wallpaper only shows on the left/right.
        const bool useTallScale = (th == kTopHeight);

        // Bind the TLUT palette right before drawing the paletted game texture.
#if REAL8_3DS_HAS_PAL8_TLUT
        if (useGpuPalette && tlutReady) {
            C3D_TlutBind(0, &gameTlut);
        }
#endif

        if (!topPreviewBlank) {

            if (!useTallScale) {
                // Fallback: uniform scaling
                float tscaleY = (float)th / (float)kPicoHeight;
                C2D_DrawImageAt(img, (float)tx, (float)ty, 0.5f, nullptr, tscaleX, tscaleY);
            } else {
                // Special vertical mapping:
                // - First 16 source rows are NOT doubled (1x)
                // - Remaining rows are doubled (2x) when th==240 -> fits 240 (16 + 112*2 = 240)

                const int kNoDoubleRows = 16;
                const int srcW = kPicoWidth;   // 128
                const int srcH = kPicoHeight;  // 128
                const int topH = kNoDoubleRows;        // 16
                const int botH = srcH - topH;          // 112

                const float dstTopH = (float)topH;     // 16 pixels tall
                float dstBotH = (float)th - dstTopH;   // 224 when th==240
                if (dstBotH < 1.0f) dstBotH = 1.0f;

                const float scaleYBot = dstBotH / (float)botH;

                const float vTopFull = 1.0f;
                const float vSplit   = 1.0f - ((float)topH / (float)srcH); // 0.875
                const float vBotFull = 0.0f;

                Tex3DS_SubTexture subTop;
                subTop.width  = srcW;
                subTop.height = topH;
                subTop.left   = 0.0f;
                subTop.right  = 1.0f;
                subTop.top    = vTopFull;
                subTop.bottom = vSplit;

                C2D_Image imgTop = img;
                imgTop.subtex = &subTop;
                C2D_DrawImageAt(imgTop, (float)tx, (float)ty, 0.5f, nullptr, tscaleX, 1.0f);

                Tex3DS_SubTexture subBot;
                subBot.width  = srcW;
                subBot.height = botH;
                subBot.left   = 0.0f;
                subBot.right  = 1.0f;
                subBot.top    = vSplit;
                subBot.bottom = vBotFull;

                C2D_Image imgBot = img;
                imgBot.subtex = &subBot;
                C2D_DrawImageAt(imgBot, (float)tx, (float)ty + dstTopH, 0.5f, nullptr, tscaleX, scaleYBot);
            }

            if (crt_filter) {
                drawScanlines(tx, ty, tw, th, 0.5f);
            }
        }

        };

        // Left eye (always drawn)
        drawTopScene(topTarget, gameImageTop);
        // Right eye (only when stereoscopic 3D is active)
        if (stereoActive) {
            drawTopScene(topTargetR, gameImageTopR);
        }

        C3D_RenderTargetClear(bottomTarget, C3D_CLEAR_ALL, kClearColor, 0);
        C2D_SceneBegin(bottomTarget);
        const int bx = (kBottomWidth - kBottomGameSize) / 2;
        const int by = kBottomPad;
        const int bw = kBottomGameSize;
        const int bh = kBottomVisibleHeight;
        if (bottomHasWallpaper && wallW > 0 && wallH > 0) {
            float scaleW = (float)kBottomWidth / (float)wallW;
            float scaleH = (float)kBottomHeight / (float)wallH;
            float scale = (scaleW > scaleH) ? scaleW : scaleH;
            float drawW = (float)wallW * scale;
            float drawH = (float)wallH * scale;
            float dstX = ((float)kBottomWidth - drawW) * 0.5f;
            float dstY = ((float)kBottomHeight - drawH) * 0.5f;
            C2D_DrawImageAt(wallpaperImage, dstX, dstY, 0.0f, nullptr, scale, scale);
        }
        float bscaleX = (float)kBottomGameSize / (float)kPicoWidth;
        float bscaleY = (float)kBottomGameSize / (float)kPicoHeight;

        // Bind TLUT palette for the bottom game blit.
#if REAL8_3DS_HAS_PAL8_TLUT
        if (useGpuPalette && tlutReady) {
            C3D_TlutBind(0, &gameTlut);
        }
#endif

        if (!inGameSingleScreen) {
            C2D_DrawImageAt(gameImageBottom, (float)bx, (float)by, 0.5f, nullptr, bscaleX, bscaleY);
        }

        // Flush once after all targets have been drawn this frame.
        C2D_Flush();
        C3D_FrameEnd(0);
        
    }

    unsigned long getMillis() override { return (unsigned long)osGetTime(); }

    void log(const char *fmt, ...) override {
        const int kBufSize = 2048;
        char buffer[kBufSize] = {0};

        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, kBufSize - 1, fmt, args);
        va_end(args);

        printf("%s\n", buffer);

        // Only persist important lines (avoid huge files)
        const bool important =
            strstr(buffer, "ERROR") || strstr(buffer, "Lua") || strstr(buffer, "[VM]") ||
            strstr(buffer, "[LUA")  || strstr(buffer, "!!!");

        if (important) {
            std::string p = resolveVirtualPath("log.txt"); // goes to sdmc:/real8/log.txt
            FILE* f = fopen(p.c_str(), "a");
            if (f) {
                fprintf(f, "%s\n", buffer);
                fclose(f);
            }
        }
    }

    void delayMs(int ms) override {
        if (ms <= 0) return;
        svcSleepThread((s64)ms * 1000000LL);
    }

    bool isFastForwardHeld() override {
        return (m_keysHeld & KEY_R) != 0;
    }

    std::vector<uint8_t> loadFile(const char *path) override {
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

    std::vector<std::string> listFiles(const char *ext) override {
        std::vector<std::string> results;
        DIR *dir = opendir(rootPath.c_str());
        if (!dir) return results;
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            std::string filename = ent->d_name;
            if (ext && strlen(ext) > 0 && filename.find(ext) == std::string::npos) continue;
            results.push_back("/" + filename);
        }
        closedir(dir);
        return results;
    }

    bool saveState(const char *filename, const uint8_t *data, size_t size) override {
        std::string fullPath = resolveVirtualPath(filename);
        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) return false;
        file.write((const char*)data, size);
        return true;
    }

    std::vector<uint8_t> loadState(const char *filename) override {
        std::string fullPath = resolveVirtualPath(filename);
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        std::streamsize size = file.tellg();
        if (size <= 0) return {};
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer((size_t)size);
        if (file.read((char*)buffer.data(), size)) return buffer;
        return {};
    }

    bool hasSaveState(const char *filename) override {
        std::string fullPath = resolveVirtualPath(filename);
        struct stat st;
        return stat(fullPath.c_str(), &st) == 0;
    }

    void deleteFile(const char *path) override {
        std::string fullPath = resolveVirtualPath(path);
        remove(fullPath.c_str());
    }

    void getStorageInfo(size_t &used, size_t &total) override {
        used = 0;
        total = 2ULL * 1024 * 1024 * 1024;
    }

    bool renameGameUI(const char *currentPath) override {
        std::string fullPath = resolveVirtualPath(currentPath);
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) return false;

        std::string stem = fullPath;
        size_t lastSlash = stem.find_last_of('/');
        if (lastSlash != std::string::npos) stem = stem.substr(lastSlash + 1);

        std::string base = stem;
        std::string ext;
        size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) {
            base = stem.substr(0, dot);
            ext = stem.substr(dot);
        }

        SwkbdState swkbd;
        char out[64] = {0};
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, sizeof(out));
        swkbdSetHintText(&swkbd, "Enter new filename");
        swkbdSetInitialText(&swkbd, base.c_str());
        SwkbdButton btn = swkbdInputText(&swkbd, out, sizeof(out));
        if (btn != SWKBD_BUTTON_CONFIRM || strlen(out) == 0) return false;

        std::string newName = std::string(out) + ext;
        std::string newPath = rootPath + "/" + newName;
        return rename(fullPath.c_str(), newPath.c_str()) == 0;
    }

    uint32_t getPlayerInput(int playerIdx) override {
        if (playerIdx != 0) return 0;
        uint32_t mask = 0;
        if (m_keysHeld & KEY_LEFT) mask |= (1 << 0);
        if (m_keysHeld & KEY_RIGHT) mask |= (1 << 1);
        if (m_keysHeld & KEY_UP) mask |= (1 << 2);
        if (m_keysHeld & KEY_DOWN) mask |= (1 << 3);
        if (m_keysHeld & KEY_B) mask |= (1 << 4);
        if (m_keysHeld & KEY_A) mask |= (1 << 5);
        if ((m_keysHeld | m_keysDown) & KEY_START) mask |= (1 << 6);
        return mask;
    }

    void pollInput() override {
        u64 nowMs = osGetTime();
        if (nowMs == m_lastInputPollMs) return;
        m_lastInputPollMs = nowMs;

        hidScanInput();
        m_keysDown = hidKeysDown();
        m_keysHeld = hidKeysHeld();

        if (m_keysHeld & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            const int bx = (kBottomWidth - kBottomGameSize) / 2;
            const int by = kBottomPad;
            const int bw = kBottomGameSize;
            const int bh = kBottomVisibleHeight;

            if (touch.px >= bx && touch.px < (bx + bw) && touch.py >= by && touch.py < (by + bh)) {
                float relX = (float)(touch.px - bx);
                float relY = (float)(touch.py - by);
                float scaleX = (float)kBottomGameSize / (float)kPicoWidth;
                float scaleY = (float)kBottomGameSize / (float)kPicoHeight;
                int mx = (int)(relX / scaleX);
                int my = (int)(relY / scaleY);
                if (mx < 0) mx = 0;
                if (mx > 127) mx = 127;
                if (my < 0) my = 0;
                if (my > 127) my = 127;
                lastTouchX = mx;
                lastTouchY = my;
                lastTouchBtn = 1;
            } else {
                lastTouchBtn = 0;
            }
        } else {
            lastTouchBtn = 0;
        }
    }

    void clearInputState() override {
        m_keysDown = 0;
        m_keysHeld = 0;
        lastTouchBtn = 0;
    }

    MouseState getMouseState() override {
        MouseState ms;
        ms.x = lastTouchX;
        ms.y = lastTouchY;
        ms.btn = lastTouchBtn;
        return ms;
    }

    bool isKeyDownScancode(int scancode) override { return false; }
    std::vector<uint8_t> getInputConfigData() override { return {}; }
    void setInputConfigData(const std::vector<uint8_t>& data) override {}

    void openGamepadConfigUI() override {
        log("[3DS] External gamepad config UI not supported.");
    }

    void pushAudio(const int16_t *samples, int count) override {
        if (!audioReady) return;

        // Reset request from VM
        if (!samples || count <= 0) {
            // Stop playback and fully reset our submit state so audio can resume reliably.
            ndspChnWaveBufClear(0);

            // Clear FIFO and rewind submission pointer.
            audioFifoReset();
            nextWaveToSubmit = 0;

            // Mark all wave buffers as reusable (some libctru versions leave status stale after WaveBufClear).
            for (int i = 0; i < kNumAudioBuffers; ++i) {
                waveBuf[i].status = NDSP_WBUF_DONE;
            }

            // Zero the backing buffers to avoid clicks/pop after reset.
            if (audioBuffer) {
                const size_t totalI16 = (size_t)kSamplesPerBuffer * (size_t)kNumAudioBuffers * 2;
                memset(audioBuffer, 0, totalI16 * sizeof(int16_t));
                DSP_FlushDataCache(audioBuffer, totalI16 * sizeof(int16_t));
            }
            return;
        }

        // Write mono samples to FIFO quickly (never blocks).
        audioFifoWriteMono(samples, count);

        // Submit any newly-available full NDSP buffers.
        pumpAudio();
    }


    NetworkInfo getNetworkInfo() override {
        if (!networkReady) return {false, "", "Offline", 0.0f};
        return {true, "", "Online", 0.0f};
    }

    bool downloadFile(const char *url, const char *savePath) override {
        if (!url || !savePath) return false;

        // Some VM codepaths may toggle networking off. Be defensive and bring it up on-demand.
        if (!networkReady) initNetwork();
        if (!networkReady || !curlReady) return false;

        if (acReady) {
            u32 wifi = 0;
            ACU_GetWifiStatus(&wifi);
            if (wifi == 0) {
                log("[3DS][NET] Wi-Fi is not connected (ACU_GetWifiStatus=0). Aborting download.");
                return false;
            }
        }

        auto pickCaBundle = [&]() -> std::string {
            std::string sdCa = rootPath + "/config/cacert.pem";
            if (access(sdCa.c_str(), F_OK) == 0) return sdCa;
            const std::string romfsCa = "romfs:/cacert.pem";
            if (access(romfsCa.c_str(), F_OK) == 0) return romfsCa;
            return "";
        };

        const std::string caBundle = pickCaBundle();

        // Temp file in the real filesystem (supports atomic-ish replace).
        std::string fullPath = resolveVirtualPath(savePath);
        std::string tempPath = fullPath + ".tmp";

        FILE *out = fopen(tempPath.c_str(), "wb");
        if (!out) return false;

        CurlWriteState state;
        state.file = out;

        auto performDownload = [&](bool insecure, long &httpCode, std::string &err) -> CURLcode {
            CURL *curl = curl_easy_init();
            if (!curl) return CURLE_FAILED_INIT;

            char errBuf[CURL_ERROR_SIZE] = {0};
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Real8-3DS");
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Accept: application/json, */*;q=0.1");
            if (headers) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }

            if (insecure) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            } else {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
                if (!caBundle.empty()) {
                    curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle.c_str());
                }
            }

            CURLcode rc = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (errBuf[0] != '\0') err = errBuf;
            if (headers) {
                curl_slist_free_all(headers);
            }
            curl_easy_cleanup(curl);
            return rc;
        };

        long httpCode = 0;
        std::string err;
        CURLcode rc = performDownload(false, httpCode, err);

        if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT || rc == CURLE_SSL_CACERT_BADFILE) {
            fclose(out);
            out = fopen(tempPath.c_str(), "wb");
            if (!out) {
                remove(tempPath.c_str());
                return false;
            }
            state = {};
            state.file = out;
            err.clear();
            httpCode = 0;
            rc = performDownload(true, httpCode, err);
        }

        fclose(out);

        if (rc != CURLE_OK || state.error || state.total == 0) {
            if (!err.empty()) {
                log("[3DS][NET] downloadFile failed: %s (HTTP %ld)", err.c_str(), httpCode);
            } else {
                log("[3DS][NET] downloadFile failed: curl error %d (HTTP %ld)", (int)rc, httpCode);
            }
            remove(tempPath.c_str());
            return false;
        }

        // Optional: quick sanity check to catch accidental gzip/binary (helps debugging)
        {
            FILE *chk = fopen(tempPath.c_str(), "rb");
            if (chk) {
                unsigned char h[2] = {0,0};
                fread(h, 1, 2, chk);
                fclose(chk);
                // gzip magic: 1F 8B
                if (h[0] == 0x1F && h[1] == 0x8B) {
                    log("ERROR [3DS][NET] response was gzipped despite Accept-Encoding: identity url=%s", url);
                    remove(tempPath.c_str());
                    return false;
                }
            }
        }

        remove(fullPath.c_str());
        return rename(tempPath.c_str(), fullPath.c_str()) == 0;
    }


    void takeScreenshot() override {
        ensureDir(rootPath + "/screenshots");
        time_t now = time(nullptr);
        struct tm *t = localtime(&now);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/screenshots/scr_%04d%02d%02d_%02d%02d%02d.bmp",
                 rootPath.c_str(),
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        if (writeBmp24(buf, screenBuffer32)) {
            log("[3DS] Screenshot saved: %s", buf);
        } else {
            log("[3DS] Screenshot failed.");
        }
    }

    void drawWallpaper(const uint8_t *pixels, int w, int h) override {
        if (!pixels || w <= 0 || h <= 0) return;

        int texW = nextPow2(w);
        int texH = nextPow2(h);
        if (w != wallW || h != wallH || texW != wallTexW || texH != wallTexH || !wallpaperTex) {
            if (wallpaperBuffer) {
                linearFree(wallpaperBuffer);
                wallpaperBuffer = nullptr;
            }
            if (wallpaperTex) {
                C3D_TexDelete(wallpaperTex);
                linearFree(wallpaperTex);
                wallpaperTex = nullptr;
            }
            if (wallpaperSubtex) {
                linearFree(wallpaperSubtex);
                wallpaperSubtex = nullptr;
            }

            wallpaperTex = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
            C3D_TexInit(wallpaperTex, texW, texH, GPU_RGBA8);
            C3D_TexSetFilter(wallpaperTex, GPU_NEAREST, GPU_NEAREST);

            wallpaperSubtex = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
            wallpaperSubtex->width = w;
            wallpaperSubtex->height = h;
            wallpaperSubtex->left = 0.0f;
            wallpaperSubtex->top = 1.0f;
            wallpaperSubtex->right = (float)w / (float)texW;
            wallpaperSubtex->bottom = 1.0f - ((float)h / (float)texH);

            wallpaperImage.tex = wallpaperTex;
            wallpaperImage.subtex = wallpaperSubtex;

            wallW = w;
            wallH = h;
            wallTexW = texW;
            wallTexH = texH;

            wallpaperBufferSize = (size_t)(texW * texH * sizeof(u32));
            wallpaperBuffer = (u32*)linearAlloc(wallpaperBufferSize);
        }

        if (!wallpaperBuffer || !wallpaperTex) return;

        for (int y = 0; y < wallTexH; ++y) {
            for (int x = 0; x < wallTexW; ++x) {
                u32 color = 0xFF000000u;
                if (x < wallW && y < wallH) {
                    const uint8_t *px = pixels + ((y * wallW + x) * 4);
                    uint8_t r = px[0];
                    uint8_t g = px[1];
                    uint8_t b = px[2];
                    uint8_t a = px[3];
                    color = packAbgr8888(r, g, b, a);
                }
                wallpaperBuffer[y * wallTexW + x] = color;
            }
        }

        GSPGPU_FlushDataCache(wallpaperBuffer, wallpaperBufferSize);
        C3D_SyncDisplayTransfer(
            (u32*)wallpaperBuffer, GX_BUFFER_DIM(wallTexW, wallTexH),
            (u32*)wallpaperTex->data, GX_BUFFER_DIM(wallTexW, wallTexH),
            (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
             GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
             GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
        );

        bottomStaticValid = false;
    }

    void clearWallpaper() override {
        if (wallpaperBuffer) {
            linearFree(wallpaperBuffer);
            wallpaperBuffer = nullptr;
        }
        if (wallpaperTex) {
            C3D_TexDelete(wallpaperTex);
            linearFree(wallpaperTex);
            wallpaperTex = nullptr;
        }
        if (wallpaperSubtex) {
            linearFree(wallpaperSubtex);
            wallpaperSubtex = nullptr;
        }
        wallW = 0;
        wallH = 0;
        wallTexW = 0;
        wallTexH = 0;
    }
    void updateOverlay() override {}
};
