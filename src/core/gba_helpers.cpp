#include "real8_vm.h"

#include <cstdio>
#include <cstring>

#if defined(__GBA__) && !defined(REAL8_GBA_FAST_LUA)
#define REAL8_GBA_FAST_LUA 1
#endif

#if REAL8_PROFILE_ENABLED && defined(__GBA__)
#ifndef REG_TM2CNT_L
#define REG_TM2CNT_L *(volatile uint16_t*)(0x04000108)
#define REG_TM2CNT_H *(volatile uint16_t*)(0x0400010A)
#define REG_TM3CNT_L *(volatile uint16_t*)(0x0400010C)
#define REG_TM3CNT_H *(volatile uint16_t*)(0x0400010E)
#endif
#ifndef TIMER_ENABLE
#define TIMER_ENABLE (1u << 7)
#endif
#ifndef TIMER_CASCADE
#define TIMER_CASCADE (1u << 2)
#endif
#ifndef TIMER_DIV_1
#define TIMER_DIV_1 0x0000
#endif

namespace {
    bool g_profileTimerInit = false;

    inline void profileInitTimer() {
        if (g_profileTimerInit) return;
        if (REG_TM2CNT_H & TIMER_ENABLE) {
            g_profileTimerInit = true;
            return;
        }
        REG_TM2CNT_H = 0;
        REG_TM3CNT_H = 0;
        REG_TM2CNT_L = 0;
        REG_TM3CNT_L = 0;
        REG_TM2CNT_H = TIMER_ENABLE | TIMER_DIV_1;
        REG_TM3CNT_H = TIMER_ENABLE | TIMER_CASCADE;
        g_profileTimerInit = true;
    }

    inline uint32_t profileReadCycles() {
        profileInitTimer();
        uint32_t high1 = REG_TM3CNT_L;
        uint32_t low = REG_TM2CNT_L;
        uint32_t high2 = REG_TM3CNT_L;
        if (high1 != high2) {
            low = REG_TM2CNT_L;
            high1 = high2;
        }
        return (high1 << 16) | low;
    }
}
#endif

void Real8VM::renderProfileOverlay() {
#if REAL8_PROFILE_ENABLED
    if (!showStats || !isGbaPlatform || profile_last_frame_cycles == 0) {
        return;
    }

    int bk_cx = gpu.cam_x, bk_cy = gpu.cam_y;
    int bk_clip_x = gpu.clip_x, bk_clip_y = gpu.clip_y;
    int bk_clip_w = gpu.clip_w, bk_clip_h = gpu.clip_h;
    uint8_t bk_pen = gpu.getPen();
    gpu.camera(0, 0); gpu.clip(0, 0, WIDTH, HEIGHT);

    const int line_h = 6;
    const int box_h = (line_h * 8) + 2;
    gpu.rectfill(0, 0, 127, box_h - 1, 0);

    auto to_us = [](uint32_t cycles) -> uint32_t {
        return (uint32_t)(((uint64_t)cycles * 1000000u) / 16777216u);
    };
    auto pct10 = [&](uint32_t cycles) -> uint32_t {
        return profile_last_frame_cycles ? (uint32_t)((cycles * 1000u) / profile_last_frame_cycles) : 0u;
    };

    char line[32];
    int y = 1;
    uint32_t vm_us = to_us(profile_last_bucket_cycles[kProfileVm]);
    uint32_t dr_us = to_us(profile_last_bucket_cycles[kProfileDraw]);
    uint32_t bl_us = to_us(profile_last_bucket_cycles[kProfileBlit]);
    uint32_t in_us = to_us(profile_last_bucket_cycles[kProfileInput]);
    uint32_t mn_us = to_us(profile_last_bucket_cycles[kProfileMenu]);
    uint32_t id_us = to_us(profile_last_bucket_cycles[kProfileIdle]);
    const uint32_t top_cycles = profile_last_bucket_cycles[kProfileVm]
        + profile_last_bucket_cycles[kProfileBlit]
        + profile_last_bucket_cycles[kProfileInput]
        + profile_last_bucket_cycles[kProfileMenu];
    const uint32_t rest_cycles = (profile_last_frame_cycles > top_cycles)
        ? (profile_last_frame_cycles - top_cycles)
        : 0u;
    uint32_t rs_us = to_us(rest_cycles);
    uint32_t vm_pct10 = pct10(profile_last_bucket_cycles[kProfileVm]);
    uint32_t dr_pct10 = pct10(profile_last_bucket_cycles[kProfileDraw]);
    uint32_t bl_pct10 = pct10(profile_last_bucket_cycles[kProfileBlit]);
    uint32_t in_pct10 = pct10(profile_last_bucket_cycles[kProfileInput]);
    uint32_t mn_pct10 = pct10(profile_last_bucket_cycles[kProfileMenu]);
    uint32_t id_pct10 = pct10(profile_last_bucket_cycles[kProfileIdle]);
    uint32_t rs_pct10 = pct10(rest_cycles);

    snprintf(line, sizeof(line), "VM %luus %lu.%lu%%",
             (unsigned long)vm_us,
             (unsigned long)(vm_pct10 / 10),
             (unsigned long)(vm_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "DR %luus %lu.%lu%%",
             (unsigned long)dr_us,
             (unsigned long)(dr_pct10 / 10),
             (unsigned long)(dr_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "BL %luus %lu.%lu%%",
             (unsigned long)bl_us,
             (unsigned long)(bl_pct10 / 10),
             (unsigned long)(bl_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "IN %luus %lu.%lu%%",
             (unsigned long)in_us,
             (unsigned long)(in_pct10 / 10),
             (unsigned long)(in_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "MN %luus %lu.%lu%%",
             (unsigned long)mn_us,
             (unsigned long)(mn_pct10 / 10),
             (unsigned long)(mn_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "ID %luus %lu.%lu%%",
             (unsigned long)id_us,
             (unsigned long)(id_pct10 / 10),
             (unsigned long)(id_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "RS %luus %lu.%lu%%",
             (unsigned long)rs_us,
             (unsigned long)(rs_pct10 / 10),
             (unsigned long)(rs_pct10 % 10));
    gpu.pprint(line, (int)strlen(line), 1, y, 11); y += line_h;
    snprintf(line, sizeof(line), "HS S%lu SS%lu L%lu R%lu B%lu",
             (unsigned long)profile_last_hotspots[kHotspotSprMasked],
             (unsigned long)profile_last_hotspots[kHotspotSspr],
             (unsigned long)profile_last_hotspots[kHotspotLineSlow],
             (unsigned long)profile_last_hotspots[kHotspotRectfillSlow],
             (unsigned long)profile_last_hotspots[kHotspotBlitDirty]);
    gpu.pprint(line, (int)strlen(line), 1, y, 11);

    gpu.camera(bk_cx, bk_cy);
    gpu.clip(bk_clip_x, bk_clip_y, bk_clip_w, bk_clip_h);
    gpu.setPen(bk_pen);
#else
    (void)0;
#endif
}

#if REAL8_PROFILE_ENABLED
void Real8VM::profileFrameBegin() {
#if defined(__GBA__)
    profile_frame_start_cycles = profileReadCycles();
    for (int i = 0; i < kProfileCount; ++i) profile_bucket_cycles[i] = 0;
    for (int i = 0; i < kHotspotCount; ++i) profile_hotspots[i] = 0;
#endif
}

void Real8VM::profileFrameEnd() {
#if defined(__GBA__)
    uint32_t now = profileReadCycles();
    profile_last_frame_cycles = now - profile_frame_start_cycles;
    std::memcpy(profile_last_bucket_cycles, profile_bucket_cycles, sizeof(profile_bucket_cycles));
    std::memcpy(profile_last_hotspots, profile_hotspots, sizeof(profile_hotspots));
#endif
}

void Real8VM::profileBegin(int id) {
#if defined(__GBA__)
    if (id >= 0 && id < kProfileCount) {
        profile_bucket_start[id] = profileReadCycles();
    }
#endif
}

void Real8VM::profileEnd(int id) {
#if defined(__GBA__)
    if (id >= 0 && id < kProfileCount) {
        uint32_t now = profileReadCycles();
        profile_bucket_cycles[id] += (now - profile_bucket_start[id]);
    }
#endif
}

void Real8VM::profileHotspot(int id) {
#if defined(__GBA__)
    if (id >= 0 && id < kHotspotCount) {
        ++profile_hotspots[id];
    }
#endif
}
#endif
