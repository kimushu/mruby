/*
** rubic.c - RiteVM accelerator for Nios II
**
** See Copyright Notice in mruby.h
*/
#if defined(ENABLE_RUBIC)
#include <stddef.h>
#include <stdarg.h>
#include <math.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/hash.h"
#include "mruby/irep.h"
#include "mruby/numeric.h"
#include "mruby/proc.h"
#include "mruby/range.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/error.h"
#include "mruby/opcode.h"
#include "value_array.h"
#include "mrb_throw.h"
#include "mruby/rubic.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define NIOS2_ICACHE_LINE_SIZE  32

#define CADDR_REGSTK  OP_MOVE
#define CADDR_REGLIT  OP_LOADL
#define CADDR_REGSYM  OP_LOADSYM
#define CADDR_LEAVE   OP_STOP

/*
 * NiosII register usage
 *
 *  r0     : zero
 *  r1     : at
 *  r2-r3  : return value
 *  r4-r7  : arguments
 *  r8-r15 : caller saved
 *  r16-r19: callee saved
 *  r20    : mruby state (callee saved)
 *  r21    : mruby stack (callee saved)
 *  r22    : mruby literal (callee saved)
 *  r23    : mruby symbol (callee saved)
 *  r24    : et
 *  r25    : bt
 *  r26    : gp
 *  r27    : sp
 *  r28    : fp
 *  r29    : ea
 *  r30    : ba
 *  r31    : ra
 */

// mrb_value
// mrb_context_run(mrb_state *mrb, struct RProc *proc, mrb_value self, unsigned int stack_keep)

#define push1(a)        "addi sp, sp, -4; stw "#a", 0(sp);"
#define pop1(a)         "ldw "#a", 0(sp); addi sp, sp, 4;"
#define push2(a,b)      "addi sp, sp, -8; stw "#a", 4(sp); stw "#b", 0(sp);"
#define pop2(a,b)       "ldw "#b", 0(sp); ldw "#a", 4(sp); addi sp, sp, 8;"
#define push3(a,b,c)    "addi sp, sp, -12; stw "#a", 8(sp); stw "#b", 4(sp); stw "#c", 0(sp);"
#define pop3(a,b,c)     "ldw "#c", 0(sp); ldw "#b", 4(sp); ldw "#a", 8(sp); addi sp, sp, 12;"

const char *tohex(uintptr_t v)
{
  static char buf[10];
  static const char *hex = "0123456789abcdef";
  buf[8] = 0;
  buf[7] = hex[(v & 15)]; v >>= 4;
  buf[6] = hex[(v & 15)]; v >>= 4;
  buf[5] = hex[(v & 15)]; v >>= 4;
  buf[4] = hex[(v & 15)]; v >>= 4;
  buf[3] = hex[(v & 15)]; v >>= 4;
  buf[2] = hex[(v & 15)]; v >>= 4;
  buf[1] = hex[(v & 15)]; v >>= 4;
  buf[0] = hex[(v & 15)]; v >>= 4;
  return buf;
}

int rubic_verbose;

rubic_native_ptr rubic_r2n(mrb_state *mrb, mrb_code *iseq)
{
  register uintptr_t ptr;
  ptr = (uintptr_t)iseq;
  ptr -= ((uintptr_t *)mrb->rubic_state.ctl_base)[OP_NOP];
  ptr &= ~(sizeof(mrb_code) - 1);
  ptr *= (RUBIC_R2N_RATIO);
  ptr += (uintptr_t)mrb->rubic_state.inst_base;
  // ptr |= 0x80000000;
  // printf("iseq: 0x%08x -> nios: 0x%08x\n", (uintptr_t)iseq, ptr);
  if(rubic_verbose) {
    puts(tohex(ptr));
  }
  return (rubic_native_ptr)ptr;
}

// struct rubic_result rubic_enter(rubic_native_ptr ptr)
__asm__(
".global rubic_enter;"
"rubic_enter:;"
"   addi  sp, sp, -4;"
"   stw   ra, 0(sp);"
"   jmp   r4;"
);

const char *const riteops[128] = {
  [OP_NOP] = "OP_NOP",
  [OP_MOVE] = "OP_MOVE",
  [OP_LOADL] = "OP_LOADL",
  [OP_LOADI] = "OP_LOADI",
  [OP_LOADSYM] = "OP_LOADSYM",
  [OP_LOADNIL] = "OP_LOADNIL",
  [OP_LOADSELF] = "OP_LOADSELF",
  [OP_LOADT] = "OP_LOADT",
  [OP_LOADF] = "OP_LOADF",

  [OP_GETGLOBAL] = "OP_GETGLOBAL",
  [OP_SETGLOBAL] = "OP_SETGLOBAL",
  [OP_GETSPECIAL] = "OP_GETSPECIAL",
  [OP_SETSPECIAL] = "OP_SETSPECIAL",
  [OP_GETIV] = "OP_GETIV",
  [OP_SETIV] = "OP_SETIV",
  [OP_GETCV] = "OP_GETCV",
  [OP_SETCV] = "OP_SETCV",
  [OP_GETCONST] = "OP_GETCONST",
  [OP_SETCONST] = "OP_SETCONST",
  [OP_GETMCNST] = "OP_GETMCNST",
  [OP_SETMCNST] = "OP_SETMCNST",
  [OP_GETUPVAR] = "OP_GETUPVAR",
  [OP_SETUPVAR] = "OP_SETUPVAR",

  [OP_JMP] = "OP_JMP",
  [OP_JMPIF] = "OP_JMPIF",
  [OP_JMPNOT] = "OP_JMPNOT",
  [OP_ONERR] = "OP_ONERR",
  [OP_RESCUE] = "OP_RESCUE",
  [OP_POPERR] = "OP_POPERR",
  [OP_RAISE] = "OP_RAISE",
  [OP_EPUSH] = "OP_EPUSH",
  [OP_EPOP] = "OP_EPOP",

  [OP_SEND] = "OP_SEND",
  [OP_SENDB] = "OP_SENDB",
  [OP_FSEND] = "OP_FSEND",
  [OP_CALL] = "OP_CALL",
  [OP_SUPER] = "OP_SUPER",
  [OP_ARGARY] = "OP_ARGARY",
  [OP_ENTER] = "OP_ENTER",
  [OP_KARG] = "OP_KARG",
  [OP_KDICT] = "OP_KDICT",

  [OP_RETURN] = "OP_RETURN",
  [OP_TAILCALL] = "OP_TAILCALL",
  [OP_BLKPUSH] = "OP_BLKPUSH",

  [OP_ADD] = "OP_ADD",
  [OP_ADDI] = "OP_ADDI",
  [OP_SUB] = "OP_SUB",
  [OP_SUBI] = "OP_SUBI",
  [OP_MUL] = "OP_MUL",
  [OP_DIV] = "OP_DIV",
  [OP_EQ] = "OP_EQ",
  [OP_LT] = "OP_LT",
  [OP_LE] = "OP_LE",
  [OP_GT] = "OP_GT",
  [OP_GE] = "OP_GE",

  [OP_ARRAY] = "OP_ARRAY",
  [OP_ARYCAT] = "OP_ARYCAT",
  [OP_ARYPUSH] = "OP_ARYPUSH",
  [OP_AREF] = "OP_AREF",
  [OP_ASET] = "OP_ASET",
  [OP_APOST] = "OP_APOST",

  [OP_STRING] = "OP_STRING",
  [OP_STRCAT] = "OP_STRCAT",

  [OP_HASH] = "OP_HASH",
  [OP_LAMBDA] = "OP_LAMBDA",
  [OP_RANGE] = "OP_RANGE",

  [OP_OCLASS] = "OP_OCLASS",
  [OP_CLASS] = "OP_CLASS",
  [OP_MODULE] = "OP_MODULE",
  [OP_EXEC] = "OP_EXEC",
  [OP_METHOD] = "OP_METHOD",
  [OP_SCLASS] = "OP_SCLASS",
  [OP_TCLASS] = "OP_TCLASS",

  [OP_DEBUG] = "OP_DEBUG",
  [OP_STOP] = "OP_STOP",
  [OP_ERR] = "OP_ERR",

  [OP_RSVD1] = "OP_RSVD1",
  [OP_RSVD2] = "OP_RSVD2",
  [OP_RSVD3] = "OP_RSVD3",
  [OP_RSVD4] = "OP_RSVD4",
  [OP_RSVD5] = "OP_RSVD5",
};

mrb_code *rubic_n2r(mrb_state *mrb, rubic_native_ptr pc)
{
  register uintptr_t ptr;
  // const char *opn;
  ptr = (uintptr_t)pc;
  ptr &= ~0x80000000;
  ptr -= (uintptr_t)mrb->rubic_state.inst_base;
  ptr /= (RUBIC_R2N_RATIO);
  ptr &= ~(sizeof(mrb_code) - 1);
  ptr -= ((uintptr_t *)mrb->rubic_state.ctl_base)[OP_NOP];
  return (mrb_code *)ptr;
}

void rubic_flush_icache(struct mrb_state *mrb, mrb_irep *irep)
{
  mrb_code *iseq;
  ssize_t ilen;

  (void)mrb;  // unused
  iseq = irep->iseq;
  ilen = (ssize_t)irep->ilen;
  for(;
      ilen > 0;
      ilen -= (NIOS2_ICACHE_LINE_SIZE / sizeof(*iseq)),
      iseq += (NIOS2_ICACHE_LINE_SIZE / sizeof(*iseq))) {
    __asm__ volatile("flushi %0":: "r"(iseq));
  }
}

void mrb_init_rubic(mrb_state *mrb)
{
  void **ctl_base = (mrb->rubic_state.ctl_base);

  ctl_base[CADDR_REGSTK] = (void *)(RUBIC_RNUM_STACK);
  ctl_base[CADDR_REGLIT] = (void *)(RUBIC_RNUM_LITERAL);
  ctl_base[CADDR_REGSYM] = (void *)(RUBIC_RNUM_SYMBOL);

  #define REGISTER_ASM_BLOCK(n, c, ...) \
    do { \
      extern int vm_##n; \
      ctl_base[n] = (void *)&vm_##n; \
      asm ( \
      "br 99f;" \
      "vm_"#n":;" c "99:;":: __VA_ARGS__); \
    } while(0)

  #define REGISTER_ASM_ALIAS(n, from) \
    do { \
      extern int vm_##from; \
      ctl_base[n] = (void *)&vm_##from; \
    } while(0)

  #define REGISTER_ASM_GIVEUP(n) \
    do { \
      extern int rubic_giveup; \
      ctl_base[n] = (void *)&rubic_giveup; \
    } while(0)

  REGISTER_ASM_BLOCK(
    CADDR_LEAVE,
    "rubic_leave:;"
    "   ldw ra, 0(sp);"
    "   addi sp, sp, 4;"
    "   ret;"
    "rubic_giveup:;"
    "   subi r2, ra, 4;"
    "   mov r3, r0;"
    "   br rubic_leave;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETGLOBAL,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_gv_get;"
  );

  REGISTER_ASM_BLOCK(
    OP_SETGLOBAL,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_gv_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETSPECIAL,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_special_get;"
  );

  REGISTER_ASM_BLOCK(
    OP_SETSPECIAL,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_special_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETIV,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_iv_get;"
  );

  REGISTER_ASM_BLOCK(
    OP_SETIV,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_iv_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETCV,
    "   mov r4, "RUBIC_REG_STATE";"
        push2(ra, r16)
    "   ldw r2, %0(r4);"  // mrb->c
    "   ldw r16, %1(r2);" // c->ci
    "   subi r2, ra, %3;"
    "   stw r2, %2(r16);" // ci->err
    "   call mrb_vm_cv_get;"
    "   stw r0, %2(r16);" // ci->err
        pop2(ra, r16)
    "   ret;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, err)),
    "i"(RUBIC_R2N_RATIO*4)
  );

  REGISTER_ASM_BLOCK(
    OP_SETCV,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_cv_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETCONST,
    "   mov r4, "RUBIC_REG_STATE";"
        push2(ra, r16)
    "   ldw r2, %0(r4);"  // mrb->c
    "   ldw r16, %1(r2);" // c->ci
    "   subi r2, ra, %3;"
    "   stw r2, %2(r16);" // ci->err
    "   call mrb_vm_const_get;"
    "   stw r0, %2(r16);" // ci->err
        pop2(ra, r16)
    "refresh_regstk:;"
    "   ldw r4, %4("RUBIC_REG_STATE");" // mrb->c
    "   ldw "RUBIC_REG_STACK", %5(r4);" // c->stack
    "   ret;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, err)),
    "i"(RUBIC_R2N_RATIO*4),
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, stack))
  );

  REGISTER_ASM_BLOCK(
    OP_SETCONST,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_vm_const_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETMCNST,
    "   mov r4, "RUBIC_REG_STATE";"
        push2(ra, r16)
    "   ldw r2, %0(r4);"  // mrb->c
    "   ldw r16, %1(r2);" // c->ci
    "   subi r2, ra, %3;"
    "   stw r2, %2(r16);" // ci->err
    "   call mrb_const_get;"
    "   stw r0, %2(r16);" // ci->err
        pop2(ra, r16)
    "   jmpi refresh_regstk;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, err)),
    "i"(RUBIC_R2N_RATIO*4)
  );

  REGISTER_ASM_BLOCK(
    OP_SETMCNST,
    "   mov r4, "RUBIC_REG_STATE";"
    "   jmpi mrb_const_set;"
  );

  REGISTER_ASM_BLOCK(
    OP_GETUPVAR,
    "   ldw r2, %0("RUBIC_REG_STATE");" // mrb->c
    "   ldw r2, %1(r2);"  // c->ci
    "   ldw r2, %2(r2);"  // ci->proc
    "   ldw r2, %3(r2);"  // proc->env
    "1: beq r0, r2, 3f;"  // !e
    "   beq r0, r5, 2f;"  // up==0
    "   subi r5, r5, 1;"  // up--
    "   ldw r2, %4(r2);"  // e->c
    "   br 1b;"
    "2: ldw r2, %5(r2);"  // e->stack
    "   add r2, r2, r6;"  // [idx]
    "   ldw r2, 0(r2);"   // e->stack[idx]
    "3: ret;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, proc)),
    "i"(offsetof(struct RProc, env)),
    "i"(offsetof(struct REnv, c)),
    "i"(offsetof(struct REnv, stack))
  );

  REGISTER_ASM_BLOCK(
    OP_SETUPVAR,
    "   mov r4, "RUBIC_REG_STATE";"
    "   ldw r2, %0(r4);"  // mrb->c
    "   ldw r2, %1(r2);"  // c->ci
    "   ldw r2, %2(r2);"  // ci->proc
    "   ldw r2, %3(r2);"  // proc->env
    "1: beq r0, r2, 3f;"  // !e
    "   beq r0, r5, 2f;"  // up==0
    "   subi r5, r5, 1;"  // up--
    "   ldw r2, %4(r2);"  // e->c
    "   br 1b;"
    "2: mov r5, r2;"
    "   ldw r2, %5(r2);"  // e->stack
    "   add r2, r2, r6;"  // [idx]
    "   stw r7, 0(r2);"   // e->stack[idx]
    "   jmpi mrb_write_barrier;"
    "3: ret;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, proc)),
    "i"(offsetof(struct RProc, env)),
    "i"(offsetof(struct REnv, c)),
    "i"(offsetof(struct REnv, stack))
  );

  // TODO: OP_ONERR
  REGISTER_ASM_GIVEUP(OP_ONERR);

  REGISTER_ASM_BLOCK(
    OP_RESCUE,
    "   ldw r2, %0("RUBIC_REG_STATE");"
    "   stw r0, %0("RUBIC_REG_STATE");"
    "   ret;",
    "i"(offsetof(mrb_state, exc))
  );

  REGISTER_ASM_BLOCK(
    OP_POPERR,
    "   ldw r2, %0("RUBIC_REG_STATE");"
    "   ldw r2, %1(r2);"
    "   ldw r3, %2(r2);"
    "   sub r3, r3, r5;"
    "   stw r3, %2(r2);"
    "   ret;",
    "i"(offsetof(mrb_state, c)),
    "i"(offsetof(struct mrb_context, ci)),
    "i"(offsetof(mrb_callinfo, ridx))
  );

  REGISTER_ASM_GIVEUP(OP_EPOP); // TODO

  REGISTER_ASM_GIVEUP(OP_RAISE); // TODO

  REGISTER_ASM_GIVEUP(OP_EPUSH); // TODO

  REGISTER_ASM_GIVEUP(OP_ARGARY); // TODO

  REGISTER_ASM_GIVEUP(OP_ENTER); // TODO

  REGISTER_ASM_GIVEUP(OP_BLKPUSH); // TODO

  REGISTER_ASM_BLOCK(
    OP_ADD,
    "   and r4, r5, r6;"
    "   andi r4, r4, 1;"
    "   beq r4, r0, rubic_giveup;"
    "   xori r4, r6, 1;"
    "   add r2, r5, r4;"
    "   xor r7, r2, r5;"
    "   xor r8, r2, r4;"
    "   and r7, r7, r8;"
    "   blt r7, r0, rubic_giveup;"
    "   ret;"
  );

/*
  REGISTER_ASM_BLOCK(
    OP_ADDI,
    "   andi r4, r5, 1;"
    "   beq r4, r0, rubic_giveup;"
    "   slli r4, r6, 1;"
    "   add r2, r5, r4;"
    "   xor r7, r2, r5;"
    "   xor r8, r2, r4;"
    "   and r7, r7, r8;"
    "   blt r7, r0, rubic_giveup;"
    "   ret;"
  );*/
  REGISTER_ASM_ALIAS(
    OP_ADDI,
    OP_ADD
  );

  REGISTER_ASM_BLOCK(
    OP_SUB,
    "   and r4, r5, r6;"
    "   andi r4, r4, 1;"
    "   beq r4, r0, rubic_giveup;"
    "   xori r4, r6, 1;"
    "   sub r2, r5, r4;"
    "   xor r7, r5, r4;"
    "   xor r8, r5, r2;"
    "   and r7, r7, r8;"
    "   blt r7, r0, rubic_giveup;"
    "   ret;"
  );

/*
  REGISTER_ASM_BLOCK(
    OP_SUBI,
    "   andi r4, r5, 1;"
    "   beq r4, r0, rubic_giveup;"
    "   slli r4, r6, 1;"
    "   sub r2, r5, r4;"
    "   xor r7, r5, r4;"
    "   xor r8, r5, r2;"
    "   and r7, r7, r8;"
    "   blt r7, r0, rubic_giveup;"
    "   ret;"
  );*/
  REGISTER_ASM_ALIAS(
    OP_SUBI,
    OP_SUB
  );

  REGISTER_ASM_GIVEUP(OP_MUL); // TODO
  REGISTER_ASM_GIVEUP(OP_DIV); // TODO
  REGISTER_ASM_GIVEUP(OP_EQ); // TODO
  REGISTER_ASM_GIVEUP(OP_LT); // TODO
  REGISTER_ASM_GIVEUP(OP_LE); // TODO
  REGISTER_ASM_GIVEUP(OP_GT); // TODO
  REGISTER_ASM_GIVEUP(OP_GE); // TODO
  // REGISTER_ASM_BLOCK(
  //   OP_EQ,
  //   "   bne r5, r6, 1f;"
  //   "   ori r2, r0, %0;"
  //   "   ret;"
  //   "1: subi r2, ra,

  #undef REGISTER_ASM_BLOCK
} /* rubic_initialize */

#if defined(__cplusplus)
}  /* extern "C" { */
#endif

#endif  /* ENABLE_RUBIC */
