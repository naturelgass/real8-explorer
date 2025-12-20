#ifndef REAL8_BINDINGS_H
#define REAL8_BINDINGS_H

#include "../../lib/z8lua/lua.h"
#include "../../lib/z8lua/lauxlib.h"
#include "../../lib/z8lua/lualib.h"

// Registers all PICO-8 functions (spr, cls, btn, etc.) into the Lua state
void register_pico8_api(lua_State* L);

#endif // REAL8_BINDINGS_H