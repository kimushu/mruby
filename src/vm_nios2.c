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
#include "mruby/string.h"
#include "mruby/vm_nios2.h"
#include "opcode.h"

#ifdef MRB_MACHINE_NIOS2

#define NAKED     __attribute__((naked))

#define TO_STR(x) TO_STR_(x)
#define TO_STR_(x) #x

#define STACK_REG_NAME  TO_STR(NIOS2_STACK_REG)
#define VMCTX_REG_NAME  TO_STR(NIOS2_VMCTX_REG)

#define PROLOGUE()      \
  mrb_value *regs;\
  mrb_vm_context *ctx;\
  mrb_state *mrb;\
  asm volatile (\
  "\tmov %0, r" VMCTX_REG_NAME "\n"\
  "\tmov %1, r" STACK_REG_NAME "\n"\
  : "=r"(ctx), "=r"(regs));\
  mrb = ctx->mrb

static inline void
stack_copy(mrb_value *dst, const mrb_value *src, size_t size)
{
  while (size-- > 0) {
    *dst++ = *src++;
  }
}

static inline struct REnv*
uvenv(mrb_state *mrb, int up)
{
  struct REnv *e = mrb->c->ci->proc->env;

  while (up--) {
    if (!e) return 0;
    e = (struct REnv*)e->c;
  }
  return e;
}

static mrb_pair
vm_argary(uint32_t bx)
{
  /* A Bx   R(A) := argument array (16=6:1:5:4) */
  PROLOGUE();
  int m1 = (bx>>10)&0x3f;
  int r  = (bx>>9)&0x1;
  int m2 = (bx>>4)&0x1f;
  int lv = (bx>>0)&0xf;
  mrb_pair args;
  mrb_value *stack;

  if (lv == 0) stack = regs + 1;
  else {
    struct REnv *e = uvenv(mrb, lv-1);
    if (!e) {
      mrb_value exc;
      static const char m[] = "super called outside of method";
      exc = mrb_exc_new(mrb, E_NOMETHOD_ERROR, m, sizeof(m) - 1);
      mrb->exc = mrb_obj_ptr(exc);
      // goto L_RAISE;
      // TODO
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
    stack_copy(rest->ptr, stack, m1);
    if (len > 0) {
      stack_copy(rest->ptr+m1, pp, len);
    }
    if (m2 > 0) {
      stack_copy(rest->ptr+m1+len, stack+m1+1, m2);
    }
    rest->len = m1+len+m2;
  }
  args.second = stack[m1+r+m2];
  mrb_gc_arena_restore(mrb, ctx->ai);

  return args;
}

static void
vm_ary_cat(mrb_value recv, mrb_value other)
{
  /* A B            mrb_ary_concat(R(A),R(B)) */
  PROLOGUE();
  mrb_ary_concat(mrb, recv, mrb_ary_splat(mrb, other));
  mrb_gc_arena_restore(mrb, ctx->ai);
}

static mrb_value
vm_ary_fetch(mrb_value recv, uint32_t index)
{
  /* A B C          R(A) := R(B)[C] */
  PROLOGUE();
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
  /* A B C          R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
  PROLOGUE();
  mrb_value ary;
  ary = mrb_ary_new_from_values(mrb, max, array);
  mrb_gc_arena_restore(mrb, ctx->ai);
  return ary;
}

static void
vm_ary_post(mrb_value *array, uint32_t pre, uint32_t post)
{
  /* A B C  *R(A),R(A+1)..R(A+C) := R(A) */
  PROLOGUE();
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
  mrb_gc_arena_restore(mrb, ctx->ai);
}

static void
vm_ary_push(mrb_value recv, mrb_value value)
{
  /* A B            R(A).push(R(B)) */
  PROLOGUE();
  mrb_ary_push(mrb, recv, value);
}

static void
vm_ary_store(mrb_value recv, uint32_t index, mrb_value value)
{
  /* A B C          R(B)[C] := R(A) */
  PROLOGUE();
  mrb_ary_set(mrb, recv, index, value);
}

static mrb_value
vm_blk_push(uint32_t bx)
{
}

static mrb_value
vm_blockexec(mrb_value recv, uint32_t bx)
{
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

static void
vm_enter(uint32_t ax)
{
}

static mrb_value
vm_getmcnst(mrb_value recv, uint32_t sym)
{
}

static void
vm_setmcnst(mrb_value recv, uint32_t sym, mrb_value value)
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
vm_hash_fetch(mrb_value hash, uint32_t sym)
{
}

static mrb_value
vm_hash_new(mrb_value *array, uint32_t pairs)
{
  /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C)) */
  PROLOGUE();
  int i = 0;
  mrb_value hash = mrb_hash_new_capa(mrb, pairs);

  while (i < pairs) {
    mrb_hash_set(mrb, hash, array[i], array[i+1]);
    i+=2;
  }
  mrb_gc_arena_restore(mrb, ctx->ai);
  return hash;
}

static void
vm_hash_store(mrb_value hash, uint32_t sym, mrb_value value)
{
}

static mrb_value
vm_lambda(uint32_t bz, uint32_t cm)
{
  /* A b c  R(A) := lambda(SEQ[b],c) (b:c = 14:2) */
  PROLOGUE();
  mrb_value proc;
  struct RProc *p;

  if (cm & OP_L_CAPTURE) {
    p = mrb_closure_new(mrb, mrb->irep[ctx->irep->idx+bz]);
  }
  else {
    p = mrb_proc_new(mrb, mrb->irep[ctx->irep->idx+bz]);
  }
  if (cm & OP_L_STRICT) p->flags |= MRB_PROC_STRICT;
  proc = mrb_obj_value(p);
  mrb_gc_arena_restore(mrb, ctx->ai);
  return proc;
}

static mrb_value
vm_load_lit(uint32_t bx)
{
}

static uint32_t
vm_load_sym(uint32_t bx)
{
}

static mrb_value
vm_newclass(mrb_value base, uint32_t sym, mrb_value super)
{
  /* A B    R(A) := newclass(R(A),Sym(B),R(A+1)) */
  PROLOGUE();
  mrb_value cls;
  struct RClass *c = 0;
  mrb_sym id = ctx->syms[sym];

  if (mrb_nil_p(base)) {
    base = mrb_obj_value(mrb->c->ci->target_class);
  }
  c = mrb_vm_define_class(mrb, base, super, id);
  cls = mrb_obj_value(c);
  mrb_gc_arena_restore(mrb, ctx->ai);
  return cls;
}

static void
vm_newmethod(mrb_value base, uint32_t sym, mrb_value closure)
{
}

static mrb_value
vm_newmodule(mrb_value base, uint32_t sym)
{
}

static void
vm_raise(mrb_value obj)
{
}

static mrb_value
vm_range_new(mrb_value first, mrb_value second, uint32_t exclude)
{
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

static void
vm_ret_break(mrb_value obj)
{
}

static void
vm_ret_normal(mrb_value obj)
{
}

static void
vm_ret_return(mrb_value obj)
{
}

static mrb_value
vm_send_array(mrb_value *argv, uint32_t sym)
{
}

static mrb_value
vm_send_normal(mrb_value *argv, uint32_t sym, uint32_t argc)
{
}

static mrb_value
vm_singleton_class(mrb_value base)
{
  /* A B    R(A) := R(B).singleton_class */
  PROLOGUE();
  mrb_value cls;
  cls = mrb_singleton_class(mrb, base);
  mrb_gc_arena_restore(mrb, ctx->ai);
  return cls;
}

static void
vm_str_cat(mrb_value recv, mrb_value other)
{
  /* A B    R(A).concat(R(B)) */
  PROLOGUE();
  mrb_str_concat(mrb, recv, other);
}

static mrb_value
vm_str_dup(uint32_t bx)
{
  /* A Bx           R(A) := str_new(Lit(Bx)) */
  mrb_value str;
  PROLOGUE();
  str = mrb_str_literal(mrb, ctx->pool[bx]);
  mrb_gc_arena_restore(mrb, ctx->ai);
  return str;
}

static mrb_value
vm_super(mrb_value *argv2, uint32_t argc)
{
}

static mrb_value
vm_target_class(void)
{
  /* A B    R(A) := target_class */
  PROLOGUE();
  if (!mrb->c->ci->target_class) {
    /* TODO */
    // static const char msg[] = "no target class or module";
    // mrb_value exc = mrb_exc_new(mrb, E_TYPE_ERROR, msg, sizeof(msg) - 1);
    // mrb->exc = mrb_obj_ptr(exc);
    // goto L_RAISE;
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

static const mrb_vm_context context_initializer = {
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
  .getmcnst        = vm_getmcnst,
  .setmcnst        = vm_setmcnst,
  .getupvar        = vm_getupvar,
  .setupvar        = vm_setupvar,
  .hash_fetch      = vm_hash_fetch,
  .hash_new        = vm_hash_new,
  .hash_store      = vm_hash_store,
  .lambda          = vm_lambda,
  .load_lit        = vm_load_lit,
  .load_sym        = vm_load_sym,
  .newclass        = vm_newclass,
  .newmethod       = vm_newmethod,
  .newmodule       = vm_newmodule,
  .raise           = vm_raise,
  .range_new       = vm_range_new,
  .rescue          = vm_rescue,
  .rescue_pop      = vm_rescue_pop,
  .rescue_push     = vm_rescue_push,
  .ret_break       = vm_ret_break,
  .ret_normal      = vm_ret_normal,
  .ret_return      = vm_ret_return,
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
mrb_vm_context_init(mrb_state *mrb)
{
  mrb_vm_context *ctx;
  ctx = mrb_malloc(mrb, sizeof(mrb_vm_context));
  memcpy(ctx, &context_initializer, sizeof(context_initializer));

  ctx->mrb = mrb;
  ctx->object_class = mrb_obj_value(mrb->object_class);

  mrb->vm_context = ctx;
}

mrb_value
mrb_vm_exec(mrb_state *mrb, mrb_code *code)
{
  asm("\t"
  "mov  r" VMCTX_REG_NAME ", %0 \n\t"
  "mov  r" STACK_REG_NAME ", %1 \n\t"
  "jmp  %2"
  :: "r"(mrb->vm_context), "r"(mrb->c->stack), "r"(code)
  );

  return mrb_nil_value(); /* never reached */
}

#endif  /* MRB_MACHINE_NIOS2 */
