#ifndef REAL8_BINDINGS_H
#define REAL8_BINDINGS_H

#include "../../lib/z8lua/lua.h"
#include "../../lib/z8lua/lauxlib.h"
#include "../../lib/z8lua/lualib.h"
#include <string>

// Registers all PICO-8 functions (spr, cls, btn, etc.) into the Lua state
void register_pico8_api(lua_State* L);
// Rebinds px9_comp/px9_decomp to native implementations (overrides Lua versions)
void register_px9_bindings(lua_State* L);
std::string p8_normalize_lua_strings(const std::string& src);

#endif // REAL8_BINDINGS_H
