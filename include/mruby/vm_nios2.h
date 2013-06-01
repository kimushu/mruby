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

typedef struct mrb_vm_context {
  mrb_state *mrb;

  mrb_pair  (*argary)         (uint32_t bx);
  void      (*ary_cat)        (mrb_value recv, mrb_value other);
  mrb_value (*ary_fetch)      (mrb_value recv, uint32_t index);
  mrb_value (*ary_new)        (mrb_value *array, uint32_t max);
  void      (*ary_post)       (mrb_value *array, uint32_t pre, uint32_t post);
  void      (*ary_push)       (mrb_value recv, mrb_value value);
  void      (*ary_store)      (mrb_value recv, uint32_t index, mrb_value value);
  mrb_value (*blk_push)       (uint32_t bx);
  mrb_value (*blockexec)      (mrb_value recv, uint32_t bx);
  mrb_value (*call)           (void);
  void      (*ensure_pop)     (uint32_t times);
  void      (*ensure_push)    (uint32_t bx);
  void      (*enter)          (uint32_t ax);
  mrb_value (*getmcnst)       (mrb_value recv, uint32_t sym);
  void      (*setmcnst)       (mrb_value recv, uint32_t sym, mrb_value value);
  mrb_value (*getupvar)       (uint32_t b, uint32_t c);
  void      (*setupvar)       (uint32_t b, uint32_t c, mrb_value value);
  mrb_value (*hash_fetch)     (mrb_value hash, uint32_t sym);
  mrb_value (*hash_new)       (mrb_value *array, uint32_t pairs);
  void      (*hash_store)     (mrb_value hash, uint32_t sym, mrb_value value);
  mrb_value (*lambda)         (uint32_t bz, uint32_t cm);
  mrb_value (*load_lit)       (uint32_t bx);
  uint32_t  (*load_sym)       (uint32_t bx);
  mrb_value (*newclass)       (mrb_value base, uint32_t sym, mrb_value super);
  void      (*newmethod)      (mrb_value base, uint32_t sym, mrb_value closure);
  mrb_value (*newmodule)      (mrb_value base, uint32_t sym);
  void      (*raise)          (mrb_value obj);
  mrb_value (*range_new)      (mrb_value first, mrb_value second, uint32_t exclude);
  mrb_value (*rescue)         (void);
  void      (*rescue_pop)     (uint32_t times);
  void      (*rescue_push)    (mrb_code *base, int32_t offset);
  void      (*ret_break)      (mrb_value obj);
  void      (*ret_normal)     (mrb_value obj);
  void      (*ret_return)     (mrb_value obj);
  mrb_value (*send_array)     (mrb_value *argv, uint32_t sym);
  mrb_value (*send_normal)    (mrb_value *argv, uint32_t sym, uint32_t argc);
  mrb_value (*singleton_class)(mrb_value base);
  void      (*str_cat)        (mrb_value recv, mrb_value other);
  mrb_value (*str_dup)        (uint32_t bx);
  mrb_value (*super)          (mrb_value *argv2, uint32_t argc);
  mrb_value (*target_class)   (void);
  void      (*stop_vm)        (void);
  void      (*raise_err)      (mrb_value obj);
  mrb_value object_class;
  mrb_value gv;
  mrb_value cv;
  mrb_value iv;
  mrb_value cnst;

  mrb_irep *irep;
  mrb_sym *syms;
  mrb_value *pool;
  int ai;
} mrb_vm_context;

void mrb_vm_context_init(mrb_state *mrb);
mrb_value mrb_vm_exec(mrb_state *mrb, mrb_code *code);

#endif  /* MRUBY_VM_NIOS2_H */
