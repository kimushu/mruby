/*
** mruby/vm_nios2.h - virtual machine definition for Nios2
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_VM_NIOS2_H
#define MRUBY_VM_NIOS2_H

#include "mruby/value.h"

#define NIOS2_STACK_REG         21  /* r21 (Callee-saved) */
#define NIOS2_VMCTX_REG         20  /* r20 (Callee-saved) */

#define NIOS2_BINARY_IDENTIFIER "NIOS"
#define NIOS2_BINARY_FORMAT_VER "2N01"
#define NIOS2_COMPILER_NAME     "KIMS"
#define NIOS2_COMPILER_VERSION  "0000"

typedef struct mrb_pair {
  mrb_value first;
  mrb_value second;
} mrb_pair;

#ifdef MRB_MACHINE_NIOS2
# define VMCTX_VAR32(t, n)    t n
# define VMCTX_FUNC(r, n, p)  r (*n) p
#else
# define VMCTX_VAR32(t, n)    uint32_t n
# define VMCTX_FUNC(r, n, p)  uint32_t n
#endif

typedef struct mrb_vm_context {
  VMCTX_VAR32(mrb_state *, mrb);

  VMCTX_FUNC(mrb_pair,  argary,         (uint32_t bx));
  VMCTX_FUNC(void,      ary_cat,        (mrb_value recv, mrb_value other));
  VMCTX_FUNC(mrb_value, ary_fetch,      (mrb_value recv, uint32_t index));
  VMCTX_FUNC(mrb_value, ary_new,        (mrb_value *array, uint32_t max));
  VMCTX_FUNC(void,      ary_post,       (mrb_value *array, uint32_t pre, uint32_t post));
  VMCTX_FUNC(void,      ary_push,       (mrb_value recv, mrb_value value));
  VMCTX_FUNC(void,      ary_store,      (mrb_value recv, uint32_t index, mrb_value value));
  VMCTX_FUNC(mrb_value, blk_push,       (uint32_t bx));
  VMCTX_FUNC(mrb_value, blockexec,      (mrb_value recv, uint32_t bx));
  VMCTX_FUNC(mrb_value, call,           (void));
  VMCTX_FUNC(void,      ensure_pop,     (uint32_t times));
  VMCTX_FUNC(void,      ensure_push,    (uint32_t bx));
  VMCTX_FUNC(void,      enter,          (uint32_t ax));
  VMCTX_FUNC(mrb_value, getmcnst,       (mrb_value recv, uint32_t sym));
  VMCTX_FUNC(void,      setmcnst,       (mrb_value recv, uint32_t sym, mrb_value value));
  VMCTX_FUNC(mrb_value, getupvar,       (uint32_t b, uint32_t c));
  VMCTX_FUNC(void,      setupvar,       (uint32_t b, uint32_t c, mrb_value value));
  VMCTX_FUNC(mrb_value, hash_fetch,     (mrb_value hash, uint32_t sym));
  VMCTX_FUNC(mrb_value, hash_new,       (mrb_value *array, uint32_t pairs));
  VMCTX_FUNC(void,      hash_store,     (mrb_value hash, uint32_t sym, mrb_value value));
  VMCTX_FUNC(mrb_value, lambda,         (uint32_t bz, uint32_t cm));
  VMCTX_FUNC(mrb_value, load_lit,       (uint32_t bx));
  VMCTX_FUNC(uint32_t,  load_sym,       (uint32_t bx));
  VMCTX_FUNC(mrb_value, newclass,       (mrb_value base, uint32_t sym, mrb_value super));
  VMCTX_FUNC(void,      newmethod,      (mrb_value base, uint32_t sym, mrb_value closure));
  VMCTX_FUNC(mrb_value, newmodule,      (mrb_value base, uint32_t sym));
  VMCTX_FUNC(void,      raise,          (mrb_value obj));
  VMCTX_FUNC(mrb_value, range_new,      (mrb_value first, mrb_value second, uint32_t exclude));
  VMCTX_FUNC(mrb_value, rescue,         (void));
  VMCTX_FUNC(void,      rescue_pop,     (uint32_t times));
  VMCTX_FUNC(void,      rescue_push,    (mrb_code *base, int32_t offset));
  VMCTX_FUNC(void,      ret_break,      (mrb_value obj));
  VMCTX_FUNC(void,      ret_normal,     (mrb_value obj));
  VMCTX_FUNC(void,      ret_return,     (mrb_value obj));
  VMCTX_FUNC(mrb_value, send_array,     (mrb_value *argv, uint32_t sym));
  VMCTX_FUNC(mrb_value, send_normal,    (mrb_value *argv, uint32_t sym, uint32_t argc));
  VMCTX_FUNC(mrb_value, singleton_class,(mrb_value base));
  VMCTX_FUNC(void,      str_cat,        (mrb_value recv, mrb_value other));
  VMCTX_FUNC(mrb_value, str_dup,        (uint32_t bx));
  VMCTX_FUNC(mrb_value, super,          (mrb_value *argv2, uint32_t argc));
  VMCTX_FUNC(mrb_value, target_class,   (void));
  VMCTX_FUNC(void,      stop_vm,        (void));
  VMCTX_FUNC(void,      raise_err,      (mrb_value obj));
  VMCTX_VAR32(mrb_value, object_class);
  VMCTX_VAR32(mrb_value, gv);
  VMCTX_VAR32(mrb_value, cv);
  VMCTX_VAR32(mrb_value, iv);
  VMCTX_VAR32(mrb_value, cnst);

  VMCTX_VAR32(mrb_irep *,  irep);
  VMCTX_VAR32(mrb_sym *,   syms);
  VMCTX_VAR32(mrb_value *, pool);
  VMCTX_VAR32(int,         ai);
} mrb_vm_context;

#undef VMCTX_VAR32
#undef VMCTX_FUNC

void mrb_vm_context_init(mrb_state *mrb);
mrb_value mrb_vm_exec(mrb_state *mrb, mrb_code *code);

#endif  /* MRUBY_VM_NIOS2_H */
