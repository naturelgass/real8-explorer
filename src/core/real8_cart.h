#pragma once

#include <vector>
#include <string>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Forward declaration so we don't need to include the heavy host header here
class IReal8Host; 

struct GameData {
#if defined(__GBA__)
    const uint8_t* gfx = nullptr;
    const uint8_t* map = nullptr;
    const uint8_t* sfx = nullptr;
    const uint8_t* music = nullptr;
    const uint8_t* sprite_flags = nullptr;
    const char* lua_code_ptr = nullptr;
    size_t lua_code_size = 0;
    const char* cart_id = nullptr;
#else
    uint8_t gfx[0x2000];
    uint8_t map[0x1000];
    uint8_t sfx[0x1100];
    uint8_t music[0x100];
    uint8_t sprite_flags[0x100];
    std::string lua_code;
    const char* lua_code_ptr = nullptr;
    size_t lua_code_size = 0;
    std::string cart_id;
#endif
};

class Real8CartLoader {
public:
    static bool LoadFromBuffer(IReal8Host* host, const std::vector<uint8_t>& buffer, GameData& outData);
};
