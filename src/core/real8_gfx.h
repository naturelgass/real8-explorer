#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "real8_memattrs.h"

// Forward Declaration to avoid circular include issues
class Real8VM;

class Real8Gfx
{
public:
    Real8Gfx(Real8VM* vm_instance);
    ~Real8Gfx();

    void init();
    void reset(); // Replaces pal_reset, clip reset, etc.

    // --- State Management ---
    void camera(int x, int y);
    void clip(int x, int y, int w, int h);
    void color(uint8_t col); // Set Pen
    void fillp(uint32_t pattern);
    
    // Palette
    static const uint8_t PALETTE_RGB[32][3];
    void pal(int c0, int c1, int p = 0);
    void pal_reset();
    void palt(int c, bool t);
    void palt_reset();
    void setPen(uint8_t col) { pen_col = col & 0x0F; }

    // Cursor (for print)
    void setCursor(int x, int y) { cur_x = x; cur_y = y; }
    int getCursorX() const { return cur_x; }
    int getCursorY() const { return cur_y; }
    uint8_t getPen() const { return pen_col; }

    // --- Primitives ---
    void cls(int c = 0);
    void pset(int x, int y, uint8_t col);
    uint8_t pget(int x, int y);
    
    void line(int x0, int y0, int x1, int y1, uint8_t c);
    void rect(int x0, int y0, int x1, int y1, uint8_t c);
    void rectfill(int x0, int y0, int x1, int y1, uint8_t c);
    void circ(int cx, int cy, int r, uint8_t c);
    void circfill(int cx, int cy, int r, uint8_t c);
    
    // --- Sprites & Map ---
    void spr(int n, int x, int y, int w = 1, int h = 1, bool fx = false, bool fy = false);
    void sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x = false, bool flip_y = false);
    
    uint8_t sget(int x, int y);
    void sset(int x, int y, uint8_t v);
    
    uint8_t mget(int x, int y);
    void mset(int x, int y, uint8_t v);
    void map(int mx, int my, int sx, int sy, int w, int h, int layer = -1);

    // --- Text ---
    int pprint(const char *s, int len, int x, int y, uint8_t c);
    void renderMessage(const char *header, std::string msg, int color);

    // --- Internal/Helpers ---
    // Used by VM to get the palette for frame flipping
    void get_screen_palette(uint8_t* out_palette); 
    
    // Low-level write used by primitives
    void put_pixel_checked(int x, int y, uint8_t col);

    void setMenuFont(bool active) { use_menu_font = active; }
    
    struct RGB { uint8_t r, g, b; };
    static RGB getPico8Color(uint8_t index);

    // Public State (read by Bindings for save states)
    int cam_x = 0, cam_y = 0;
    int clip_x = 0, clip_y = 0, clip_w = 128, clip_h = 128;
    int last_line_x = 0, last_line_y = 0;
    uint8_t draw_mask = 0; 

    // Make pattern public if needed by VM, or keep private if logic stays in Gfx
    uint32_t fillp_pattern = 0xFFFFFFFF;

    // Reseting State
    struct GfxState {
        int cam_x, cam_y;
        int clip_x, clip_y, clip_w, clip_h;
        uint8_t pen_col;
        uint8_t draw_mask;
        uint32_t fillp_pattern;
        uint8_t palette_map[16];
        uint8_t screen_palette[256];
        bool palt_map[16];
    };

    uint8_t palette_map[16];
    uint8_t screen_palette[256];
    bool palt_map[16];

    void saveState(GfxState& out);
    void restoreState(const GfxState& in);

    // Allow VM to read this directly for fast blit if RAM is not ready
    uint8_t* get_screen_palette_ptr() { return screen_palette; } 
    
    // Ensure palette_map and screen_palette are public or have getters?
    // Actually, making Real8Gfx a 'friend' of Real8VM or exposing these is cleanest.
    friend class Real8VM;

private:
    Real8VM* vm; // Back-reference to access RAM/FB

    int cur_x = 0, cur_y = 0;
    uint8_t pen_col = 6;
    


    bool use_alt_font = false;
    bool use_menu_font = false;

    // Helpers
    void put_pixel_raw(int x, int y, uint8_t col);
    void spr_fast(int n, int x, int y, int w, int h, bool fx, bool fy);
    
    int draw_char_default(uint8_t p8, int x, int y, uint8_t col);
    int draw_char_custom(uint8_t p8, int x, int y, uint8_t col);
    void put_bitrow_1bpp(int x, int y, uint8_t bits, int w, uint8_t col);
    
    uint8_t get_pixel_ram(uint32_t base_addr, int x, int y);
    void set_pixel_ram(uint32_t base_addr, int x, int y, uint8_t color);
};