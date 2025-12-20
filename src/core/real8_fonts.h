#pragma once

#include <cstdint>
#include <string>

// Retrieve the bitmap pointer for a standard 4x6 character
const uint8_t* p8_4x6_bits(uint8_t p8);

// Retrieve the bitmap pointer for a 5x6 (menu) character
const uint8_t* p8_5x6_bits(uint8_t p8);

// Converts UTF-8 strings (like arrow emojis) into PICO-8 char codes
std::string convertUTF8toP8SCII(const char* src);