#include <gba.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>

#include "gba_host.hpp"
#include "cart_blob.h"

#include "../../core/real8_vm.h"
#include "../../core/real8_cart.h"

#include "build/cart_blob_bin.h"
#include "build/splash_img_bin.h"
#include "build/splash_pal_bin.h"

namespace {
    const size_t kCartFixedBytes = 0x4300;
    const int kScreenCenterX = 64;
    const int kFontWidth = 5;

    #define EWRAM_DATA __attribute__((section(".ewram")))
    #define IWRAM_DATA __attribute__((section(".iwram")))

    #define REAL8_GBA_FB_SECTION EWRAM_DATA
    #define REAL8_GBA_STATE_SECTION EWRAM_DATA

#ifndef REAL8_GBA_DEFAULT_SKIN_ON
#define REAL8_GBA_DEFAULT_SKIN_ON 1
#endif

    EWRAM_DATA alignas(4) uint8_t gba_ram[0x8000];
    REAL8_GBA_FB_SECTION alignas(4) uint8_t gba_fb[128][128];

    REAL8_GBA_STATE_SECTION static GameData g_game;
    REAL8_GBA_STATE_SECTION static GbaHost g_host;
    EWRAM_DATA static Real8VM g_vm(&g_host);

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
    static inline void dma3Copy32Wait(const void* src, void* dst, u32 count) {
        REG_DMA3SAD = (u32)src;
        REG_DMA3DAD = (u32)dst;
        REG_DMA3CNT = count | DMA_32 | DMA_SRC_INC | DMA_DST_INC | DMA_START_NOW | DMA_ENABLE;
        while (REG_DMA3CNT & DMA_ENABLE) {
        }
    }

    static inline void dma3Copy16Wait(const void* src, void* dst, u32 count) {
        REG_DMA3SAD = (u32)src;
        REG_DMA3DAD = (u32)dst;
        REG_DMA3CNT = count | DMA_16 | DMA_SRC_INC | DMA_DST_INC | DMA_START_NOW | DMA_ENABLE;
        while (REG_DMA3CNT & DMA_ENABLE) {
        }
    }

    static void showSplash() {
        REG_DISPCNT = MODE_4 | BG2_ON | 0x80;
        const size_t maxBytes = 240u * 160u;
        size_t copyBytes = splash_img_bin_size;
        if (copyBytes > maxBytes) copyBytes = maxBytes;
        if (copyBytes & 1u) copyBytes -= 1u;

        const size_t palEntries = splash_pal_bin_size / 2;
        size_t palCount = (palEntries < 256) ? palEntries : 256;
        const u16* pal = reinterpret_cast<const u16*>(splash_pal_bin);
        for (size_t i = 0; i < palCount; ++i) {
            BG_PALETTE[i] = pal[i];
        }
        for (size_t i = palCount; i < 256; ++i) {
            BG_PALETTE[i] = 0;
        }

        void* vramPtr = reinterpret_cast<void*>(VRAM);

#if defined(__GBA__)
        const uintptr_t srcAddr = reinterpret_cast<uintptr_t>(splash_img_bin);
        const uintptr_t dstAddr = reinterpret_cast<uintptr_t>(vramPtr);
        if (((srcAddr | dstAddr) & 3u) == 0 && (copyBytes % 4u) == 0u) {
            dma3Copy32Wait(splash_img_bin, vramPtr, (u32)(copyBytes / 4u));
        } else {
            dma3Copy16Wait(splash_img_bin, vramPtr, (u32)(copyBytes / 2u));
        }
#else
        std::memcpy(vramPtr, splash_img_bin, copyBytes);
#endif
        if (copyBytes < maxBytes) {
            uint8_t* vramBytes = reinterpret_cast<uint8_t*>(vramPtr);
            std::memset(vramBytes + copyBytes, 0, maxBytes - copyBytes);
        }
        REG_DISPCNT = MODE_4 | BG2_ON;
    }

    bool loadCartFromBlob(GameData &game, const uint8_t** rom_view, size_t* rom_size) {
        if (rom_view) *rom_view = nullptr;
        if (rom_size) *rom_size = 0;

        const uint8_t* blob = cart_blob_bin;
        const size_t blob_size = cart_blob_bin_size;

        if (blob_size < sizeof(CartBlobHeader)) return false;

        CartBlobHeader header{};
        memcpy(&header, blob, sizeof(header));
        if (memcmp(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE) != 0) return false;
        if (header.raw_size < kCartFixedBytes) return false;
        if (header.flags != CART_BLOB_FLAG_NONE) return false;
        if (header.comp_size != header.raw_size) return false;
        if (header.comp_size > (blob_size - sizeof(CartBlobHeader))) return false;

        const uint8_t* payload = blob + sizeof(CartBlobHeader);

        size_t lua_size = header.raw_size - kCartFixedBytes;
        const uint8_t* src = payload;

        game.gfx = src; src += 0x2000;
        game.map = src; src += 0x1000;
        game.sprite_flags = src; src += 0x100;
        game.music = src; src += 0x100;
        game.sfx = src; src += 0x1100;
        game.lua_code_ptr = reinterpret_cast<const char*>(src);
        game.lua_code_size = lua_size;
        game.cart_id = "game.p8.png";

        if (rom_view) *rom_view = payload;
        if (rom_size) *rom_size = kCartFixedBytes;

        return true;
    }

    int getCenteredX(const char* text) {
        if (!text) return kScreenCenterX;
        int textLenInPixels = (int)strlen(text) * kFontWidth;
        return kScreenCenterX - (textLenInPixels / 2);
    }

    void copySingleLine(char* dst, size_t dstSize, const char* src) {
        if (!dst || dstSize == 0) return;
        dst[0] = '\0';
        if (!src) return;
        size_t i = 0;
        while (src[i] && src[i] != '\n' && i + 1 < dstSize) {
            dst[i] = src[i];
            ++i;
        }
        dst[i] = '\0';
    }

    void showErrorOverlay(Real8VM& vm, const char* header, const char* msg, int color) {
        char headerBuf[24];
        char msgBuf[32];
        copySingleLine(headerBuf, sizeof(headerBuf), header);
        copySingleLine(msgBuf, sizeof(msgBuf), msg);

        vm.gpu.setMenuFont(true);
        vm.gpu.cls(0);
        vm.gpu.rectfill(0, 50, 127, 75, color);
        vm.gpu.pprint(headerBuf, (int)strlen(headerBuf), getCenteredX(headerBuf), 55, 7);
        vm.gpu.pprint(msgBuf, (int)strlen(msgBuf), getCenteredX(msgBuf), 65, 7);
        vm.gpu.setMenuFont(false);
        vm.show_frame();
    }

    constexpr const char* kMenuOptions[] = {
        "CONTINUE",
        "RESET GAME",
        "SHOW FPS",
        "SHOW SKIN",
        "CREDITS"
    };
    constexpr int kMenuOptionCount = (int)(sizeof(kMenuOptions) / sizeof(kMenuOptions[0]));

    struct GbaInGameMenu {
        bool active = false;
        bool inputLatch = false;
        bool requestExit = false;
        int selection = 0;
        Real8Gfx::GfxState gfx_backup{};
        bool showingCredits = false;
        bool showSkin = (REAL8_GBA_DEFAULT_SKIN_ON != 0);
        uint32_t inputMask = 0;
        uint32_t prevInputMask = 0;

        void build(Real8VM& vm) {
            (void)vm;
            selection = 0;
            showingCredits = false;
            inputMask = 0;
            prevInputMask = 0;
        }

        void open(Real8VM& vm) {
            vm.gpu.saveState(gfx_backup);
            vm.gpu.reset();
            build(vm);
            active = true;
        }

        void close(Real8VM& vm) {
            vm.gpu.restoreState(gfx_backup);
            active = false;
            inputLatch = true;
        }

        void syncInput(Real8VM& vm, GbaHost& host) {
            prevInputMask = inputMask;
            vm.btn_states[0] = host.getPlayerInput(0);
            for (int i = 1; i < 8; ++i) vm.btn_states[i] = 0;

            inputMask = vm.btn_states[0];
            vm.btn_mask = inputMask;

            for (int p = 0; p < 8; ++p) {
                for (int b = 0; b < 6; ++b) {
                    if (vm.btn_states[p] & (1u << b)) {
                        if (vm.btn_counters[p][b] < 255) vm.btn_counters[p][b]++;
                    } else {
                        vm.btn_counters[p][b] = 0;
                    }
                }
            }
            host.consumeLatchedInput();
        }

        void renderMessage(Real8VM& vm, const char* header, const char* msg, int color) {
            vm.gpu.setMenuFont(true);
            vm.gpu.cls(0);
            vm.gpu.rectfill(0, 50, 127, 75, color);
            vm.gpu.pprint(header, (int)strlen(header), getCenteredX(header), 55, 7);
            vm.gpu.pprint(msg, (int)strlen(msg), getCenteredX(msg), 65, 7);
            vm.gpu.setMenuFont(false);
        }

        void renderCredits(Real8VM& vm) {
            vm.gpu.setMenuFont(true);
            vm.gpu.cls(0);
            vm.gpu.fillp(0);

            int w = 110;
            int h = 70;
            int x = (128 - w) / 2;
            int y = (128 - h) / 2;

            vm.gpu.rectfill(x, y, x + w, y + h, 1);
            vm.gpu.rect(x, y, x + w, y + h, 12);
            vm.gpu.rectfill(x, y, x + w, y + 9, 12);

            const char* title = "CREDITS";
            vm.gpu.pprint(title, (int)strlen(title), getCenteredX(title), y + 2, 7);

            int textY = y + 18;
            const char* line1 = "REAL-8 VM";
            vm.gpu.pprint(line1, (int)strlen(line1), getCenteredX(line1), textY, 6);

            textY += 12;
            const char* line2 = "by @natureglass";
            vm.gpu.pprint(line2, (int)strlen(line2), getCenteredX(line2), textY, 7);

            textY += 8;
            const char* line3 = "Alex Daskalakis";
            vm.gpu.pprint(line3, (int)strlen(line3), getCenteredX(line3), textY, 7);

            textY += 14;
            std::string line4 = std::string("Ver ") + IReal8Host::REAL8_VERSION + " for " + vm.host->getPlatform();
            vm.gpu.pprint(line4.c_str(), (int)line4.length(), getCenteredX(line4.c_str()), textY, 11);

            vm.gpu.setMenuFont(false);
        }

        void render(Real8VM& vm) {
            if (showingCredits) {
                renderCredits(vm);
                return;
            }

            vm.gpu.setMenuFont(true);
            vm.gpu.fillp(0xA5A5);
            vm.gpu.rectfill(0, 0, 128, 128, 0);
            vm.gpu.fillp(0);

            int mw = 100;
            int mh = (kMenuOptionCount * 11) + 16;
            int mx = (128 - mw) / 2;
            int my = (128 - mh) / 2;

            vm.gpu.rectfill(mx, my, mx + mw, my + mh, 0);
            vm.gpu.rect(mx, my, mx + mw, my + mh, 1);
            vm.gpu.rectfill(mx, my, mx + mw, my + 9, 1);

            const char* title = "PAUSED";
            vm.gpu.pprint(title, (int)strlen(title), getCenteredX(title), my + 2, 6);

            for (int i = 0; i < kMenuOptionCount; ++i) {
                const char* option = kMenuOptions[i];
                int oy = my + 15 + (i * 11);
                int ox = mx + 13;
                int col = (i == selection) ? 7 : 6;

                if (i == selection) vm.gpu.pprint(">", 1, ox - 6, oy, 7);
                vm.gpu.pprint(option, (int)strlen(option), ox, oy, col);

                if (strcmp(option, "MUSIC") == 0) {
                    for (int b = 0; b < 10; ++b) {
                        vm.gpu.pprint("|", 1, mx + mw - 45 + (b * 3), oy, (b < vm.volume_music) ? 11 : 5);
                    }
                } else if (strcmp(option, "SFX") == 0) {
                    for (int b = 0; b < 10; ++b) {
                        vm.gpu.pprint("|", 1, mx + mw - 45 + (b * 3), oy, (b < vm.volume_sfx) ? 11 : 5);
                    }
                } else if (strcmp(option, "SHOW FPS") == 0) {
                    const char* status = vm.showStats ? "ON" : "OFF";
                    int statusCol = vm.showStats ? 11 : 8;
                    int txtW = (int)strlen(status) * kFontWidth;
                    int statusX = (mx + mw) - txtW - 10;
                    vm.gpu.pprint(status, (int)strlen(status), statusX, oy, statusCol);
                } else if (strcmp(option, "SHOW SKIN") == 0) {
                    const char* status = showSkin ? "ON" : "OFF";
                    int statusCol = showSkin ? 11 : 8;
                    int txtW = (int)strlen(status) * kFontWidth;
                    int statusX = (mx + mw) - txtW - 10;
                    vm.gpu.pprint(status, (int)strlen(status), statusX, oy, statusCol);
                }
            }

            vm.gpu.setMenuFont(false);
        }

        void update(Real8VM& vm, GbaHost& host, GameData& game) {
            if (kMenuOptionCount == 0) return;

            if (showingCredits) {
                if ((inputMask & ~prevInputMask) != 0) {
                    showingCredits = false;
                    close(vm);
                }
                return;
            }

            if (vm.btnp(2)) {
                selection--;
                if (selection < 0) selection = kMenuOptionCount - 1;
            }
            if (vm.btnp(3)) {
                selection++;
                if (selection >= kMenuOptionCount) selection = 0;
            }

            if (vm.btnp(5)) {
                const char* action = kMenuOptions[selection];

                if (strcmp(action, "CONTINUE") == 0) {
                    close(vm);
                } else if (strcmp(action, "RESET GAME") == 0) {
                    showSplash();
                    host.waitForVBlank();
                    vm.rebootVM();
                    if (vm.loadGame(game)) {
                        vm.resetInputState();
                        host.resetVideo();
                        host.setSplashBackdrop(showSkin);
                        host.clearBorders();
                        close(vm);
                    } else {
                        renderMessage(vm, "ERROR", "RESET FAILED", 8);
                        vm.show_frame();
                        build(vm);
                    }
                } else if (strcmp(action, "SHOW FPS") == 0) {
                    vm.showStats = !vm.showStats;
                } else if (strcmp(action, "SHOW SKIN") == 0) {
                    showSkin = !showSkin;
                    host.setSplashBackdrop(showSkin);
                } else if (strcmp(action, "CREDITS") == 0) {
                    showingCredits = true;
                }
            }

            if (vm.btnp(4)) {
                close(vm);
            }
        }
    };

    void gbaSoftReset() {
#if defined(RESTART_ALL)
        SoftReset(RESTART_ALL);
#elif defined(RESTART_EWRAM)
        SoftReset(RESTART_EWRAM);
#else
        SoftReset((RESTART_FLAG)0);
#endif
    }
}

int main(void) {
    auto showSolid = [](u16 rgb) {
        REG_DISPCNT = MODE_4 | BG2_ON;
        BG_PALETTE[0] = RGB5(0, 0, 0);
        BG_PALETTE[1] = rgb;
        u16* vram = (u16*)VRAM;
        for (int i = 0; i < (240 * 160) / 2; ++i) {
            vram[i] = 0x0101;
        }
    };
    auto waitVBlank = []() {
        while (REG_VCOUNT >= 160) {
        }
        while (REG_VCOUNT < 160) {
        }
    };

    showSplash();

    GbaHost& host = g_host;
    Real8VM& vm = g_vm;
    host.setProfileVM(&vm);

    host.log("[BOOT] start");
    host.log("[BOOT] vm bytes %lu", (unsigned long)sizeof(Real8VM));
    host.renderDebugOverlay();

    vm.ram = gba_ram;
    vm.fb = (uint8_t (*)[128])gba_fb;
    vm.setRomView(nullptr, 0, true);

    host.log("[BOOT] initMemory");
    host.renderDebugOverlay();
    if (!vm.initMemory()) {
        host.log("[BOOT] initMemory failed");
        showSolid(RGB5(31, 31, 0));
        host.renderDebugOverlay();
        while (true) host.waitForVBlank();
    }
    host.log("[BOOT] initMemory ok");
    host.renderDebugOverlay();

    GameData& game = g_game;
    game.gfx = nullptr;
    game.map = nullptr;
    game.sprite_flags = nullptr;
    game.music = nullptr;
    game.sfx = nullptr;
    game.lua_code_ptr = nullptr;
    game.lua_code_size = 0;
    game.cart_id = nullptr;
    host.log("[BOOT] load blob");
    host.renderDebugOverlay();
    const uint8_t* rom_view = nullptr;
    size_t rom_size = 0;
    if (!loadCartFromBlob(game, &rom_view, &rom_size)) {
        host.log("[BOOT] blob fail");
        showSolid(RGB5(31, 0, 31));
        host.renderDebugOverlay();
        while (true) host.waitForVBlank();
    }
    vm.setRomView(rom_view, rom_size, true);
    const bool hasLua = (game.lua_code_ptr && game.lua_code_size > 0);
    if (!hasLua) {
        host.log("[BOOT] lua missing");
        showSolid(RGB5(31, 0, 0));
        host.renderDebugOverlay();
        while (true) host.waitForVBlank();
    }
    host.log("[BOOT] blob ok");
    host.renderDebugOverlay();

    if (!vm.loadGame(game)) {
        const char* errTitle = vm.lastErrorTitle[0] ? vm.lastErrorTitle : "ERROR";
        const char* errDetail = vm.lastErrorDetail[0] ? vm.lastErrorDetail : "LOAD FAILED";
        showErrorOverlay(vm, errTitle, errDetail, 8);
        while (true) host.waitForVBlank();
    }
    host.log("[BOOT] loadGame ok");
    host.renderDebugOverlay();

    host.resetVideo();
    host.clearBorders();

    GbaInGameMenu menu;
    host.setSplashBackdrop(menu.showSkin);

        while (true) {
        REAL8_PROFILE_FRAME_BEGIN(&vm);

        // Input: once per refresh (waitForVBlank() resets inputPolled)
        REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileInput);
        host.pollInput();
        REAL8_PROFILE_END(&vm, Real8VM::kProfileInput);

        // Run menu/VM every refresh. Real8VM enforces 30fps carts internally by
        // skipping Lua every other call when targetFPS==30.
        if (menu.active) {
            REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileMenu);
            menu.syncInput(vm, host);
            menu.update(vm, host, game);
            if (menu.requestExit) {
                menu.requestExit = false;
                gbaSoftReset();
            }
            if (menu.active) {
                menu.render(vm);
            }
            REAL8_PROFILE_END(&vm, Real8VM::kProfileMenu);
        } else {
            REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileMenu);
            uint32_t menuInput = host.getPlayerInput(0);
            bool menuPressed = ((menuInput & (1u << 6)) != 0);
            if (menu.inputLatch) {
                if (menuInput == 0) {
                    menu.inputLatch = false;
                }
                menuPressed = false;
            }
            REAL8_PROFILE_END(&vm, Real8VM::kProfileMenu);

            if (menuPressed) {
                REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileMenu);
                menu.open(vm);
                menu.render(vm);
                REAL8_PROFILE_END(&vm, Real8VM::kProfileMenu);
            } else {
                REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileVm);
                vm.runFrame();
                REAL8_PROFILE_END(&vm, Real8VM::kProfileVm);
            }
        }

#if !REAL8_GBA_SKIP_VBLANK
        // One VBlank wait per refresh = fixed pacing + correct ID profiling
        REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileIdle);
        host.waitForVBlank();
        REAL8_PROFILE_END(&vm, Real8VM::kProfileIdle);
#endif

        // Present during VBlank (on 30fps carts this typically no-ops on skipped refreshes)
        REAL8_PROFILE_BEGIN(&vm, Real8VM::kProfileBlit);
        vm.show_frame();
        REAL8_PROFILE_END(&vm, Real8VM::kProfileBlit);

        REAL8_PROFILE_FRAME_END(&vm);
    }

}
