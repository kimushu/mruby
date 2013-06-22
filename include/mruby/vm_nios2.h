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
  NIOS2_VAR32(mrb_value,  object_class);

  NIOS2_VAR32(void *, argary);
  NIOS2_VAR32(void *, ary_cat);
  NIOS2_VAR32(void *, ary_fetch);
  NIOS2_VAR32(void *, ary_new);
  NIOS2_VAR32(void *, ary_post);
  NIOS2_VAR32(void *, ary_push);
  NIOS2_VAR32(void *, ary_store);
  NIOS2_VAR32(void *, blk_push);
  NIOS2_VAR32(void *, blockexec);
  NIOS2_VAR32(void *, call);
  NIOS2_VAR32(void *, ensure_pop);
  NIOS2_VAR32(void *, ensure_push);
  NIOS2_VAR32(void *, enter);
  NIOS2_VAR32(void *, getglobal);
  NIOS2_VAR32(void *, setglobal);
  NIOS2_VAR32(void *, ivget);
  NIOS2_VAR32(void *, ivset);
  NIOS2_VAR32(void *, cvget);
  NIOS2_VAR32(void *, cvset);
  NIOS2_VAR32(void *, constget);
  NIOS2_VAR32(void *, constset);
  NIOS2_VAR32(void *, getmcnst);
  NIOS2_VAR32(void *, setmcnst);
  NIOS2_VAR32(void *, getupvar);
  NIOS2_VAR32(void *, setupvar);
  NIOS2_VAR32(void *, hash_new);
  NIOS2_VAR32(void *, lambda);
  NIOS2_VAR32(void *, newclass);
  NIOS2_VAR32(void *, newmethod);
  NIOS2_VAR32(void *, newmodule);
  NIOS2_VAR32(void *, raise);
  NIOS2_VAR32(void *, range_new);
  NIOS2_VAR32(void *, rescue);
  NIOS2_VAR32(void *, rescue_pop);
  NIOS2_VAR32(void *, rescue_push);
  NIOS2_VAR32(void *, ret);
  NIOS2_VAR32(void *, send_array);
  NIOS2_VAR32(void *, send_normal);
  NIOS2_VAR32(void *, singleton_class);
  NIOS2_VAR32(void *, str_cat);
  NIOS2_VAR32(void *, str_dup);
  NIOS2_VAR32(void *, super_array);
  NIOS2_VAR32(void *, super_normal);
  NIOS2_VAR32(void *, target_class);
  NIOS2_VAR32(void *, stop);
  NIOS2_VAR32(void *, runtime_err);
  NIOS2_VAR32(void *, localjump_err);

  NIOS2_FUNCP(void,       dprintf,        (const char*, ...));
} mrb_vm_env;

#undef NIOS2_VAR32
#undef NIOS2_FUNCP

void mrb_vm_env_init(mrb_state *mrb);
mrb_value mrb_vm_exec(mrb_state *mrb, mrb_code *code);

#endif  /* MRUBY_VM_NIOS2_H */
