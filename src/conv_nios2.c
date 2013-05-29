/*
** conv_nios2.c - mruby code converter for Nios2
**
** See Copyright Notice in mruby.h
*/

#ifdef MRB_CONVERTER_NIOS2

#define MRB_WORD_BOXING
#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/irep.h"
#include "opcode.h"
#include "asm_nios2.h"

#define ESTIMATE_RITE2NIOS    4
#define CALL_MAXARGS 127

// #define VMM(name)         offsetof(mrb_vm_state, name)
#define VMM(name)           0

#define STACK_REG         21  /* r21 (Callee-saved) */
#define MRBVM_REG         20  /* r20 (Callee-saved) */

typedef struct scope
{
  mrb_state *mrb;
  mrb_irep *irep;

  mrb_code *rite_pc;
  size_t rite_ilen;

  mrb_code *new_iseq;
  size_t pc;
  size_t icapa;
} convert_scope;

static mrb_code *allocseq(convert_scope *s, size_t space);
static const char *convert_irep(convert_scope *s);

static mrb_code *
allocseq(convert_scope *s, size_t space)
{
  size_t pc = s->pc;
  if ((pc + space) > s->icapa) {
    s->icapa = pc + space + s->rite_ilen * ESTIMATE_RITE2NIOS;
    s->new_iseq = mrb_realloc(s->mrb, s->new_iseq, sizeof(mrb_code)*s->icapa);
  }

  s->pc += space;
  return &s->new_iseq[pc];
}

static const char *convert_irep(convert_scope *s)
{
  mrb_code i, *p;
  uint32_t sbx, sign;

  for(; s->rite_ilen > 0; --s->rite_ilen, ++s->rite_pc) {
    i = *s->rite_pc;
    *s->rite_pc = MKOP_Ax(OP_NOP, s->pc << 4);

    switch(GET_OPCODE(i)) {
    case OP_NOP:
      /*         no operation */
      p = allocseq(s, 1);
      p[0] = NIOS2_nop();
      break;
    case OP_MOVE:
      /* A B     R(A) := R(B) */
      p = allocseq(s, 2);
      p[0] = NIOS2_ldw(2, GETARG_B(i)*4, STACK_REG);
      p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LOADL:
      /* A Bx    R(A) := Lit(Bx) */
      p = allocseq(s, 4);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_lit), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LOADI:
      /* A sBx   R(A) := sBx */
      sbx = GETARG_sBx(i);
      sign = sbx>>(15-MRB_FIXNUM_SHIFT);
      if (sign == 0 || sign == ((1<<(16+1+MRB_FIXNUM_SHIFT))-1)) {
        p = allocseq(s, 2);
        p[0] = NIOS2_movi(2, (sbx<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG);
        p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      }
      else {
        p = allocseq(s, 3);
        p[0] = NIOS2_movhi(2, sbx>>(16-MRB_FIXNUM_SHIFT));
        p[1] = NIOS2_ori(2, 2, (sbx<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG);
        p[2] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      }
      break;
    case OP_LOADSYM:
      /* A Bx    R(A) := Sym(Bx) */
      p = allocseq(s, 4);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LOADNIL:
      /* A       R(A) := nil */
      p = allocseq(s, 1);
      p[0] = NIOS2_stw(0, GETARG_A(i)*4, STACK_REG);  /* MRB_Qnil must be defined as zero */
      break;
    case OP_LOADSELF:
      /* A       R(A) := self */
      p = allocseq(s, 2);
      p[0] = NIOS2_ldw(2, 0*4, STACK_REG);
      p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LOADT:
      /* A       R(A) := true */
      p = allocseq(s, 2);
      p[0] = NIOS2_movui(2, MRB_Qtrue);
      p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LOADF:
      /* A       R(A) := false */
      p = allocseq(s, 2);
      p[0] = NIOS2_movui(2, MRB_Qfalse);
      p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_GETGLOBAL:
      /* A Bx    R(A) := getglobal(Sym(Bx)) */
    case OP_GETIV:
      /* A Bx    R(A) := ivget(Sym(Bx)) */
    case OP_GETCV:
      /* A Bx    R(A) := cvget(Sym(Bx)) */
    case OP_GETCONST:
      /* A Bx    R(A) := constget(Sym(Bx)) */
      p = allocseq(s, 8);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      switch (GET_OPCODE(i)) {
      case OP_GETGLOBAL:
        p[3] = NIOS2_ldw(4, VMM(gv), MRBVM_REG);
        break;
      case OP_GETIV:
        p[3] = NIOS2_ldw(4, VMM(iv), MRBVM_REG);
        break;
      case OP_GETCV:
        p[3] = NIOS2_ldw(4, VMM(cv), MRBVM_REG);
        break;
      case OP_GETCONST:
        p[3] = NIOS2_ldw(4, VMM(cnst), MRBVM_REG);
        break;
      }
      p[4] = NIOS2_mov(5, 2);
      p[5] = NIOS2_ldw(2, VMM(hash_fetch), MRBVM_REG);
      p[6] = NIOS2_callr(2);
      p[7] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_SETGLOBAL:
      /* A Bx    setglobal(Sym(Bx),R(A)) */
    case OP_SETIV:
      /* A Bx    ivset(Sym(Bx),R(A)) */
    case OP_SETCV:
      /* A Bx    cvset(Sym(Bx),R(A)) */
    case OP_SETCONST:
      /* A Bx    constset(Sym(Bx),R(A)) */
      p = allocseq(s, 8);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      switch (GET_OPCODE(i)) {
      case OP_GETGLOBAL:
        p[3] = NIOS2_ldw(4, VMM(gv), MRBVM_REG);
        break;
      case OP_GETIV:
        p[3] = NIOS2_ldw(4, VMM(iv), MRBVM_REG);
        break;
      case OP_GETCV:
        p[3] = NIOS2_ldw(4, VMM(cv), MRBVM_REG);
        break;
      case OP_GETCONST:
        p[3] = NIOS2_ldw(4, VMM(cnst), MRBVM_REG);
        break;
      }
      p[4] = NIOS2_mov(5, 2);
      p[5] = NIOS2_ldw(6, GETARG_A(i)*4, STACK_REG);
      p[6] = NIOS2_ldw(2, VMM(hash_store), MRBVM_REG);
      p[7] = NIOS2_callr(2);
      break;
    /* case OP_GETSPECIAL: */
    /* case OP_SETSPECIAL: */
    case OP_GETMCNST:
      /* A Bx    R(A) := R(A)::Sym(B) */
      p = allocseq(s, 8);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[4] = NIOS2_mov(5, 2);
      p[5] = NIOS2_ldw(2, VMM(getmcnst), MRBVM_REG);
      p[6] = NIOS2_callr(2);
      p[7] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_SETMCNST:
      /* A Bx    R(A+1)::Sym(B) := R(A) */
      p = allocseq(s, 8);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_ldw(4, (GETARG_A(i)+1)*4, STACK_REG);
      p[4] = NIOS2_mov(5, 2);
      p[5] = NIOS2_ldw(6, GETARG_A(i)*4, STACK_REG);
      p[6] = NIOS2_ldw(2, VMM(setmcnst), MRBVM_REG);
      p[7] = NIOS2_callr(2);
      break;
    case OP_GETUPVAR:
      /* A B C   R(A) := uvget(B,C) */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(2, VMM(getupvar), MRBVM_REG);
      p[1] = NIOS2_movui(4, GETARG_B(i));
      p[2] = NIOS2_movui(5, GETARG_C(i));
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_SETUPVAR:
      /* A B C   uvset(B,C,R(A)) */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(2, VMM(setupvar), MRBVM_REG);
      p[1] = NIOS2_movui(4, GETARG_B(i));
      p[2] = NIOS2_movui(5, GETARG_C(i));
      p[3] = NIOS2_stw(6, GETARG_A(i)*4, STACK_REG);
      p[4] = NIOS2_callr(2);
      break;
    case OP_JMP:
      /* sBx     pc+=sBx */
      p = allocseq(s, 1);
      p[0] = NIOS2_br(GETARG_sBx(i)); /* placeholder */
      *s->rite_pc |= MKARG_Ax(1);
      break;
    case OP_JMPIF:
      /* A sBx   if R(A) pc+=sBx */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_rori(2, 2, MRB_FIXNUM_SHIFT);
      p[2] = NIOS2_cmpleui(2, 2, MRB_Qfalse>>MRB_FIXNUM_SHIFT);
      p[3] = NIOS2_beq(0, 2, GETARG_sBx(i)); /* placeholder */
      *s->rite_pc |= MKARG_Ax(4);
      break;
    case OP_JMPNOT:
      /* A sBx   if !R(A) pc+=sBx */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_rori(2, 2, MRB_FIXNUM_SHIFT);
      p[2] = NIOS2_cmpleui(2, 2, MRB_Qfalse>>MRB_FIXNUM_SHIFT);
      p[3] = NIOS2_bne(0, 2, GETARG_sBx(i)); /* placeholder */
      *s->rite_pc |= MKARG_Ax(4);
      break;
    case OP_ONERR:
      /* sBx     rescue_push(pc+sBx) */
      p = allocseq(s, 4);
      p[0] = NIOS2_nextpc(4);
      p[1] = NIOS2_ldw(2, VMM(rescue_push), MRBVM_REG);
      p[2] = NIOS2_movi(5, GETARG_sBx(i));
      p[3] = NIOS2_callr(2);
      *s->rite_pc |= MKARG_Ax(3);
      break;
    case OP_RESCUE:
      /* A       clear(exc); R(A) := exception (ignore when A=0) */
      if (GETARG_A(i) == 0) {
        p = allocseq(s, 2);
        p[0] = NIOS2_ldw(2, VMM(rescue), MRBVM_REG);
        p[1] = NIOS2_callr(2);
      }
      else {
        p = allocseq(s, 3);
        p[0] = NIOS2_ldw(2, VMM(rescue), MRBVM_REG);
        p[1] = NIOS2_callr(2);
        p[2] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      }
      break;
    case OP_POPERR:
      /* A       A.times{rescue_pop()} */
      p = allocseq(s, 3);
      p[0] = NIOS2_movui(4, GETARG_A(i));
      p[1] = NIOS2_ldw(2, VMM(rescue_pop), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      break;
    case OP_RAISE:
      /* A       raise(R(A)) */
      p = allocseq(s, 3);
      p[0] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_ldw(2, VMM(raise), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      break;
    case OP_EPUSH:
      /* Bx      ensure_push(SEQ[Bx]) */
      p = allocseq(s, 3);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(ensure_push), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      break;
    case OP_EPOP:
      /* A       A.times{ensure_pop().call} */
      p = allocseq(s, 3);
      p[0] = NIOS2_movui(4, GETARG_A(i));
      p[1] = NIOS2_ldw(2, VMM(ensure_pop), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      break;
    case OP_SEND:
    L_OP_SEND:
      /* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C),&R(A+C+1)) */
      p = allocseq(s, 1);
      if (GETARG_C(i) == CALL_MAXARGS) {
        p[0] = NIOS2_stw(0, (GETARG_A(i)+2)*4, STACK_REG);
      }
      else {
        p[0] = NIOS2_stw(0, (GETARG_A(i)+GETARG_C(i)+1)*4, STACK_REG);
      }
      /* fall through */
    case OP_SENDB:
      /* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C)) */
      p = allocseq(s, (GETARG_C(i) < CALL_MAXARGS) ? 8 : 7);
      p[0] = NIOS2_movui(4, GETARG_B(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_addi(4, STACK_REG, GETARG_A(i)*4);
      p[4] = NIOS2_mov(5, 2);
      if (GETARG_C(i) < CALL_MAXARGS) {
        p[5] = NIOS2_movui(6, GETARG_C(i));
        p[6] = NIOS2_ldw(2, VMM(send_normal), MRBVM_REG);
        p[7] = NIOS2_callr(2);
      }
      else {
        p[5] = NIOS2_ldw(2, VMM(send_array), MRBVM_REG);
        p[6] = NIOS2_callr(2);
      }
      break;
    /* case OP_FSEND: */
    case OP_CALL:
      /* A       R(A) := self.call(frame.argc, frame.argv) */
      p = allocseq(s, 2);
      p[0] = NIOS2_ldw(2, VMM(call), MRBVM_REG);
      p[1] = NIOS2_callr(2);
      break;
    case OP_SUPER:
      /* A B C   R(A) := super(R(A+1),... ,R(A+C-1)) */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(2, VMM(super), MRBVM_REG);
      p[1] = NIOS2_addi(4, STACK_REG, (GETARG_A(i)+1)*4);
      p[2] = NIOS2_movui(5, GETARG_C(i));
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_ARGARY:
      /* A Bx   R(A) := argument array (16=6:1:5:4) */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(2, VMM(argary), MRBVM_REG);
      p[1] = NIOS2_movui(4, GETARG_Bx(i));
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_ENTER:
      /* Ax      arg setup according to flags (24=5:5:1:5:5:1:1) */
      if (GETARG_Ax(i) > 0xffff) {
        p = allocseq(s, 4);
        p[0] = NIOS2_ldw(2, VMM(enter), MRBVM_REG);
        p[1] = NIOS2_movhi(4, GETARG_Ax(i)>>16);
        p[2] = NIOS2_ori(4, 4, GETARG_Ax(i)&0xffff);
        p[3] = NIOS2_callr(2);
      }
      else {
        p = allocseq(s, 3);
        p[0] = NIOS2_ldw(2, VMM(enter), MRBVM_REG);
        p[1] = NIOS2_movui(4, GETARG_Ax(i));
        p[2] = NIOS2_callr(2);
      }
      break;
    /* case OP_KARG: */
    /* case OP_KDICT: */
    case OP_RETURN:
      p = allocseq(s, 3);
      /* A B     return R(A) (B=normal,in-block return/break) */
      switch (GETARG_B(i)) {
      case OP_R_NORMAL:
        p[0] = NIOS2_ldw(2, VMM(ret_normal), MRBVM_REG);
        break;
      case OP_R_BREAK:
        p[0] = NIOS2_ldw(2, VMM(ret_break), MRBVM_REG);
        break;
      case OP_R_RETURN:
        p[0] = NIOS2_ldw(2, VMM(ret_return), MRBVM_REG);
        break;
      }
      p[1] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[2] = NIOS2_callr(2);
      break;
    /* case OP_TAILCALL: */
    case OP_BLKPUSH:
      /* A Bx    R(A) := block (16=6:1:5:4) */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(2, VMM(blk_push), MRBVM_REG);
      p[1] = NIOS2_movui(4, GETARG_Bx(i));
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_ADD:
      /* A B C   R(A) := R(A)+R(A+1) (mSyms[B]=:+,C=1) */
      p = allocseq(s, 13);
      p[0]  = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1]  = NIOS2_ldw(3, (GETARG_A(i)+1)*4, STACK_REG);
      /* Class check */
      p[2]  = NIOS2_and(4, 2, 3);
      p[3]  = NIOS2_andi(4, 4, MRB_FIXNUM_FLAG);
      p[4]  = NIOS2_beq(0, 4, 8*4); /* To method call */
      /* Execute */
      p[5]  = NIOS2_subi(3, 3, 1);
      p[6]  = NIOS2_add(4, 2, 3);
      /* Overflow check */
      p[7]  = NIOS2_xor(5, 4, 2);
      p[8]  = NIOS2_xor(6, 4, 3);
      p[9]  = NIOS2_and(5, 5, 6);
      p[10] = NIOS2_blt(5, 0, 2*4); /* To method call */
      p[11] = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[12] = NIOS2_br(9*4);
      /* Method call */
      goto L_OP_SEND;
    case OP_ADDI:
      /* A B C   R(A) := R(A)+C (mSyms[B]=:+) */
      if (GETARG_C(i) == 0) {
        break;
      }
      p = allocseq(s, 9);
      p[0] = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      /* Class check */
      p[1] = NIOS2_andi(3, 2, MRB_FIXNUM_FLAG);
      p[2] = NIOS2_beq(0, 3, 4*4); /* To method call */
      /* Execute */
      p[3] = NIOS2_addi(3, 2, GETARG_C(i)<<MRB_FIXNUM_SHIFT);
      /* Overflow check */
      if (GETARG_C(i) > 0) {
        p[4] = NIOS2_blt(3, 2, 2*4);  /* To method call */
      }
      else {
        p[4] = NIOS2_bgt(3, 2, 2*4);  /* To method call */
      }
      p[5] = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[6] = NIOS2_br((9+2)*4);
      /* Method call */
      p[7] = NIOS2_movi(2, (GETARG_C(i)<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG);
      p[8] = NIOS2_stw(2, (GETARG_A(i)+1)*4, STACK_REG);
      goto L_OP_SEND;
    case OP_SUB:
      /* A B C   R(A) := R(A)-R(A+1) (mSyms[B]=:-,C=1) */
      p = allocseq(s, 13);
      p[0]  = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1]  = NIOS2_ldw(3, (GETARG_A(i)+1)*4, STACK_REG);
      /* Class check */
      p[2]  = NIOS2_and(4, 2, 3);
      p[3]  = NIOS2_andi(4, 4, MRB_FIXNUM_FLAG);
      p[4]  = NIOS2_beq(0, 4, 8*4); /* To method call */
      /* Execute */
      p[5]  = NIOS2_subi(3, 3, 1);
      p[6]  = NIOS2_sub(4, 2, 3);
      /* Overflow check */
      p[7]  = NIOS2_xor(5, 4, 2);
      p[8]  = NIOS2_xor(6, 4, 3);
      p[9]  = NIOS2_and(5, 5, 6);
      p[10] = NIOS2_blt(5, 0, 2*4); /* To method call */
      p[11] = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[12] = NIOS2_br(9*4);
      /* Method call */
      goto L_OP_SEND;
    case OP_SUBI:
      /* A B C   R(A) := R(A)-C (mSyms[B]=:-) */
      if (GETARG_C(i) == 0) {
        break;
      }
      p = allocseq(s, 9);
      p[0] = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      /* Class check */
      p[1] = NIOS2_andi(3, 2, MRB_FIXNUM_FLAG);
      p[2] = NIOS2_beq(0, 3, 4*4); /* To method call */
      /* Execute */
      p[3] = NIOS2_subi(3, 2, GETARG_C(i)<<MRB_FIXNUM_SHIFT);
      /* Overflow check */
      if (GETARG_C(i) < 0) {
        p[4] = NIOS2_blt(3, 2, 2*4);  /* To method call */
      }
      else {
        p[4] = NIOS2_bgt(3, 2, 2*4);  /* To method call */
      }
      p[5] = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[6] = NIOS2_br((9+2)*4);
      /* Method call */
      p[7] = NIOS2_movi(2, (GETARG_C(i)<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG);
      p[8] = NIOS2_stw(2, (GETARG_A(i)+1)*4, STACK_REG);
      goto L_OP_SEND;
    case OP_MUL:
      goto L_OP_SEND;
    case OP_DIV:
      goto L_OP_SEND;
    case OP_EQ:
      /* A B C   R(A) := R(A)==R(A+1) (mSyms[B]=:==,C=1) */
      p = allocseq(s, 11);
      p[0]  = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1]  = NIOS2_ldw(3, (GETARG_A(i)+1)*4, STACK_REG);
      p[2]  = NIOS2_or(4, 2, 3);
      p[3]  = NIOS2_beq(4, 0, 4*4); /* NilClass vs. NilClass => true */
      p[4]  = NIOS2_cmpltui(4, 4, 1<<MRB_SPECIAL_SHIFT);
      p[5]  = NIOS2_beq(4, 0, 5*4); /* To method call */
      /* Qxxx/Fixnum vs. Qxxx/Fixnum */
      p[6]  = NIOS2_movui(4, MRB_Qfalse);
      p[7]  = NIOS2_bne(2, 3, 1*4);
      p[8]  = NIOS2_movui(4, MRB_Qtrue);
      p[9]  = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[10] = NIOS2_br(9*4);
      /* Method call */
      goto L_OP_SEND;
    case OP_LT:
      /* A B C   R(A) := R(A)<R(A+1)  (mSyms[B]=:<,C=1) */
    case OP_LE:
      /* A B C   R(A) := R(A)<=R(A+1)  (mSyms[B]=:<=,C=1) */
    case OP_GT:
      /* A B C   R(A) := R(A)>R(A+1)  (mSyms[B]=:>,C=1) */
    case OP_GE:
      /* A B C   R(A) := R(A)>=R(A+1)  (mSyms[B]=:>=,C=1) */
      p = allocseq(s, 10);
      p[0] = NIOS2_ldw(2, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_ldw(3, (GETARG_A(i)+1)*4, STACK_REG);
      p[2] = NIOS2_and(4, 2, 3);
      p[3] = NIOS2_andi(4, 4, MRB_FIXNUM_FLAG);
      p[4] = NIOS2_beq(4, 0, 5*4);
      /* Fixnum vs. Fixnum */
      p[5] = NIOS2_movui(4, MRB_Qfalse);
      switch (GET_OPCODE(i)) {
      case OP_LT:
        p[6] = NIOS2_bge(2, 3, 1*4);
        break;
      case OP_LE:
        p[6] = NIOS2_bgt(2, 3, 1*4);
        break;
      case OP_GT:
        p[6] = NIOS2_ble(2, 3, 1*4);
        break;
      case OP_GE:
        p[6] = NIOS2_blt(2, 3, 1*4);
        break;
      }
      p[7] = NIOS2_movui(4, MRB_Qtrue);
      p[8] = NIOS2_stw(4, GETARG_A(i)*4, STACK_REG);
      p[9] = NIOS2_br(9*4);
      /* Method call */
      goto L_OP_SEND;
    case OP_ARRAY:
      /* A B C   R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
      p = allocseq(s, 5);
      p[0] = NIOS2_addi(4, STACK_REG, GETARG_B(i)*4);
      p[1] = NIOS2_movui(5, GETARG_C(i));
      p[2] = NIOS2_ldw(2, VMM(ary_new), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_ARYPUSH:
      /* A B     ary_push(R(A),R(B)) */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_ldw(5, GETARG_B(i)*4, STACK_REG);
      p[2] = NIOS2_ldw(2, VMM(ary_push), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      break;
    case OP_AREF:
      /* A B C   R(A) := R(B)[C] */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(4, GETARG_B(i)*4, STACK_REG);
      p[1] = NIOS2_movui(5, GETARG_C(i));
      p[2] = NIOS2_ldw(2, VMM(ary_fetch), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_ASET:
      /* A B C   R(B)[C] := R(A) */
      p = allocseq(s, 5);
      p[0] = NIOS2_addi(4, STACK_REG, GETARG_B(i)*4);
      p[1] = NIOS2_movui(5, GETARG_C(i));
      p[2] = NIOS2_ldw(6, GETARG_A(i)*4, STACK_REG);
      p[3] = NIOS2_ldw(2, VMM(ary_store), MRBVM_REG);
      p[4] = NIOS2_callr(2);
      break;
    case OP_APOST:
      /* A B C   *R(A),R(A+1)..R(A+C) := R(A)[B..-1] */
      p = allocseq(s, 5);
      p[0] = NIOS2_addi(4, STACK_REG, GETARG_A(i)*4);
      p[1] = NIOS2_movui(5, GETARG_B(i));
      p[2] = NIOS2_movui(6, GETARG_C(i));
      p[3] = NIOS2_ldw(2, VMM(ary_post), MRBVM_REG);
      p[4] = NIOS2_callr(2);
      break;
    case OP_STRING:
      /* A Bx    R(A) := str_dup(Lit(Bx)) */
      p = allocseq(s, 4);
      p[0] = NIOS2_movui(4, GETARG_Bx(i));
      p[1] = NIOS2_ldw(2, VMM(str_dup), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_STRCAT:
      /* A B     str_cat(R(A),R(B)) */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_ldw(5, GETARG_B(i)*4, STACK_REG);
      p[2] = NIOS2_ldw(2, VMM(str_cat), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      break;
    case OP_HASH:
      /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+2*C-1)) */
      p = allocseq(s, 5);
      p[0] = NIOS2_addi(4, GETARG_B(i)*4, STACK_REG);
      p[1] = NIOS2_movui(5, GETARG_C(i));
      p[2] = NIOS2_ldw(2, VMM(hash_new), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_LAMBDA:
      /* A Bz Cz R(A) := lambda(SEQ[Bz],Cm) */
      p = allocseq(s, 5);
      p[0] = NIOS2_movui(4, GETARG_b(i));
      p[1] = NIOS2_movui(5, GETARG_c(i));
      p[2] = NIOS2_ldw(2, VMM(lambda), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_RANGE:
      /* A B C   R(A) := range_new(R(B),R(B+1),C) (C=0:'..', C=1:'...') */
      p = allocseq(s, 6);
      p[0] = NIOS2_addi(4, GETARG_B(i)*4, STACK_REG);
      p[1] = NIOS2_addi(5, (GETARG_B(i)+1)*4, STACK_REG);
      p[2] = NIOS2_movui(6, GETARG_C(i));
      p[3] = NIOS2_ldw(2, VMM(range_new), MRBVM_REG);
      p[4] = NIOS2_callr(2);
      p[5] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_OCLASS:
      /* A       R(A) := ::Object */
      p = allocseq(s, 2);
      p[0] = NIOS2_ldw(2, VMM(object_class), MRBVM_REG);
      p[1] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_CLASS:
      /* A B     R(A) := newclass(R(A),mSym(B),R(A+1)) */
      p = allocseq(s, 6);
      p[0] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_movui(5, GETARG_B(i));
      p[2] = NIOS2_ldw(6, (GETARG_A(i)+1)*4, STACK_REG);
      p[3] = NIOS2_ldw(2, VMM(newclass), MRBVM_REG);
      p[4] = NIOS2_callr(2);
      p[5] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_MODULE:
      /* A B     R(A) := newmodule(R(A),mSym(B)) */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[1] = NIOS2_movui(5, GETARG_B(i));
      p[2] = NIOS2_ldw(2, VMM(newmodule), MRBVM_REG);
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_EXEC:
      /* A Bx    R(A) := blockexec(R(A),SEQ[Bx]) */
      p = allocseq(s, 5);
      p[0] = NIOS2_ldw(2, VMM(blockexec), MRBVM_REG);
      p[1] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[2] = NIOS2_movui(5, GETARG_Bx(i));
      p[3] = NIOS2_callr(2);
      p[4] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_METHOD:
      /* A B     R(A).newmethod(mSym(B),R(A+1)) */
      p = allocseq(s, 8);
      p[0] = NIOS2_movui(4, GETARG_B(i));
      p[1] = NIOS2_ldw(2, VMM(load_sym), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_ldw(4, GETARG_A(i)*4, STACK_REG);
      p[4] = NIOS2_mov(5, 2);
      p[5] = NIOS2_ldw(6, (GETARG_A(i)+1)*4, STACK_REG);
      p[6] = NIOS2_ldw(2, VMM(newmethod), MRBVM_REG);
      p[7] = NIOS2_callr(2);
      break;
    case OP_SCLASS:
      /* A B     R(A) := R(B).singleton_class */
      p = allocseq(s, 4);
      p[0] = NIOS2_ldw(4, GETARG_B(i)*4, STACK_REG);
      p[1] = NIOS2_ldw(2, VMM(singleton_class), MRBVM_REG);
      p[2] = NIOS2_callr(2);
      p[3] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    case OP_TCLASS:
      /* A       R(A) := target_class */
      p[0] = NIOS2_ldw(2, VMM(target_class), MRBVM_REG);
      p[1] = NIOS2_callr(2);
      p[2] = NIOS2_stw(2, GETARG_A(i)*4, STACK_REG);
      break;
    default:
      return "unknown instruction";
    }
  }
}

const char *
mrb_convert_to_nios2(mrb_state *mrb, mrb_irep *irep)
{
  convert_scope scope;
  const char *errmsg;

  scope.mrb = mrb;
  scope.irep = irep;

  scope.rite_pc = irep->iseq;
  scope.rite_ilen = irep->ilen;

  scope.new_iseq = NULL;
  scope.pc = 0;
  scope.icapa = 0;

  errmsg = convert_irep(&scope);

  if (errmsg == NULL) {
    mrb_free(mrb, irep->iseq);
    irep->iseq = scope.new_iseq;
    irep->ilen = scope.pc;
  }
  else {
    mrb_free(mrb, scope.new_iseq);
  }
  return errmsg;
}

#endif  /* MRB_CONVERTER_NIOS2 */
