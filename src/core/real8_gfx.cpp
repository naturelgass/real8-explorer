#include "real8_gfx.h"
#include "real8_vm.h" // Needed to access vm->ram and vm->fb
#include "real8_fonts.h"
#include <algorithm>
#include <cstring>

#if defined(__GBA__)
#define IWRAM_CODE __attribute__((section(".iwram"), long_call))
#define EWRAM_DATA __attribute__((section(".ewram")))
#else
#define IWRAM_CODE
#define EWRAM_DATA
#endif

#if defined(__GBA__)
#ifndef REAL8_GBA_IWRAM_CIRC
#define REAL8_GBA_IWRAM_CIRC 1
#endif
#ifndef REAL8_GBA_IWRAM_PUTPIX
#define REAL8_GBA_IWRAM_PUTPIX 1
#endif
#ifndef REAL8_GBA_IWRAM_PSETGET
#define REAL8_GBA_IWRAM_PSETGET 1
#endif
#ifndef REAL8_GBA_IWRAM_GETPIX
#define REAL8_GBA_IWRAM_GETPIX 1
#endif
#ifndef REAL8_GBA_IWRAM_CLS
#define REAL8_GBA_IWRAM_CLS 1
#endif
#ifndef REAL8_GBA_IWRAM_OBJBATCH
#define REAL8_GBA_IWRAM_OBJBATCH 1
#endif
#ifndef REAL8_GBA_IWRAM_MAP
#define REAL8_GBA_IWRAM_MAP 1
#endif
#ifndef REAL8_GBA_SPRITE_BASE_CACHE
#define REAL8_GBA_SPRITE_BASE_CACHE 1
#endif
#ifndef REAL8_GBA_IWRAM_SPR
#define REAL8_GBA_IWRAM_SPR 1
#endif
#ifndef REAL8_GBA_IWRAM_SSPR
#define REAL8_GBA_IWRAM_SSPR 1
#endif
#ifndef REAL8_GBA_IWRAM_SPR_HELPERS
#define REAL8_GBA_IWRAM_SPR_HELPERS 1
#endif
#if REAL8_GBA_IWRAM_CIRC
#define IWRAM_CIRC_CODE IWRAM_CODE
#else
#define IWRAM_CIRC_CODE
#endif
#if REAL8_GBA_IWRAM_PUTPIX
#define IWRAM_PUTPIX_CODE IWRAM_CODE
#else
#define IWRAM_PUTPIX_CODE
#endif
#if REAL8_GBA_IWRAM_PSETGET
#define IWRAM_PSETGET_CODE IWRAM_CODE
#else
#define IWRAM_PSETGET_CODE
#endif
#if REAL8_GBA_IWRAM_GETPIX
#define IWRAM_GETPIX_CODE IWRAM_CODE
#else
#define IWRAM_GETPIX_CODE
#endif
#if REAL8_GBA_IWRAM_CLS
#define IWRAM_CLS_CODE IWRAM_CODE
#else
#define IWRAM_CLS_CODE
#endif
#if REAL8_GBA_IWRAM_OBJBATCH
#define IWRAM_OBJBATCH_CODE IWRAM_CODE
#else
#define IWRAM_OBJBATCH_CODE
#endif
#if REAL8_GBA_IWRAM_MAP
#define IWRAM_MAP_CODE IWRAM_CODE
#else
#define IWRAM_MAP_CODE
#endif
#if REAL8_GBA_IWRAM_SPR
#define IWRAM_SPR_CODE IWRAM_CODE
#else
#define IWRAM_SPR_CODE
#endif
#if REAL8_GBA_IWRAM_SSPR
#define IWRAM_SSPR_CODE IWRAM_CODE
#else
#define IWRAM_SSPR_CODE
#endif
#if REAL8_GBA_IWRAM_SPR_HELPERS
#define IWRAM_SPR_HELP_CODE IWRAM_CODE
#else
#define IWRAM_SPR_HELP_CODE
#endif
#else
#define IWRAM_CIRC_CODE
#define IWRAM_PUTPIX_CODE
#define IWRAM_PSETGET_CODE
#define IWRAM_GETPIX_CODE
#define IWRAM_CLS_CODE
#define IWRAM_OBJBATCH_CODE
#define IWRAM_MAP_CODE
#define IWRAM_SPR_CODE
#define IWRAM_SSPR_CODE
#define IWRAM_SPR_HELP_CODE
#endif
// Palette Definitions
const uint8_t Real8Gfx::PALETTE_RGB[32][3] = {
    // Standard (0-15)
    {0, 0, 0},       {29, 43, 83},    {126, 37, 83},   {0, 135, 81},
    {171, 82, 54},   {95, 87, 79},    {194, 195, 199}, {255, 241, 232},
    {255, 0, 77},    {255, 163, 0},   {255, 236, 39},  {0, 228, 54},
    {41, 173, 255},  {131, 118, 156}, {255, 119, 168}, {255, 204, 170},
    // Extended (16-31)
    {41, 24, 20},    {17, 29, 53},    {66, 33, 54},    {18, 83, 89},
    {116, 47, 41},   {73, 51, 59},    {162, 136, 121}, {243, 239, 125},
    {190, 18, 80},   {255, 108, 36},  {168, 231, 46},  {0, 181, 67},
    {6, 90, 181},    {117, 70, 101},  {255, 110, 89},  {255, 157, 129}
};

Real8Gfx::RGB Real8Gfx::getPico8Color(uint8_t index) {
    uint8_t safe = index & 0x1F;
    if (safe >= 16 && index < 128) safe = index & 0x0F; // Fallback for standard range
    // Handle extended palette mapping logic if needed
    return {PALETTE_RGB[safe][0], PALETTE_RGB[safe][1], PALETTE_RGB[safe][2]};
}

Real8Gfx::Real8Gfx(Real8VM* vm_instance) : vm(vm_instance) {
    //init();
}

Real8Gfx::~Real8Gfx() {}

void Real8Gfx::init() {
    reset();
}

void Real8Gfx::reset() {
    cam_x = 0; cam_y = 0;
    clip_x = 0; clip_y = 0; 
    clip_w = Real8VM::WIDTH; clip_h = Real8VM::HEIGHT;
    
    pen_col = 6;
    cur_x = 0; cur_y = 0;
    draw_mask = 0;
    fillp(0);
    pal_reset();
    palt_reset();
}

void Real8Gfx::beginFrame() {
    objBatchAllowed = true;
    objBatchActive = false;
    objSpriteCount = 0;
#if defined(__GBA__) && REAL8_GBA_SPRITE_BASE_CACHE
    sprite_base_cache_valid = false;
#endif
    if (vm && vm->host) vm->host->beginFrame();
}

// --- Helpers ---

namespace {
struct SpriteChunkLut {
    uint16_t expand[256];
    uint8_t mask[256];
    uint8_t palette_cache[16];
    uint8_t palt_cache[16];
    bool valid = false;
};

static EWRAM_DATA SpriteChunkLut g_spriteChunkLut;

static inline void IWRAM_SPR_HELP_CODE updateSpriteChunkLut(const uint8_t* palette_map, const bool* palt_map) {
    bool same = g_spriteChunkLut.valid;
    for (int i = 0; i < 16 && same; ++i) {
        const uint8_t palt_val = palt_map[i] ? 1u : 0u;
        if (g_spriteChunkLut.palette_cache[i] != palette_map[i] || g_spriteChunkLut.palt_cache[i] != palt_val) {
            same = false;
        }
    }
    if (same) return;

    for (int i = 0; i < 16; ++i) {
        g_spriteChunkLut.palette_cache[i] = palette_map[i];
        g_spriteChunkLut.palt_cache[i] = palt_map[i] ? 1u : 0u;
    }
    for (int i = 0; i < 256; ++i) {
        uint8_t lo = (uint8_t)(i & 0x0F);
        uint8_t hi = (uint8_t)(i >> 4);
        uint8_t mapped_lo = palette_map[lo];
        uint8_t mapped_hi = palette_map[hi];
        g_spriteChunkLut.expand[i] = (uint16_t)(mapped_lo | (mapped_hi << 8));
        uint8_t mask = 0;
        if (palt_map[lo]) mask |= 1u;
        if (palt_map[hi]) mask |= 2u;
        g_spriteChunkLut.mask[i] = mask;
    }
    g_spriteChunkLut.valid = true;
}

static inline void IWRAM_CODE store_opaque_chunk(uint8_t* dst, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3) {
    const uintptr_t addr = (uintptr_t)dst;
    if ((addr & 3u) == 0) {
        uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
        dst32[0] = (uint32_t)p0 | ((uint32_t)p1 << 16);
        dst32[1] = (uint32_t)p2 | ((uint32_t)p3 << 16);
    } else if ((addr & 1u) == 0) {
        uint16_t* dst16 = reinterpret_cast<uint16_t*>(dst);
        dst16[0] = p0;
        dst16[1] = p1;
        dst16[2] = p2;
        dst16[3] = p3;
    } else {
        dst[0] = (uint8_t)p0; dst[1] = (uint8_t)(p0 >> 8);
        dst[2] = (uint8_t)p1; dst[3] = (uint8_t)(p1 >> 8);
        dst[4] = (uint8_t)p2; dst[5] = (uint8_t)(p2 >> 8);
        dst[6] = (uint8_t)p3; dst[7] = (uint8_t)(p3 >> 8);
    }
}
} // namespace

static inline int isqrt_int(int v) {
    if (v <= 0) return 0;
    int res = 0;
    int bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

uint32_t IWRAM_SPR_HELP_CODE Real8Gfx::sprite_base_addr() const {
    // Some builds/configs set spriteSheetMemMapping to 0x60 (sprites at 0x6000),
    // but not all carts/ports keep a mirrored copy of sprite data in that region.
    // If 0x6000 looks empty, fall back to the canonical PICO-8 sprite sheet base (0x0000).
    if (!vm || !vm->ram) return 0x0000;

    const uint8_t mapping = vm->hwState.spriteSheetMemMapping;
#if REAL8_GBA_SPRITE_BASE_CACHE
    if (sprite_base_cache_valid && sprite_base_cache_mapping == mapping) {
        return sprite_base_cache;
    }
#endif

    uint32_t base = 0x0000;
    if (mapping == 0x60) {
        // Cheap heuristic: sample a small window. If everything is 0, assume not mirrored.
        const uint8_t* r = vm->ram;
        bool any6000 = false;
        for (int i = 0; i < 64; ++i) {
            if (r[0x6000 + i] != 0) { any6000 = true; break; }
        }
        if (any6000) {
            base = 0x6000;
        } else {
            // If 0x0000 has data, use it.
            bool any0 = false;
            for (int i = 0; i < 64; ++i) {
                if (r[i] != 0) { any0 = true; break; }
            }
            // Both look empty; default to 0x6000 to preserve mapping intent.
            base = any0 ? 0x0000 : 0x6000;
        }
    } else {
        base = 0x0000;
    }
#if REAL8_GBA_SPRITE_BASE_CACHE
    sprite_base_cache_mapping = mapping;
    sprite_base_cache = base;
    sprite_base_cache_valid = true;
#endif
    return base;
}


uint8_t IWRAM_GETPIX_CODE Real8Gfx::get_pixel_ram(uint32_t base_addr, int x, int y) {
    if (!vm->ram || x < 0 || x > 127 || y < 0 || y > 127) return 0;
    uint32_t idx = base_addr + (y * 64) + (x >> 1);
    uint8_t val = vm->ram[idx];
    return (x & 1) ? (val >> 4) : (val & 0x0F);
}

void Real8Gfx::set_pixel_ram(uint32_t base_addr, int x, int y, uint8_t color) {
    if (!vm->ram || x < 0 || x > 127 || y < 0 || y > 127) return;
    uint32_t idx = base_addr + (y * 64) + (x >> 1);
    uint8_t current = vm->ram[idx];
    uint8_t mask = (x & 1) ? 0x0F : 0xF0;
    uint8_t val = (x & 1) ? (color << 4) : (color & 0x0F);
    vm->ram[idx] = (current & mask) | val;
}

void Real8Gfx::get_screen_palette(uint8_t* out_palette) {
    if (vm->ram) {
        for (int i = 0; i < 16; i++) out_palette[i] = vm->ram[0x5F10 + i];
    } else {
        for (int i = 0; i < 16; i++) out_palette[i] = screen_palette[i];
    }
}

void Real8Gfx::updatePaletteFlags() {
    if (paletteStateDirty) {
        paletteIdentity = true;
        for (int i = 0; i < 16; ++i) {
            if (palette_map[i] != i) {
                paletteIdentity = false;
                break;
            }
        }
        paletteStateDirty = false;
    }
    if (paltStateDirty) {
        paltDefault = palt_map[0];
        if (paltDefault) {
            for (int i = 1; i < 16; ++i) {
                if (palt_map[i]) {
                    paltDefault = false;
                    break;
                }
            }
        }
        paltStateDirty = false;
    }
}

void IWRAM_OBJBATCH_CODE Real8Gfx::invalidateObjBatch() {
    if (!objBatchAllowed) return;
    if (objBatchActive) {
        if (vm && vm->host) vm->host->cancelSpriteBatch();
        objBatchAllowed = false;
        for (int i = 0; i < objSpriteCount; ++i) {
            const ObjSprite& s = objSprites[i];
            spr_fast(s.n, s.x, s.y, s.w, s.h, s.fx, s.fy);
        }
    }
    objBatchAllowed = false;
    objBatchActive = false;
    objSpriteCount = 0;
}

bool IWRAM_OBJBATCH_CODE Real8Gfx::tryQueueObjSprite(int n, int x, int y, int w, int h, bool fx, bool fy) {
    if (!objBatchAllowed || !vm || !vm->host || !vm->ram) return false;
    if (w != 1 || h != 1) return false;
    if (draw_mask != 0 || fillp_pattern != 0xFFFFFFFFu) return false;
    if (clip_x != 0 || clip_y != 0 || clip_w != Real8VM::WIDTH || clip_h != Real8VM::HEIGHT) return false;
    updatePaletteFlags();
    if (!paletteIdentity || !paltDefault) return false;
    if (n < 0 || n >= 256) return false;
    if (objSpriteCount >= kMaxObjSprites) return false;

    const uint8_t* spriteSheet = vm->ram + sprite_base_addr();
    const int sx = x - cam_x;
    const int sy = y - cam_y;
    if (!vm->host->queueSprite(spriteSheet, n, sx, sy, w, h, fx, fy)) return false;

    if (objSpriteCount == 0) {
        vm->mark_dirty_rect(0, 0, 0, 0);
    }

    objSprites[objSpriteCount++] = {n, x, y, w, h, fx, fy};
    objBatchActive = true;
    return true;
}

// --- Primitives Implementation ---

void IWRAM_PUTPIX_CODE Real8Gfx::put_pixel_raw(int x, int y, uint8_t col) {
    if (!vm->fb) return;
    if (x < 0 || x >= Real8VM::RAW_WIDTH || y < 0 || y >= Real8VM::HEIGHT) return;

    if (draw_mask != 0) {
        uint8_t old = vm->fb[y][x];
        uint8_t effective_mask = ((x & 1) == 0) ? (draw_mask & 0x0F) : ((draw_mask >> 4) & 0x0F);
        vm->fb[y][x] = (old & ~effective_mask) | ((col & 0x0F) & effective_mask);
    } else {
        vm->fb[y][x] = col & 0x0F;
    }
}

void IWRAM_PUTPIX_CODE Real8Gfx::put_pixel_checked(int x, int y, uint8_t col) {
    int sx = x - cam_x;
    int sy = y - cam_y;
    if (sx < clip_x || sy < clip_y || sx >= (clip_x + clip_w) || sy >= (clip_y + clip_h)) return;

    if (fillp_pattern != 0xFFFFFFFFu) {
        // PICO-8 fillp coordinates are screen relative
        int bit_index = ((sy & 3) << 2) | (sx & 3); 
        if (!((fillp_pattern >> (15 - bit_index)) & 1)) return; // Fixed fillp logic direction
    }
    
    put_pixel_raw(sx, sy, palette_map[col & 0x0F]);
}

void IWRAM_PSETGET_CODE Real8Gfx::pset(int x, int y, uint8_t col) { 
    invalidateObjBatch();
    int sx = x - cam_x;
    int sy = y - cam_y;
    if (sx < clip_x || sy < clip_y || sx >= (clip_x + clip_w) || sy >= (clip_y + clip_h)) return;

    if (fillp_pattern != 0xFFFFFFFFu) {
        int bit_index = ((sy & 3) << 2) | (sx & 3);
        if (!((fillp_pattern >> (15 - bit_index)) & 1)) return;
    }

    put_pixel_raw(sx, sy, palette_map[col & 0x0F]);
    if (vm && !vm->skipDirtyRect) vm->mark_dirty_rect(sx, sy, sx, sy);
}

uint8_t IWRAM_PSETGET_CODE Real8Gfx::pget(int x, int y) {
    // pget is relative to camera in PICO-8? No, pget(x,y) gets pixel at x,y relative to screen origin (0,0)
    // Actually, PICO-8 manual says pget is affected by camera.
    // However, most emulators impl pget as raw framebuffer access + camera offset.
    int rx = x - cam_x;
    int ry = y - cam_y;
    if (!vm->fb || rx < 0 || rx >= Real8VM::WIDTH || ry < 0 || ry >= Real8VM::HEIGHT) return 0;
    return vm->fb[ry][rx] & 0x0F;
}

void IWRAM_CLS_CODE Real8Gfx::cls(int c) {
    invalidateObjBatch();
    if (!vm->fb) return;
    uint8_t stored = c & 0x0F;
    std::memset(&vm->fb[0][0], stored, (size_t)Real8VM::RAW_WIDTH * Real8VM::HEIGHT);
    
    // Also update 0x6000 RAM mirror if available
    if (vm->screen_ram) {
        std::memset(vm->screen_ram, (stored << 4) | stored, 0x2000);
    }
    vm->mark_dirty_rect(0, 0, 127, 127);
    
    // PICO-8 cls resets cursor
    cur_x = 0; cur_y = 0;
}

// --- Shapes ---

// Clipping Helper
const int INSIDE = 0; const int LEFT = 1; const int RIGHT = 2; const int BOTTOM = 4; const int TOP = 8;
static int computeOutCode(int x, int y, int xmin, int ymin, int xmax, int ymax) {
    int code = INSIDE;
    if (x < xmin) code |= LEFT; else if (x > xmax) code |= RIGHT;
    if (y < ymin) code |= TOP; else if (y > ymax) code |= BOTTOM;
    return code;
}

void IWRAM_CODE Real8Gfx::line(int x0, int y0, int x1, int y1, uint8_t c) {
    invalidateObjBatch();
    int sx0 = x0 - cam_x; int sy0 = y0 - cam_y;
    int sx1 = x1 - cam_x; int sy1 = y1 - cam_y;
    int xmin = clip_x; int ymin = clip_y;
    int xmax = clip_x + clip_w - 1; int ymax = clip_y + clip_h - 1;

    // Cohen-Sutherland
    int code0 = computeOutCode(sx0, sy0, xmin, ymin, xmax, ymax);
    int code1 = computeOutCode(sx1, sy1, xmin, ymin, xmax, ymax);
    bool accept = false;

    while (true) {
        if (!(code0 | code1)) { accept = true; break; }
        else if (code0 & code1) { break; }
        else {
            int codeOut = code0 ? code0 : code1;
            int x, y;
            // Use long long to prevent integer overflow during clipping math
            // when coordinates are very large (common in "advanced" map rendering).
            if (codeOut & BOTTOM) { 
                long long num = (long long)(sx1 - sx0) * (ymax - sy0);
                long long den = (sy1 - sy0);
                x = sx0 + (int)(num / den); 
                y = ymax; 
            }
            else if (codeOut & TOP) { 
                long long num = (long long)(sx1 - sx0) * (ymin - sy0);
                long long den = (sy1 - sy0);
                x = sx0 + (int)(num / den); 
                y = ymin; 
            }
            else if (codeOut & RIGHT) { 
                long long num = (long long)(sy1 - sy0) * (xmax - sx0);
                long long den = (sx1 - sx0);
                y = sy0 + (int)(num / den); 
                x = xmax; 
            }
            else { 
                long long num = (long long)(sy1 - sy0) * (xmin - sx0);
                long long den = (sx1 - sx0);
                y = sy0 + (int)(num / den); 
                x = xmin; 
            }

            if (codeOut == code0) { sx0 = x; sy0 = y; code0 = computeOutCode(sx0, sy0, xmin, ymin, xmax, ymax); }
            else { sx1 = x; sy1 = y; code1 = computeOutCode(sx1, sy1, xmin, ymin, xmax, ymax); }
        }
    }

    if (!accept) return;

    int dirty_x0 = std::min(sx0, sx1);
    int dirty_y0 = std::min(sy0, sy1);
    int dirty_x1 = std::max(sx0, sx1);
    int dirty_y1 = std::max(sy0, sy1);
    if (vm) vm->mark_dirty_rect(dirty_x0, dirty_y0, dirty_x1, dirty_y1);

    if (fillp_pattern == 0xFFFFFFFFu && draw_mask == 0 && vm && vm->fb && sy0 == sy1) {
        int xStart = std::min(sx0, sx1);
        int xEnd = std::max(sx0, sx1);
        uint8_t mapped = palette_map[c & 0x0F];
        std::memset(&vm->fb[sy0][xStart], mapped, (size_t)(xEnd - xStart + 1));
        return;
    }

    REAL8_PROFILE_HOTSPOT(vm, Real8VM::kHotspotLineSlow);
    int dx = abs(sx1 - sx0), sx = sx0 < sx1 ? 1 : -1;
    int dy = -abs(sy1 - sy0), sy = sy0 < sy1 ? 1 : -1;
    int err = dx + dy;
    
    // Convert back to world coords for put_pixel_checked logic
    int drawX = sx0 + cam_x; 
    int drawY = sy0 + cam_y;
    int targetX = sx1 + cam_x;
    int targetY = sy1 + cam_y;

    while (true) {
        put_pixel_checked(drawX, drawY, c);
        if (drawX == targetX && drawY == targetY) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; drawX += sx; }
        if (e2 <= dx) { err += dx; drawY += sy; }
    }
}

void Real8Gfx::rect(int x0, int y0, int x1, int y1, uint8_t c) {
    invalidateObjBatch();
    line(x0, y0, x1, y0, c); line(x1, y0, x1, y1, c);
    line(x1, y1, x0, y1, c); line(x0, y1, x0, y0, c);
}

void IWRAM_CODE Real8Gfx::rectfill(int x0, int y0, int x1, int y1, uint8_t c) {
    invalidateObjBatch();
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    int sx0 = std::max(clip_x, x0 - cam_x);
    int sy0 = std::max(clip_y, y0 - cam_y);
    int sx1 = std::min(clip_x + clip_w - 1, x1 - cam_x);
    int sy1 = std::min(clip_y + clip_h - 1, y1 - cam_y);
    if (sx1 < sx0 || sy1 < sy0) return;
    
    uint8_t mapped = palette_map[c & 0x0F];

    if (fillp_pattern == 0xFFFFFFFFu && draw_mask == 0 && vm && vm->fb) {
        size_t rowCount = (size_t)(sx1 - sx0 + 1);
        for (int y = sy0; y <= sy1; ++y) {
            std::memset(&vm->fb[y][sx0], mapped, rowCount);
        }
        vm->mark_dirty_rect(sx0, sy0, sx1, sy1);
        return;
    }
    
    REAL8_PROFILE_HOTSPOT(vm, Real8VM::kHotspotRectfillSlow);
    for (int y = sy0; y <= sy1; ++y) {
        for (int x = sx0; x <= sx1; ++x) {
            if (fillp_pattern != 0xFFFFFFFFu) {
                // Fillp check
                int bit_index = ((y & 3) << 2) | (x & 3);
                if (!((fillp_pattern >> (15 - bit_index)) & 1u)) continue;
            }
            if (vm->fb) vm->fb[y][x] = mapped;
        }
    }
    vm->mark_dirty_rect(sx0, sy0, sx1, sy1);
}

static int clamp_rrect_radius(int r, int width, int height) {
    if (r < 0) r = 0;
    int max_r = std::min((width - 1) / 2, (height - 1) / 2);
    if (r > max_r) r = max_r;
    return r;
}

static void draw_rrect_corners(Real8Gfx* gfx, int x0, int y0, int x1, int y1, int r, uint8_t c) {
    int tlx = x0 + r, tly = y0 + r;
    int trx = x1 - r, try_ = y0 + r;
    int blx = x0 + r, bly = y1 - r;
    int brx = x1 - r, bry = y1 - r;

    int x = r, y = 0, err = 0;
    while (x >= y) {
        gfx->put_pixel_checked(tlx - x, tly - y, c);
        gfx->put_pixel_checked(tlx - y, tly - x, c);
        gfx->put_pixel_checked(trx + x, try_ - y, c);
        gfx->put_pixel_checked(trx + y, try_ - x, c);
        gfx->put_pixel_checked(blx - x, bly + y, c);
        gfx->put_pixel_checked(blx - y, bly + x, c);
        gfx->put_pixel_checked(brx + x, bry + y, c);
        gfx->put_pixel_checked(brx + y, bry + x, c);
        y++; err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

static void fill_rrect_corners(Real8Gfx* gfx, int x0, int y0, int x1, int y1, int r, uint8_t c) {
    int tlx = x0 + r, tly = y0 + r;
    int trx = x1 - r, try_ = y0 + r;
    int blx = x0 + r, bly = y1 - r;
    int brx = x1 - r, bry = y1 - r;
    int r2 = r * r;

    for (int dy = 0; dy <= r; ++dy) {
        int dx = isqrt_int(r2 - dy * dy);
        int y_top = tly - dy;
        int y_bot = bly + dy;

        for (int x = tlx - dx; x <= tlx; ++x) gfx->put_pixel_checked(x, y_top, c);
        for (int x = trx; x <= trx + dx; ++x) gfx->put_pixel_checked(x, y_top, c);
        for (int x = blx - dx; x <= blx; ++x) gfx->put_pixel_checked(x, y_bot, c);
        for (int x = brx; x <= brx + dx; ++x) gfx->put_pixel_checked(x, y_bot, c);
    }
}

void Real8Gfx::rrect(int x, int y, int w, int h, int r, uint8_t c) {
    invalidateObjBatch();
    if (w <= 0 || h <= 0) return;
    int x0 = x, y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    int width = x1 - x0 + 1;
    int height = y1 - y0 + 1;
    int radius = clamp_rrect_radius(r, width, height);
    if (radius <= 0) {
        rect(x0, y0, x1, y1, c);
        return;
    }

    int sx0 = x0 - cam_x;
    int sy0 = y0 - cam_y;
    int sx1 = x1 - cam_x;
    int sy1 = y1 - cam_y;
    int dx0 = std::max(clip_x, sx0);
    int dy0 = std::max(clip_y, sy0);
    int dx1 = std::min(clip_x + clip_w - 1, sx1);
    int dy1 = std::min(clip_y + clip_h - 1, sy1);
    if (vm && dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);

    line(x0 + radius, y0, x1 - radius, y0, c);
    line(x0 + radius, y1, x1 - radius, y1, c);
    line(x0, y0 + radius, x0, y1 - radius, c);
    line(x1, y0 + radius, x1, y1 - radius, c);
    draw_rrect_corners(this, x0, y0, x1, y1, radius, c);
}

void Real8Gfx::rrectfill(int x, int y, int w, int h, int r, uint8_t c) {
    invalidateObjBatch();
    if (w <= 0 || h <= 0) return;
    int x0 = x, y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    int width = x1 - x0 + 1;
    int height = y1 - y0 + 1;
    int radius = clamp_rrect_radius(r, width, height);
    if (radius <= 0) {
        rectfill(x0, y0, x1, y1, c);
        return;
    }

    int sx0 = x0 - cam_x;
    int sy0 = y0 - cam_y;
    int sx1 = x1 - cam_x;
    int sy1 = y1 - cam_y;
    int dx0 = std::max(clip_x, sx0);
    int dy0 = std::max(clip_y, sy0);
    int dx1 = std::min(clip_x + clip_w - 1, sx1);
    int dy1 = std::min(clip_y + clip_h - 1, sy1);
    if (vm && dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);

    int inner_x0 = x0 + radius;
    int inner_x1 = x1 - radius;
    if (inner_x0 <= inner_x1) rectfill(inner_x0, y0, inner_x1, y1, c);

    int side_y0 = y0 + radius;
    int side_y1 = y1 - radius;
    if (side_y0 <= side_y1) {
        rectfill(x0, side_y0, x0 + radius - 1, side_y1, c);
        rectfill(x1 - radius + 1, side_y0, x1, side_y1, c);
    }

    fill_rrect_corners(this, x0, y0, x1, y1, radius, c);
}

void IWRAM_CIRC_CODE Real8Gfx::circ(int cx, int cy, int r, uint8_t c) {
    invalidateObjBatch();
    int sx0 = cx - r - cam_x;
    int sy0 = cy - r - cam_y;
    int sx1 = cx + r - cam_x;
    int sy1 = cy + r - cam_y;
    int dx0 = std::max(clip_x, sx0);
    int dy0 = std::max(clip_y, sy0);
    int dx1 = std::min(clip_x + clip_w - 1, sx1);
    int dy1 = std::min(clip_y + clip_h - 1, sy1);
    if (vm && dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);

    int x = r, y = 0, err = 0;
    while (x >= y) {
        put_pixel_checked(cx + x, cy + y, c); put_pixel_checked(cx + y, cy + x, c);
        put_pixel_checked(cx - y, cy + x, c); put_pixel_checked(cx - x, cy + y, c);
        put_pixel_checked(cx - x, cy - y, c); put_pixel_checked(cx - y, cy - x, c);
        put_pixel_checked(cx + y, cy - x, c); put_pixel_checked(cx + x, cy - y, c);
        y++; err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

void IWRAM_CIRC_CODE Real8Gfx::circfill(int cx, int cy, int r, uint8_t c) {
    invalidateObjBatch();
    int sx0 = cx - r - cam_x;
    int sy0 = cy - r - cam_y;
    int sx1 = cx + r - cam_x;
    int sy1 = cy + r - cam_y;
    int dx0 = std::max(clip_x, sx0);
    int dy0 = std::max(clip_y, sy0);
    int dx1 = std::min(clip_x + clip_w - 1, sx1);
    int dy1 = std::min(clip_y + clip_h - 1, sy1);
    if (vm && dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);

    int x = r, y = 0, err = 0;
    while (x >= y) {
        for (int xi = cx - x; xi <= cx + x; ++xi) put_pixel_checked(xi, cy + y, c);
        for (int xi = cx - x; xi <= cx + x; ++xi) put_pixel_checked(xi, cy - y, c);
        for (int xi = cx - y; xi <= cx + y; ++xi) put_pixel_checked(xi, cy + x, c);
        for (int xi = cx - y; xi <= cx + y; ++xi) put_pixel_checked(xi, cy - x, c);
        y++; err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

// --- Sprites ---

void IWRAM_CODE Real8Gfx::spr_fast(int n, int x, int y, int w, int h, bool fx, bool fy) {
    if (!vm->ram || !vm->fb) return;
    int sx = x - cam_x; int sy = y - cam_y;
    int x0 = std::max(clip_x, sx); int y0 = std::max(clip_y, sy);
    int x1 = std::min(clip_x + clip_w, sx + (w * 8));
    int y1 = std::min(clip_y + clip_h, sy + (h * 8));
    if (x0 >= x1 || y0 >= y1) return;
    vm->mark_dirty_rect(x0, y0, x1 - 1, y1 - 1);

    int sheet_base_x = (n % 16) * 8;
    int sheet_base_y = (n / 16) * 8;
    uint32_t sprite_base = sprite_base_addr();
    // Chunked sprite blit reduces per-pixel branching on masked sprites.
    const bool use_chunked = (!fx && fillp_pattern == 0xFFFFFFFFu);
    if (use_chunked) {
        updateSpriteChunkLut(palette_map, palt_map);
    }

    for (int cy = y0; cy < y1; cy++) {
        int spy = cy - sy; if (fy) spy = (h * 8) - 1 - spy;
        int sheet_y = sheet_base_y + spy;
        uint32_t row_addr = sheet_y * 64; 
        if (use_chunked) {
            int dst_x = x0;
            int src_x = sheet_base_x + (dst_x - sx);
            uint8_t* dest_ptr = &vm->fb[cy][dst_x];

            if (src_x & 1) {
                uint32_t addr = sprite_base + row_addr + (src_x >> 1);
                if (addr < 0x8000) {
                    uint8_t byte = vm->ram[addr];
                    uint8_t col = (src_x & 1) ? (byte >> 4) : (byte & 0x0F);
                    if (!palt_map[col]) dest_ptr[0] = palette_map[col];
                }
                ++dst_x;
                ++src_x;
                ++dest_ptr;
            }

            for (; dst_x + 7 < x1; dst_x += 8, src_x += 8, dest_ptr += 8) {
                uint32_t addr = sprite_base + row_addr + (src_x >> 1);
                if (addr + 3 >= 0x8000) {
                    for (int i = 0; i < 8; ++i) {
                        uint8_t col = get_pixel_ram(sprite_base, src_x + i, sheet_y);
                        if (!palt_map[col]) dest_ptr[i] = palette_map[col];
                    }
                    continue;
                }

                uint8_t b0 = vm->ram[addr];
                uint8_t b1 = vm->ram[addr + 1];
                uint8_t b2 = vm->ram[addr + 2];
                uint8_t b3 = vm->ram[addr + 3];
                uint8_t mask = (uint8_t)(g_spriteChunkLut.mask[b0]
                    | (g_spriteChunkLut.mask[b1] << 2)
                    | (g_spriteChunkLut.mask[b2] << 4)
                    | (g_spriteChunkLut.mask[b3] << 6));

                if (mask == 0xFF) continue;
                if (mask == 0x00) {
                    uint16_t p0 = g_spriteChunkLut.expand[b0];
                    uint16_t p1 = g_spriteChunkLut.expand[b1];
                    uint16_t p2 = g_spriteChunkLut.expand[b2];
                    uint16_t p3 = g_spriteChunkLut.expand[b3];
                    store_opaque_chunk(dest_ptr, p0, p1, p2, p3);
                } else {
                    uint8_t bytes[4] = {b0, b1, b2, b3};
                    for (int i = 0; i < 8; ++i) {
                        uint8_t col = (i & 1) ? (bytes[i >> 1] >> 4) : (bytes[i >> 1] & 0x0F);
                        if (!palt_map[col]) dest_ptr[i] = palette_map[col];
                    }
                }
            }

            for (; dst_x < x1; ++dst_x, ++src_x, ++dest_ptr) {
                uint32_t addr = sprite_base + row_addr + (src_x >> 1);
                if (addr < 0x8000) {
                    uint8_t byte = vm->ram[addr];
                    uint8_t col = (src_x & 1) ? (byte >> 4) : (byte & 0x0F);
                    if (!palt_map[col]) dest_ptr[0] = palette_map[col];
                }
            }
        } else {
            uint8_t* dest_ptr = &vm->fb[cy][x0];
            for (int cx = x0; cx < x1; cx++) {
                int spx = cx - sx; if (fx) spx = (w * 8) - 1 - spx;
                int sheet_x = sheet_base_x + spx;
                uint32_t addr = sprite_base + row_addr + (sheet_x >> 1);
                if (addr < 0x8000) {
                    uint8_t byte = vm->ram[addr];
                    uint8_t col = (sheet_x & 1) ? (byte >> 4) : (byte & 0x0F);
                    if (!palt_map[col]) *dest_ptr = palette_map[col];
                }
                dest_ptr++;
            }
        }
    }
}

void IWRAM_SPR_CODE Real8Gfx::spr(int n, int x, int y, int w, int h, bool fx, bool fy) {
    if (tryQueueObjSprite(n, x, y, w, h, fx, fy)) return;
    invalidateObjBatch();
    if (draw_mask == 0) { spr_fast(n, x, y, w, h, fx, fy); return; }

    REAL8_PROFILE_HOTSPOT(vm, Real8VM::kHotspotSprMasked);
    int dx0 = std::max(clip_x, x - cam_x);
    int dy0 = std::max(clip_y, y - cam_y);
    int dx1 = std::min(clip_x + clip_w - 1, (x - cam_x) + w * 8 - 1);
    int dy1 = std::min(clip_y + clip_h - 1, (y - cam_y) + h * 8 - 1);
    if (dx1 < dx0 || dy1 < dy0) return;
    uint32_t sprite_base = sprite_base_addr();

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            int current_tx = fx ? (w - 1 - tx) : tx;
            int current_ty = fy ? (h - 1 - ty) : ty;
            int idx = n + current_tx + current_ty * 16;
            int base_x = (idx % 16) * 8; int base_y = (idx / 16) * 8;
            int draw_y = (y - cam_y) + ty * 8; int draw_x = (x - cam_x) + tx * 8;

            for (int py = 0; py < 8; ++py) {
                int sy = (fy ? 7 - py : py); int dst_y = draw_y + py;
                if (dst_y < dy0 || dst_y > dy1) continue;
                for (int px = 0; px < 8; ++px) {
                    int sx = (fx ? 7 - px : px); int dst_x = draw_x + px;
                    if (dst_x < dx0 || dst_x > dx1) continue;
                    uint8_t col = get_pixel_ram(sprite_base, base_x + sx, base_y + sy);
                    if (!palt_map[col]) put_pixel_raw(dst_x, dst_y, palette_map[col]);
                }
            }
        }
    }
    vm->mark_dirty_rect(dx0, dy0, dx1, dy1);
}

void IWRAM_SSPR_CODE Real8Gfx::sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x, bool flip_y) {
    invalidateObjBatch();
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !vm->fb) return;
    REAL8_PROFILE_HOTSPOT(vm, Real8VM::kHotspotSspr);
    // 1x tile-aligned sspr can reuse the spr fast path.
    if (draw_mask == 0 && !flip_x && !flip_y && sw == dw && sh == dh
        && ((sx | sy | sw | sh) & 7) == 0) {
        int tile_x = sx >> 3;
        int tile_y = sy >> 3;
        int n = tile_y * 16 + tile_x;
        int w_tiles = sw >> 3;
        int h_tiles = sh >> 3;
        spr(n, dx, dy, w_tiles, h_tiles, false, false);
        return;
    }
    int screen_dx = dx - cam_x; int screen_dy = dy - cam_y;
    uint32_t step_u = ((uint32_t)sw << 16) / dw;
    uint32_t step_v = ((uint32_t)sh << 16) / dh;
    int min_y = clip_y; int max_y = clip_y + clip_h;
    int min_x = clip_x; int max_x = clip_x + clip_w;
    int dirty_x0 = std::max(min_x, screen_dx);
    int dirty_y0 = std::max(min_y, screen_dy);
    int dirty_x1 = std::min(max_x - 1, screen_dx + dw - 1);
    int dirty_y1 = std::min(max_y - 1, screen_dy + dh - 1);
    if (dirty_x1 < dirty_x0 || dirty_y1 < dirty_y0) return;
    if (vm) vm->mark_dirty_rect(dirty_x0, dirty_y0, dirty_x1, dirty_y1);
    uint32_t sprite_base = sprite_base_addr();

    for (int yy = 0; yy < dh; ++yy) {
        int dst_y = screen_dy + yy;
        if (dst_y < min_y || dst_y >= max_y) continue;
        int v_int = (yy * step_v) >> 16; if (flip_y) v_int = sh - 1 - v_int;
        if (v_int < 0) v_int = 0; if (v_int >= sh) v_int = sh - 1;
        int srcy = sy + v_int;

        uint32_t u = 0;
        for (int xx = 0; xx < dw; ++xx) {
            int dst_x = screen_dx + xx;
            if (dst_x >= min_x && dst_x < max_x) {
                int u_int = u >> 16; if (flip_x) u_int = sw - 1 - u_int;
                if (u_int < 0) u_int = 0; if (u_int >= sw) u_int = sw - 1;
                int srcx = sx + u_int;
                uint8_t c = get_pixel_ram(sprite_base, srcx, srcy);
                if (!palt_map[c]) {
                    vm->fb[dst_y][dst_x] = palette_map[c];
                }
            }
            u += step_u;
        }
    }
}

// --- Map ---

uint8_t Real8Gfx::sget(int x, int y) {
    return get_pixel_ram(sprite_base_addr(), x, y);
}

void Real8Gfx::sset(int x, int y, uint8_t v) {
    invalidateObjBatch();
    uint32_t base = sprite_base_addr();
    set_pixel_ram(base, x, y, v);
    if (base == 0x6000 && vm->ram) {
        uint32_t idx = base + (y * 64) + (x >> 1);
        if (idx < 0x8000) vm->screenByteToFB(idx - 0x6000, vm->ram[idx]);
    }
}

uint8_t IWRAM_MAP_CODE Real8Gfx::mget(int x, int y) {
    if (!vm->ram) return 0;

    const bool bigMap = vm->hwState.mapMemMapping >= 0x80;
    int mapSize = bigMap ? (0x10000 - (vm->hwState.mapMemMapping << 8)) : 8192;
    const int mapW = (vm->hwState.widthOfTheMap == 0) ? 256 : vm->hwState.widthOfTheMap;
    if (mapW <= 0) return 0;
    int mapH = mapSize / mapW;

    if (x < 0 || y < 0 || x >= mapW || y >= mapH) return 0;
    int idx = y * mapW + x;

    if (bigMap) {
        const int userDataSize = 0x8000 - 0x4300;
        if (mapSize > userDataSize) mapSize = userDataSize;
        mapH = mapSize / mapW;
        if (x < 0 || y < 0 || x >= mapW || y >= mapH) return 0;
        idx = y * mapW + x;
        int offset = 0x8000 - mapSize;
        if (offset < 0x4300) offset = 0x4300;
        if (idx < 0 || idx >= mapSize) return 0;
        return vm->ram[offset + idx];
    }

    if (idx < 4096) return vm->ram[0x2000 + idx];
    if (idx < 8192) return vm->ram[0x1000 + (idx - 4096)];
    return 0;
}

void Real8Gfx::mset(int x, int y, uint8_t v) {
    if (!vm->ram) return;

    const bool bigMap = vm->hwState.mapMemMapping >= 0x80;
    int mapSize = bigMap ? (0x10000 - (vm->hwState.mapMemMapping << 8)) : 8192;
    const int mapW = (vm->hwState.widthOfTheMap == 0) ? 256 : vm->hwState.widthOfTheMap;
    if (mapW <= 0) return;
    int mapH = mapSize / mapW;

    if (x < 0 || y < 0 || x >= mapW || y >= mapH) return;
    int idx = y * mapW + x;

    if (bigMap) {
        const int userDataSize = 0x8000 - 0x4300;
        if (mapSize > userDataSize) mapSize = userDataSize;
        mapH = mapSize / mapW;
        if (x < 0 || y < 0 || x >= mapW || y >= mapH) return;
        idx = y * mapW + x;
        int offset = 0x8000 - mapSize;
        if (offset < 0x4300) offset = 0x4300;
        if (idx < 0 || idx >= mapSize) return;
        vm->ram[offset + idx] = v;
        return;
    }

    if (idx < 4096) vm->ram[0x2000 + idx] = v;
    else if (idx < 8192) vm->ram[0x1000 + (idx - 4096)] = v;
}

void IWRAM_MAP_CODE Real8Gfx::map(int mx, int my, int sx, int sy, int w, int h, int layer) {
    if (!vm->ram) return;
    int sx0 = sx - cam_x;
    int sy0 = sy - cam_y;
    int sx1 = sx0 + (w * 8) - 1;
    int sy1 = sy0 + (h * 8) - 1;
    int dx0 = std::max(clip_x, sx0);
    int dy0 = std::max(clip_y, sy0);
    int dx1 = std::min(clip_x + clip_w - 1, sx1);
    int dy1 = std::min(clip_y + clip_h - 1, sy1);
    if (vm && dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            uint8_t tile = mget(mx + i, my + j);
            if (tile == 0) continue;
            if (layer != -1 && (vm->sprite_flags[tile] & layer) == 0) continue;
            spr(tile, sx + i * 8, sy + j * 8);
        }
    }
}

// --- State ---

void Real8Gfx::camera(int x, int y) {
    invalidateObjBatch();
    cam_x = x;
    cam_y = y;
}
void Real8Gfx::clip(int x, int y, int w, int h) {
    invalidateObjBatch();
    clip_x = x;
    clip_y = y;
    clip_w = w;
    clip_h = h;
}
void Real8Gfx::color(uint8_t col) { pen_col = col & 0x0F; }
void Real8Gfx::fillp(uint32_t pattern) { fillp_pattern = pattern ? (pattern & 0xFFFFu) : 0xFFFFFFFFu; }

void Real8Gfx::pal(int a, int b, int p) {
    invalidateObjBatch();
    if (p == 1) {
        screen_palette[a & 0xFF] = b & 0xFF;
    } else {
        // Do not mask 'b' with 0x0F. 
        // PICO-8 allows mapping to colors 128+ (extended palette) in the draw state.
        // While the screen is 4-bit, the internal state should preserve the full byte 
        // until the draw call decides how to handle it (or for 'pget' consistency).
        palette_map[a & 0x0F] = b; // Removed & 0x0F
    }
    paletteStateDirty = true;
}

void Real8Gfx::pal_reset() {
    invalidateObjBatch();
    for (int i = 0; i < 16; i++) palette_map[i] = i;
    for (int i = 0; i < 256; i++) screen_palette[i] = i & 0x0F;
    palt_reset();
    paletteIdentity = true;
    paletteStateDirty = false;
    if (vm->ram) {
        for (int i = 0; i < 16; i++) {
            vm->ram[0x5F00 + i] = i; vm->ram[0x5F10 + i] = i;
        }
    }
}

void Real8Gfx::palt(int c, bool t) {
    invalidateObjBatch();
    palt_map[c & 0x0F] = t;
    paltStateDirty = true;
}
void Real8Gfx::palt_reset() {
    invalidateObjBatch();
    for (int i = 0; i < 16; i++) palt_map[i] = false;
    palt_map[0] = true;
    paltDefault = true;
    paltStateDirty = false;
}

// --- Text ---

void Real8Gfx::put_bitrow_1bpp(int x, int y, uint8_t bits, int w, uint8_t col) {
    for (int i = 0; i < w; i++) if (bits & (0x80 >> i)) put_pixel_checked(x + i, y, col);
}

int Real8Gfx::draw_char_default(uint8_t p8, int x, int y, uint8_t col) {
    if (use_menu_font) {
        const uint8_t *rows = p8_5x6_bits(p8);
        for (int r = 0; r < 6; r++) put_bitrow_1bpp(x, y + r, rows[r], 4, col);
        return 5;
    } else {
        const uint8_t *rows = p8_4x6_bits(p8);
        for (int r = 0; r < 6; r++) put_bitrow_1bpp(x, y + r, rows[r], 4, col);
        return 4;
    }
}

int Real8Gfx::draw_char_custom(uint8_t p8, int x, int y, uint8_t col) {
    uint8_t *a = vm->cf_attr();
    int wdef = (p8 < 128) ? a[0x000] : a[0x001];
    int h = a[0x002], xo = (int8_t)a[0x003], yo = (int8_t)a[0x004];
    if (h == 0) return draw_char_default(p8, x, y, col);

    int adj = 0, yup = 0;
    if (p8 >= 16) {
        uint8_t nib = vm->cf_adj()[(p8 - 16) >> 1];
        nib = (p8 & 1) ? (nib >> 4) : (nib & 0x0F);
        static const int8_t map[8] = {0, 1, 2, 3, -4, -3, -2, -1};
        adj = map[nib & 7];
        if (nib & 8) yup = 1;
    }
    
    // Correct bounds checking for Custom Font
    // The buffer is 0x800 bytes. The graphics start at +0x80.
    // So valid data is only up to index 0x780 (1920). 
    // Character 255 * 8 = 2040, which overflows!
    
    int offset = (int)p8 * 8;
    
    // Available space for graphics is Total(0x800) - Header(0x80) = 0x780
    if (offset + 8 > 0x780) return 0; // Prevent OOB Read

    const uint8_t *g = vm->cf_gfx() + offset;
    int draw_h = std::min(8, h);
    for (int r = 0; r < draw_h; r++) put_bitrow_1bpp(x + xo, y + yo + yup + r, g[r], 8, col);
    int adv = std::max(0, wdef + adj);
    return adv > 0 ? adv : wdef;
}

int Real8Gfx::pprint(const char *s, int len, int x, int y, uint8_t col) {
    invalidateObjBatch();
    int cx = x, cy = y;
    bool drew_any = false;
    int min_x = 0, min_y = 0, max_x = -1, max_y = -1;
    for (int i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)s[i];
        if (ch == '\n') { cy += 6; cx = x; continue; }
        if (ch == '\r') { cx = x; continue; }
        if (ch == '\t') { cx = ((cx - x + 16) / 16) * 16 + x; continue; }
        if (ch == '\b') { cx = std::max(x, cx - 5); continue; }
        if (ch < 16) {
            if (ch == 14) use_alt_font = true;
            else if (ch == 15) use_alt_font = false;
            continue;
        }
        int adv = use_alt_font ? draw_char_custom(ch, cx, cy, col) : draw_char_default(ch, cx, cy, col);
        if (adv > 0) {
            int x0 = cx;
            int y0 = cy;
            int x1 = cx + adv - 1;
            int y1 = cy + 5;
            if (use_alt_font && vm) {
                uint8_t *a = vm->cf_attr();
                int h = a[0x002];
                int xo = (int8_t)a[0x003];
                int yo = (int8_t)a[0x004];
                int draw_h = std::min(8, h);
                if (draw_h <= 0) draw_h = 6;
                x0 = cx + xo;
                y0 = cy + yo;
                x1 = x0 + 7;
                y1 = y0 + draw_h - 1;
            }
            if (!drew_any) {
                min_x = x0; min_y = y0; max_x = x1; max_y = y1;
                drew_any = true;
            } else {
                if (x0 < min_x) min_x = x0;
                if (y0 < min_y) min_y = y0;
                if (x1 > max_x) max_x = x1;
                if (y1 > max_y) max_y = y1;
            }
        }
        cx += adv;
    }
    if (drew_any && vm) {
        int sx0 = min_x - cam_x;
        int sy0 = min_y - cam_y;
        int sx1 = max_x - cam_x;
        int sy1 = max_y - cam_y;
        int dx0 = std::max(clip_x, sx0);
        int dy0 = std::max(clip_y, sy0);
        int dx1 = std::min(clip_x + clip_w - 1, sx1);
        int dy1 = std::min(clip_y + clip_h - 1, sy1);
        if (dx0 <= dx1 && dy0 <= dy1) vm->mark_dirty_rect(dx0, dy0, dx1, dy1);
    }
    return cx;
}

void Real8Gfx::renderMessage(const char *header, std::string msg, int color) {
    bool old_menu = use_menu_font;
    use_menu_font = true;
    cls(0);
    rectfill(0, 50, 127, 75, color);
    int hX = 64 - ((strlen(header) * 5) / 2);
    pprint(header, strlen(header), hX, 55, 7);
    int mX = 64 - ((msg.length() * 5) / 2);
    pprint(msg.c_str(), msg.length(), mX, 65, 7);
    use_menu_font = old_menu;
}

void Real8Gfx::saveState(GfxState& out) {
    out.cam_x = cam_x; out.cam_y = cam_y;
    out.clip_x = clip_x; out.clip_y = clip_y; 
    out.clip_w = clip_w; out.clip_h = clip_h;
    out.pen_col = pen_col;
    out.draw_mask = draw_mask;
    out.fillp_pattern = fillp_pattern;
    
    std::memcpy(out.palette_map, palette_map, 16);
    std::memcpy(out.screen_palette, screen_palette, 256);
    std::memcpy(out.palt_map, palt_map, 16);
}

void Real8Gfx::restoreState(const GfxState& in) {
    cam_x = in.cam_x; cam_y = in.cam_y;
    clip_x = in.clip_x; clip_y = in.clip_y;
    clip_w = in.clip_w; clip_h = in.clip_h;
    pen_col = in.pen_col;
    draw_mask = in.draw_mask;
    fillp_pattern = in.fillp_pattern;

    std::memcpy(palette_map, in.palette_map, 16);
    std::memcpy(screen_palette, in.screen_palette, 256);
    std::memcpy(palt_map, in.palt_map, 16);
    paletteStateDirty = true;
    paltStateDirty = true;

    // Sync back to RAM if VM is attached (Crucial for game logic that reads RAM)
    if (vm && vm->ram) {
        // Sync Draw Palette (0x5F00)
        for (int i = 0; i < 16; i++) vm->ram[0x5F00 + i] = palette_map[i];
        // Sync Screen Palette (0x5F10)
        for (int i = 0; i < 16; i++) vm->ram[0x5F10 + i] = screen_palette[i];
        // Sync Camera
        vm->ram[0x5F28] = (uint8_t)(cam_x & 0xFF); vm->ram[0x5F29] = (uint8_t)((cam_x >> 8) & 0xFF);
        vm->ram[0x5F2A] = (uint8_t)(cam_y & 0xFF); vm->ram[0x5F2B] = (uint8_t)((cam_y >> 8) & 0xFF);
        // Sync Clip
        vm->ram[0x5F20] = (uint8_t)clip_x; vm->ram[0x5F21] = (uint8_t)clip_y;
        vm->ram[0x5F22] = (uint8_t)(clip_x + clip_w); vm->ram[0x5F23] = (uint8_t)(clip_y + clip_h);
    }
}
