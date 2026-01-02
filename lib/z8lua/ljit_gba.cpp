/*
** Baseline AOT JIT for GBA (direct-threaded, non-tracing).
** Enabled with LUA_GBA_BASELINE_JIT.
*/

#include "ljit_gba.h"

#if LUA_GBA_BASELINE_JIT

#include "lmem.h"


#ifndef LUA_GBA_JIT_BUDGET_BYTES
/* Total bytes of EWRAM heap we allow for compiled protos. */
#define LUA_GBA_JIT_BUDGET_BYTES (64 * 1024)
#endif

static size_t g_lua_jit_bytes_used = 0;

/* Weak default; platform can override (e.g. gba_host.cpp). */
#if defined(__GNUC__)
__attribute__((weak))
#endif
void luaJitOnFailure(lua_State *L) { (void)L; }

/* Weak default; platform can override (e.g. gba_host.cpp). */
/*
** Notes on performance:
** - The dispatch loop is usually bandwidth bound on GBA.
** - Keeping LuaJitOp compact (see ljit_gba.h) improves ICache/DCache use.
** - Pre-decoding Bx/sBx/Ax into aux (optional) can shave a few ops from
**   the hot handlers.
*/


static void luaJitDisableProto(lua_State *L, Proto *p) {
  if (!p) return;
  p->jit_flags |= LUA_JIT_FLAG_DISABLED;
  if (!(p->jit_flags & LUA_JIT_FLAG_FAIL_SHOWN)) {
    p->jit_flags |= LUA_JIT_FLAG_FAIL_SHOWN;
    luaJitOnFailure(L);
  }
}

LuaJitProto *luaJitCompileProto(lua_State *L, Proto *p) {
  if (!p) return NULL;
  if (p->jit_flags & LUA_JIT_FLAG_DISABLED) return NULL;
  if (p->jit) return p->jit;

  if (p->sizecode <= 0 || p->sizecode > LUA_GBA_JIT_MAX_OPS) {
    luaJitDisableProto(L, p);
    return NULL;
  }

  size_t count = cast(size_t, p->sizecode);
  size_t bytes = sizeof(LuaJitProto) + sizeof(LuaJitOp) * (count - 1);
    /* Hard budget guard to prevent EWRAM exhaustion/fragmentation. */
  if (bytes > (size_t)LUA_GBA_JIT_BUDGET_BYTES ||
      g_lua_jit_bytes_used + bytes > (size_t)LUA_GBA_JIT_BUDGET_BYTES) {
    luaJitDisableProto(L, p);
    return NULL;
  }
LuaJitProto *jit = cast(LuaJitProto *, luaM_malloc(L, bytes));
  if (!jit) {
    luaJitDisableProto(L, p);
    return NULL;
  }

  jit->sizecode = p->sizecode;
  Instruction *code = p->code;
  for (int pc = 0; pc < p->sizecode; ++pc) {
    Instruction i = code[pc];
    LuaJitOp *op = &jit->ops[pc];
    const OpCode opc = GET_OPCODE(i);
    op->op = cast(lu_byte, opc);
    op->a = cast(lu_byte, GETARG_A(i));
    op->b = cast(unsigned short, GETARG_B(i));
    op->c = cast(unsigned short, GETARG_C(i));
    op->aux = 0;
    op->extra = 0;
    op->flags = LUA_JIT_OPFLAG_NONE;

    /* Optional predecode of instruction modes into aux.
       Handlers may ignore these flags and recompute on demand. */
    switch (getOpMode(opc)) {
      case iABx:
        op->aux = cast(int, GETARG_Bx(i));
        op->flags |= LUA_JIT_OPFLAG_AUX_BX;
        break;
      case iAsBx:
        op->aux = cast(int, GETARG_sBx(i));
        op->flags |= LUA_JIT_OPFLAG_AUX_SBX;
        break;
      case iAx:
        /* Note: OP_EXTRAARG itself is iAx, but is usually skipped by
           handlers for LOADKX/SETLIST. Storing it here is harmless. */
        op->aux = cast(int, GETARG_Ax(i));
        op->flags |= LUA_JIT_OPFLAG_AUX_AX;
        break;
      default:
        break;
    }

    if (op->op == OP_LOADKX || (op->op == OP_SETLIST && op->c == 0)) {
      if (pc + 1 >= p->sizecode || GET_OPCODE(code[pc + 1]) != OP_EXTRAARG) {
        luaM_free(L, jit);
        luaJitDisableProto(L, p);
        return NULL;
      }
      /* Fuse OP_EXTRAARG into the current op. */
      op->aux = cast(int, GETARG_Ax(code[pc + 1]));
      op->flags &= ~(LUA_JIT_OPFLAG_AUX_BX | LUA_JIT_OPFLAG_AUX_SBX);
      op->flags |= LUA_JIT_OPFLAG_AUX_AX;
      op->extra = 1;
    }
  }

  g_lua_jit_bytes_used += bytes;
  p->jit = jit;
  return jit;
}

void luaJitFreeProto(lua_State *L, Proto *p) {
  if (!p || !p->jit) return;
  LuaJitProto *jit = p->jit;
  size_t bytes = sizeof(LuaJitProto) + sizeof(LuaJitOp) * (cast(size_t, jit->sizecode) - 1);
  if (g_lua_jit_bytes_used >= bytes) g_lua_jit_bytes_used -= bytes;
  else g_lua_jit_bytes_used = 0;
  luaM_free(L, jit);
  p->jit = NULL;
  /* Keep the DISABLED/FAIL_SHOWN bits so we don't thrash recompilation. */
  p->jit_flags &= (LUA_JIT_FLAG_DISABLED | LUA_JIT_FLAG_FAIL_SHOWN);
}

#endif
