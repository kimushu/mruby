/*
** vm_nios2.c - virtual machine for Nios2
**
** See Copyright Notice in mruby.h
*/

#include <setjmp.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/hash.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/range.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/vm_nios2.h"
#include "value_array.h"
#include "opcode.h"

#ifdef MRB_MACHINE_NIOS2

#define NAKED     __attribute__((naked))

#define TO_STR(x) TO_STR_(x)
#define TO_STR_(x) #x

#define PROLOGUE_mrb \
  mrb_state *mrb;\
  asm("ldw %0, %1(r" TO_STR(NIOS2_VMENV_REG) ")":\
      "=r"(mrb): "i"(offsetof(mrb_vm_env, mrb)))

#define PROLOGUE_mrb_vmc \
  PROLOGUE_mrb;\
  mrb_vm_context *vmc;\
  asm("ldw %0, %1(r" TO_STR(NIOS2_VMENV_REG) ")":\
      "=r"(vmc): "i"(offsetof(mrb_vm_env, ctx)))

#define PROLOGUE_regs \
  mrb_value *regs;\
  asm("mov %0, r%1": "=r"(regs): "i"(NIOS2_STACK_REG))

#define PROLOGUE_ret \
  mrb_code *ret;\
  asm("mov %0, ra": "=r"(ret))

#define PROLOGUE_regs_mrb \
  PROLOGUE_regs; PROLOGUE_mrb

#define PROLOGUE_regs_mrb_vmc \
  PROLOGUE_regs; PROLOGUE_mrb_vmc

#define PROLOGUE_regs_mrb_vmc_ret \
  PROLOGUE_regs; PROLOGUE_mrb_vmc; PROLOGUE_ret

void mrb_vm_stack_copy(mrb_value *dst, const mrb_value *src, size_t size);
void mrb_vm_stack_extend(mrb_state *mrb, int room, int keep);
struct REnv* mrb_vm_uvenv(mrb_state *mrb, int up);
struct REnv* mrb_vm_top_env(mrb_state *mrb, struct RProc *proc);
mrb_callinfo* mrb_vm_cipush(mrb_state *mrb);
void mrb_vm_cipop(mrb_state *mrb);
void mrb_vm_ecall(mrb_state *mrb, int i);

static mrb_pair
vm_argary(uint32_t bx)
{
  PROLOGUE_regs_mrb_vmc;
  /* A Bx   R(A) := argument array (16=6:1:5:4) */
  int m1 = (bx>>10)&0x3f;
  int r  = (bx>>9)&0x1;
  int m2 = (bx>>4)&0x1f;
  int lv = (bx>>0)&0xf;
  mrb_pair args;
  mrb_value *stack;

  if (lv == 0) stack = regs + 1;
  else {
    struct REnv *e = mrb_vm_uvenv(mrb, lv-1);
    if (!e) {
      mrb_value exc;
      static const char m[] = "super called outside of method";
      exc = mrb_exc_new(mrb, E_NOMETHOD_ERROR, m, sizeof(m) - 1);
      mrb->exc = mrb_obj_ptr(exc);
      // goto L_RAISE;
      // TODO
      while(1);
    }
    stack = e->stack + 1;
  }
  if (r == 0) {
    args.first = mrb_ary_new_from_values(mrb, m1+m2, stack);
  }
  else {
    mrb_value *pp = NULL;
    struct RArray *rest;
    int len = 0;

    if (mrb_array_p(stack[m1])) {
      struct RArray *ary = mrb_ary_ptr(stack[m1]);

      pp = ary->ptr;
      len = ary->len;
    }
    args.first = mrb_ary_new_capa(mrb, m1+len+m2);
    rest = mrb_ary_ptr(args.first);
    mrb_vm_stack_copy(rest->ptr, stack, m1);
    if (len > 0) {
      mrb_vm_stack_copy(rest->ptr+m1, pp, len);
    }
    if (m2 > 0) {
      mrb_vm_stack_copy(rest->ptr+m1+len, stack+m1+1, m2);
    }
    rest->len = m1+len+m2;
  }
  args.second = stack[m1+r+m2];
  mrb_gc_arena_restore(mrb, vmc->ai);

  return args;
}

static void
vm_ary_cat(mrb_value recv, mrb_value other)
{
  PROLOGUE_mrb_vmc;
  /* A B            mrb_ary_concat(R(A),R(B)) */
  mrb_ary_concat(mrb, recv, mrb_ary_splat(mrb, other));
  mrb_gc_arena_restore(mrb, vmc->ai);
}

static mrb_value
vm_ary_fetch(mrb_value recv, uint32_t index)
{
  PROLOGUE_mrb;
  /* A B C          R(A) := R(B)[C] */
  if (!mrb_array_p(recv)) {
    if (index == 0) {
      return recv;
    }
    else {
      return mrb_nil_value();
    }
  }
  else {
    return mrb_ary_ref(mrb, recv, index);
  }
}

static mrb_value
vm_ary_new(mrb_value *array, uint32_t max)
{
  PROLOGUE_mrb_vmc;
  /* A B C          R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
  mrb_value ary;
  ary = mrb_ary_new_from_values(mrb, max, array);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return ary;
}

static void
vm_ary_post(mrb_value *array, uint32_t pre, uint32_t post)
{
  PROLOGUE_mrb_vmc;
  /* A B C  *R(A),R(A+1)..R(A+C) := R(A) */
  int a = 0;
  mrb_value v = array[0];

  if (!mrb_array_p(v)) {
    array[a++] = mrb_ary_new_capa(mrb, 0);
    while (post--) {
      array[a] = mrb_nil_value();
      a++;
    }
  }
  else {
    struct RArray *ary = mrb_ary_ptr(v);
    int len = ary->len;
    int i;

    if (len > pre + post) {
      array[a++] = mrb_ary_new_from_values(mrb, len - pre - post, ary->ptr+pre);
      while (post--) {
        array[a++] = ary->ptr[len-post-1];
      }
    }
    else {
      array[a++] = mrb_ary_new_capa(mrb, 0);
      for (i=0; i+pre<len; i++) {
        array[a+i] = ary->ptr[pre+i];
      }
      while (i < post) {
        array[a+i] = mrb_nil_value();
        i++;
      }
    }
  }
  mrb_gc_arena_restore(mrb, vmc->ai);
}

static void
vm_ary_push(mrb_value recv, mrb_value value)
{
  PROLOGUE_mrb;
  /* A B            R(A).push(R(B)) */
  mrb_ary_push(mrb, recv, value);
}

static void
vm_ary_store(mrb_value recv, uint32_t index, mrb_value value)
{
  PROLOGUE_mrb;
  /* A B C          R(B)[C] := R(A) */
  mrb_ary_set(mrb, recv, index, value);
}

static mrb_value
vm_blk_push(uint32_t bx)
{
  PROLOGUE_regs_mrb;
  /* A Bx   R(A) := block (16=6:1:5:4) */
  int m1 = (bx>>10)&0x3f;
  int r  = (bx>>9)&0x1;
  int m2 = (bx>>4)&0x1f;
  int lv = (bx>>0)&0xf;
  mrb_value *stack;

  if (lv == 0) stack = regs + 1;
  else {
    struct REnv *e = mrb_vm_uvenv(mrb, lv-1);
    if (!e) {
      /* localjump_error(mrb, LOCALJUMP_ERROR_YIELD);
      goto L_RAISE; TODO */
      while(1);
    }
    stack = e->stack + 1;
  }
  return stack[m1+r+m2];
}

static mrb_value
vm_blockexec(int a, uint32_t bx)
{
  PROLOGUE_regs_mrb_vmc_ret;
  /* A Bx   R(A) := blockexec(R(A),SEQ[Bx]) */
  mrb_callinfo *ci;
  mrb_value recv = regs[a];
  struct RProc *p;

  /* prepare stack */
  ci = mrb_vm_cipush(mrb);
  ci->pc = ret;
  ci->acc = a;
  ci->mid = 0;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = 0;
  ci->target_class = mrb_class_ptr(recv);

  /* prepare stack */
  mrb->c->stack += a;

  p = mrb_proc_new(mrb, mrb->irep[vmc->irep->idx+bx]);
  p->target_class = ci->target_class;
  ci->proc = p;

  if (MRB_PROC_CFUNC_P(p)) {
    mrb_value result;
    result = p->body.func(mrb, recv);
    mrb_gc_arena_restore(mrb, vmc->ai);
    /*if (mrb->exc) goto L_RAISE;*/;
    /* pop stackpos */
    regs = mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
    mrb_vm_cipop(mrb);
    return result;
  }
  else {
    vmc->irep = p->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    mrb_vm_stack_extend(mrb, vmc->irep->nregs, 1);
    ci->nregs = vmc->irep->nregs;
    return mrb_vm_exec(mrb, vmc->irep->iseq);
  }
}

static mrb_value
vm_call(void)
{
}

static void
vm_ensure_pop(uint32_t times)
{
}

static void
vm_ensure_push(uint32_t bx)
{
}

static int32_t
vm_enter(uint32_t ax)
{
  PROLOGUE_regs_mrb;
  /* Ax             arg setup according to flags (24=5:5:1:5:5:1:1) */
  /* number of optional arguments times OP_JMP should follow */
  int m1 = (ax>>18)&0x1f;
  int o  = (ax>>13)&0x1f;
  int r  = (ax>>12)&0x1;
  int m2 = (ax>>7)&0x1f;
  /* unused
     int k  = (ax>>2)&0x1f;
     int kd = (ax>>1)&0x1;
     int b  = (ax>>0)& 0x1;
     */
  int argc = mrb->c->ci->argc;
  mrb_value *argv = regs+1;
  mrb_value *argv0 = argv;
  int len = m1 + o + r + m2;
  mrb_value *blk = &argv[argc < 0 ? 1 : argc];

  if (argc < 0) {
    struct RArray *ary = mrb_ary_ptr(regs[1]);
    argv = ary->ptr;
    argc = ary->len;
    mrb_gc_protect(mrb, regs[1]);
  }
  if (mrb->c->ci->proc && MRB_PROC_STRICT_P(mrb->c->ci->proc)) {
    if (argc >= 0) {
      if (argc < m1 + m2 || (r == 0 && argc > len)) {
        /* TODO
        argnum_error(mrb, m1+m2);
        goto L_RAISE;*/
        while(1);
      }
    }
  }
  else if (len > 1 && argc == 1 && mrb_array_p(argv[0])) {
    argc = mrb_ary_ptr(argv[0])->len;
    argv = mrb_ary_ptr(argv[0])->ptr;
  }
  mrb->c->ci->argc = len;
  if (argc < len) {
    regs[len+1] = *blk; /* move block */
    if (argv0 != argv) {
      value_move(&regs[1], argv, argc-m2); /* m1 + o */
    }
    if (m2) {
      int mlen = m2;
      if (argc-m2 <= m1) {
        mlen = argc - m1;
      }
      value_move(&regs[len-m2+1], &argv[argc-mlen], mlen);
    }
    if (r) {
      regs[m1+o+1] = mrb_ary_new_capa(mrb, 0);
    }
    if (o == 0) return 0;
    else
      return (argc - m1 - m2);
  }
  else {
    if (argv0 != argv) {
      regs[len+1] = *blk; /* move block */
      value_move(&regs[1], argv, m1+o);
    }
    if (r) {
      regs[m1+o+1] = mrb_ary_new_from_values(mrb, argc-m1-o-m2, argv+m1+o);
    }
    if (m2) {
      if (argc-m2 > m1) {
        value_move(&regs[m1+o+r+1], &argv[argc-m2], m2);
      }
    }
    if (argv0 == argv) {
      regs[len+1] = *blk; /* move block */
    }
    return o;
  }
}

static mrb_value
vm_getglobal(mrb_sym sym)
{
}

static void
vm_setglobal(mrb_sym sym, mrb_value value)
{
}

static mrb_value
vm_ivget(mrb_sym sym)
{
}

static void
vm_ivset(mrb_sym sym, mrb_value value)
{
}

static mrb_value
vm_cvget(mrb_sym sym)
{
}

static void
vm_cvset(mrb_sym sym, mrb_value value)
{
}

static mrb_value
vm_constget(mrb_sym sym)
{
}

static void
vm_constset(mrb_sym sym, mrb_value value)
{
}

static mrb_value
vm_getmcnst(mrb_value recv, mrb_sym sym)
{
  PROLOGUE_mrb;
  /* A B C  R(A) := R(C)::Sym(B) */
  return mrb_const_get(mrb, recv, sym);
}

static void
vm_setmcnst(mrb_value recv, mrb_sym sym, mrb_value value)
{
}

static mrb_value
vm_getupvar(uint32_t b, uint32_t c)
{
}

static void
vm_setupvar(uint32_t b, uint32_t c, mrb_value value)
{
}

static mrb_value
vm_hash_new(mrb_value *array, uint32_t pairs)
{
  PROLOGUE_mrb_vmc;
  /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C)) */
  int i = 0;
  mrb_value hash = mrb_hash_new_capa(mrb, pairs);

  while (i < pairs) {
    mrb_hash_set(mrb, hash, array[i], array[i+1]);
    i+=2;
  }
  mrb_gc_arena_restore(mrb, vmc->ai);
  return hash;
}

static mrb_value
vm_lambda(uint32_t bz, uint32_t cm)
{
  PROLOGUE_mrb_vmc;
  /* A b c  R(A) := lambda(SEQ[b],c) (b:c = 14:2) */
  mrb_value proc;
  struct RProc *p;

  if (cm & OP_L_CAPTURE) {
    p = mrb_closure_new(mrb, mrb->irep[vmc->irep->idx+bz]);
  }
  else {
    p = mrb_proc_new(mrb, mrb->irep[vmc->irep->idx+bz]);
  }
  if (cm & OP_L_STRICT) p->flags |= MRB_PROC_STRICT;
  proc = mrb_obj_value(p);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return proc;
}

static mrb_value
vm_newclass(mrb_value base, mrb_sym sym, mrb_value super)
{
  PROLOGUE_mrb_vmc;
  /* A B    R(A) := newclass(R(A),Sym(B),R(A+1)) */
  mrb_value cls;
  struct RClass *c = 0;

  if (mrb_nil_p(base)) {
    base = mrb_obj_value(mrb->c->ci->target_class);
  }
  c = mrb_vm_define_class(mrb, base, super, sym);
  cls = mrb_obj_value(c);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return cls;
}

static void
vm_newmethod(mrb_value base, mrb_sym sym, mrb_value closure)
{
  PROLOGUE_mrb_vmc;
  /* A B            R(A).newmethod(Sym(B),R(A+1)) */
  struct RClass *c = mrb_class_ptr(base);

  mrb_define_method_vm(mrb, c, sym, closure);
  mrb_gc_arena_restore(mrb, vmc->ai);
}

static mrb_value
vm_newmodule(mrb_value base, mrb_sym sym)
{
  PROLOGUE_mrb_vmc;
  /* A B            R(A) := newmodule(R(A),Sym(B)) */
  struct RClass *c = 0;
  mrb_value cls;

  if (mrb_nil_p(base)) {
    base = mrb_obj_value(mrb->c->ci->target_class);
  }
  c = mrb_vm_define_module(mrb, base, sym);
  cls = mrb_obj_value(c);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return cls;
}

static void
vm_raise(mrb_value obj)
{
}

static mrb_value
vm_range_new(mrb_value first, mrb_value second, uint32_t exclude)
{
  PROLOGUE_mrb_vmc;
  /* A B C  R(A) := range_new(R(B),R(B+1),C) */
  mrb_value range = mrb_range_new(mrb, first, second, exclude);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return range;
}

static mrb_value
vm_rescue(void)
{
}

static void
vm_rescue_pop(uint32_t times)
{
}

static void
vm_rescue_push(mrb_code *base, int32_t offset)
{
}

static void *
vm_ret(mrb_value v, int b)
{
  PROLOGUE_regs_mrb_vmc;
  /* A      return R(A) */
  mrb_code *pc;
  if (mrb->exc) {
    /* TODO: */
    while(1);
  }
  else {
    mrb_callinfo *ci = mrb->c->ci;
    int acc, eidx = mrb->c->ci->eidx;
    struct RProc *proc = vmc->proc;
    switch (b) {
      case OP_R_RETURN:
        // Fall through to OP_R_NORMAL otherwise
        if (proc->env && !MRB_PROC_STRICT_P(proc)) {
          struct REnv *e = mrb_vm_top_env(mrb, proc);

          if (e->cioff < 0) {
            /*TODO:localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            goto L_RAISE;*/
            while(1);
          }
          ci = mrb->c->cibase + e->cioff;
          if (ci == mrb->c->cibase) {
            /*TODO:localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            goto L_RAISE;*/
            while(1);
          }
          mrb->c->ci = ci;
          break;
        }
      case OP_R_NORMAL:
        if (ci == mrb->c->cibase) {
          if (!mrb->c->prev) { /* toplevel return */
            /*TODO:localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            goto L_RAISE;*/
            while(1);
          }
          if (mrb->c->prev->ci == mrb->c->prev->cibase) {
            /*TODO:mrb_value exc = mrb_exc_new3(mrb, E_RUNTIME_ERROR, mrb_str_new(mrb, "double resume", 13));
            mrb->exc = mrb_obj_ptr(exc);
            goto L_RAISE;*/
            while(1);
          }
          /* automatic yield at the end */
          mrb->c->status = MRB_FIBER_TERMINATED;
          mrb->c = mrb->c->prev;
        }
        ci = mrb->c->ci;
        break;
      case OP_R_BREAK:
        if (proc->env->cioff < 0) {
          /*TODO:localjump_error(mrb, LOCALJUMP_ERROR_BREAK);
          goto L_RAISE;*/
          while(1);
        }
        ci = mrb->c->ci = mrb->c->cibase + proc->env->cioff + 1;
        break;
      default:
        /* cannot happen */
        break;
    }
    ci = mrb->c->ci = mrb->c->cibase + proc->env->cioff + 1;
    while (eidx > mrb->c->ci[-1].eidx) {
      mrb_vm_ecall(mrb, --eidx);
    }
    mrb_vm_cipop(mrb);
    acc = ci->acc;
    pc = ci->pc;
    regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    if (acc < 0) {
      mrb->jmp = vmc->prev_jmp;
      return mrb_obj_ptr(v);
    }
    // DEBUG(printf("from :%s\n", mrb_sym2name(mrb, ci->mid)));
    vmc->proc = mrb->c->ci->proc;
    vmc->irep = vmc->proc->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;

    regs[acc] = v;
  }
  return pc;
}

static mrb_value
vm_send_array(mrb_value *argv, mrb_sym sym)
{
}

static mrb_value
vm_send_normal(mrb_value *argv, mrb_sym sym, uint32_t argc)
{
}

static mrb_value
vm_singleton_class(mrb_value base)
{
  PROLOGUE_mrb_vmc;
  /* A B    R(A) := R(B).singleton_class */
  mrb_value cls;
  cls = mrb_singleton_class(mrb, base);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return cls;
}

static void
vm_str_cat(mrb_value recv, mrb_value other)
{
  PROLOGUE_mrb;
  /* A B    R(A).concat(R(B)) */
  mrb_str_concat(mrb, recv, other);
}

static mrb_value
vm_str_dup(mrb_value lit)
{
  PROLOGUE_mrb_vmc;
  /* A Bx           R(A) := str_new(Lit(Bx)) */
  mrb_value str = mrb_str_literal(mrb, lit);
  mrb_gc_arena_restore(mrb, vmc->ai);
  return str;
}

static mrb_value
vm_super(mrb_value *argv2, uint32_t argc)
{
}

static mrb_value
vm_target_class(void)
{
  PROLOGUE_mrb;
  /* A B    R(A) := target_class */
  if (!mrb->c->ci->target_class) {
    /* TODO */
    // static const char msg[] = "no target class or module";
    // mrb_value exc = mrb_exc_new(mrb, E_TYPE_ERROR, msg, sizeof(msg) - 1);
    // mrb->exc = mrb_obj_ptr(exc);
    // goto L_RAISE;
    while(1);
  }
  return mrb_obj_value(mrb->c->ci->target_class);
}

static void
vm_stop_vm(void)
{
}

static void
vm_raise_err(mrb_value obj)
{
}

static const mrb_vm_env env_initializer = {
  .argary          = vm_argary,
  .ary_cat         = vm_ary_cat,
  .ary_fetch       = vm_ary_fetch,
  .ary_new         = vm_ary_new,
  .ary_post        = vm_ary_post,
  .ary_push        = vm_ary_push,
  .ary_store       = vm_ary_store,
  .blk_push        = vm_blk_push,
  .blockexec       = vm_blockexec,
  .call            = vm_call,
  .ensure_pop      = vm_ensure_pop,
  .ensure_push     = vm_ensure_push,
  .enter           = vm_enter,
  .getglobal       = vm_getglobal,
  .setglobal       = vm_setglobal,
  .ivget           = vm_ivget,
  .ivset           = vm_ivset,
  .cvget           = vm_cvget,
  .cvset           = vm_cvset,
  .constget        = vm_constget,
  .constset        = vm_constset,
  .getmcnst        = vm_getmcnst,
  .setmcnst        = vm_setmcnst,
  .getupvar        = vm_getupvar,
  .setupvar        = vm_setupvar,
  .hash_new        = vm_hash_new,
  .lambda          = vm_lambda,
  .newclass        = vm_newclass,
  .newmethod       = vm_newmethod,
  .newmodule       = vm_newmodule,
  .raise           = vm_raise,
  .range_new       = vm_range_new,
  .rescue          = vm_rescue,
  .rescue_pop      = vm_rescue_pop,
  .rescue_push     = vm_rescue_push,
  .ret             = vm_ret,
  // .ret_break       = vm_ret_break,
  // .ret_normal      = vm_ret_normal,
  // .ret_return      = vm_ret_return,
  .send_array      = vm_send_array,
  .send_normal     = vm_send_normal,
  .singleton_class = vm_singleton_class,
  .str_cat         = vm_str_cat,
  .str_dup         = vm_str_dup,
  .super           = vm_super,
  .target_class    = vm_target_class,
  .stop_vm         = vm_stop_vm,
  .raise_err       = vm_raise_err,
};

void
mrb_vm_env_init(mrb_state *mrb)
{
  mrb_vm_env *vme;
  vme = mrb_malloc(mrb, sizeof(mrb_vm_env));
  memcpy(vme, &env_initializer, sizeof(env_initializer));

  vme->mrb = mrb;
  vme->object_class = mrb_obj_value(mrb->object_class);

  mrb->vm_env = vme;
}

mrb_value
NAKED mrb_vm_exec(mrb_state *mrb, mrb_code *code)
{
  asm("\t"
  "ldw  r%0, %2(r4)\n\t"  /* VMENV_REG = mrb->vm_env; */
  "ldw  r%1, %3(r4)\n\t"  /* temp = mrb->c; */
  "ldw  r%1, %4(r%1)\n\t" /* STACK_REG = temp->stack; */
  "jmp  r5\n"
  ::
  "i"(NIOS2_VMENV_REG), "i"(NIOS2_STACK_REG),
  "i"(offsetof(mrb_state, vm_env)),
  "i"(offsetof(mrb_state, c)),
  "i"(offsetof(struct mrb_context, stack))
  );

  return mrb_nil_value(); /* never reached */
}

#endif  /* MRB_MACHINE_NIOS2 */
