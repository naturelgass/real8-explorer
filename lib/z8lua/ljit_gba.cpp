/*
** Baseline AOT JIT for GBA (direct-threaded, non-tracing).
** Enabled with LUA_GBA_BASELINE_JIT.
*/

#include "ljit_gba.h"

#if defined(LUA_GBA_BASELINE_JIT)

#include "lmem.h"

#if defined(__GNUC__)
__attribute__((weak))
#endif
void luaJitOnFailure(lua_State *L) {
  (void)L;
}

LuaJitProto *luaJitCompileProto(lua_State *L, Proto *p) {
  if (!p) return NULL;
  if (p->jit_flags & LUA_JIT_FLAG_DISABLED) return NULL;
  if (p->jit) return p->jit;

  if (p->sizecode <= 0 || p->sizecode > LUA_GBA_JIT_MAX_OPS) {
    p->jit_flags |= LUA_JIT_FLAG_DISABLED;
    return NULL;
  }

  size_t count = cast(size_t, p->sizecode);
  size_t bytes = sizeof(LuaJitProto) + sizeof(LuaJitOp) * (count - 1);
  LuaJitProto *jit = cast(LuaJitProto *, luaM_malloc(L, bytes));
  if (!jit) {
    p->jit_flags |= LUA_JIT_FLAG_DISABLED;
    return NULL;
  }

  jit->sizecode = p->sizecode;
  Instruction *code = p->code;
  for (int pc = 0; pc < p->sizecode; ++pc) {
    Instruction i = code[pc];
    LuaJitOp *op = &jit->ops[pc];
    op->op = cast(lu_byte, GET_OPCODE(i));
    op->a = cast(lu_byte, GETARG_A(i));
    op->b = cast(unsigned short, GETARG_B(i));
    op->c = cast(unsigned short, GETARG_C(i));
    op->aux = 0;
    op->extra = 0;

    if (op->op == OP_LOADKX || (op->op == OP_SETLIST && op->c == 0)) {
      if (pc + 1 >= p->sizecode || GET_OPCODE(code[pc + 1]) != OP_EXTRAARG) {
        luaM_free(L, jit);
        p->jit_flags |= LUA_JIT_FLAG_DISABLED;
        return NULL;
      }
      op->aux = GETARG_Ax(code[pc + 1]);
      op->extra = 1;
    }
  }

  p->jit = jit;
  return jit;
}

void luaJitFreeProto(lua_State *L, Proto *p) {
  if (!p || !p->jit) return;
  luaM_free(L, p->jit);
  p->jit = NULL;
  p->jit_flags = 0;
}

#endif
