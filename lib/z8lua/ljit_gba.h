/*
** Baseline AOT JIT for GBA (direct-threaded, non-tracing).
** Enabled with LUA_GBA_BASELINE_JIT.
*/

#ifndef ljit_gba_h
#define ljit_gba_h

#include "lobject.h"
#include "lopcodes.h"


/* Treat LUA_GBA_BASELINE_JIT as numeric (0/1). */
#ifndef LUA_GBA_BASELINE_JIT
#define LUA_GBA_BASELINE_JIT 0
#endif
#if LUA_GBA_BASELINE_JIT

#ifndef LUA_GBA_JIT_MAX_OPS
#define LUA_GBA_JIT_MAX_OPS 8192
#endif

/*
** JIT op representation.
**
** The baseline JIT on GBA is typically bandwidth/ICache bound.
** The old LuaJitOp layout was 16 bytes on ARM due to padding.
** A compact layout improves cache residency and reduces memory traffic.
**
*/


#define LUA_JIT_FLAG_DISABLED 0x01
#define LUA_JIT_FLAG_FAIL_SHOWN 0x02

/* Optional per-op flags (LuaJitOp::flags). */
#define LUA_JIT_OPFLAG_NONE     0x00
#define LUA_JIT_OPFLAG_AUX_BX   0x01  /* aux contains GETARG_Bx for iABx ops */
#define LUA_JIT_OPFLAG_AUX_SBX  0x02  /* aux contains GETARG_sBx for iAsBx ops */
#define LUA_JIT_OPFLAG_AUX_AX   0x04  /* aux contains GETARG_Ax (iAx or EXTRAARG fused) */

/* Helper to preserve old meaning: LuaJitOp::extra == 1 means fused EXTRAARG. */

typedef struct LuaJitOp {
  /* Compact: 12 bytes on ARM (aux first to avoid padding). */
  int aux;
  unsigned short b;
  unsigned short c;
  lu_byte a;
  lu_byte op;
  lu_byte extra; /* fused EXTRAARG (LOADKX / SETLIST C==0) */
  lu_byte flags; /* auxiliary decoded-mode flags (optional) */
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
