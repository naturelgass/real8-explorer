#include "real8_cart.h"
#include "real8_compression.h"
#include <lodePNG.h> 
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

// --------------------------------------------------------------------------
// STATIC HELPERS
// --------------------------------------------------------------------------

static inline int p8_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Extracts a specific section (e.g., "__lua__") from the p8 text file
static std::string extractSection(const std::string &src, const char *name) {
    size_t start = src.find(name);
    if (start == std::string::npos) return "";
    start += strlen(name);
    // Skip newlines/whitespace after the tag
    while (start < src.length() && (src[start] == '_' || src[start] == '\n' || src[start] == '\r')) start++;
    
    size_t end = src.find("\n__", start);
    if (end == std::string::npos) end = src.length();
    
    return src.substr(start, end - start);
}

// --------------------------------------------------------------------------
// LOADER IMPLEMENTATION
// --------------------------------------------------------------------------

bool Real8CartLoader::LoadFromBuffer(IReal8Host* host, const std::vector<uint8_t>& buffer, GameData& outData) {    // 1. Clear Output Data
    memset(outData.gfx, 0, sizeof(outData.gfx));
    memset(outData.map, 0, sizeof(outData.map));
    memset(outData.sfx, 0, sizeof(outData.sfx));
    memset(outData.music, 0, sizeof(outData.music));
    memset(outData.sprite_flags, 0, sizeof(outData.sprite_flags));
    outData.lua_code = "";
    outData.cart_id = ""; // Ensure you added this field to GameData in .h

    if (buffer.empty()) return false;

    // ----------------------------------------------------------------------
    // FORMAT A: PNG CART
    // ----------------------------------------------------------------------
    if (buffer.size() > 8 && buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G') {
        unsigned w, h;
        unsigned char *image = nullptr;
        unsigned error = lodepng_decode32(&image, &w, &h, buffer.data(), buffer.size());

        if (error || !image) { if(image) free(image); return false; }

        // Decode PICO-8 Steganography (RGBA low bits -> Byte)
        std::vector<uint8_t> cart_data;
        cart_data.reserve(0x8000); // 32KB Standard Cart
        size_t total_pixels = w * h;

        for (size_t i = 0; i < total_pixels; ++i) {
            if (cart_data.size() >= 0x8000) break;
            uint8_t r = image[i * 4 + 0];
            uint8_t g = image[i * 4 + 1];
            uint8_t b = image[i * 4 + 2];
            uint8_t a = image[i * 4 + 3];
            // Decode: A(2) R(2) G(2) B(2)
            uint8_t val = ((a & 3) << 6) | ((r & 3) << 4) | ((g & 3) << 2) | (b & 3);
            cart_data.push_back(val);
        }
        free(image);

        if (cart_data.size() < 0x4300) return false;

        // Copy GFX (0x0000 - 0x1FFF)
        memcpy(outData.gfx, cart_data.data() + 0x0000, 0x2000);
        
        // Copy MAP (0x2000 - 0x2FFF)
        memcpy(outData.map, cart_data.data() + 0x2000, 0x1000);
        
        // Copy GFF (Sprite Flags) (0x3000 - 0x30FF)
        memcpy(outData.sprite_flags, cart_data.data() + 0x3000, 0x100);
        
        // Copy MUSIC (0x3100 - 0x31FF)
        memcpy(outData.music, cart_data.data() + 0x3100, 0x100);
        
        // Copy SFX (0x3200 - 0x42FF)
        memcpy(outData.sfx, cart_data.data() + 0x3200, 0x1100);

        // Decompress Code (0x4300 - 0x7FFF)
        char *luaBuf = (char *)calloc(1, 65536);
        if (luaBuf) {
            // Pass the 'host' pointer instead of nullptr
            int codeLen = decompress_pico8_code(host, cart_data.data() + 0x4300, 0x8000 - 0x4300, luaBuf, 65536);
            if (codeLen > 0) {
                outData.lua_code = std::string(luaBuf, codeLen);
            }
            free(luaBuf);
        }

        return true;
    }

    // ----------------------------------------------------------------------
    // FORMAT B: TEXT CART (.p8)
    // ----------------------------------------------------------------------
    std::string content(buffer.begin(), buffer.end());

    // 1. GFX (__gfx__)
    std::string gfxData = extractSection(content, "__gfx__");
    if (!gfxData.empty()) {
        int x = 0, y = 0;
        for (char c : gfxData) {
            if (!isxdigit((unsigned char)c)) continue;
            int v = p8_hex(c);
            if (y < 128 && x < 128) {
                // Pixel Logic: 2 pixels per byte (low nibble = even x, high nibble = odd x)
                // Note: PICO-8 memory is (val & 0x0F) for even, (val >> 4) for odd?
                // Actually PICO-8 RAM is: low nibble is pixel X, high nibble is pixel X+1
                int idx = (y * 64) + (x / 2);
                uint8_t current = outData.gfx[idx];
                
                if (x % 2 == 0) {
                    outData.gfx[idx] = (current & 0xF0) | (v & 0x0F); // Set low nibble
                } else {
                    outData.gfx[idx] = (current & 0x0F) | ((v & 0x0F) << 4); // Set high nibble
                }
                
                if (++x >= 128) { x = 0; y++; }
            }
        }
    }

    // 2. GFF (__gff__)
    std::string gffData = extractSection(content, "__gff__");
    if (!gffData.empty()) {
        int idx = 0; bool high = true; uint8_t val = 0;
        for (char c : gffData) {
            if (!isxdigit((unsigned char)c)) continue;
            int v = p8_hex(c);
            // Text GFF is usually linear hex bytes
            if (high) { val = v << 4; high = false; }
            else {
                val |= v; high = true;
                if (idx < 256) outData.sprite_flags[idx++] = val;
            }
        }
    }

    // 3. MAP (__map__)
    std::string mapSec = extractSection(content, "__map__");
    if (!mapSec.empty()) {
        int x = 0, y = 0; bool high = true; uint8_t val = 0;
        for (char c : mapSec) {
            if (!isxdigit((unsigned char)c)) continue;
            int v = p8_hex(c);
            // Map data in RAM is straight bytes, one byte per tile
            if (high) { val = v << 4; high = false; }
            else {
                val |= v; high = true;
                if (y < 64 && x < 128) {
                    outData.map[y * 128 + x] = val;
                    if (++x >= 128) { x = 0; y++; }
                }
            }
        }
    }

    // 4. MUSIC (__music__)
    std::string musData = extractSection(content, "__music__");
    if (!musData.empty()) {
        int pat = 0;
        const char *p = musData.c_str();
        while (*p && pat < 64) {
            while (*p && !isxdigit((unsigned char)*p)) p++;
            if (!*p) break;

            int flags = p8_hex(*p++);
            if (isxdigit((unsigned char)*p)) flags = (flags << 4) | p8_hex(*p++);
            while (*p && !isxdigit((unsigned char)*p) && *p != '-') p++;

            int ch[4] = {-1, -1, -1, -1};
            for(int i=0; i<4; i++) {
                if (*p) {
                    ch[i] = strtol(p, (char **)&p, 10);
                    while (*p && !isdigit((unsigned char)*p) && *p != '-') p++;
                }
            }

            uint8_t m[4];
            for(int i=0; i<4; i++) {
                // Convert IDs and flags to memory byte format
                m[i] = (ch[i] & 0x3F) | ((ch[i] < 0 || ch[i] > 63) ? 0x40 : 0);
            }
            if (flags & 1) m[0] |= 0x80; // Loop Start
            if (flags & 2) m[1] |= 0x80; // Loop Back
            if (flags & 4) m[2] |= 0x80; // Stop

            for(int i=0; i<4; i++) outData.music[pat * 4 + i] = m[i];
            pat++;
        }
    }

    // 5. SFX (__sfx__)
    std::string sfxSec = extractSection(content, "__sfx__");
    if (!sfxSec.empty()) {
        int sfx_id = 0;
        const char *p = sfxSec.c_str();
        while (*p && sfx_id < 64) {
            while (*p && !isxdigit((unsigned char)*p)) p++;
            if (!*p) break;

            // Editor Mode (Header)
            uint8_t header[4];
            for (int h = 0; h < 4; h++) {
                int v1 = p8_hex(*p++); int v2 = p8_hex(*p++);
                header[h] = (v1 << 4) | v2;
            }
            // Bytes 64-67 of the SFX struct are the header
            for(int h=0; h<4; h++) outData.sfx[sfx_id * 68 + 64 + h] = header[h];

            // Note Data
            for (int n = 0; n < 32; n++) {
                while (*p && isspace((unsigned char)*p)) p++; 
                if (!*p) break;
                int pitch = (p8_hex(p[0]) << 4) | p8_hex(p[1]);
                int instr = p8_hex(p[2]); int vol = p8_hex(p[3]); int eff = p8_hex(p[4]);
                p += 5;
                
                int offset = sfx_id * 68 + (n * 2);
                outData.sfx[offset] = (uint8_t)pitch;
                outData.sfx[offset + 1] = (uint8_t)((instr << 5) | (vol << 2) | (eff & 3));
            }
            sfx_id++;
        }
    }

    // 6. LUA (__lua__)
    outData.lua_code = extractSection(content, "__lua__");

    return true;
}