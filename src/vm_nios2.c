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
#include "error.h"
#include "opcode.h"

#ifdef MRB_MACHINE_NIOS2

#define NAKED     __attribute__((naked))

#define TO_STR(x) TO_STR_(x)
#define TO_STR_(x) #x

// #define ENABLE_DPRINTF
#ifdef ENABLE_DPRINTF
#define DPRINTF(mrb, fmt, ...) \
  if(mrb->vm_env->dprintf) mrb->vm_env->dprintf(fmt, ## __VA_ARGS__)
#else
#define DPRINTF(mrb, fmt, ...)  do {} while(0)
#endif

#define ENTRY_vme(name, rtype, argc, ...) \
  static void\
  NAKED name##_entry(void)\
  {\
    asm("\t"\
    "addi sp, sp, -4\n\t"\
    "stw  ra, 0(sp)\n\t"\
    "mov  r%0, r%1\n\t"\
    "call " #name "\n\t"\
    "ldw  ra, 0(sp)\n\t"\
    "addi sp, sp, 4\n\t"\
    "ret\n"\
    ::\
    "i"(argc+4), "i"(NIOS2_VMENV_REG));\
  }\
  __attribute__((used))\
  static rtype\
  name(__VA_ARGS__ mrb_vm_env *vme)

#define ENTRY_mrb(name, rtype, argc, ...)  \
  static void\
  NAKED name##_entry(void)\
  {\
    asm("\t"\
    "addi sp, sp, -4\n\t"\
    "stw  ra, 0(sp)\n\t"\
    "ldw  r%0, %1(r%2)\n\t"\
    "call " #name "\n\t"\
    "ldw  ra, 0(sp)\n\t"\
    "addi sp, sp, 4\n\t"\
    "ret\n"\
    ::\
    "i"(argc+4), "i"(offsetof(mrb_vm_env, mrb)), "i"(NIOS2_VMENV_REG));\
  }\
  __attribute__((used))\
  static rtype\
  name(__VA_ARGS__ mrb_state *mrb)

typedef struct {
  mrb_code *pc;     /* 0 */
  mrb_value *regs;  /* 4 */
} vm_jmpinfo;

#define ENTRY_vme_jmp(name, rtype, argc, ...)  \
  static void\
  NAKED name##_entry(void)\
  {\
    asm("\t"\
    "addi sp, sp, -8\n\t"\
    "stw  ra, %0(sp)\n\t"\
    "stw  r%1, %2(sp)\n\t"\
    "mov  r%3, r%4\n\t"\
    "mov  r%5, sp\n\t"\
    "call " #name "\n\t"\
    "ldw  r%1, %2(sp)\n\t"\
    "ldw  ra, %0(sp)\n\t"\
    "addi sp, sp, 8\n\t"\
    "ret\n"\
    ::\
    "i"(offsetof(vm_jmpinfo, pc)),\
    "i"(NIOS2_STACK_REG), "i"(offsetof(vm_jmpinfo, regs)),\
    "i"(argc+4), "i"(NIOS2_VMENV_REG),\
    "i"(argc+5));\
  }\
  __attribute__((used))\
  static rtype\
  name(__VA_ARGS__ mrb_vm_env *vme, vm_jmpinfo *jmp)

static const char debug_indents[] = "                ";
#define DPRINT_INDENT(mrb)    DPRINTF(mrb, "%s", debug_indents + (16 - (mrb->c->ci - mrb->c->cibase)))

typedef enum {
  LOCALJUMP_ERROR_RETURN = 0,
  LOCALJUMP_ERROR_BREAK = 1,
  LOCALJUMP_ERROR_YIELD = 2
} localjump_error_kind;

void mrb_vm_stack_copy(mrb_value *dst, const mrb_value *src, size_t size);
void mrb_vm_stack_extend(mrb_state *mrb, int room, int keep);
struct REnv* mrb_vm_uvenv(mrb_state *mrb, int up);
struct REnv* mrb_vm_top_env(mrb_state *mrb, struct RProc *proc);
mrb_callinfo* mrb_vm_cipush(mrb_state *mrb);
void mrb_vm_cipop(mrb_state *mrb);
void mrb_vm_ecall(mrb_state *mrb, int i);
void mrb_vm_localjump_error(mrb_state *mrb, localjump_error_kind kind);
void mrb_vm_argnum_error(mrb_state *mrb, int num);

static void vm_raise_exc(mrb_vm_env *vme, vm_jmpinfo *jmp);
static void vm_epilogue(void);

ENTRY_vme_jmp(vm_argary, mrb_pair, 1, uint32_t bx, )
{
  /* A Bx   R(A) := argument array (16=6:1:5:4) */
  mrb_state *mrb = vme->mrb;
  int m1 = (bx>>10)&0x3f;
  int r  = (bx>>9)&0x1;
  int m2 = (bx>>4)&0x1f;
  int lv = (bx>>0)&0xf;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_ARGARY (0x%x:0x%x:0x%x:0x%x)\n", m1, r, m2, lv);
  mrb_pair args;
  mrb_value *stack;

  if (lv == 0) stack = jmp->regs + 1;
  else {
    struct REnv *e = mrb_vm_uvenv(mrb, lv-1);
    if (!e) {
      mrb_value exc;
      static const char m[] = "super called outside of method";
      exc = mrb_exc_new(mrb, E_NOMETHOD_ERROR, m, sizeof(m) - 1);
      mrb->exc = mrb_obj_ptr(exc);
      vm_raise_exc(vme, jmp);
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
  mrb_gc_arena_restore(mrb, vme->ctx->ai);

  return args;
}

ENTRY_vme(vm_ary_cat, void, 2, mrb_value recv, mrb_value other, )
{
  /* A B            mrb_ary_concat(R(A),R(B)) */
  mrb_state *mrb = vme->mrb;
  mrb_ary_concat(mrb, recv, mrb_ary_splat(mrb, other));
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
}

ENTRY_mrb(vm_ary_fetch, mrb_value, 2, mrb_value recv, uint32_t index, )
{
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

ENTRY_vme(vm_ary_new, mrb_value, 2, mrb_value *array, uint32_t max, )
{
  /* A B C          R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
  mrb_state *mrb = vme->mrb;
  mrb_value ary;
  ary = mrb_ary_new_from_values(mrb, max, array);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return ary;
}

ENTRY_vme(vm_ary_post, void, 3, mrb_value *array, uint32_t pre, uint32_t post, )
{
  /* A B C  *R(A),R(A+1)..R(A+C) := R(A) */
  mrb_state *mrb = vme->mrb;
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
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
}

ENTRY_mrb(vm_ary_push, void, 2, mrb_value recv, mrb_value value, )
{
  /* A B            R(A).push(R(B)) */
  mrb_ary_push(mrb, recv, value);
}

ENTRY_mrb(vm_ary_store, void, 3, mrb_value recv, uint32_t index, mrb_value value, )
{
  /* A B C          R(B)[C] := R(A) */
  mrb_ary_set(mrb, recv, index, value);
}

ENTRY_vme_jmp(vm_blk_push, mrb_value, 1, uint32_t bx, )
{
  /* A Bx   R(A) := block (16=6:1:5:4) */
  mrb_state *mrb = vme->mrb;
  int m1 = (bx>>10)&0x3f;
  int r  = (bx>>9)&0x1;
  int m2 = (bx>>4)&0x1f;
  int lv = (bx>>0)&0xf;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_BLKPUSH (0x%x:0x%x:0x%x:0x%x)\n", m1, r, m2, lv);
  mrb_value *stack;

  if (lv == 0) stack = jmp->regs + 1;
  else {
    struct REnv *e = mrb_vm_uvenv(mrb, lv-1);
    if (!e) {
      mrb_vm_localjump_error(mrb, LOCALJUMP_ERROR_YIELD);
      vm_raise_exc(vme, jmp);
    }
    stack = e->stack + 1;
  }
  return stack[m1+r+m2];
}

ENTRY_vme_jmp(vm_blockexec, mrb_value, 2, int a, uint32_t bx, )
{
  /* A Bx   R(A) := blockexec(R(A),SEQ[Bx]) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_EXEC (a=0x%x, SEQ[0x%x])\n", a, bx);
  mrb_callinfo *ci;
  mrb_value recv = jmp->regs[a];
  struct RProc *p;

  /* prepare stack */
  ci = mrb_vm_cipush(mrb);
  ci->pc = jmp->pc;
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
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    jmp->regs = mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
    mrb_vm_cipop(mrb);
    return result;
  }
  else {
    vmc->irep = p->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    mrb_vm_stack_extend(mrb, vmc->irep->nregs, 1);
    ci->nregs = vmc->irep->nregs;
    jmp->regs = mrb->c->stack;
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
    return mrb_nil_value();
  }
}

ENTRY_vme_jmp(vm_call, void, 0, )
{
  /* A      R(A) := self.call(frame.argc, frame.argv) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  mrb_callinfo *ci;
  mrb_value recv = mrb->c->stack[0];
  struct RProc *m = mrb_proc_ptr(recv);

  /* replace callinfo */
  ci = mrb->c->ci;
  ci->target_class = m->target_class;
  ci->proc = m;
  if (m->env) {
    if (m->env->mid) {
      ci->mid = m->env->mid;
    }
    if (!m->env->stack) {
      m->env->stack = mrb->c->stack;
    }
  }

  /* prepare stack */
  if (MRB_PROC_CFUNC_P(m)) {
    recv = m->body.func(mrb, recv);
    mrb_gc_arena_restore(mrb, vmc->ai);
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    ci = mrb->c->ci;
    jmp->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    jmp->regs[ci->acc] = recv;
    jmp->pc = ci->pc;
    mrb_vm_cipop(mrb);
    vmc->irep = mrb->c->ci->proc->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
  }
  else {
    /* setup environment for calling method */
    vmc->proc = m;
    vmc->irep = m->body.irep;
    if (!vmc->irep) {
      mrb->c->stack[0] = mrb_nil_value();
      asm("break");
      while(1);
      // goto L_RETURN; TODO
    }
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    ci->nregs = vmc->irep->nregs;
    if (ci->argc < 0) {
      mrb_vm_stack_extend(mrb, (vmc->irep->nregs < 3) ? 3 : vmc->irep->nregs, 3);
    }
    else {
      mrb_vm_stack_extend(mrb, vmc->irep->nregs, ci->argc+2);
    }
    jmp->regs = mrb->c->stack;
    jmp->regs[0] = m->env->stack[0];
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
  }
}

ENTRY_vme(vm_ensure_pop, void, 1, uint32_t times, )
{
  /* A      A.times{ensure_pop().call} */
  mrb_state *mrb = vme->mrb;
  int n;

  for (n=0; n<times; n++) {
    mrb_vm_ecall(mrb, --mrb->c->ci->eidx);
  }
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
}

ENTRY_vme(vm_ensure_push, void, 1, uint32_t bx, )
{
  /* Bx     ensure_push(SEQ[Bx]) */
  mrb_state *mrb = vme->mrb;
  struct RProc *p;

  p = mrb_closure_new(mrb, mrb->irep[vme->ctx->irep->idx+bx]);
  /* push ensure_stack */
  if (mrb->c->esize <= mrb->c->ci->eidx) {
    if (mrb->c->esize == 0) mrb->c->esize = 16;
    else mrb->c->esize *= 2;
    mrb->c->ensure = (struct RProc **)mrb_realloc(mrb, mrb->c->ensure, sizeof(struct RProc*) * mrb->c->esize);
  }
  mrb->c->ensure[mrb->c->ci->eidx++] = p;
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
}

ENTRY_vme_jmp(vm_enter, int32_t, 1, uint32_t ax, )
{
  /* Ax             arg setup according to flags (24=5:5:1:5:5:1:1) */
  /* number of optional arguments times OP_JMP should follow */
  mrb_state *mrb = vme->mrb;
  int m1 = (ax>>18)&0x1f;
  int o  = (ax>>13)&0x1f;
  int r  = (ax>>12)&0x1;
  int m2 = (ax>>7)&0x1f;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_ENTER (0x%x:0x%x:0x%x:0x%x:x:x:x)\n", m1, o, r, m2);
  /* unused
     int k  = (ax>>2)&0x1f;
     int kd = (ax>>1)&0x1;
     int b  = (ax>>0)& 0x1;
     */
  int argc = mrb->c->ci->argc;
  mrb_value *argv = jmp->regs+1;
  mrb_value *argv0 = argv;
  int len = m1 + o + r + m2;
  mrb_value *blk = &argv[argc < 0 ? 1 : argc];

  if (argc < 0) {
    struct RArray *ary = mrb_ary_ptr(jmp->regs[1]);
    argv = ary->ptr;
    argc = ary->len;
    mrb_gc_protect(mrb, jmp->regs[1]);
  }
  if (mrb->c->ci->proc && MRB_PROC_STRICT_P(mrb->c->ci->proc)) {
    if (argc >= 0) {
      if (argc < m1 + m2 || (r == 0 && argc > len)) {
        mrb_vm_argnum_error(mrb, m1+m2);
        vm_raise_exc(vme, jmp);
      }
    }
  }
  else if (len > 1 && argc == 1 && mrb_array_p(argv[0])) {
    argc = mrb_ary_ptr(argv[0])->len;
    argv = mrb_ary_ptr(argv[0])->ptr;
  }
  mrb->c->ci->argc = len;
  if (argc < len) {
    jmp->regs[len+1] = *blk; /* move block */
    if (argv0 != argv) {
      value_move(&jmp->regs[1], argv, argc-m2); /* m1 + o */
    }
    if (m2) {
      int mlen = m2;
      if (argc-m2 <= m1) {
        mlen = argc - m1;
      }
      value_move(&jmp->regs[len-m2+1], &argv[argc-mlen], mlen);
    }
    if (r) {
      jmp->regs[m1+o+1] = mrb_ary_new_capa(mrb, 0);
    }
    if (o == 0) return 0;
    else
      return (argc - m1 - m2);
  }
  else {
    if (argv0 != argv) {
      jmp->regs[len+1] = *blk; /* move block */
      value_move(&jmp->regs[1], argv, m1+o);
    }
    if (r) {
      jmp->regs[m1+o+1] = mrb_ary_new_from_values(mrb, argc-m1-o-m2, argv+m1+o);
    }
    if (m2) {
      if (argc-m2 > m1) {
        value_move(&jmp->regs[m1+o+r+1], &argv[argc-m2], m2);
      }
    }
    if (argv0 == argv) {
      jmp->regs[len+1] = *blk; /* move block */
    }
    return o;
  }
}

ENTRY_mrb(vm_getglobal, mrb_value, 1, mrb_sym sym, )
{
  /* A B    R(A) := getglobal(Sym(B)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETGLOBAL (sym=:%s)\n", mrb_sym2name(mrb, sym));
  return mrb_gv_get(mrb, sym);
}

ENTRY_mrb(vm_setglobal, void, 2, mrb_sym sym, mrb_value value, )
{
  /* setglobal(Sym(b), R(A)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETGLOBAL (sym=:%s, value=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(value));
  mrb_gv_set(mrb, sym, value);
}

ENTRY_mrb(vm_ivget, mrb_value, 1, mrb_sym sym, )
{
  /* A Bx   R(A) := ivget(Bx) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETIV (sym=:%s)\n", mrb_sym2name(mrb, sym));
  return mrb_vm_iv_get(mrb, sym);
}

ENTRY_mrb(vm_ivset, void, 2, mrb_sym sym, mrb_value value, )
{
  /* ivset(Sym(B),R(A)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETIV (sym=:%s, value=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(value));
  mrb_vm_iv_set(mrb, sym, value);
}

ENTRY_mrb(vm_cvget, mrb_value, 1, mrb_sym sym, )
{
  /* A B    R(A) := cvget(Sym(B)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETCV (sym=:%s)\n", mrb_sym2name(mrb, sym));
  return mrb_vm_cv_get(mrb, sym);
}

ENTRY_mrb(vm_cvset, void, 2, mrb_sym sym, mrb_value value, )
{
  /* ivset(Sym(B),R(A)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETCV (sym=:%s, value=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(value));
  mrb_vm_cv_set(mrb, sym, value);
}

ENTRY_mrb(vm_constget, mrb_value, 1, mrb_sym sym, )
{
  /* A B    R(A) := constget(Sym(B)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETCONST (sym=:%s)\n", mrb_sym2name(mrb, sym));
  return mrb_vm_const_get(mrb, sym);
}

ENTRY_mrb(vm_constset, void, 2, mrb_sym sym, mrb_value value, )
{
  /* A B    constset(Sym(B),R(A)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETCONST (sym=:%s, value=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(value));
  mrb_vm_const_set(mrb, sym, value);
}

ENTRY_mrb(vm_getmcnst, mrb_value, 2, mrb_value recv, mrb_sym sym, )
{
  /* A B C  R(A) := R(C)::Sym(B) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETMCNST (recv=0x%x, sym=:%s)\n", mrb_obj_ptr(recv), mrb_sym2name(mrb, sym));
  return mrb_const_get(mrb, recv, sym);
}

ENTRY_mrb(vm_setmcnst, void, 3, mrb_value recv, mrb_sym sym, mrb_value value, )
{
  /* A B C  R(A+1)::Sym(B) := R(A) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETMCNST (recv=0x%x, sym=:%s, value=0x%x)\n", mrb_obj_ptr(recv), mrb_sym2name(mrb, sym), value);
  mrb_const_set(mrb, recv, sym, value);
}

ENTRY_mrb(vm_getupvar, mrb_value, 2, uint32_t idx, uint32_t up, )
{
  /* A B C  R(A) := uvget(B,C) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_GETUPVAR (idx=0x%x, up=0x%x)\n", idx, up);
  struct REnv *e = mrb_vm_uvenv(mrb, up);

  if (!e) {
    return mrb_nil_value();
  }
  else {
    return e->stack[idx];
  }
}

ENTRY_mrb(vm_setupvar, void, 3, uint32_t idx, uint32_t up, mrb_value value, )
{
  /* A B C  uvset(B,C,R(A)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SETUPVAR (idx=0x%x, up=0x%x, value=0x%x)\n", idx, up, mrb_obj_ptr(value));
  struct REnv *e = mrb_vm_uvenv(mrb, up);

  if (e) {
    e->stack[idx] = value;
    mrb_write_barrier(mrb, (struct RBasic*)e);
  }
}

ENTRY_vme(vm_hash_new, mrb_value, 2, mrb_value *array, uint32_t pairs, )
{
  /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C)) */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_HASH (array=0x%x, pairs=0x%x)\n", array, pairs);
  int i = 0;
  mrb_value hash = mrb_hash_new_capa(mrb, pairs);

  while (i < pairs) {
    mrb_hash_set(mrb, hash, array[i], array[i+1]);
    i+=2;
  }
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return hash;
}

ENTRY_vme(vm_lambda, mrb_value, 2, uint32_t bz, uint32_t cm, )
{
  /* A b c  R(A) := lambda(SEQ[b],c) (b:c = 14:2) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_LAMBDA (SEQ[0x%x], cm=0x%x)\n", bz, cm);
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

ENTRY_vme(vm_newclass, mrb_value, 3, mrb_value base, mrb_sym sym, mrb_value super, )
{
  /* A B    R(A) := newclass(R(A),Sym(B),R(A+1)) */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_CLASS (sym=:%s, base=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(base));
  mrb_value cls;
  struct RClass *c = 0;

  if (mrb_nil_p(base)) {
    base = mrb_obj_value(mrb->c->ci->target_class);
  }
  c = mrb_vm_define_class(mrb, base, super, sym);
  cls = mrb_obj_value(c);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return cls;
}

ENTRY_vme(vm_newmethod, void, 3, mrb_value base, mrb_sym sym, mrb_value closure, )
{
  /* A B            R(A).newmethod(Sym(B),R(A+1)) */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_METHOD (sym=:%s, base=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(base));
  struct RClass *c = mrb_class_ptr(base);

  mrb_define_method_vm(mrb, c, sym, closure);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
}

ENTRY_vme(vm_newmodule, mrb_value, 2, mrb_value base, mrb_sym sym, )
{
  /* A B            R(A) := newmodule(R(A),Sym(B)) */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_MODULE (sym=:%s, base=0x%x)\n", mrb_sym2name(mrb, sym), mrb_obj_ptr(base));
  struct RClass *c = 0;
  mrb_value cls;

  if (mrb_nil_p(base)) {
    base = mrb_obj_value(mrb->c->ci->target_class);
  }
  c = mrb_vm_define_module(mrb, base, sym);
  cls = mrb_obj_value(c);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return cls;
}

ENTRY_vme_jmp(vm_raise, void, 1, mrb_value obj, )
{
  /* A       raise(R(A)) */
  vme->mrb->exc = mrb_obj_ptr(obj);
  vm_raise_exc(vme, jmp);
}

ENTRY_vme(vm_range_new, mrb_value, 3, mrb_value first, mrb_value second, uint32_t exclude, )
{
  /* A B C  R(A) := range_new(R(B),R(B+1),C) */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_RANGE (0x%x..%s0x%x)\n",
    mrb_obj_ptr(first), exclude ? "." : "", mrb_obj_ptr(second));
  mrb_value range = mrb_range_new(mrb, first, second, exclude);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return range;
}

ENTRY_mrb(vm_rescue, mrb_value, 0, )
{
  /* A      R(A) := exc; clear(exc) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_RESCUE\n");
  return mrb_obj_value(mrb->exc);
}

ENTRY_mrb(vm_rescue_pop, void, 1, uint32_t times, )
{
  /* A       A.times{rescue_pop()} */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_POPERR (times=0x%x)\n", times);
  while (times--) {
    mrb->c->ci->ridx--;
  }
}

ENTRY_vme_jmp(vm_rescue_push, void, 1, int32_t offset, )
{
  /* sBx    pc+=sBx on exception */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_ONERR (offset=0x%x)\n", offset);
  if (mrb->c->rsize <= mrb->c->ci->ridx) {
    if (mrb->c->rsize == 0) mrb->c->rsize = 16;
    else mrb->c->rsize *= 2;
    mrb->c->rescue = (mrb_code **)mrb_realloc(mrb, mrb->c->rescue, sizeof(mrb_code*) * mrb->c->rsize);
  }
  mrb->c->rescue[mrb->c->ci->ridx++] = jmp->pc + offset;
}

ENTRY_vme_jmp(vm_ret, mrb_value, 2, mrb_value v, int b, )
{
  /* A      return R(A) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_RETURN (b=%x)\n", b);
  if (mrb->exc) vm_raise_exc(vme, jmp);
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
            mrb_vm_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            vm_raise_exc(vme, jmp);
          }
          ci = mrb->c->cibase + e->cioff;
          if (ci == mrb->c->cibase) {
            mrb_vm_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            vm_raise_exc(vme, jmp);
          }
          mrb->c->ci = ci;
          break;
        }
      case OP_R_NORMAL:
        if (ci == mrb->c->cibase) {
          if (!mrb->c->prev) { /* toplevel return */
            mrb_vm_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
            vm_raise_exc(vme, jmp);
          }
          if (mrb->c->prev->ci == mrb->c->prev->cibase) {
            mrb_value exc = mrb_exc_new3(mrb, E_RUNTIME_ERROR, mrb_str_new(mrb, "double resume", 13));
            mrb->exc = mrb_obj_ptr(exc);
            vm_raise_exc(vme, jmp);
          }
          /* automatic yield at the end */
          mrb->c->status = MRB_FIBER_TERMINATED;
          mrb->c = mrb->c->prev;
        }
        ci = mrb->c->ci;
        break;
      case OP_R_BREAK:
        if (proc->env->cioff < 0) {
          mrb_vm_localjump_error(mrb, LOCALJUMP_ERROR_BREAK);
          vm_raise_exc(vme, jmp);
        }
        ci = mrb->c->ci = mrb->c->cibase + proc->env->cioff + 1;
        break;
      default:
        /* cannot happen */
        break;
    }
    while (eidx > mrb->c->ci[-1].eidx) {
      mrb_vm_ecall(mrb, --eidx);
    }
    mrb_vm_cipop(mrb);
    acc = ci->acc;
    jmp->pc = ci->pc;
    jmp->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    if (acc < 0) {
      mrb->jmp = vmc->prev_jmp;
      jmp->pc = (mrb_code *)vm_epilogue;
      DPRINT_INDENT(mrb);
      DPRINTF(mrb, "--> return to epilogue\n");
      return v;
    }
    // DEBUG(printf("from :%s\n", mrb_sym2name(mrb, ci->mid)));
    vmc->proc = mrb->c->ci->proc;
    vmc->irep = vmc->proc->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> return to 0x%x\n", jmp->pc);

    jmp->regs[acc] = v;
  }
  return mrb_nil_value();
}

ENTRY_vme_jmp(vm_send_array, void, 2, int a, mrb_sym mid, )
{
  /* A B C  R(A) := call(R(A),Sym(B),R(A+1),... ,R(A+C-1)) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SEND (a=0x%x, n=*, mid=:%s)\n", a, mrb_sym2name(mrb, mid));
  struct RProc *m;
  struct RClass *c;
  mrb_callinfo *ci;
  mrb_value recv, result;

  recv = jmp->regs[a];
  c = mrb_class(mrb, recv);
  m = mrb_method_search_vm(mrb, &c, mid);
  if (!m) {
    mrb_value sym = mrb_symbol_value(mid);

    mid = mrb_intern2(mrb, "method_missing", 14);
    m = mrb_method_search_vm(mrb, &c, mid);
    mrb_ary_unshift(mrb, jmp->regs[a+1], sym);
  }

  /* push callinfo */
  ci = mrb_vm_cipush(mrb);
  ci->mid = mid;
  ci->proc = m;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = -1;
  if (c->tt == MRB_TT_ICLASS) {
    ci->target_class = c->c;
  }
  else {
    ci->target_class = c;
  }

  ci->pc = jmp->pc;
  ci->acc = a;

  /* prepare stack */
  mrb->c->stack += a;

  if (MRB_PROC_CFUNC_P(m)) {
    ci->nregs = 3;
    result = m->body.func(mrb, recv);
    mrb->c->stack[0] = result;
    mrb_gc_arena_restore(mrb, vmc->ai);
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    ci = mrb->c->ci;
    if (!ci->target_class) { /* return from context modifying method (resume/yield) */
      if (!MRB_PROC_CFUNC_P(ci[-1].proc)) {
        vmc->irep = ci[-1].proc->body.irep;
        vmc->pool = vmc->irep->pool;
        vmc->syms = vmc->irep->syms;
      }
    }
    jmp->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    jmp->pc = ci->pc;
    mrb_vm_cipop(mrb);
  }
  else {
    /* setup environment for calling method */
    vmc->proc = mrb->c->ci->proc = m;
    vmc->irep = m->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    ci->nregs = vmc->irep->nregs;
    mrb_vm_stack_extend(mrb, (vmc->irep->nregs < 3) ? 3 : vmc->irep->nregs, 3);
    jmp->regs = mrb->c->stack;
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
  }
}

ENTRY_vme_jmp(vm_send_normal, void, 2, uint32_t n_a, mrb_sym mid, )
{
  /* A B C  R(A) := call(R(A),Sym(B),R(A+1),..2 ,R(A+C-1)) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  int a = n_a & ((1 << 9) - 1);
  int n = (n_a >> 9);
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SEND (a=0x%x, n=0x%x, mid=:%s)\n", a, n, mrb_sym2name(mrb, mid));
  struct RProc *m;
  struct RClass *c;
  mrb_callinfo *ci;
  mrb_value recv, result;

  recv = jmp->regs[a];
  c = mrb_class(mrb, recv);
  m = mrb_method_search_vm(mrb, &c, mid);
  if (!m) {
    mrb_value sym = mrb_symbol_value(mid);

    mid = mrb_intern2(mrb, "method_missing", 14);
    m = mrb_method_search_vm(mrb, &c, mid);
    value_move(jmp->regs+a+2, jmp->regs+a+1, ++n);
    jmp->regs[a+1] = sym;
  }

  /* push callinfo */
  ci = mrb_vm_cipush(mrb);
  ci->mid = mid;
  ci->proc = m;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = n;
  if (c->tt == MRB_TT_ICLASS) {
    ci->target_class = c->c;
  }
  else {
    ci->target_class = c;
  }

  ci->pc = jmp->pc;
  ci->acc = a;

  /* prepare stack */
  mrb->c->stack += a;

  if (MRB_PROC_CFUNC_P(m)) {
    ci->nregs = n + 2;
    result = m->body.func(mrb, recv);
    mrb->c->stack[0] = result;
    mrb_gc_arena_restore(mrb, vmc->ai);
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    ci = mrb->c->ci;
    if (!ci->target_class) { /* return from context modifying method (resume/yield) */
      if (!MRB_PROC_CFUNC_P(ci[-1].proc)) {
        vmc->irep = ci[-1].proc->body.irep;
        vmc->pool = vmc->irep->pool;
        vmc->syms = vmc->irep->syms;
      }
    }
    jmp->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    jmp->pc = ci->pc;
    mrb_vm_cipop(mrb);
  }
  else {
    /* setup environment for calling method */
    vmc->proc = mrb->c->ci->proc = m;
    vmc->irep = m->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    ci->nregs = vmc->irep->nregs;
    mrb_vm_stack_extend(mrb, vmc->irep->nregs,  ci->argc+2);
    jmp->regs = mrb->c->stack;
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
  }
}

ENTRY_vme(vm_singleton_class, mrb_value, 1, mrb_value base, )
{
  /* A B    R(A) := R(B).singleton_class */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SCLASS (base=0x%x)\n", mrb_obj_ptr(base));
  mrb_value cls;
  cls = mrb_singleton_class(mrb, base);
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return cls;
}

ENTRY_mrb(vm_str_cat, void, 2, mrb_value recv, mrb_value other, )
{
  /* A B    R(A).concat(R(B)) */
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_STRCAT (recv=0x%x, \"%s\")\n", mrb_obj_ptr(recv), mrb_str_to_cstr(mrb, other));
  mrb_str_concat(mrb, recv, other);
}

ENTRY_vme(vm_str_dup, mrb_value, 1, mrb_value lit, )
{
  /* A Bx           R(A) := str_new(Lit(Bx)) */
  mrb_state *mrb = vme->mrb;
  mrb_value str = mrb_str_literal(mrb, lit);
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_STRING (\"%s\")\n", mrb_str_to_cstr(mrb, str));
  mrb_gc_arena_restore(mrb, vme->ctx->ai);
  return str;
}

ENTRY_vme_jmp(vm_super_array, void, 1, uint32_t a, )
{
  /* A B C  R(A) := super(R(A+1),... ,R(A+C-1)) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  mrb_value recv;
  mrb_callinfo *ci = mrb->c->ci;
  struct RProc *m;
  struct RClass *c;
  mrb_sym mid = ci->mid;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SUPER (a=0x%x, n=*, (mid=:%s))\n", a, mrb_sym2name(mrb, mid));

  recv = jmp->regs[0];
  c = mrb->c->ci->target_class->super;
  m = mrb_method_search_vm(mrb, &c, mid);
  if (!m) {
    mid = mrb_intern2(mrb, "method_missing", 14);
    m = mrb_method_search_vm(mrb, &c, mid);
    mrb_ary_unshift(mrb, jmp->regs[a+1], mrb_symbol_value(ci->mid));
  }

  /* push callinfo */
  ci = mrb_vm_cipush(mrb);
  ci->mid = mid;
  ci->proc = m;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = -1;
  ci->target_class = m->target_class;
  ci->pc = jmp->pc;

  /* prepare stack */
  mrb->c->stack += a;
  mrb->c->stack[0] = recv;

  if (MRB_PROC_CFUNC_P(m)) {
    mrb->c->stack[0] = m->body.func(mrb, recv);
    mrb_gc_arena_restore(mrb, vmc->ai);
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    jmp->regs = mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
    mrb_vm_cipop(mrb);
  }
  else {
    /* fill callinfo */
    ci->acc = a;

    /* setup environment for calling method */
    ci->proc = m;
    vmc->irep = m->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    ci->nregs = vmc->irep->nregs;
    mrb_vm_stack_extend(mrb, (vmc->irep->nregs < 3) ? 3 : vmc->irep->nregs, 3);
    jmp->regs = mrb->c->stack;
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
  }
}

ENTRY_vme_jmp(vm_super_normal, void, 1, uint32_t n_a, )
{
  /* A B C  R(A) := super(R(A+1),... ,R(A+C-1)) */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  mrb_value recv;
  mrb_callinfo *ci = mrb->c->ci;
  struct RProc *m;
  struct RClass *c;
  mrb_sym mid = ci->mid;
  int a = n_a & ((1 << 9) - 1);
  int n = (n_a >> 9);
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_SUPER (a=0x%x, n=0x%x, (mid=:%s))\n", a, n, mrb_sym2name(mrb, mid));

  recv = jmp->regs[0];
  c = mrb->c->ci->target_class->super;
  m = mrb_method_search_vm(mrb, &c, mid);
  if (!m) {
    mid = mrb_intern2(mrb, "method_missing", 14);
    m = mrb_method_search_vm(mrb, &c, mid);
    value_move(jmp->regs+a+2, jmp->regs+a+1, ++n);
    jmp->regs[a+1] = mrb_symbol_value(ci->mid);
  }

  /* push callinfo */
  ci = mrb_vm_cipush(mrb);
  ci->mid = mid;
  ci->proc = m;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = n;
  ci->target_class = m->target_class;
  ci->pc = jmp->pc;

  /* prepare stack */
  mrb->c->stack += a;
  mrb->c->stack[0] = recv;

  if (MRB_PROC_CFUNC_P(m)) {
    mrb->c->stack[0] = m->body.func(mrb, recv);
    mrb_gc_arena_restore(mrb, vmc->ai);
    if (mrb->exc) vm_raise_exc(vme, jmp);
    /* pop stackpos */
    jmp->regs = mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
    mrb_vm_cipop(mrb);
  }
  else {
    /* fill callinfo */
    ci->acc = a;

    /* setup environment for calling method */
    ci->proc = m;
    vmc->irep = m->body.irep;
    vmc->pool = vmc->irep->pool;
    vmc->syms = vmc->irep->syms;
    ci->nregs = vmc->irep->nregs;
    mrb_vm_stack_extend(mrb, vmc->irep->nregs, ci->argc+2);
    jmp->regs = mrb->c->stack;
    jmp->pc = vmc->irep->iseq;
    DPRINT_INDENT(mrb);
    DPRINTF(mrb, "--> jump to 0x%x\n", jmp->pc);
  }
}

ENTRY_vme_jmp(vm_target_class, mrb_value, 0, )
{
  /* A B    R(A) := target_class */
  mrb_state *mrb = vme->mrb;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_TCLASS\n");
  if (!mrb->c->ci->target_class) {
    static const char msg[] = "no target class or module";
    mrb_value exc = mrb_exc_new(mrb, E_TYPE_ERROR, msg, sizeof(msg) - 1);
    mrb->exc = mrb_obj_ptr(exc);
    vm_raise_exc(vme, jmp);
  }
  return mrb_obj_value(mrb->c->ci->target_class);
}

ENTRY_vme_jmp(vm_stop, mrb_value, 0, )
{
  /*        stop VM */
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "OP_STOP\n");
  int n = mrb->c->ci->eidx;

  while (n--) {
    mrb_vm_ecall(mrb, n);
  }
  mrb->jmp = vmc->prev_jmp;
  jmp->pc = (mrb_code *)vm_epilogue;
  DPRINT_INDENT(mrb);
  DPRINTF(mrb, "--> jump to epilogue\n");
  if (mrb->exc) {
    return mrb_obj_value(mrb->exc);
  }
  return jmp->regs[vmc->irep->nlocals];
}

ENTRY_vme_jmp(vm_runtime_err, void, 1, mrb_value msg, )
{
  /* A Bx    raise RuntimeError with message Lit(Bx) */
  mrb_state *mrb = vme->mrb;
  mrb->exc = mrb_obj_ptr(mrb_exc_new3(mrb, E_RUNTIME_ERROR, msg));
  vm_raise_exc(vme, jmp);
}

ENTRY_vme_jmp(vm_localjump_err, void, 1, mrb_value msg, )
{
  /* A Bx    raise LocalJumpError with message Lit(Bx) */
  mrb_state *mrb = vme->mrb;
  mrb->exc = mrb_obj_ptr(mrb_exc_new3(mrb, E_LOCALJUMP_ERROR, msg));
  vm_raise_exc(vme, jmp);
}

static const mrb_vm_env env_initializer = {
  .argary          = vm_argary_entry,
  .ary_cat         = vm_ary_cat_entry,
  .ary_fetch       = vm_ary_fetch_entry,
  .ary_new         = vm_ary_new_entry,
  .ary_post        = vm_ary_post_entry,
  .ary_push        = vm_ary_push_entry,
  .ary_store       = vm_ary_store_entry,
  .blk_push        = vm_blk_push_entry,
  .blockexec       = vm_blockexec_entry,
  .call            = vm_call_entry,
  .ensure_pop      = vm_ensure_pop_entry,
  .ensure_push     = vm_ensure_push_entry,
  .enter           = vm_enter_entry,
  .getglobal       = vm_getglobal_entry,
  .setglobal       = vm_setglobal_entry,
  .ivget           = vm_ivget_entry,
  .ivset           = vm_ivset_entry,
  .cvget           = vm_cvget_entry,
  .cvset           = vm_cvset_entry,
  .constget        = vm_constget_entry,
  .constset        = vm_constset_entry,
  .getmcnst        = vm_getmcnst_entry,
  .setmcnst        = vm_setmcnst_entry,
  .getupvar        = vm_getupvar_entry,
  .setupvar        = vm_setupvar_entry,
  .hash_new        = vm_hash_new_entry,
  .lambda          = vm_lambda_entry,
  .newclass        = vm_newclass_entry,
  .newmethod       = vm_newmethod_entry,
  .newmodule       = vm_newmodule_entry,
  .raise           = vm_raise_entry,
  .range_new       = vm_range_new_entry,
  .rescue          = vm_rescue_entry,
  .rescue_pop      = vm_rescue_pop_entry,
  .rescue_push     = vm_rescue_push_entry,
  .ret             = vm_ret_entry,
  // .ret_break       = vm_ret_break_entry,
  // .ret_normal      = vm_ret_normal_entry,
  // .ret_return      = vm_ret_return_entry,
  .send_array      = vm_send_array_entry,
  .send_normal     = vm_send_normal_entry,
  .singleton_class = vm_singleton_class_entry,
  .str_cat         = vm_str_cat_entry,
  .str_dup         = vm_str_dup_entry,
  .super_array     = vm_super_array_entry,
  .super_normal    = vm_super_normal_entry,
  .target_class    = vm_target_class_entry,
  .stop            = vm_stop_entry,
  .runtime_err     = vm_runtime_err_entry,
  .localjump_err   = vm_localjump_err_entry,
};

void (*nios2_dprintf)(const char *, ...);
void (*nios2_msleep)(uint32_t);

void
mrb_vm_env_init(mrb_state *mrb)
{
  mrb_vm_env *vme;
  vme = mrb_malloc(mrb, sizeof(mrb_vm_env));
  memcpy(vme, &env_initializer, sizeof(env_initializer));

  vme->mrb = mrb;
  vme->object_class = mrb_obj_value(mrb->object_class);
  vme->dprintf = nios2_dprintf;

  mrb->vm_env = vme;
}

mrb_value
NAKED mrb_vm_exec(mrb_state *mrb, mrb_code *code)
{
  asm("\t"
  "addi sp, sp, -12\n\t"  /* Preserve stack area (4*3) */
  "stw  ra, 8(sp)\n\t"    /* Save return address */
  "stw  r%0, 4(sp)\n\t"   /* Save VMENV_REG */
  "stw  r%1, 0(sp)\n\t"   /* Save STACK_REG */
  "ldw  r%0, %2(r4)\n\t"  /* VMENV_REG = mrb->vm_env; */
  "ldw  r%1, %3(r4)\n\t"  /* temp = mrb->c; */
  "ldw  r%1, %4(r%1)\n\t" /* STACK_REG = temp->stack; */
  "jmp  r5\n"             /* Jump to VM code */
  ::
  "i"(NIOS2_VMENV_REG), "i"(NIOS2_STACK_REG),
  "i"(offsetof(mrb_state, vm_env)),
  "i"(offsetof(mrb_state, c)),
  "i"(offsetof(struct mrb_context, stack))
  );

  return mrb_nil_value(); /* never reached */
}

static void
NAKED vm_epilogue(void)
{
  asm("\t"
  "ldw  r%1, 0(sp)\n\t"   /* Restore STACK_REG */
  "ldw  r%0, 4(sp)\n\t"   /* Restore VMENV_REG */
  "ldw  ra, 8(sp)\n\t"    /* Restore return address */
  "addi sp, sp, 12\n\t"   /* Discard stack area (4*3) */
  "ret\n"                 /* Return to caller (C-function) */
  ::
  "i"(NIOS2_VMENV_REG), "i"(NIOS2_STACK_REG)
  );
}

ENTRY_vme_jmp(vm_raise_exc, void, 0, )
{
  mrb_state *mrb = vme->mrb;
  mrb_vm_context *vmc = vme->ctx;
  mrb_callinfo *ci;
  int eidx;

  ci = mrb->c->ci;
  mrb_obj_iv_ifnone(mrb, mrb->exc, mrb_intern2(mrb, "lastpc", 6), mrb_voidp_value(mrb, jmp->pc));
  mrb_obj_iv_ifnone(mrb, mrb->exc, mrb_intern2(mrb, "ciidx", 5), mrb_fixnum_value(ci - mrb->c->cibase));
  eidx = ci->eidx;
  if (ci == mrb->c->cibase) {
    if (ci->ridx == 0) {
      vm_stop(vme, jmp);
      return;
    }
    goto L_RESCUE;
  }
  while (eidx > ci[-1].eidx) {
    mrb_vm_ecall(mrb, --eidx);
  }
  while (ci[0].ridx == ci[-1].ridx) {
    mrb_vm_cipop(mrb);
    ci = mrb->c->ci;
    mrb->c->stack = mrb->c->stbase + ci[1].stackidx;
    if (ci[1].acc < 0 && vmc->prev_jmp) {
      mrb->jmp = vmc->prev_jmp;
      longjmp(*(jmp_buf*)mrb->jmp, 1);
    }
    while (eidx > ci->eidx) {
      mrb_vm_ecall(mrb, --eidx);
    }
    if (ci == mrb->c->cibase) {
      if (ci->ridx == 0) {
        jmp->regs = mrb->c->stack = mrb->c->stbase;
        vm_stop(vme, jmp);
        return;
      }
      break;
    }
  }
L_RESCUE:
  vmc->irep = ci->proc->body.irep;
  vmc->pool = vmc->irep->pool;
  vmc->syms = vmc->irep->syms;
  jmp->regs = mrb->c->stack = mrb->c->stbase + ci[1].stackidx;
  jmp->pc = mrb->c->rescue[--ci->ridx];
}

mrb_code *
mrb_vm_raise_handler(void)
{
  return vm_raise_exc_entry;
}

uint8_t readb(void *address)
{
  return __builtin_ldbuio(address);
}

void writeb(uint8_t value, void *address)
{
  __builtin_stbio(address, value);
}

uint16_t readw(void *address)
{
  return __builtin_ldhuio(address);
}

void writew(uint16_t value, void *address)
{
  __builtin_sthio(address, value);
}

uint32_t readl(void *address)
{
  return __builtin_ldwio(address);
}

void writel(uint32_t value, void *address)
{
  __builtin_stwio(address, value);
}

void msleep(uint32_t msecs)
{
  if(nios2_msleep) (*nios2_msleep)(msecs);
}

#endif  /* MRB_MACHINE_NIOS2 */
