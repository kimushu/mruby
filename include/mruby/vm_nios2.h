/*
** mruby/vm_nios2.h - virtual machine definition for Nios2
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_VM_NIOS2_H
#define MRUBY_VM_NIOS2_H

#include "mruby/value.h"
#include <setjmp.h>

#define NIOS2_VMENV_REG         20  /* r20 (Callee-saved) */
#define NIOS2_STACK_REG         21  /* r21 (Callee-saved) */

#define NIOS2_BINARY_IDENTIFIER "NIOS"
#define NIOS2_BINARY_FORMAT_VER "2N01"
#define NIOS2_COMPILER_NAME     "KIMS"
#define NIOS2_COMPILER_VERSION  "0000"

typedef struct mrb_pair {
  mrb_value first;
  mrb_value second;
} mrb_pair;

#ifdef MRB_MACHINE_NIOS2
# define NIOS2_VAR32(t, n)      t n
# define NIOS2_FUNCP(r, n, p)   r (*n) p
#else
# define NIOS2_VAR32(t, n)      uint32_t n
# define NIOS2_FUNCP(r, n, p)   uint32_t n
#endif

typedef struct mrb_vm_context {
  NIOS2_VAR32(struct RProc*, proc);
  NIOS2_VAR32(mrb_irep *,    irep);
  NIOS2_VAR32(mrb_sym *,     syms);
  NIOS2_VAR32(mrb_value *,   pool);
  NIOS2_VAR32(int,           ai);
  NIOS2_VAR32(jmp_buf *,     prev_jmp);
} mrb_vm_context;

typedef struct mrb_vm_env {
  NIOS2_VAR32(mrb_state *, mrb);
  NIOS2_VAR32(mrb_vm_context *, ctx);

  NIOS2_FUNCP(mrb_pair,   argary,         (uint32_t bx));
  NIOS2_FUNCP(void,       ary_cat,        (mrb_value recv, mrb_value other));
  NIOS2_FUNCP(mrb_value,  ary_fetch,      (mrb_value recv, uint32_t index));
  NIOS2_FUNCP(mrb_value,  ary_new,        (mrb_value *array, uint32_t max));
  NIOS2_FUNCP(void,       ary_post,       (mrb_value *array, uint32_t pre, uint32_t post));
  NIOS2_FUNCP(void,       ary_push,       (mrb_value recv, mrb_value value));
  NIOS2_FUNCP(void,       ary_store,      (mrb_value recv, uint32_t index, mrb_value value));
  NIOS2_FUNCP(mrb_value,  blk_push,       (uint32_t bx));
  NIOS2_FUNCP(mrb_value,  blockexec,      (int a, uint32_t bx));
  NIOS2_FUNCP(mrb_value,  call,           (void));
  NIOS2_FUNCP(void,       ensure_pop,     (uint32_t times));
  NIOS2_FUNCP(void,       ensure_push,    (uint32_t bx));
  NIOS2_FUNCP(int32_t,    enter,          (uint32_t ax));
  NIOS2_FUNCP(mrb_value,  getglobal,      (mrb_sym sym));
  NIOS2_FUNCP(void,       setglobal,      (mrb_sym sym, mrb_value value));
  NIOS2_FUNCP(mrb_value,  ivget,          (mrb_sym sym));
  NIOS2_FUNCP(void,       ivset,          (mrb_sym sym, mrb_value value));
  NIOS2_FUNCP(mrb_value,  cvget,          (mrb_sym sym));
  NIOS2_FUNCP(void,       cvset,          (mrb_sym sym, mrb_value value));
  NIOS2_FUNCP(mrb_value,  constget,       (mrb_sym sym));
  NIOS2_FUNCP(void,       constset,       (mrb_sym sym, mrb_value value));
  NIOS2_FUNCP(mrb_value,  getmcnst,       (mrb_value recv, mrb_sym sym));
  NIOS2_FUNCP(void,       setmcnst,       (mrb_value recv, mrb_sym sym, mrb_value value));
  NIOS2_FUNCP(mrb_value,  getupvar,       (uint32_t b, uint32_t c));
  NIOS2_FUNCP(void,       setupvar,       (uint32_t b, uint32_t c, mrb_value value));
  NIOS2_FUNCP(mrb_value,  hash_new,       (mrb_value *array, uint32_t pairs));
  NIOS2_FUNCP(mrb_value,  lambda,         (uint32_t bz, uint32_t cm));
  NIOS2_FUNCP(mrb_value,  newclass,       (mrb_value base, mrb_sym sym, mrb_value super));
  NIOS2_FUNCP(void,       newmethod,      (mrb_value base, mrb_sym sym, mrb_value closure));
  NIOS2_FUNCP(mrb_value,  newmodule,      (mrb_value base, mrb_sym sym));
  NIOS2_FUNCP(void,       raise,          (mrb_value obj));
  NIOS2_FUNCP(mrb_value,  range_new,      (mrb_value first, mrb_value second, uint32_t exclude));
  NIOS2_FUNCP(mrb_value,  rescue,         (void));
  NIOS2_FUNCP(void,       rescue_pop,     (uint32_t times));
  NIOS2_FUNCP(void,       rescue_push,    (mrb_code *base, int32_t offset));
  NIOS2_FUNCP(void,       ret,            (mrb_value obj));
  // NIOS2_FUNCP(void,       ret_break,      (mrb_value obj));
  // NIOS2_FUNCP(void,       ret_normal,     (mrb_value obj));
  // NIOS2_FUNCP(void,       ret_return,     (mrb_value obj));
  NIOS2_FUNCP(mrb_value,  send_array,     (mrb_value *argv, mrb_sym sym));
  NIOS2_FUNCP(mrb_value,  send_normal,    (mrb_value *argv, mrb_sym sym, uint32_t argc));
  NIOS2_FUNCP(mrb_value,  singleton_class,(mrb_value base));
  NIOS2_FUNCP(void,       str_cat,        (mrb_value recv, mrb_value other));
  NIOS2_FUNCP(mrb_value,  str_dup,        (uint32_t bx));
  NIOS2_FUNCP(mrb_value,  super,          (mrb_value *argv2, uint32_t argc));
  NIOS2_FUNCP(mrb_value,  target_class,   (void));
  NIOS2_FUNCP(void,       stop_vm,        (void));
  NIOS2_FUNCP(void,       raise_err,      (mrb_value obj));
  NIOS2_VAR32(mrb_value,  object_class);
  NIOS2_VAR32(mrb_value,  gv);
  NIOS2_VAR32(mrb_value,  cv);
  NIOS2_VAR32(mrb_value,  iv);
  NIOS2_VAR32(mrb_value,  cnst);
} mrb_vm_env;

#undef NIOS2_VAR32
#undef NIOS2_FUNCP

void mrb_vm_env_init(mrb_state *mrb);
mrb_value mrb_vm_exec(mrb_state *mrb, mrb_code *code);

#endif  /* MRUBY_VM_NIOS2_H */
