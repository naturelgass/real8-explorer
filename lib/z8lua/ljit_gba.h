/*
** Baseline AOT JIT for GBA (direct-threaded, non-tracing).
** Enabled with LUA_GBA_BASELINE_JIT.
*/

#ifndef ljit_gba_h
#define ljit_gba_h

#include "lobject.h"
#include "lopcodes.h"

#if defined(LUA_GBA_BASELINE_JIT)

#ifndef LUA_GBA_JIT_MAX_OPS
#define LUA_GBA_JIT_MAX_OPS 8192
#endif

#define LUA_JIT_FLAG_DISABLED 0x01
#define LUA_JIT_FLAG_FAIL_SHOWN 0x02

typedef struct LuaJitOp {
  lu_byte op;
  lu_byte a;
  unsigned short b;
  unsigned short c;
  int aux;
  lu_byte extra;
} LuaJitOp;

typedef struct LuaJitProto {
  int sizecode;
  LuaJitOp ops[1];
} LuaJitProto;

LUAI_FUNC LuaJitProto *luaJitCompileProto(lua_State *L, Proto *p);
LUAI_FUNC void luaJitFreeProto(lua_State *L, Proto *p);
LUAI_FUNC void luaJitOnFailure(lua_State *L);

#endif

#endif
