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
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <functional>
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

    const int kSampleRate = 32000;

    #ifndef REAL8_3DS_AUDIO_NO_GLITCH
    #define REAL8_3DS_AUDIO_NO_GLITCH 1
    #endif

    #if REAL8_3DS_AUDIO_NO_GLITCH
    const int kSamplesPerBuffer = 1024;
    const int kNumAudioBuffers = 6;
    const int kFifoTargetMs = 140;
    const int kFifoMinStartMs = 80;
    const int kFifoMaxMs = 300;
    #else
    const int kSamplesPerBuffer = 1024;
    const int kNumAudioBuffers = 4;
    const int kFifoTargetMs = 140;
    const int kFifoMinStartMs = 50;
    const int kFifoMaxMs = 200;
    #endif
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

    static inline void swizzleCopyPal8(const u8* srcLinear, u8* dstTiled, int srcW, int srcH, int dstW, bool maskLowNibble) {
        const u8* mort = mortonLut64();
        if (srcW <= 0 || srcH <= 0) return;
        const int tilesX = dstW / 8;
        // 8-bit texel: 64 bytes per 8x8 tile
        for (int ty = 0; ty < srcH; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = 0; tx < srcW; tx += 8) {
                const int tileX = tx >> 3;
                u8* dstTile = dstTiled + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear + (ty + y) * srcW + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 v = srcRow[x];
                        if (maskLowNibble) v &= 0x0F;
                        dstTile[mort[(y<<3) | x]] = v;
                    }
                }
            }
        }
    }
    static inline void swizzleCopyPal8Dirty(const u8* srcLinear, u8* dstTiled, int srcW, int srcH, int dstW, bool maskLowNibble,
                                            int x0, int y0, int x1, int y1) {
        const u8* mort = mortonLut64();
        if (srcW <= 0 || srcH <= 0) return;
        const int tilesX = dstW / 8;
        int tx0 = x0 & ~7;
        int ty0 = y0 & ~7;
        int tx1 = x1 | 7;
        int ty1 = y1 | 7;
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= srcW) tx1 = srcW - 1;
        if (ty1 >= srcH) ty1 = srcH - 1;
        for (int ty = ty0; ty <= ty1; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = tx0; tx <= tx1; tx += 8) {
                const int tileX = tx >> 3;
                u8* dstTile = dstTiled + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear + (ty + y) * srcW + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 v = srcRow[x];
                        if (maskLowNibble) v &= 0x0F;
                        dstTile[mort[(y<<3) | x]] = v;
                    }
                }
            }
        }
    }
    static inline void swizzleCopyRgb565FromIdx(const u8* srcLinear, u16* dstTiled565, const u16* pal565,
                                                int srcW, int srcH, int dstW) {
        const u8* mort = mortonLut64();
        if (srcW <= 0 || srcH <= 0) return;
        const int tilesX = dstW / 8;
        // 16-bit texel: 64 u16 per 8x8 tile
        for (int ty = 0; ty < srcH; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = 0; tx < srcW; tx += 8) {
                const int tileX = tx >> 3;
                u16* dstTile = dstTiled565 + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear + (ty + y) * srcW + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 c = srcRow[x] & 0x0F;
                        dstTile[mort[(y<<3) | x]] = pal565[c];
                    }
                }
            }
        }
    }
    static inline void swizzleCopyRgb565FromIdxDirty(const u8* srcLinear, u16* dstTiled565,
                                                     const u16* pal565, int srcW, int srcH, int dstW,
                                                     int x0, int y0, int x1, int y1) {
        const u8* mort = mortonLut64();
        if (srcW <= 0 || srcH <= 0) return;
        const int tilesX = dstW / 8;
        int tx0 = x0 & ~7;
        int ty0 = y0 & ~7;
        int tx1 = x1 | 7;
        int ty1 = y1 | 7;
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= srcW) tx1 = srcW - 1;
        if (ty1 >= srcH) ty1 = srcH - 1;
        for (int ty = ty0; ty <= ty1; ty += 8) {
            const int tileY = ty >> 3;
            for (int tx = tx0; tx <= tx1; tx += 8) {
                const int tileX = tx >> 3;
                u16* dstTile = dstTiled565 + (tileY * tilesX + tileX) * 64;
                for (int y = 0; y < 8; ++y) {
                    const u8* srcRow = srcLinear + (ty + y) * srcW + tx;
                    for (int x = 0; x < 8; ++x) {
                        u8 c = srcRow[x] & 0x0F;
                        dstTile[mort[(y<<3) | x]] = pal565[c];
                    }
                }
            }
        }
    }

    void buildGameRect(bool stretch, bool hasWallpaper, int screenW, int screenH,
                   int gameW, int gameH,
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
            if (gameW == kPicoWidth && gameH == kPicoHeight) {
                // Force 3x width (128 * 3 = 384) instead of filling the whole top screen width.
                int targetW = gameW * 3; // 384
                if (targetW > screenW) targetW = screenW;

                outW = targetW;
                outX = (screenW - outW) / 2;

                // Keep full height (no top/bottom padding)
                outY = padY;
                outH = screenH - (padY * 2);
                if (outH < 1) outH = 1;

                outScale = (float)outW / (float)gameW;
                return;
            }

            outX = padX;
            outY = padY;
            outW = availW;
            outH = availH;
            outScale = (float)outW / (float)gameW;
            return;
        }

        float scale = std::min((float)availW / (float)gameW,
                            (float)availH / (float)gameH);

        outW = (int)((float)gameW * scale);
        outH = (int)((float)gameH * scale);
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

    bool writeBmp24(const std::string &path, const uint32_t *pixels, int width, int height) {
        if (!pixels || width <= 0 || height <= 0) return false;
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
    struct DirtyRect {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        bool valid = false;
    };
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
    C3D_Tex *scanlineTex = nullptr;
    Tex3DS_SubTexture *scanlineSubtex = nullptr;
    C2D_Image scanlineImage;
    C3D_RenderTarget *topTarget = nullptr;
    C3D_RenderTarget *topTargetR = nullptr; // Right-eye target (3D mode)
    C3D_RenderTarget *bottomTarget = nullptr;

    // --- Game framebuffer upload path ---
    // Legacy (CPU conversion): indices -> RGB565 into linear buffers, then DMA to tiled VRAM texture.
    u16 *pixelBuffer565Top = nullptr;
    u16 *pixelBuffer565Bottom = nullptr;
    size_t pixelBufferSizeTop = 0;
    size_t pixelBufferSizeBottom = 0;

    // GPU palette path: upload 8-bit indices, GPU does palette lookup (PAL8 + TLUT).
    u8 *indexBufferTop = nullptr;
    u8 *indexBufferBottom = nullptr;
    size_t indexBufferSizeTop = 0;
    size_t indexBufferSizeBottom = 0;

    // Runtime switch: true when PAL8+TLUT is available and initialized successfully.
    bool useGpuPalette = false;
    bool presentedThisLoop = false;

#if REAL8_3DS_HAS_PAL8_TLUT
    C3D_Tlut gameTlut;
    alignas(0x10) u16 tlutData[256] = {};
    u16 lastPalette565[16] = {};
    bool tlutReady = false;
#endif
    u16 cachedPalette565[16] = {};
    u32 cachedPalette32[16] = {};
    uint8_t lastPaletteMap[16] = {};
    bool paletteCacheValid = false;

    std::vector<uint32_t> screenBuffer32;
    int screenW = kPicoWidth;
    int screenH = kPicoHeight;
    int topW = kPicoWidth;
    int topH = kPicoHeight;
    int bottomW = kPicoWidth;
    int bottomH = kPicoHeight;
    int topTexW = kPicoWidth;
    int topTexH = kPicoHeight;
    int bottomTexW = kPicoWidth;
    int bottomTexH = kPicoHeight;
    bool screenshotPending = false;
    std::string pendingScreenshotPath;
    u32 *wallpaperBuffer = nullptr;
    size_t wallpaperBufferSize = 0;
    int wallW = 0;
    int wallH = 0;
    int wallTexW = 0;
    int wallTexH = 0;
    u32 *scanlineBuffer = nullptr;
    size_t scanlineBufferSize = 0;
    int scanW = 0;
    int scanH = 0;
    int scanTexW = 0;
    int scanTexH = 0;
    bool topPreviewBlankHint = false;
    bool topPreviewHintValid = false;
    bool stereoBuffersValid = false;
    float lastStereoSlider = -1.0f;
    bool lastStereoActive = false;
    int lastStereoDepth = 0;
    int lastStereoConv = 0;
    bool lastStereoSwap = false;

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
    bool sensorsActive = false;
    u64 lastSensorUs = 0;
    bool fastForwardOverride = false;

    std::string rootPath = "sdmc:/real8";

    // ---- Audio FIFO (mono) to avoid blocking the emulation thread ----
    static const size_t kAudioFifoSamples = (size_t)((kSampleRate * kFifoMaxMs) / 1000);
    int16_t* audioFifo = nullptr;
    size_t audioFifoHead = 0;   // write pos
    size_t audioFifoTail = 0;   // read pos
    size_t audioFifoCount = 0;  // number of valid mono samples in fifo
    int nextWaveToSubmit = 0;
    bool audioStarted = false;
    uint32_t audioUnderruns = 0;
    uint32_t audioOverruns = 0;
    u64 audioStatsLastMs = 0;
    double lastRateCorrection = 0.0;

    // Resampler state (VM -> NDSP)
    uint64_t resamplePosFp = 0;
    int16_t resamplePrev = 0;
    bool resampleHasPrev = false;
    std::vector<int16_t> resampleScratch;

    static inline int16_t quantizeToU8S16(int16_t s) {
        uint16_t u = (uint16_t)((int)s + 32768);
        uint8_t q = (uint8_t)(u >> 8);
        return (int16_t)(((int)q - 128) << 8);
    }

    inline void audioFifoReset() {
        audioFifoHead = audioFifoTail = audioFifoCount = 0;
        audioStarted = false;
    }

    inline size_t audioFifoFree() const {
        return kAudioFifoSamples - audioFifoCount;
    }

    inline void audioFifoWriteMono(const int16_t* src, int count) {
        if (!audioFifo || count <= 0) return;
        const size_t maxSamples = (size_t)((kSampleRate * kFifoMaxMs) / 1000);

        // If incoming chunk is huge, keep the newest tail and drop the rest.
        if ((size_t)count > maxSamples) {
            src += (count - (int)maxSamples);
            count = (int)maxSamples;
            audioFifoHead = audioFifoTail;
            audioFifoCount = 0;
            audioOverruns++;
        }

        // If we'd exceed the max, drop oldest samples to make room.
        size_t needed = audioFifoCount + (size_t)count;
        if (needed > maxSamples) {
            size_t drop = needed - maxSamples;
            if (drop > audioFifoCount) drop = audioFifoCount;
            audioFifoTail = (audioFifoTail + drop) % kAudioFifoSamples;
            audioFifoCount -= drop;
            audioOverruns++;
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

        if (!audioStarted) {
            const size_t minStart = (size_t)((kSampleRate * kFifoMinStartMs) / 1000);
            if (audioFifoCount < minStart) return;
            audioStarted = true;
        }

        // Submit any finished buffers (refill + requeue).
        for (int i = 0; i < kNumAudioBuffers; ++i) {
            int idx = (nextWaveToSubmit + i) % kNumAudioBuffers;
            if (waveBufIsBusy(idx)) continue;

            int16_t* dst = (int16_t*)waveBuf[idx].data_pcm16; // mono
            size_t available = audioFifoCount;
            int toCopy = (available >= (size_t)kSamplesPerBuffer) ? kSamplesPerBuffer : (int)available;

            for (int s = 0; s < toCopy; ++s) {
                dst[s] = audioFifo[audioFifoTail];
                audioFifoTail = (audioFifoTail + 1) % kAudioFifoSamples;
            }
            if (toCopy < kSamplesPerBuffer) {
                memset(dst + toCopy, 0, (size_t)(kSamplesPerBuffer - toCopy) * sizeof(int16_t));
                audioUnderruns++;
            }
            audioFifoCount -= (size_t)toCopy;

            submitAudioBuffer(idx);
            nextWaveToSubmit = (idx + 1) % kNumAudioBuffers;
        }

        if (debugVMRef) {
            const u64 now = osGetTime();
            if (now - audioStatsLastMs >= 5000) {
                audioStatsLastMs = now;
                const size_t fifoMs = (size_t)((audioFifoCount * 1000) / kSampleRate);
                int queued = 0;
                for (int i = 0; i < kNumAudioBuffers; ++i) {
                    if (waveBufIsBusy(i)) queued++;
                }
                unsigned long genMax = debugVMRef->audio.gen_ms_max;
                debugVMRef->audio.gen_ms_max = 0;
                log("[3DS][AUDIO] fifo=%lums queued=%d underruns=%lu overruns=%lu gen_max=%lums corr=%.3f%%",
                    (unsigned long)fifoMs, queued,
                    (unsigned long)audioUnderruns, (unsigned long)audioOverruns,
                    (unsigned long)genMax, lastRateCorrection * 100.0);
            }
        }
    }

    void freeGameTextures() {
        if (pixelBuffer565Top) {
            linearFree(pixelBuffer565Top);
            pixelBuffer565Top = nullptr;
        }
        if (pixelBuffer565Bottom) {
            linearFree(pixelBuffer565Bottom);
            pixelBuffer565Bottom = nullptr;
        }
        pixelBufferSizeTop = 0;
        pixelBufferSizeBottom = 0;

        if (indexBufferTop) {
            linearFree(indexBufferTop);
            indexBufferTop = nullptr;
        }
        if (indexBufferBottom) {
            linearFree(indexBufferBottom);
            indexBufferBottom = nullptr;
        }
        indexBufferSizeTop = 0;
        indexBufferSizeBottom = 0;

        if (gameTexTopR) {
            C3D_TexDelete(gameTexTopR);
            linearFree(gameTexTopR);
            gameTexTopR = nullptr;
        }
        if (gameTexTop) {
            C3D_TexDelete(gameTexTop);
            linearFree(gameTexTop);
            gameTexTop = nullptr;
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
        if (gameSubtexTop) {
            linearFree(gameSubtexTop);
            gameSubtexTop = nullptr;
        }
        if (gameSubtexTopR) {
            linearFree(gameSubtexTopR);
            gameSubtexTopR = nullptr;
        }

        gameImage = {};
        gameImageBottom = {};
        gameImageTop = {};
        gameImageTopR = {};

        stereoBuffersValid = false;
        lastStereoDepth = 0;
        lastStereoConv = 0;
        lastStereoSwap = false;
    }

    bool initGameTextures(int newTopW, int newTopH, int newBottomW, int newBottomH) {
        if (newTopW <= 0 || newTopH <= 0 || newBottomW <= 0 || newBottomH <= 0) return false;
        freeGameTextures();

        topW = newTopW;
        topH = newTopH;
        bottomW = newBottomW;
        bottomH = newBottomH;
        topTexW = nextPow2(topW);
        topTexH = nextPow2(topH);
        bottomTexW = nextPow2(bottomW);
        bottomTexH = nextPow2(bottomH);
        bottomStaticValid = false;

#if REAL8_3DS_HAS_PAL8_TLUT
        const GPU_TEXCOLOR texFmt = useGpuPalette ? GPU_PAL8 : GPU_RGB565;
#else
        const GPU_TEXCOLOR texFmt = GPU_RGB565;
#endif
        auto initTex = [&](C3D_Tex* tex, int w, int h) -> bool {
#if REAL8_3DS_DIRECT_TEX_UPDATE
            return C3D_TexInit(tex, w, h, texFmt);
#else
            return C3D_TexInitVRAM(tex, w, h, texFmt);
#endif
        };

        gameTex = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
        gameTexTop = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
        gameTexTopR = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
        if (!gameTex || !gameTexTop || !gameTexTopR) return false;

        if (!initTex(gameTex, bottomTexW, bottomTexH) ||
            !initTex(gameTexTop, topTexW, topTexH) ||
            !initTex(gameTexTopR, topTexW, topTexH)) {
            return false;
        }

        C3D_TexSetFilter(gameTex, GPU_NEAREST, GPU_NEAREST);
        GPU_TEXTURE_FILTER_PARAM topFilter = interpolation ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(gameTexTop, topFilter, topFilter);
        C3D_TexSetFilter(gameTexTopR, topFilter, topFilter);

        gameSubtex = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        gameSubtexBottom = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        gameSubtexTop = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        gameSubtexTopR = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        if (!gameSubtex || !gameSubtexBottom || !gameSubtexTop || !gameSubtexTopR) return false;

        auto fillSubtex = [](Tex3DS_SubTexture* sub, int w, int h, int texW, int texH) {
            sub->width = w;
            sub->height = h;
            sub->left = 0.0f;
            sub->top = 1.0f;
            sub->right = (float)w / (float)texW;
            sub->bottom = 1.0f - ((float)h / (float)texH);
        };

        fillSubtex(gameSubtex, bottomW, bottomH, bottomTexW, bottomTexH);
        fillSubtex(gameSubtexBottom, bottomW, bottomH, bottomTexW, bottomTexH);

        fillSubtex(gameSubtexTop, topW, topH, topTexW, topTexH);
        *gameSubtexTopR = *gameSubtexTop;

        gameImage.tex = gameTex;
        gameImage.subtex = gameSubtex;
        gameImageBottom.tex = gameTex;
        gameImageBottom.subtex = gameSubtexBottom;
        gameImageTop.tex = gameTexTop;
        gameImageTop.subtex = gameSubtexTop;
        gameImageTopR.tex = gameTexTopR;
        gameImageTopR.subtex = gameSubtexTopR;

        pixelBufferSizeTop = (size_t)(topTexW * topTexH * sizeof(u16));
        pixelBufferSizeBottom = (size_t)(bottomTexW * bottomTexH * sizeof(u16));
        pixelBuffer565Top = (u16*)linearAlloc(pixelBufferSizeTop);
        pixelBuffer565Bottom = (u16*)linearAlloc(pixelBufferSizeBottom);

#if REAL8_3DS_HAS_PAL8_TLUT
        indexBufferSizeTop = (size_t)(topTexW * topTexH);
        indexBufferSizeBottom = (size_t)(bottomTexW * bottomTexH);
        indexBufferTop = (u8*)linearAlloc(indexBufferSizeTop);
        indexBufferBottom = (u8*)linearAlloc(indexBufferSizeBottom);
#endif

        if (!pixelBuffer565Top || !pixelBuffer565Bottom) return false;
#if REAL8_3DS_HAS_PAL8_TLUT
        if (useGpuPalette && (!indexBufferTop || !indexBufferBottom)) return false;
#endif

        stereoBuffersValid = false;
        return true;
    }

    void ensureGameTextures(int newTopW, int newTopH, int newBottomW, int newBottomH) {
        if (newTopW == topW && newTopH == topH && newBottomW == bottomW && newBottomH == bottomH &&
            gameTex && gameTexTop && gameTexTopR) {
            return;
        }
        initGameTextures(newTopW, newTopH, newBottomW, newBottomH);
    }

    void updateMotionSensors() {
        if (!debugVMRef || !debugVMRef->ram) return;

        const bool enabled = (debugVMRef->ram[0x5FE0] & 0x01) != 0;
        if (!enabled) {
            if (sensorsActive) {
                HIDUSER_DisableAccelerometer();
                HIDUSER_DisableGyroscope();
                sensorsActive = false;
            }
            debugVMRef->motion.flags = 0x03; // accel + gyro present, data invalid
            debugVMRef->motion.dt_us = 0;
            debugVMRef->motion.accel_x = 0;
            debugVMRef->motion.accel_y = 0;
            debugVMRef->motion.accel_z = 0;
            debugVMRef->motion.gyro_x = 0;
            debugVMRef->motion.gyro_y = 0;
            debugVMRef->motion.gyro_z = 0;
            return;
        }

        if (!sensorsActive) {
            HIDUSER_EnableAccelerometer();
            HIDUSER_EnableGyroscope();
            sensorsActive = true;
            lastSensorUs = 0;
        }

        accelVector accel{};
        angularRate gyro{};
        hidAccelRead(&accel);
        hidGyroRead(&gyro);

        constexpr int kAccelUnitsPerG = 256;
        constexpr int kGyroUnitsPerDps = 16;

        debugVMRef->motion.accel_x = (int32_t)accel.x * 65536 / kAccelUnitsPerG;
        debugVMRef->motion.accel_y = (int32_t)accel.y * 65536 / kAccelUnitsPerG;
        debugVMRef->motion.accel_z = (int32_t)accel.z * 65536 / kAccelUnitsPerG;
        debugVMRef->motion.gyro_x = (int32_t)gyro.x * 65536 / kGyroUnitsPerDps;
        debugVMRef->motion.gyro_y = (int32_t)gyro.y * 65536 / kGyroUnitsPerDps;
        debugVMRef->motion.gyro_z = (int32_t)gyro.z * 65536 / kGyroUnitsPerDps;
        debugVMRef->motion.flags = 0x07; // accel + gyro present, data valid

        const u64 nowUs = osGetTime() * 1000ULL;
        if (lastSensorUs == 0) {
            debugVMRef->motion.dt_us = 0;
        } else {
            const u64 delta = nowUs - lastSensorUs;
            debugVMRef->motion.dt_us = (delta > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)delta;
        }
        lastSensorUs = nowUs;
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
#if REAL8_3DS_HAS_PAL8_TLUT
        memset(tlutData, 0, sizeof(tlutData));
        memset(lastPalette565, 0xFF, sizeof(lastPalette565)); // force first upload
        tlutReady = C3D_TlutInit(&gameTlut, 256, GPU_RGB565);
        if (tlutReady) {
            C3D_TlutLoad(&gameTlut, tlutData);
            useGpuPalette = true;
        } else {
            useGpuPalette = false;
        }
#else
        useGpuPalette = false;
#endif

        initGameTextures(kPicoWidth, kPicoHeight, kPicoWidth, kPicoHeight);

    }

    void logGfxConfig() {
#if REAL8_3DS_HAS_PAL8_TLUT
        const char* pal8 = "enabled";
        const int tlutReadyVal = tlutReady ? 1 : 0;
#else
        const char* pal8 = "disabled";
        const int tlutReadyVal = 0;
#endif
#if REAL8_3DS_DIRECT_TEX_UPDATE
        const char* direct = "direct";
#else
        const char* direct = "vram";
#endif
        log("[3DS][GFX] PAL8+TLUT %s, update %s, tlutReady=%d, useGpuPalette=%d",
            pal8, direct, tlutReadyVal, useGpuPalette ? 1 : 0);

#if !REAL8_3DS_HAS_PAL8_TLUT
        log("[3DS][GFX] PAL8+TLUT not available; RGB565 fallback in use.");
#else
        if (!useGpuPalette) {
            log("[3DS][GFX] TLUT init failed; RGB565 fallback in use.");
        }
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

        ndspSetOutputMode(NDSP_OUTPUT_MONO);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        ndspChnSetRate(0, kSampleRate);
        ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);

        float mix[12];
        memset(mix, 0, sizeof(mix));
        mix[0] = 1.0f;
        ndspChnSetMix(0, mix);

        size_t totalFrames = (size_t)kSamplesPerBuffer * (size_t)kNumAudioBuffers;
        size_t totalI16 = totalFrames; // mono
        audioBuffer = (int16_t*)linearAlloc(totalI16 * sizeof(int16_t));
        if (!audioBuffer) {
            log("[3DS][AUDIO] linearAlloc failed for audio buffer");
            ndspExit();
            return;
        }
        memset(audioBuffer, 0, totalI16 * sizeof(int16_t));

        memset(waveBuf, 0, sizeof(waveBuf));
        for (int i = 0; i < kNumAudioBuffers; ++i) {
            waveBuf[i].data_vaddr = audioBuffer + (i * kSamplesPerBuffer);
            waveBuf[i].data_pcm16 = (int16_t*)waveBuf[i].data_vaddr;
            waveBuf[i].nsamples   = kSamplesPerBuffer;
            waveBuf[i].status     = NDSP_WBUF_DONE;
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
        ensureDir(rootPath + "/carts");
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
        freeGameTextures();

#if REAL8_3DS_HAS_PAL8_TLUT
        indexBufferSizeTop = 0;
        indexBufferSizeBottom = 0;

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
        if (scanlineBuffer) {
            linearFree(scanlineBuffer);
            scanlineBuffer = nullptr;
        }
        if (scanlineTex) {
            C3D_TexDelete(scanlineTex);
            linearFree(scanlineTex);
            scanlineTex = nullptr;
        }
        if (scanlineSubtex) {
            linearFree(scanlineSubtex);
            scanlineSubtex = nullptr;
        }
        scanlineBufferSize = 0;
        scanW = 0;
        scanH = 0;
        scanTexW = 0;
        scanTexH = 0;
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

        auto isCartFile = [](const std::string& name) {
            if (name.length() >= 3 && name.compare(name.length() - 3, 3, ".p8") == 0) return true;
            if (name.length() >= 4 && name.compare(name.length() - 4, 4, ".png") == 0) return true;
            return false;
        };

        std::string targetDir = rootPath;
        if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".sav") {
            targetDir = rootPath + "/saves";
        } else if (fname == "config.dat" || fname == "wallpaper.png" || fname == "favorites.txt" ||
                   fname == "gameslist.json" || fname == "gamesrepo.txt") {
            targetDir = rootPath + "/config";
        } else if (isCartFile(fname)) {
            targetDir = rootPath + "/carts";
        }

        ensureDir(targetDir);
        return targetDir + "/" + fname;
    }

    void submitAudioBuffer(int bufIndex) {
        int16_t *buf = (int16_t*)waveBuf[bufIndex].data_pcm16;
        DSP_FlushDataCache(buf, kSamplesPerBuffer * sizeof(int16_t));
        ndspChnWaveBufAdd(0, &waveBuf[bufIndex]);
    }

    void drawScanlines(int x, int y, int w, int h, float z) {
        if (w <= 0 || h <= 0) return;
        if (!ensureScanlineTexture(w, h)) {
            const u32 color = C2D_Color32(0, 0, 0, 80);
            for (int yy = 0; yy < h; yy += 2) {
                C2D_DrawRectSolid((float)x, (float)(y + yy), z, (float)w, 1.0f, color);
            }
            return;
        }

        C2D_DrawImageAt(scanlineImage, (float)x, (float)y, z, nullptr, 1.0f, 1.0f);
    }

    bool isLinearVmFramebuffer(const uint8_t* buffer) const {
        return debugVMRef && debugVMRef->fb_is_linear && buffer == debugVMRef->fb;
    }

    bool getDirtyRectForBuffer(const uint8_t* buffer, int fb_w, int fb_h, DirtyRect& out) const {
        if (!debugVMRef || buffer != debugVMRef->fb) return false;
        int x0 = debugVMRef->dirty_x0;
        int y0 = debugVMRef->dirty_y0;
        int x1 = debugVMRef->dirty_x1;
        int y1 = debugVMRef->dirty_y1;
        if (x1 < 0 || y1 < 0) return false;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= fb_w) x1 = fb_w - 1;
        if (y1 >= fb_h) y1 = fb_h - 1;
        if (x0 > x1 || y0 > y1) return false;
        out.x0 = x0;
        out.y0 = y0;
        out.x1 = x1;
        out.y1 = y1;
        out.valid = true;
        return true;
    }

    void alignDirtyRectToTiles(DirtyRect& r, int fb_w, int fb_h) const {
        if (!r.valid) return;
        r.x0 &= ~7;
        r.y0 &= ~7;
        r.x1 |= 7;
        r.y1 |= 7;
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 >= fb_w) r.x1 = fb_w - 1;
        if (r.y1 >= fb_h) r.y1 = fb_h - 1;
    }

    bool ensureScanlineTexture(int w, int h) {
        if (w <= 0 || h <= 0) return false;
        const int texW = nextPow2(w);
        const int texH = nextPow2(h);
        if (scanlineTex && scanlineSubtex && scanlineBuffer &&
            w == scanW && h == scanH && texW == scanTexW && texH == scanTexH) {
            return true;
        }

        if (scanlineBuffer) {
            linearFree(scanlineBuffer);
            scanlineBuffer = nullptr;
        }
        if (scanlineTex) {
            C3D_TexDelete(scanlineTex);
            linearFree(scanlineTex);
            scanlineTex = nullptr;
        }
        if (scanlineSubtex) {
            linearFree(scanlineSubtex);
            scanlineSubtex = nullptr;
        }

        scanlineTex = (C3D_Tex*)linearAlloc(sizeof(C3D_Tex));
        if (!scanlineTex) return false;
        C3D_TexInit(scanlineTex, texW, texH, GPU_RGBA8);
        C3D_TexSetFilter(scanlineTex, GPU_NEAREST, GPU_NEAREST);

        scanlineSubtex = (Tex3DS_SubTexture*)linearAlloc(sizeof(Tex3DS_SubTexture));
        if (!scanlineSubtex) {
            C3D_TexDelete(scanlineTex);
            linearFree(scanlineTex);
            scanlineTex = nullptr;
            return false;
        }

        scanlineSubtex->width = w;
        scanlineSubtex->height = h;
        scanlineSubtex->left = 0.0f;
        scanlineSubtex->top = 1.0f;
        scanlineSubtex->right = (float)w / (float)texW;
        scanlineSubtex->bottom = 1.0f - ((float)h / (float)texH);

        scanlineImage.tex = scanlineTex;
        scanlineImage.subtex = scanlineSubtex;

        scanlineBufferSize = (size_t)(texW * texH * sizeof(u32));
        scanlineBuffer = (u32*)linearAlloc(scanlineBufferSize);
        if (!scanlineBuffer) {
            C3D_TexDelete(scanlineTex);
            linearFree(scanlineTex);
            scanlineTex = nullptr;
            linearFree(scanlineSubtex);
            scanlineSubtex = nullptr;
            scanlineBufferSize = 0;
            return false;
        }

        scanW = w;
        scanH = h;
        scanTexW = texW;
        scanTexH = texH;

        const u32 lineColor = packAbgr8888(0, 0, 0, 80);
        for (int yy = 0; yy < texH; ++yy) {
            const bool inRow = (yy < h);
            const bool drawLine = inRow && ((yy & 1) == 0);
            u32* row = scanlineBuffer + (yy * texW);
            for (int xx = 0; xx < texW; ++xx) {
                if (!inRow || xx >= w) {
                    row[xx] = 0;
                } else {
                    row[xx] = drawLine ? lineColor : 0;
                }
            }
        }

        GSPGPU_FlushDataCache(scanlineBuffer, scanlineBufferSize);
        C3D_SyncDisplayTransfer(
            (u32*)scanlineBuffer, GX_BUFFER_DIM(scanTexW, scanTexH),
            (u32*)scanlineTex->data, GX_BUFFER_DIM(scanTexW, scanTexH),
            (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
             GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
             GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
        );

        return true;
    }

    void updatePaletteLutIfNeeded(const uint8_t* palette_map) {
        uint8_t fallback[16];
        if (!palette_map) {
            for (int i = 0; i < 16; ++i) fallback[i] = (uint8_t)i;
            palette_map = fallback;
        }

        if (paletteCacheValid && memcmp(palette_map, lastPaletteMap, 16) == 0) return;

        memcpy(lastPaletteMap, palette_map, 16);
        paletteCacheValid = true;

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

            cachedPalette565[i] = packBgr565(r, g, b);
            cachedPalette32[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
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
        void blitFrameToTexture(const uint8_t* framebuffer, int fb_w, int fb_h, C3D_Tex *destTex,
                                const uint16_t *paletteLUT565, const uint32_t *paletteLUT32,
                                bool updateScreenshot, u8 *destIndexBuffer,
                                const DirtyRect* dirty = nullptr) {
            // 1) Optional screenshot capture (rare): keep CPU conversion only when needed.
            if (updateScreenshot) {
                const size_t pixelCount = (size_t)fb_w * (size_t)fb_h;
                if (screenBuffer32.size() != pixelCount) {
                    screenBuffer32.assign(pixelCount, 0);
                }
                screenW = fb_w;
                screenH = fb_h;
                size_t idx = 0;
                for (int y = 0; y < fb_h; ++y) {
                    const uint8_t* srcRow = framebuffer + (size_t)y * (size_t)fb_w;
                    for (int x = 0; x < fb_w; ++x) {
                        uint8_t col = srcRow[x] & 0x0F;
                        screenBuffer32[idx++] = paletteLUT32[col];
                    }
                }
            }
            // 2) Update GPU palette if it changed this frame.
            updateGpuPaletteIfNeeded(paletteLUT565);

#if REAL8_3DS_DIRECT_TEX_UPDATE
            // 3) Write indices directly into the CPU-accessible texture in swizzled (tiled) order.
            if (dirty && dirty->valid) {
                swizzleCopyPal8Dirty((const u8*)framebuffer, (u8*)destTex->data, fb_w, fb_h, destTex->width,
                                     /*maskLowNibble=*/true, dirty->x0, dirty->y0, dirty->x1, dirty->y1);
            } else {
                swizzleCopyPal8((const u8*)framebuffer, (u8*)destTex->data, fb_w, fb_h, destTex->width, /*maskLowNibble=*/true);
            }
            GSPGPU_FlushDataCache(destTex->data, (size_t)destTex->width * (size_t)destTex->height);
            (void)destIndexBuffer;
#else
            // 3) Copy indices linearly then use GX display transfer to swizzle into VRAM.
            const int destW = (int)destTex->width;
            const int destH = (int)destTex->height;
            u8* srcLinear = destIndexBuffer;
            if (destIndexBuffer && destW == fb_w && destIndexBuffer == (u8*)framebuffer) {
                srcLinear = (u8*)framebuffer;
            } else if (destIndexBuffer) {
                if (dirty && dirty->valid) {
                    const int w = dirty->x1 - dirty->x0 + 1;
                    for (int y = dirty->y0; y <= dirty->y1; ++y) {
                        memcpy(destIndexBuffer + y * destW + dirty->x0,
                               framebuffer + (size_t)y * (size_t)fb_w + dirty->x0, (size_t)w);
                    }
                } else {
                    for (int y = 0; y < fb_h; ++y) {
                        memcpy(destIndexBuffer + y * destW,
                               framebuffer + (size_t)y * (size_t)fb_w, (size_t)fb_w);
                    }
                }
                srcLinear = destIndexBuffer;
            } else {
                return;
            }
            GSPGPU_FlushDataCache(srcLinear, (size_t)destW * (size_t)destH);
            C3D_SyncDisplayTransfer(
                (u32*)srcLinear, GX_BUFFER_DIM(destW, destH),
                (u32*)destTex->data, GX_BUFFER_DIM(destW, destH),
                (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(1) |
                 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_I8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_I8) |
                 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
            );
#endif
        }

    #endif // REAL8_3DS_HAS_PAL8_TLUT
    
        // Legacy path: CPU converts indices -> RGB565 and uploads a 16-bit texture.
        void blitFrameToTexture(const uint8_t* framebuffer, int fb_w, int fb_h, C3D_Tex *destTex,
                                const uint16_t *paletteLUT565, const uint32_t *paletteLUT32,
                                bool updateScreenshot, u16 *destBuffer565,
                                const DirtyRect* dirty = nullptr) {
#if REAL8_3DS_DIRECT_TEX_UPDATE
            // Write RGB565 directly into the CPU-accessible texture in swizzled (tiled) order.
            // Screenshot conversion (rare) stays linear for simplicity.
            if (updateScreenshot) {
                const size_t pixelCount = (size_t)fb_w * (size_t)fb_h;
                if (screenBuffer32.size() != pixelCount) {
                    screenBuffer32.assign(pixelCount, 0);
                }
                screenW = fb_w;
                screenH = fb_h;
                size_t idx = 0;
                for (int y = 0; y < fb_h; ++y) {
                    const uint8_t* srcRow = framebuffer + (size_t)y * (size_t)fb_w;
                    for (int x = 0; x < fb_w; ++x) {
                        uint8_t col = srcRow[x] & 0x0F;
                        screenBuffer32[idx++] = paletteLUT32[col];
                    }
                }
            }
            // Swizzled write for the texture.
            if (dirty && dirty->valid) {
                swizzleCopyRgb565FromIdxDirty((const u8*)framebuffer, (u16*)destTex->data,
                                              paletteLUT565, fb_w, fb_h, destTex->width,
                                              dirty->x0, dirty->y0, dirty->x1, dirty->y1);
            } else {
                swizzleCopyRgb565FromIdx((const u8*)framebuffer, (u16*)destTex->data, paletteLUT565, fb_w, fb_h, destTex->width);
            }
            GSPGPU_FlushDataCache(destTex->data, (size_t)destTex->width * (size_t)destTex->height * sizeof(u16));
#else
            const int destW = (int)destTex->width;
            const int destH = (int)destTex->height;
            if (dirty && dirty->valid && !updateScreenshot) {
                for (int y = dirty->y0; y <= dirty->y1; ++y) {
                    const int row = y * destW;
                    for (int x = dirty->x0; x <= dirty->x1; ++x) {
                        uint8_t col = framebuffer[(size_t)y * (size_t)fb_w + x] & 0x0F;
                        destBuffer565[row + x] = paletteLUT565[col];
                    }
                }
            } else {
                const size_t pixelCount = (size_t)fb_w * (size_t)fb_h;
                if (updateScreenshot) {
                    if (screenBuffer32.size() != pixelCount) {
                        screenBuffer32.assign(pixelCount, 0);
                    }
                    screenW = fb_w;
                    screenH = fb_h;
                }
                size_t idx = 0;
                for (int y = 0; y < fb_h; ++y) {
                    const uint8_t* srcRow = framebuffer + (size_t)y * (size_t)fb_w;
                    const int row = y * destW;
                    for (int x = 0; x < fb_w; ++x) {
                        uint8_t col = srcRow[x] & 0x0F;
                        uint16_t rgb565 = paletteLUT565[col];
                        destBuffer565[row + x] = rgb565;
                        if (updateScreenshot) {
                            screenBuffer32[idx] = paletteLUT32[col];
                        }
                        idx++;
                    }
                }
            }

            GSPGPU_FlushDataCache(destBuffer565, (size_t)destW * (size_t)destH * sizeof(u16));
            C3D_SyncDisplayTransfer(
                (u32*)destBuffer565, GX_BUFFER_DIM(destW, destH),
                (u32*)destTex->data, GX_BUFFER_DIM(destW, destH),
                (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
            );
#endif
        }

    // When browsing directories, the shell may give a dedicated top buffer.
    // If no preview exists, that top buffer is usually cleared to 0 (all pixels 0).
    // In that case we should not draw the top preview texture, so wallpaper/skin stays visible.
    bool isBlankTopPreview(const uint8_t* topbuffer, const uint8_t* bottombuffer) const {
        // Normal gameplay path uses the same framebuffer for both (flipScreen() calls flipScreens(fb,fb,...))
        // Never treat that as "missing preview".
        if (topbuffer == bottombuffer) return false;
        if (!topPreviewHintValid) return false;
        return topPreviewBlankHint;
    }

    public:
    Real8VM *debugVMRef = nullptr;
    bool crt_filter = false;
    bool interpolation = false;
    bool bottomWallpaperVisible = (REAL8_BOTTOM_NOBACK == 0);

    bool bottomStaticValid = false;
    bool lastInGameSingleScreen = false;
    bool lastBottomHasWallpaper = false;

    ThreeDSHost() {
        initGfx();

        // IMPORTANT (audio robustness):
        // ndspInit() may need RomFS mounted when DSP firmware is bundled with the app (romfs:/dspfirm.cdc).
        // Initializing FS (and RomFS) first lets audio work out-of-the-box on installs without sdmc:/3ds/dspfirm.cdc.
        initFs();
        logGfxConfig();
        initAudio();

        initNetwork();
    }

    ~ThreeDSHost() {
        shutdownAudio();
        shutdownNetwork();
        if (sensorsActive) {
            HIDUSER_DisableAccelerometer();
            HIDUSER_DisableGyroscope();
            sensorsActive = false;
        }
        shutdownGfx();
    }

    void beginLoop() { presentedThisLoop = false; }
    bool didPresent() const { return presentedThisLoop; }

    const char *getPlatform() const override { return "3DS"; }
    std::string getClipboardText() override { return ""; }

    void setTopPreviewBlankHint(bool blank) override {
        topPreviewBlankHint = blank;
        topPreviewHintValid = true;
    }

    void clearTopPreviewBlankHint() override {
        topPreviewHintValid = false;
    }

    void* allocLinearFramebuffer(size_t bytes, size_t align) override {
        (void)align;
        void* ptr = linearAlloc(bytes);
        if (ptr) std::memset(ptr, 0, bytes);
        return ptr;
    }

    void freeLinearFramebuffer(void* ptr) override {
        if (ptr) linearFree(ptr);
    }

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

    void onFramebufferResize(int fb_w, int fb_h) override {
        if (fb_w <= 0 || fb_h <= 0) return;
        ensureGameTextures(fb_w, fb_h, fb_w, fb_h);
        bottomStaticValid = false;
        stereoBuffersValid = false;
    }

    void waitForDebugEvent() override {
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

    void flipScreen(const uint8_t* framebuffer, int fb_w, int fb_h, uint8_t *palette_map) override {
        flipScreens(framebuffer, fb_w, fb_h, framebuffer, fb_w, fb_h, palette_map);
    }

    void flipScreens(const uint8_t* topbuffer, int top_w, int top_h,
                     const uint8_t* bottombuffer, int bottom_w, int bottom_h,
                     uint8_t *palette_map) override {
        updatePaletteLutIfNeeded(palette_map);
        updateMotionSensors();
        if (!topbuffer || !bottombuffer) return;
        if (top_w <= 0 || top_h <= 0 || bottom_w <= 0 || bottom_h <= 0) return;
        ensureGameTextures(top_w, top_h, bottom_w, bottom_h);
        if (!gameTex || !gameTexTop || !gameTexTopR || !gameSubtex || !gameSubtexBottom || !gameSubtexTop || !gameSubtexTopR) return;
        const bool wantScreenshot = screenshotPending;
        bool capturedThisFrame = false;

        const bool inGameSingleScreen = (topbuffer == bottombuffer);
        const int mode = (debugVMRef ? debugVMRef->r8_vmode_cur : 0);
        const int bottomMode = (debugVMRef ? debugVMRef->bottom_vmode_cur : mode);

        if (inGameSingleScreen != lastInGameSingleScreen) {
            bottomStaticValid = false;
            lastInGameSingleScreen = inGameSingleScreen;
        }

        bool topPreviewBlank = false;
        if (!inGameSingleScreen && topbuffer) {
            topPreviewBlank = isBlankTopPreview(topbuffer, bottombuffer);
        }

        auto updateBottomTexture = [&](bool allowScreenshot) {
#if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                u8* bottomIndexSrc = indexBufferBottom;
                if (isLinearVmFramebuffer(bottombuffer) && gameTex &&
                    gameTex->width == bottom_w && gameTex->height == bottom_h) {
                    bottomIndexSrc = (u8*)bottombuffer;
                }
                DirtyRect dirtyBottom;
                if (getDirtyRectForBuffer(bottombuffer, bottom_w, bottom_h, dirtyBottom)) {
                    alignDirtyRectToTiles(dirtyBottom, bottom_w, bottom_h);
                }
                blitFrameToTexture(bottombuffer, bottom_w, bottom_h, gameTex, cachedPalette565, cachedPalette32,
                                   allowScreenshot && wantScreenshot, bottomIndexSrc,
                                   dirtyBottom.valid ? &dirtyBottom : nullptr);
            } else
#endif
            {
                DirtyRect dirtyBottom;
                if (getDirtyRectForBuffer(bottombuffer, bottom_w, bottom_h, dirtyBottom)) {
                    alignDirtyRectToTiles(dirtyBottom, bottom_w, bottom_h);
                }
                blitFrameToTexture(bottombuffer, bottom_w, bottom_h, gameTex, cachedPalette565, cachedPalette32,
                                   allowScreenshot && wantScreenshot, pixelBuffer565Bottom,
                                   dirtyBottom.valid ? &dirtyBottom : nullptr);
            }
            if (allowScreenshot && wantScreenshot) capturedThisFrame = true;
        };


        uint8_t st_flags = 0;
        uint8_t st_mode = 3;
        int8_t st_depth = 0;
        int8_t st_conv = 0;
        if (debugVMRef && debugVMRef->ram) {
            st_flags = debugVMRef->ram[0x5F80];
            st_mode = debugVMRef->ram[0x5F81];
            st_depth = (int8_t)debugVMRef->ram[0x5F82];
            st_conv = (int8_t)debugVMRef->ram[0x5F83];
        }
        const bool stereoEnable = (st_flags & 0x01) != 0;
        const bool swapEyes = (st_flags & 0x02) != 0;

        constexpr int kConvPxPerLevel = 1;

        int depthLevel = st_depth;
        if (st_mode == 3 && depthLevel == 0) depthLevel = 1;
        const int convPx = st_conv * kConvPxPerLevel;

        const bool stereoCapable =
            (debugVMRef && !debugVMRef->isShellUI &&
             debugVMRef->stereo_layers != nullptr &&
             topTargetR != nullptr && gameTexTopR != nullptr);

        // Stereoscopic 3D (top screen): if enabled in the VM and the 3D slider is up,
        // render separate left/right eye images from vm->stereo_layers using bucket-based depth.
        const float stereoSlider = osGet3DSliderState();
        bool stereoActive = false;
        if (st_mode == 3) {
            stereoActive = stereoCapable && debugVMRef->stereoscopic && stereoSlider > 0.01f;
        } else if (st_mode == 1 && stereoEnable) {
            stereoActive = stereoCapable && stereoSlider > 0.01f;
        }

        // When 3D is disabled, the system duplicates the left framebuffer to the right.
        gfxSet3D(stereoActive);

        if (stereoActive) {
            static std::vector<uint8_t> eyeL;
            static std::vector<uint8_t> eyeR;
            static std::vector<uint8_t> zL;
            static std::vector<uint8_t> zR;

            const size_t pixelCount = (size_t)top_w * (size_t)top_h;
            if (eyeL.size() != pixelCount) {
                eyeL.assign(pixelCount, 0);
                eyeR.assign(pixelCount, 0);
                zL.assign(pixelCount, 0);
                zR.assign(pixelCount, 0);
                stereoBuffersValid = false;
            }

            constexpr float kPxPerBucket = 1.0f; // base pixels per bucket
            const float bucketScale = (float)depthLevel * stereoSlider * kPxPerBucket;
            const int maxShiftClamp = (int)ceilf(fabsf((float)Real8VM::STEREO_BUCKET_MAX * bucketScale) + (float)std::abs(convPx));
            bool fullClear = !stereoBuffersValid || !lastStereoActive ||
                (lastStereoSlider < 0.0f || fabsf(stereoSlider - lastStereoSlider) > 0.001f) ||
                (depthLevel != lastStereoDepth) || (convPx != lastStereoConv) || (swapEyes != lastStereoSwap);

            int srcX0 = 0;
            int srcY0 = 0;
            int srcX1 = top_w - 1;
            int srcY1 = top_h - 1;

            int clearX0 = 0;
            int clearY0 = 0;
            int clearX1 = top_w - 1;
            int clearY1 = top_h - 1;

            if (!fullClear && debugVMRef) {
                int dx0 = debugVMRef->dirty_x0;
                int dy0 = debugVMRef->dirty_y0;
                int dx1 = debugVMRef->dirty_x1;
                int dy1 = debugVMRef->dirty_y1;

                if (dx1 < 0 || dy1 < 0) {
                    fullClear = true;
                } else {
                    if (dx0 < 0) dx0 = 0;
                    if (dy0 < 0) dy0 = 0;
                    if (dx1 >= top_w) dx1 = top_w - 1;
                    if (dy1 >= top_h) dy1 = top_h - 1;
                    if (dx0 > dx1 || dy0 > dy1) {
                        fullClear = true;
                    } else {
                        srcX0 = dx0;
                        srcY0 = dy0;
                        srcX1 = dx1;
                        srcY1 = dy1;

                        int maxShift = maxShiftClamp;
                        if (maxShift < 0) maxShift = 0;
                        clearX0 = srcX0 - maxShift;
                        clearX1 = srcX1 + maxShift;
                        if (clearX0 < 0) clearX0 = 0;
                        if (clearX1 >= top_w) clearX1 = top_w - 1;
                        clearY0 = srcY0;
                        clearY1 = srcY1;
                    }
                }
            }

            DirtyRect stereoDirty;
            if (fullClear) {
                std::fill(eyeL.begin(), eyeL.end(), 0);
                std::fill(eyeR.begin(), eyeR.end(), 0);
                std::fill(zL.begin(), zL.end(), 0);
                std::fill(zR.begin(), zR.end(), 0);
            } else {
                const int clearW = clearX1 - clearX0 + 1;
                for (int y = clearY0; y <= clearY1; ++y) {
                    std::memset(eyeL.data() + (size_t)y * (size_t)top_w + clearX0, 0, (size_t)clearW);
                    std::memset(eyeR.data() + (size_t)y * (size_t)top_w + clearX0, 0, (size_t)clearW);
                    std::memset(zL.data() + (size_t)y * (size_t)top_w + clearX0, 0, (size_t)clearW);
                    std::memset(zR.data() + (size_t)y * (size_t)top_w + clearX0, 0, (size_t)clearW);
                }
                stereoDirty.x0 = clearX0;
                stereoDirty.y0 = clearY0;
                stereoDirty.x1 = clearX1;
                stereoDirty.y1 = clearY1;
                stereoDirty.valid = true;
                alignDirtyRectToTiles(stereoDirty, top_w, top_h);
            }

            for (int li = 0; li < Real8VM::STEREO_LAYER_COUNT; ++li) {
                const int bucket = li - Real8VM::STEREO_BUCKET_BIAS;
                int shift = (int)lroundf((float)bucket * bucketScale) + convPx;
                if (swapEyes) shift = -shift;
                if (shift < -maxShiftClamp) shift = -maxShiftClamp;
                if (shift >  maxShiftClamp) shift =  maxShiftClamp;
                const uint8_t zval = (uint8_t)(bucket < 0 ? -bucket : bucket); // |bucket| in 0..7

                for (int y = srcY0; y <= srcY1; ++y) {
                    const uint8_t* src_row = debugVMRef->stereo_layer_row(li, y);
                    for (int x = srcX0; x <= srcX1; ++x) {
                        uint8_t src = src_row[x];
                        if (src == 0xFF) continue;
                        src &= 0x0F;

                        const int lx = x + shift;
                        if ((unsigned)lx < (unsigned)top_w) {
                            const size_t i = (size_t)y * (size_t)top_w + (size_t)lx;
                            if (zval >= zL[i]) { zL[i] = zval; eyeL[i] = src; }
                        }

                        const int rx = x - shift;
                        if ((unsigned)rx < (unsigned)top_w) {
                            const size_t i = (size_t)y * (size_t)top_w + (size_t)rx;
                            if (zval >= zR[i]) { zR[i] = zval; eyeR[i] = src; }
                        }
                    }
                }
            }

#if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                // Use separate CPU buffers for the two uploads to avoid any chance of overlap.
                blitFrameToTexture(eyeL.data(), top_w, top_h, gameTexTop,  cachedPalette565, cachedPalette32, wantScreenshot,  indexBufferTop,
                                   stereoDirty.valid ? &stereoDirty : nullptr);
                blitFrameToTexture(eyeR.data(), top_w, top_h, gameTexTopR, cachedPalette565, cachedPalette32, false, indexBufferBottom,
                                   stereoDirty.valid ? &stereoDirty : nullptr);
            } else
#endif
            {
                blitFrameToTexture(eyeL.data(), top_w, top_h, gameTexTop,  cachedPalette565, cachedPalette32, wantScreenshot,  pixelBuffer565Top,
                                   stereoDirty.valid ? &stereoDirty : nullptr);
                blitFrameToTexture(eyeR.data(), top_w, top_h, gameTexTopR, cachedPalette565, cachedPalette32, false, pixelBuffer565Bottom,
                                   stereoDirty.valid ? &stereoDirty : nullptr);
            }
            if (wantScreenshot) capturedThisFrame = true;
            stereoBuffersValid = true;
            lastStereoSlider = stereoSlider;
            lastStereoDepth = depthLevel;
            lastStereoConv = convPx;
            lastStereoSwap = swapEyes;
            if (!inGameSingleScreen) {
                updateBottomTexture(false);
            }
        } else {

        if (inGameSingleScreen) {
            // Update top texture and screenshot buffer from the game framebuffer.
            DirtyRect dirtyTop;
            if (getDirtyRectForBuffer(topbuffer, top_w, top_h, dirtyTop)) {
                alignDirtyRectToTiles(dirtyTop, top_w, top_h);
            }
            #if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette) {
                u8* topIndexSrc = indexBufferTop;
                if (isLinearVmFramebuffer(topbuffer) && gameTexTop &&
                    gameTexTop->width == top_w && gameTexTop->height == top_h) {
                    topIndexSrc = (u8*)topbuffer;
                }
                blitFrameToTexture(topbuffer, top_w, top_h, gameTexTop, cachedPalette565, cachedPalette32, wantScreenshot, topIndexSrc,
                                   dirtyTop.valid ? &dirtyTop : nullptr);
            } else
#endif
            {
                blitFrameToTexture(topbuffer, top_w, top_h, gameTexTop, cachedPalette565, cachedPalette32, wantScreenshot, pixelBuffer565Top,
                                   dirtyTop.valid ? &dirtyTop : nullptr);
            }
            if (wantScreenshot) capturedThisFrame = true;
        } else {
            // Normal: top preview + bottom UI
            if (!topPreviewBlank) {
                DirtyRect dirtyTop;
                if (getDirtyRectForBuffer(topbuffer, top_w, top_h, dirtyTop)) {
                    alignDirtyRectToTiles(dirtyTop, top_w, top_h);
                }
                #if REAL8_3DS_HAS_PAL8_TLUT
                if (useGpuPalette) {
                    u8* topIndexSrc = indexBufferTop;
                    if (isLinearVmFramebuffer(topbuffer) && gameTexTop &&
                        gameTexTop->width == top_w && gameTexTop->height == top_h) {
                        topIndexSrc = (u8*)topbuffer;
                    }
                    blitFrameToTexture(topbuffer, top_w, top_h, gameTexTop, cachedPalette565, cachedPalette32, false, topIndexSrc,
                                       dirtyTop.valid ? &dirtyTop : nullptr);
                } else
#endif
                {
                    blitFrameToTexture(topbuffer, top_w, top_h, gameTexTop, cachedPalette565, cachedPalette32, false, pixelBuffer565Top,
                                       dirtyTop.valid ? &dirtyTop : nullptr);
                }
            }
            updateBottomTexture(true);
        }

        
        }

        lastStereoActive = stereoActive;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        int tx, ty, tw, th;
        float tscale;
        bool hasWallpaper = (wallpaperTex != nullptr);
        bool topStretch = debugVMRef && debugVMRef->stretchScreen;
        bool topHasWallpaper = hasWallpaper && (!debugVMRef || (debugVMRef->showSkin && !topStretch));
        bool bottomScreenActive = false;
        if (debugVMRef) {
            if (debugVMRef->ram) {
                bottomScreenActive = (debugVMRef->ram[Real8VM::BOTTOM_GPIO_ADDR] & 0x03) != 0;
            } else {
                bottomScreenActive = debugVMRef->isBottomScreenEnabled();
            }
        }
        bool bottomHasWallpaper = bottomWallpaperVisible && topHasWallpaper;
        const int logicalTopW = topStretch ? kTopWidth : kBottomWidth;
        if (mode == 3 && top_w == kTopWidth && top_h == kTopHeight) {
            tw = top_w;
            th = top_h;
            tx = (logicalTopW - tw) / 2;
            ty = 0;
            tscale = 1.0f;
        } else if (mode == 2 && top_w == 200 && top_h == 120) {
            tw = top_w * 2;
            th = top_h * 2;
            tx = (logicalTopW - tw) / 2;
            ty = (kTopHeight - th) / 2;
            tscale = 2.0f;
        } else if (mode == 0 && !topStretch) {
            tw = top_w;
            th = top_h;
            tx = (logicalTopW - tw) / 2;
            ty = (kTopHeight - th) / 2;
            tscale = 1.0f;
        } else {
            buildGameRect(topStretch, topHasWallpaper, logicalTopW, kTopHeight, top_w, top_h, tx, ty, tw, th, tscale);
        }
        if (logicalTopW != kTopWidth) {
            tx += (kTopWidth - logicalTopW) / 2;
        }

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

            const float tscaleX = (float)tw / (float)top_w;
            const float drawX = (float)tx;
            const float drawY = (float)ty;

        // Use the "2x (minus 16px)" vertical mapping whenever the final height is the full
        // top-screen height (240). This keeps the game full-height even when the skin/wallpaper
        // is enabled, so the wallpaper only shows on the left/right.
        const bool useTallScale = (top_w == kPicoWidth && top_h == kPicoHeight && th == kTopHeight);

        // Bind the TLUT palette right before drawing the paletted game texture.
#if REAL8_3DS_HAS_PAL8_TLUT
        if (useGpuPalette && tlutReady) {
            C3D_TlutBind(0, &gameTlut);
        }
#endif

        if (!topPreviewBlank) {

            if (!useTallScale) {
                // Fallback: uniform scaling
                float tscaleY = (float)th / (float)top_h;
                C2D_DrawImageAt(img, drawX, drawY, 0.5f, nullptr, tscaleX, tscaleY);
            } else {
                // Special vertical mapping:
                // - First 8 source rows are NOT doubled (1x)
                // - Middle rows are doubled (2x)
                // - Last 8 source rows are NOT doubled (1x)
                // Fits th==240: 8 + (112*2) + 8 = 240

                const int kNoDoubleRows = 8;
                const int srcW = top_w;   // 128
                const int srcH = top_h;  // 128
                const int topH = kNoDoubleRows;         // 8
                const int botH = kNoDoubleRows;         // 8
                const int midH = srcH - topH - botH;    // 112

                const float dstTopH = (float)topH;      // 8 pixels tall
                const float dstBotH = (float)botH;      // 8 pixels tall
                float dstMidH = (float)th - dstTopH - dstBotH; // 224 when th==240
                if (dstMidH < 1.0f) dstMidH = 1.0f;

                const float scaleYMid = dstMidH / (float)midH;

                const float vTopFull = 1.0f;
                const float vTopSplit = 1.0f - ((float)topH / (float)srcH); // 0.9375
                const float vBotSplit = (float)botH / (float)srcH;          // 0.0625
                const float vBotFull = 0.0f;

                Tex3DS_SubTexture subTop;
                subTop.width  = srcW;
                subTop.height = topH;
                subTop.left   = 0.0f;
                subTop.right  = 1.0f;
                subTop.top    = vTopFull;
                subTop.bottom = vTopSplit;

                C2D_Image imgTop = img;
                imgTop.subtex = &subTop;
                C2D_DrawImageAt(imgTop, drawX, drawY, 0.5f, nullptr, tscaleX, 1.0f);

                Tex3DS_SubTexture subMid;
                subMid.width  = srcW;
                subMid.height = midH;
                subMid.left   = 0.0f;
                subMid.right  = 1.0f;
                subMid.top    = vTopSplit;
                subMid.bottom = vBotSplit;

                C2D_Image imgMid = img;
                imgMid.subtex = &subMid;
                C2D_DrawImageAt(imgMid, drawX, drawY + dstTopH, 0.5f, nullptr, tscaleX, scaleYMid);

                Tex3DS_SubTexture subBot;
                subBot.width  = srcW;
                subBot.height = botH;
                subBot.left   = 0.0f;
                subBot.right  = 1.0f;
                subBot.top    = vBotSplit;
                subBot.bottom = vBotFull;

                C2D_Image imgBot = img;
                imgBot.subtex = &subBot;
                C2D_DrawImageAt(imgBot, drawX, drawY + dstTopH + dstMidH, 0.5f, nullptr, tscaleX, 1.0f);
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

        if (bottomHasWallpaper != lastBottomHasWallpaper) {
            bottomStaticValid = false;
            lastBottomHasWallpaper = bottomHasWallpaper;
        }

        if (inGameSingleScreen) {
            if (!bottomStaticValid) {
                C3D_RenderTargetClear(bottomTarget, C3D_CLEAR_ALL, kClearColor, 0);
                C2D_SceneBegin(bottomTarget);
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
                bottomStaticValid = true;
            }
        } else {
            C3D_RenderTargetClear(bottomTarget, C3D_CLEAR_ALL, kClearColor, 0);
            C2D_SceneBegin(bottomTarget);
            int bx = 0;
            int by = 0;
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
            float bscaleX = 1.0f;
            float bscaleY = 1.0f;
            int drawW = bottom_w;
            int drawH = bottom_h;
            const bool bottomGameTallScale =
                (bottomMode == 1 && bottom_w == kPicoWidth && bottom_h == kPicoHeight &&
                 debugVMRef && !debugVMRef->isShellUI);
            if (bottomGameTallScale) {
                float scale = std::min((float)kBottomWidth / (float)bottom_w,
                                       (float)kBottomHeight / (float)bottom_h);
                drawW = (int)((float)bottom_w * scale);
                drawH = (int)((float)bottom_h * scale);
                bx = (kBottomWidth - drawW) / 2;
                by = (kBottomHeight - drawH) / 2;
                bscaleX = scale;
                bscaleY = scale;
            } else if (bottomMode == 0 || bottomMode == 1) {
                bx = (kBottomWidth - bottom_w) / 2;
                by = (kBottomHeight - bottom_h) / 2;
                bscaleX = 1.0f;
                bscaleY = 1.0f;
                drawW = bottom_w;
                drawH = bottom_h;
            } else if (bottomMode == 2) {
                const float scale = 2.0f;
                drawW = (int)((float)bottom_w * scale);
                drawH = (int)((float)bottom_h * scale);
                bx = (kBottomWidth - drawW) / 2;
                by = (kBottomHeight - drawH) / 2;
                bscaleX = scale;
                bscaleY = scale;
            } else {
                float scale = std::min((float)kBottomWidth / (float)bottom_w, (float)kBottomHeight / (float)bottom_h);
                drawW = (int)((float)bottom_w * scale);
                drawH = (int)((float)bottom_h * scale);
                bx = (kBottomWidth - drawW) / 2;
                by = (kBottomHeight - drawH) / 2;
                bscaleX = scale;
                bscaleY = scale;
            }
            // Bind TLUT palette for the bottom game blit.
#if REAL8_3DS_HAS_PAL8_TLUT
            if (useGpuPalette && tlutReady) {
                C3D_TlutBind(0, &gameTlut);
            }
#endif

            if (!bottomGameTallScale) {
                C2D_DrawImageAt(gameImageBottom, (float)bx, (float)by, 0.5f, nullptr, bscaleX, bscaleY);
            } else {
                const int kNoDoubleRows = 8;
                const int srcW = bottom_w;
                const int srcH = bottom_h;
                const int topH = kNoDoubleRows;
                const int botH = kNoDoubleRows;
                const int midH = srcH - topH - botH;

                const float dstTopH = (float)topH;
                const float dstBotH = (float)botH;
                float dstMidH = (float)drawH - dstTopH - dstBotH;
                if (dstMidH < 1.0f) dstMidH = 1.0f;

                const float scaleYMid = dstMidH / (float)midH;
                const float tscaleX = (float)drawW / (float)bottom_w;
                const float drawX = (float)bx;
                const float drawY = (float)by;

                const float vTopFull = 1.0f;
                const float vTopSplit = 1.0f - ((float)topH / (float)srcH);
                const float vBotSplit = (float)botH / (float)srcH;
                const float vBotFull = 0.0f;

                Tex3DS_SubTexture subTop;
                subTop.width  = srcW;
                subTop.height = topH;
                subTop.left   = 0.0f;
                subTop.right  = 1.0f;
                subTop.top    = vTopFull;
                subTop.bottom = vTopSplit;

                C2D_Image imgTop = gameImageBottom;
                imgTop.subtex = &subTop;
                C2D_DrawImageAt(imgTop, drawX, drawY, 0.5f, nullptr, tscaleX, 1.0f);

                Tex3DS_SubTexture subMid;
                subMid.width  = srcW;
                subMid.height = midH;
                subMid.left   = 0.0f;
                subMid.right  = 1.0f;
                subMid.top    = vTopSplit;
                subMid.bottom = vBotSplit;

                C2D_Image imgMid = gameImageBottom;
                imgMid.subtex = &subMid;
                C2D_DrawImageAt(imgMid, drawX, drawY + dstTopH, 0.5f, nullptr, tscaleX, scaleYMid);

                Tex3DS_SubTexture subBot;
                subBot.width  = srcW;
                subBot.height = botH;
                subBot.left   = 0.0f;
                subBot.right  = 1.0f;
                subBot.top    = vBotSplit;
                subBot.bottom = vBotFull;

                C2D_Image imgBot = gameImageBottom;
                imgBot.subtex = &subBot;
                C2D_DrawImageAt(imgBot, drawX, drawY + dstTopH + dstMidH, 0.5f, nullptr, tscaleX, 1.0f);
            }
        }

        // Flush once after all targets have been drawn this frame.
        C2D_Flush();
        C3D_FrameEnd(0);
        presentedThisLoop = true;

        if (screenshotPending && capturedThisFrame) {
            if (writeBmp24(pendingScreenshotPath, screenBuffer32.data(), screenW, screenH)) {
                log("[3DS] Screenshot saved: %s", pendingScreenshotPath.c_str());
            } else {
                log("[3DS] Screenshot failed.");
            }
            screenshotPending = false;
            pendingScreenshotPath.clear();
        }
        
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
        return fastForwardOverride || ((m_keysHeld & KEY_R) != 0);
    }

    void setFastForwardHeld(bool held) override {
        fastForwardOverride = held;
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
        std::string cartsDir = rootPath + "/carts";
        ensureDir(cartsDir);

        std::function<void(const std::string&, const std::string&)> addFilesRecursive;
        addFilesRecursive = [&](const std::string& baseDir, const std::string& relDir) {
            std::string fullDir = baseDir;
            if (!relDir.empty()) fullDir += "/" + relDir;

            DIR *dir = opendir(fullDir.c_str());
            if (!dir) return;
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                std::string name = ent->d_name;
                std::string relPath = relDir.empty() ? name : (relDir + "/" + name);
                std::string fullPath = fullDir + "/" + name;

                struct stat st;
                if (stat(fullPath.c_str(), &st) != 0) continue;
                if (S_ISDIR(st.st_mode)) {
                    addFilesRecursive(baseDir, relPath);
                } else if (S_ISREG(st.st_mode)) {
                    if (ext && strlen(ext) > 0 && relPath.find(ext) == std::string::npos) continue;
                    results.push_back("/" + relPath);
                }
            }
            closedir(dir);
        };

        addFilesRecursive(cartsDir, "");
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

            const int gameW = (bottomW > 0) ? bottomW : kPicoWidth;
            const int gameH = (bottomH > 0) ? bottomH : kPicoHeight;
            int bx = 0;
            int by = 0;
            int bw = 0;
            int bh = 0;
            float scaleX = 1.0f;
            float scaleY = 1.0f;
            const int bottomMode = (debugVMRef ? debugVMRef->bottom_vmode_cur : 0);
            const bool bottomGameTallScale =
                (bottomMode == 1 && gameW == kPicoWidth && gameH == kPicoHeight &&
                 debugVMRef && !debugVMRef->isShellUI);
            if (bottomGameTallScale) {
                float scale = std::min((float)kBottomWidth / (float)gameW,
                                       (float)kBottomHeight / (float)gameH);
                bw = (int)((float)gameW * scale);
                bh = (int)((float)gameH * scale);
                bx = (kBottomWidth - bw) / 2;
                by = (kBottomHeight - bh) / 2;
                scaleX = scale;
                scaleY = scale;
            } else if (bottomMode == 0 || bottomMode == 1) {
                bw = gameW;
                bh = gameH;
                bx = (kBottomWidth - bw) / 2;
                by = (kBottomHeight - bh) / 2;
                scaleX = 1.0f;
                scaleY = 1.0f;
            } else if (bottomMode == 2) {
                const float scale = 2.0f;
                bw = (int)((float)gameW * scale);
                bh = (int)((float)gameH * scale);
                bx = (kBottomWidth - bw) / 2;
                by = (kBottomHeight - bh) / 2;
                scaleX = scale;
                scaleY = scale;
            } else {
                float scale = std::min((float)kBottomWidth / (float)gameW,
                                       (float)kBottomHeight / (float)gameH);
                bw = (int)((float)gameW * scale);
                bh = (int)((float)gameH * scale);
                bx = (kBottomWidth - bw) / 2;
                by = (kBottomHeight - bh) / 2;
                scaleX = scale;
                scaleY = scale;
            }

            if (touch.px >= bx && touch.px < (bx + bw) && touch.py >= by && touch.py < (by + bh)) {
                float relX = (float)(touch.px - bx);
                float relY = (float)(touch.py - by);
                int mx = (int)(relX / scaleX);
                int my = (int)(relY / scaleY);
                if (mx < 0) mx = 0;
                if (mx >= gameW) mx = gameW - 1;
                if (my < 0) my = 0;
                if (my >= gameH) my = gameH - 1;
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

        const bool isPaused = (debugVMRef && debugVMRef->isShellUI);
        const bool fastForward = isFastForwardHeld();

        // Reset request from VM
        if (isPaused || !samples || count <= 0) {
            // Stop playback and fully reset our submit state so audio can resume reliably.
            ndspChnWaveBufClear(0);

            // Clear FIFO and rewind submission pointer.
            audioFifoReset();
            nextWaveToSubmit = 0;
            audioUnderruns = 0;
            audioOverruns = 0;
            audioStatsLastMs = 0;
            lastRateCorrection = 0.0;

            // Mark all wave buffers as reusable (some libctru versions leave status stale after WaveBufClear).
            for (int i = 0; i < kNumAudioBuffers; ++i) {
                waveBuf[i].status = NDSP_WBUF_DONE;
            }

            // Zero the backing buffers to avoid clicks/pop after reset.
            if (audioBuffer) {
                const size_t totalI16 = (size_t)kSamplesPerBuffer * (size_t)kNumAudioBuffers;
                memset(audioBuffer, 0, totalI16 * sizeof(int16_t));
                DSP_FlushDataCache(audioBuffer, totalI16 * sizeof(int16_t));
            }
            resamplePosFp = 0;
            resamplePrev = 0;
            resampleHasPrev = false;
            resampleScratch.clear();
            return;
        }

        double correction = 0.0;
        if (!fastForward && !isPaused) {
            const double fifoMs = (double)audioFifoCount * 1000.0 / (double)kSampleRate;
            const double errorMs = fifoMs - (double)kFifoTargetMs;
            const double kServoGain = 0.00005;
            const double kServoMax = 0.005;
            correction = errorMs * kServoGain;
            if (correction > kServoMax) correction = kServoMax;
            if (correction < -kServoMax) correction = -kServoMax;
        }
        lastRateCorrection = correction;

        const uint64_t baseStepFp =
            ((uint64_t)AudioEngine::SAMPLE_RATE_NUM << 32) /
            ((uint64_t)AudioEngine::SAMPLE_RATE_DEN * (uint64_t)kSampleRate);
        uint64_t stepFp = (uint64_t)llround((double)baseStepFp * (1.0 + correction));
        if (stepFp < 1) stepFp = 1;

        // Resample VM output to NDSP rate (linear), with 8-bit source quantization.
        resampleScratch.clear();
        {
            const double step = (double)stepFp / (double)(1ull << 32);
            const double est = (step > 0.0) ? ((double)count / step) : (double)count;
            resampleScratch.reserve((size_t)(est + 4.0));
        }

        int idx = 0;
        if (!resampleHasPrev && count > 0) {
            resamplePrev = quantizeToU8S16(samples[0]);
            resampleHasPrev = true;
            idx = 1;
        }

        uint64_t pos = resamplePosFp;
        int16_t prev = resamplePrev;

        for (; idx < count; ++idx) {
            int16_t curr = quantizeToU8S16(samples[idx]);
            while (pos <= (1ull << 32)) {
                const uint32_t t = (uint32_t)pos;
                const int32_t delta = (int32_t)curr - (int32_t)prev;
                const int32_t out = (int32_t)prev + (int32_t)((delta * (int64_t)t) >> 32);
                resampleScratch.push_back((int16_t)out);
                pos += stepFp;
            }
            pos -= (1ull << 32);
            prev = curr;
        }

        resamplePrev = prev;
        resamplePosFp = pos;

        if (!resampleScratch.empty()) {
            // Write mono samples to FIFO quickly (never blocks).
            audioFifoWriteMono(resampleScratch.data(), (int)resampleScratch.size());
        }

        // Submit any newly-available full NDSP buffers.
        pumpAudio();
    }


    NetworkInfo getNetworkInfo() override {
        // NOTE:
        //  - `networkReady` only tells us that SOC/curl are initialized.
        //  - The shell needs a "can I show repo games?" answer even when the network stack is toggled off
        //    (e.g. after exiting a game on 3DS).
        //
        // On 3DS we treat `connected` as "Wi‑Fi associated" (ACU_GetWifiStatus != 0). This is the best
        // low-cost signal we have without performing an external probe.
        bool connected = false;

    #if REAL8_HAS_ACU
        u32 wifi = 0;

        // ACU can be used even when SOC/curl are not active. If AC isn't already initialized, init it
        // temporarily just for this query to avoid leaking handles when `networkReady` is false.
        bool tempAc = false;
        if (!acReady) {
            Result rcInit = acInit();
            if (R_SUCCEEDED(rcInit)) {
                tempAc = true;
            }
        }

        if (acReady || tempAc) {
            Result rcWifi = ACU_GetWifiStatus(&wifi);
            connected = R_SUCCEEDED(rcWifi) && (wifi != 0);
        }

        if (tempAc) {
            acExit();
        }
    #else
        connected = false;
    #endif

        if (!connected) return {false, "", "Offline", 0.0f};
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

        pendingScreenshotPath = buf;
        screenshotPending = true;
        log("[3DS] Screenshot queued: %s", buf);
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
        bottomStaticValid = false;
    }
    void updateOverlay() override {}
};
