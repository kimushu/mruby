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
#include "mruby/vm_nios2.h"
#include "opcode.h"
#include "asm_nios2.h"

#define JUMPOFFSET_BITS       5
#define ESTIMATE_RITE2NIOS    4
#define CALL_MAXARGS 127

#define CTX(name)         offsetof(mrb_vm_context, name)

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

static void allocseq(convert_scope *s, size_t space);
static void genop(convert_scope *s, uint32_t c);
static const char *convert_iseq(convert_scope *s);
static const char *convert_irep(mrb_state *mrb, mrb_irep *irep);
static void codedump(mrb_state *mrb, int n);

struct mrb_machine machine_nios2 = {
  NIOS2_BINARY_IDENTIFIER,
  NIOS2_BINARY_FORMAT_VER,
  NIOS2_COMPILER_NAME,
  NIOS2_COMPILER_VERSION,
  convert_irep,
  codedump,
};

enum nios2_disasm_format {
  fmt_none,
  fmt_opx,
  fmt_cab,
  fmt_basv,
  fmt_bauv,
  fmt_absv,
  fmt_sv,
  fmt_i5,
  fmt_i26,
  fmt_a,
  fmt_cust,
  fmt_sva,
  fmt_ldst,
  fmt_ca,
  fmt_buv,
  fmt_bsv,
  fmt_c,
  fmt_cn,
  fmt_cai5,
  fmt_na,
};

struct nios2_disasm_map {
  const char *mnemonic;
  enum nios2_disasm_format format;
};

static const struct nios2_disasm_map disasm_map[] = {
  {"call",  fmt_i26}, {"jmpi",  fmt_i26}, {NULL,    fmt_none},{"ldbu",  fmt_ldst},
  {"addi",  fmt_basv},{"stb",   fmt_ldst},{"br",    fmt_sv},  {"ldb",   fmt_ldst},
  {"cmpgei",fmt_basv},{NULL,    fmt_none},{NULL,    fmt_none},{"ldhu",  fmt_ldst},
  {"andi",  fmt_bauv},{"sth",   fmt_ldst},{"bge",   fmt_absv},{"ldh",   fmt_ldst},
  {"cmplti",fmt_basv},{NULL,    fmt_none},{NULL,    fmt_none},{"initda",fmt_sva},
  {"ori",   fmt_bauv},{"stw",   fmt_ldst},{"blt",   fmt_absv},{"ldw",   fmt_ldst},
  {"cmpnei",fmt_basv},{NULL,    fmt_none},{NULL,    fmt_none},{"flushda",fmt_sva},
  {"xori",  fmt_bauv},{NULL,    fmt_none},{"bne",   fmt_absv},{NULL,    fmt_none},
  {"cmpeqi",fmt_basv},{NULL,    fmt_none},{NULL,    fmt_none},{"ldbuio",fmt_ldst},
  {"muli",  fmt_basv},{"stbio", fmt_ldst},{"beq",   fmt_absv},{"ldbio", fmt_ldst},
  {"cmpgeui",fmt_basv},{NULL,   fmt_none},{NULL,    fmt_none},{"ldhuio",fmt_ldst},
  {"andhi", fmt_bauv},{"sthio", fmt_ldst},{"bgeu",  fmt_absv},{"ldhio", fmt_ldst},
  {"cmpltui",fmt_basv},{NULL,   fmt_none},{"custom",fmt_cust},{"initd", fmt_sva},
  {"orhi",  fmt_bauv},{"stwio", fmt_ldst},{"bltu",  fmt_absv},{"ldwio", fmt_ldst},
  {"rdprs", fmt_basv},{NULL,    fmt_none},{NULL,    fmt_opx}, {"flushd",fmt_sva},
  {"xorhi", fmt_bauv},{NULL,    fmt_none},{NULL,    fmt_opx}, {NULL,    fmt_none},
};

static const struct nios2_disasm_map disasm_mapx[] = {
  {NULL,    fmt_none},{"eret",  fmt_none},{"roli",  fmt_cai5},{"rol",   fmt_cab},
  {"flushp",fmt_none},{"ret",   fmt_none},{"nor",   fmt_cab}, {"mulxuu",fmt_cab},
  {"cmpge", fmt_cab}, {"breg",  fmt_none},{NULL,    fmt_none},{"ror",   fmt_cab},
  {"flushi",fmt_a},   {"jmp",   fmt_a},   {"and",   fmt_cab}, {NULL,    fmt_none},
  {"cmplt", fmt_cab}, {NULL,    fmt_none},{"slli",  fmt_cai5},{"sll",   fmt_cab},
  {"wrprs", fmt_ca},  {NULL,    fmt_none},{"or",    fmt_cab}, {"mulxsu",fmt_cab},
  {"cmpne", fmt_cab}, {NULL,    fmt_none},{"srli",  fmt_cai5},{"srl",   fmt_cab},
  {"nextpc",fmt_c},   {"callr", fmt_a},   {"xor",   fmt_cab}, {"mulxss",fmt_cab},
  {"cmpeq", fmt_cab}, {NULL,    fmt_none},{NULL,    fmt_none},{NULL,    fmt_none},
  {"divu",  fmt_cab}, {"div",   fmt_cab}, {"rdctl", fmt_cn},  {"mul",   fmt_cab},
  {"cmpgeu",fmt_cab}, {"initi", fmt_a},   {NULL,    fmt_none},{NULL,    fmt_none},
  {NULL,    fmt_none},{"trap",  fmt_i5},  {"wrctl", fmt_na},  {NULL,    fmt_none},
  {"cmpltu",fmt_cab}, {"add",   fmt_cab}, {NULL,    fmt_none},{NULL,    fmt_none},
  {"break", fmt_i5},  {NULL,    fmt_none},{"sync",  fmt_none},{NULL,    fmt_none},
  {NULL,    fmt_none},{"sub",   fmt_cab}, {"srai",  fmt_cai5},{"sra",   fmt_cab},
  {NULL,    fmt_none},{NULL,    fmt_none},{NULL,    fmt_none},{NULL,    fmt_none},
};

static void
allocseq(convert_scope *s, size_t space)
{
  size_t pc = s->pc;
  if ((pc + space) > s->icapa) {
    s->icapa = pc + space + s->rite_ilen * ESTIMATE_RITE2NIOS;
    s->new_iseq = mrb_realloc(s->mrb, s->new_iseq, sizeof(mrb_code)*s->icapa);
  }
}

static void
genop(convert_scope *s, uint32_t c)
{
  s->new_iseq[s->pc++] = c;
}

static const char *
convert_iseq(convert_scope *s)
{
  mrb_code i;
  uint32_t sbx, sign;
  mrb_code *src_pc;
  size_t src_ilen;

  /* convert instructions */
  for(src_pc = s->rite_pc, src_ilen = s->rite_ilen;
      src_ilen > 0;
      --src_ilen, ++src_pc) {
    i = *src_pc;
    *src_pc = MKOP_Ax(OP_NOP, s->pc << JUMPOFFSET_BITS);

    switch(GET_OPCODE(i)) {
    case OP_NOP:
      /*         no operation */
      allocseq(s, 1);
      genop(s, NIOS2_nop());
      break;
    case OP_MOVE:
      /* A B     R(A) := R(B) */
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LOADL:
      /* A Bx    R(A) := Lit(Bx) */
      allocseq(s, 4);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_lit), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LOADI:
      /* A sBx   R(A) := sBx */
      sbx = GETARG_sBx(i);
      sign = (uint32_t)sbx>>(15-MRB_FIXNUM_SHIFT);
      if (sign == 0 || sign == ((1<<(16+1+MRB_FIXNUM_SHIFT))-1)) {
        allocseq(s, 2);
        genop(s, NIOS2_movi(2, (sbx<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG));
        genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      }
      else {
        allocseq(s, 3);
        genop(s, NIOS2_movhi(2, sbx>>(16-MRB_FIXNUM_SHIFT)));
        genop(s, NIOS2_ori(2, 2, (sbx<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG));
        genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      }
      break;
    case OP_LOADSYM:
      /* A Bx    R(A) := Sym(Bx) */
      allocseq(s, 4);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LOADNIL:
      /* A       R(A) := nil */
      allocseq(s, 1);
      genop(s, NIOS2_stw(0, GETARG_A(i)*4, NIOS2_STACK_REG));  /* MRB_Qnil must be defined as zero */
      break;
    case OP_LOADSELF:
      /* A       R(A) := self */
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, 0*4, NIOS2_STACK_REG));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LOADT:
      /* A       R(A) := true */
      allocseq(s, 2);
      genop(s, NIOS2_movui(2, MRB_Qtrue));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LOADF:
      /* A       R(A) := false */
      allocseq(s, 2);
      genop(s, NIOS2_movui(2, MRB_Qfalse));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_GETGLOBAL:
      /* A Bx    R(A) := getglobal(Sym(Bx)) */
    case OP_GETIV:
      /* A Bx    R(A) := ivget(Sym(Bx)) */
    case OP_GETCV:
      /* A Bx    R(A) := cvget(Sym(Bx)) */
    case OP_GETCONST:
      /* A Bx    R(A) := constget(Sym(Bx)) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      switch (GET_OPCODE(i)) {
      case OP_GETGLOBAL:
        genop(s, NIOS2_ldw(4, CTX(gv), NIOS2_VMCTX_REG));
        break;
      case OP_GETIV:
        genop(s, NIOS2_ldw(4, CTX(iv), NIOS2_VMCTX_REG));
        break;
      case OP_GETCV:
        genop(s, NIOS2_ldw(4, CTX(cv), NIOS2_VMCTX_REG));
        break;
      case OP_GETCONST:
        genop(s, NIOS2_ldw(4, CTX(cnst), NIOS2_VMCTX_REG));
        break;
      }
      genop(s, NIOS2_mov(5, 2));
      genop(s, NIOS2_ldw(2, CTX(hash_fetch), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_SETGLOBAL:
      /* A Bx    setglobal(Sym(Bx),R(A)) */
    case OP_SETIV:
      /* A Bx    ivset(Sym(Bx),R(A)) */
    case OP_SETCV:
      /* A Bx    cvset(Sym(Bx),R(A)) */
    case OP_SETCONST:
      /* A Bx    constset(Sym(Bx),R(A)) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      switch (GET_OPCODE(i)) {
      case OP_GETGLOBAL:
        genop(s, NIOS2_ldw(4, CTX(gv), NIOS2_VMCTX_REG));
        break;
      case OP_GETIV:
        genop(s, NIOS2_ldw(4, CTX(iv), NIOS2_VMCTX_REG));
        break;
      case OP_GETCV:
        genop(s, NIOS2_ldw(4, CTX(cv), NIOS2_VMCTX_REG));
        break;
      case OP_GETCONST:
        genop(s, NIOS2_ldw(4, CTX(cnst), NIOS2_VMCTX_REG));
        break;
      }
      genop(s, NIOS2_mov(5, 2));
      genop(s, NIOS2_ldw(6, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(hash_store), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    /* case OP_GETSPECIAL: */
    /* case OP_SETSPECIAL: */
    case OP_GETMCNST:
      /* A Bx    R(A) := R(A)::Sym(B) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_mov(5, 2));
      genop(s, NIOS2_ldw(2, CTX(getmcnst), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_SETMCNST:
      /* A Bx    R(A+1)::Sym(B) := R(A) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_ldw(4, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_mov(5, 2));
      genop(s, NIOS2_ldw(6, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(setmcnst), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_GETUPVAR:
      /* A B C   R(A) := uvget(B,C) */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(2, CTX(getupvar), NIOS2_VMCTX_REG));
      genop(s, NIOS2_movui(4, GETARG_B(i)));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_SETUPVAR:
      /* A B C   uvset(B,C,R(A)) */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(2, CTX(setupvar), NIOS2_VMCTX_REG));
      genop(s, NIOS2_movui(4, GETARG_B(i)));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_stw(6, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_JMP:
      /* sBx     pc+=sBx */
      allocseq(s, 1);
      genop(s, NIOS2_br(GETARG_sBx(i))); /* placeholder */
      *src_pc |= MKARG_Ax(0+1);
      break;
    case OP_JMPIF:
      /* A sBx   if R(A) pc+=sBx */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_rori(2, 2, MRB_FIXNUM_SHIFT));
      genop(s, NIOS2_cmpleui(2, 2, MRB_Qfalse>>MRB_FIXNUM_SHIFT));
      genop(s, NIOS2_beq(0, 2, GETARG_sBx(i))); /* placeholder */
      *src_pc |= MKARG_Ax(3+1);
      break;
    case OP_JMPNOT:
      /* A sBx   if !R(A) pc+=sBx */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_rori(2, 2, MRB_FIXNUM_SHIFT));
      genop(s, NIOS2_cmpleui(2, 2, MRB_Qfalse>>MRB_FIXNUM_SHIFT));
      genop(s, NIOS2_bne(0, 2, GETARG_sBx(i))); /* placeholder */
      *src_pc |= MKARG_Ax(3+1);
      break;
    case OP_ONERR:
      /* sBx     rescue_push(pc+sBx) */
      allocseq(s, 4);
      genop(s, NIOS2_nextpc(4));
      genop(s, NIOS2_ldw(2, CTX(rescue_push), NIOS2_VMCTX_REG));
      genop(s, NIOS2_movi(5, GETARG_sBx(i)));  /* placeholder */
      genop(s, NIOS2_callr(2));
      *src_pc |= MKARG_Ax(2+1);
      break;
    case OP_RESCUE:
      /* A       clear(exc); R(A) := exception (ignore when A=0) */
      allocseq(s, 3);
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, CTX(rescue), NIOS2_VMCTX_REG));
      if (GETARG_A(i) != 0) {
        genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      }
      break;
    case OP_POPERR:
      /* A       A.times{rescue_pop()} */
      allocseq(s, 3);
      genop(s, NIOS2_movui(4, GETARG_A(i)));
      genop(s, NIOS2_ldw(2, CTX(rescue_pop), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_RAISE:
      /* A       raise(R(A)) */
      allocseq(s, 3);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(raise), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_EPUSH:
      /* Bx      ensure_push(SEQ[Bx]) */
      allocseq(s, 3);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(ensure_push), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_EPOP:
      /* A       A.times{ensure_pop().call} */
      allocseq(s, 3);
      genop(s, NIOS2_movui(4, GETARG_A(i)));
      genop(s, NIOS2_ldw(2, CTX(ensure_pop), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_SEND:
    L_OP_SEND:
      /* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C),&R(A+C+1)) */
      allocseq(s, 1);
      if (GETARG_C(i) == CALL_MAXARGS) {
        genop(s, NIOS2_stw(0, (GETARG_A(i)+2)*4, NIOS2_STACK_REG));
      }
      else {
        genop(s, NIOS2_stw(0, (GETARG_A(i)+GETARG_C(i)+1)*4, NIOS2_STACK_REG));
      }
      /* fall through */
    case OP_SENDB:
      /* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C)) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_B(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_addi(4, NIOS2_STACK_REG, GETARG_A(i)*4));
      genop(s, NIOS2_mov(5, 2));
      if (GETARG_C(i) < CALL_MAXARGS) {
        genop(s, NIOS2_movui(6, GETARG_C(i)));
        genop(s, NIOS2_ldw(2, CTX(send_normal), NIOS2_VMCTX_REG));
      }
      else {
        genop(s, NIOS2_ldw(2, CTX(send_array), NIOS2_VMCTX_REG));
      }
      genop(s, NIOS2_callr(2));
      break;
    /* case OP_FSEND: */
    case OP_CALL:
      /* A       R(A) := self.call(frame.argc, frame.argv) */
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, CTX(call), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_SUPER:
      /* A B C   R(A) := super(R(A+1),... ,R(A+C-1)) */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(2, CTX(super), NIOS2_VMCTX_REG));
      genop(s, NIOS2_addi(4, NIOS2_STACK_REG, (GETARG_A(i)+1)*4));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_ARGARY:
      /* A Bx   R(A) := argument array (16=6:1:5:4) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(2, CTX(argary), NIOS2_VMCTX_REG));
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_ENTER:
      /* Ax      arg setup according to flags (24=5:5:1:5:5:1:1) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(2, CTX(enter), NIOS2_VMCTX_REG));
      if (GETARG_Ax(i) > 0xffff) {
        genop(s, NIOS2_movhi(4, GETARG_Ax(i)>>16));
        if ((GETARG_Ax(i)&0xffff) > 0) {
          genop(s, NIOS2_ori(4, 4, GETARG_Ax(i)&0xffff));
        }
      }
      else {
        genop(s, NIOS2_movui(4, GETARG_Ax(i)));
      }
      genop(s, NIOS2_callr(2));
      break;
    /* case OP_KARG: */
    /* case OP_KDICT: */
    case OP_RETURN:
      allocseq(s, 3);
      /* A B     return R(A) (B=normal,in-block return/break) */
      switch (GETARG_B(i)) {
      case OP_R_NORMAL:
        genop(s, NIOS2_ldw(2, CTX(ret_normal), NIOS2_VMCTX_REG));
        break;
      case OP_R_BREAK:
        genop(s, NIOS2_ldw(2, CTX(ret_break), NIOS2_VMCTX_REG));
        break;
      case OP_R_RETURN:
        genop(s, NIOS2_ldw(2, CTX(ret_return), NIOS2_VMCTX_REG));
        break;
      }
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_callr(2));
      break;
    /* case OP_TAILCALL: */
    case OP_BLKPUSH:
      /* A Bx    R(A) := block (16=6:1:5:4) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(2, CTX(blk_push), NIOS2_VMCTX_REG));
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_ADD:
      /* A B C   R(A) := R(A)+R(A+1) (mSyms[B]=:+,C=1) */
      allocseq(s, 13);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(3, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      /* Class check */
      genop(s, NIOS2_and(4, 2, 3));
      genop(s, NIOS2_andi(4, 4, MRB_FIXNUM_FLAG));
      genop(s, NIOS2_beq(0, 4, 8*4)); /* To method call */
      /* Execute */
      genop(s, NIOS2_subi(3, 3, 1));
      genop(s, NIOS2_add(4, 2, 3));
      /* Overflow check */
      genop(s, NIOS2_xor(5, 4, 2));
      genop(s, NIOS2_xor(6, 4, 3));
      genop(s, NIOS2_and(5, 5, 6));
      genop(s, NIOS2_blt(5, 0, 2*4)); /* To method call */
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br(9*4));
      /* Method call */
      goto L_OP_SEND;
    case OP_ADDI:
      /* A B C   R(A) := R(A)+C (mSyms[B]=:+) */
      if (GETARG_C(i) == 0) {
        break;
      }
      allocseq(s, 9);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      /* Class check */
      genop(s, NIOS2_andi(3, 2, MRB_FIXNUM_FLAG));
      genop(s, NIOS2_beq(0, 3, 4*4)); /* To method call */
      /* Execute */
      genop(s, NIOS2_addi(3, 2, GETARG_C(i)<<MRB_FIXNUM_SHIFT));
      /* Overflow check */
      if (GETARG_C(i) > 0) {
        genop(s, NIOS2_blt(3, 2, 2*4));  /* To method call */
      }
      else {
        genop(s, NIOS2_bgt(3, 2, 2*4));  /* To method call */
      }
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br((9+2)*4));
      /* Method call */
      genop(s, NIOS2_movi(2, (GETARG_C(i)<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG));
      genop(s, NIOS2_stw(2, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      goto L_OP_SEND;
    case OP_SUB:
      /* A B C   R(A) := R(A)-R(A+1) (mSyms[B]=:-,C=1) */
      allocseq(s, 13);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(3, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      /* Class check */
      genop(s, NIOS2_and(4, 2, 3));
      genop(s, NIOS2_andi(4, 4, MRB_FIXNUM_FLAG));
      genop(s, NIOS2_beq(0, 4, 8*4)); /* To method call */
      /* Execute */
      genop(s, NIOS2_subi(3, 3, 1));
      genop(s, NIOS2_sub(4, 2, 3));
      /* Overflow check */
      genop(s, NIOS2_xor(5, 4, 2));
      genop(s, NIOS2_xor(6, 4, 3));
      genop(s, NIOS2_and(5, 5, 6));
      genop(s, NIOS2_blt(5, 0, 2*4)); /* To method call */
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br(9*4));
      /* Method call */
      goto L_OP_SEND;
    case OP_SUBI:
      /* A B C   R(A) := R(A)-C (mSyms[B]=:-) */
      if (GETARG_C(i) == 0) {
        break;
      }
      allocseq(s, 9);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      /* Class check */
      genop(s, NIOS2_andi(3, 2, MRB_FIXNUM_FLAG));
      genop(s, NIOS2_beq(0, 3, 4*4)); /* To method call */
      /* Execute */
      genop(s, NIOS2_subi(3, 2, GETARG_C(i)<<MRB_FIXNUM_SHIFT));
      /* Overflow check */
      if (GETARG_C(i) < 0) {
        genop(s, NIOS2_blt(3, 2, 2*4));  /* To method call */
      }
      else {
        genop(s, NIOS2_bgt(3, 2, 2*4));  /* To method call */
      }
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br((9+2)*4));
      /* Method call */
      genop(s, NIOS2_movi(2, (GETARG_C(i)<<MRB_FIXNUM_SHIFT)|MRB_FIXNUM_FLAG));
      genop(s, NIOS2_stw(2, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      goto L_OP_SEND;
    case OP_MUL:
      goto L_OP_SEND;
    case OP_DIV:
      goto L_OP_SEND;
    case OP_EQ:
      /* A B C   R(A) := R(A)==R(A+1) (mSyms[B]=:==,C=1) */
      allocseq(s, 11);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(3, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_or(4, 2, 3));
      genop(s, NIOS2_beq(4, 0, 4*4)); /* NilClass vs. NilClass => true */
      genop(s, NIOS2_cmpltui(4, 4, 1<<MRB_SPECIAL_SHIFT));
      genop(s, NIOS2_beq(4, 0, 5*4)); /* To method call */
      /* Qxxx/Fixnum vs. Qxxx/Fixnum */
      genop(s, NIOS2_movui(4, MRB_Qfalse));
      genop(s, NIOS2_bne(2, 3, 1*4));
      genop(s, NIOS2_movui(4, MRB_Qtrue));
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br(9*4));
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
      allocseq(s, 10);
      genop(s, NIOS2_ldw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(3, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_and(4, 2, 3));
      genop(s, NIOS2_andi(4, 4, MRB_FIXNUM_FLAG));
      genop(s, NIOS2_beq(4, 0, 5*4));
      /* Fixnum vs. Fixnum */
      genop(s, NIOS2_movui(4, MRB_Qfalse));
      switch (GET_OPCODE(i)) {
      case OP_LT:
        genop(s, NIOS2_bge(2, 3, 1*4));
        break;
      case OP_LE:
        genop(s, NIOS2_bgt(2, 3, 1*4));
        break;
      case OP_GT:
        genop(s, NIOS2_ble(2, 3, 1*4));
        break;
      case OP_GE:
        genop(s, NIOS2_blt(2, 3, 1*4));
        break;
      }
      genop(s, NIOS2_movui(4, MRB_Qtrue));
      genop(s, NIOS2_stw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_br(9*4));
      /* Method call */
      goto L_OP_SEND;
    case OP_ARRAY:
      /* A B C   R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
      allocseq(s, 5);
      genop(s, NIOS2_addi(4, NIOS2_STACK_REG, GETARG_B(i)*4));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_ldw(2, CTX(ary_new), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_ARYCAT:
      /* A B     ary_cat(R(A),R(B)) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(5, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(ary_cat), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_ARYPUSH:
      /* A B     ary_push(R(A),R(B)) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(5, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(ary_push), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_AREF:
      /* A B C   R(A) := R(B)[C] */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(4, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_ldw(2, CTX(ary_fetch), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_ASET:
      /* A B C   R(B)[C] := R(A) */
      allocseq(s, 5);
      genop(s, NIOS2_addi(4, NIOS2_STACK_REG, GETARG_B(i)*4));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_ldw(6, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(ary_store), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_APOST:
      /* A B C   *R(A),R(A+1)..R(A+C) := R(A)[B..-1] */
      allocseq(s, 5);
      genop(s, NIOS2_addi(4, NIOS2_STACK_REG, GETARG_A(i)*4));
      genop(s, NIOS2_movui(5, GETARG_B(i)));
      genop(s, NIOS2_movui(6, GETARG_C(i)));
      genop(s, NIOS2_ldw(2, CTX(ary_post), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_STRING:
      /* A Bx    R(A) := str_dup(Lit(Bx)) */
      allocseq(s, 4);
      genop(s, NIOS2_movui(4, GETARG_Bx(i)));
      genop(s, NIOS2_ldw(2, CTX(str_dup), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_STRCAT:
      /* A B     str_cat(R(A),R(B)) */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(5, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(str_cat), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_HASH:
      /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+2*C-1)) */
      allocseq(s, 5);
      genop(s, NIOS2_addi(4, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(5, GETARG_C(i)));
      genop(s, NIOS2_ldw(2, CTX(hash_new), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_LAMBDA:
      /* A Bz Cz R(A) := lambda(SEQ[Bz],Cm) */
      allocseq(s, 5);
      genop(s, NIOS2_movui(4, GETARG_b(i)));
      genop(s, NIOS2_movui(5, GETARG_c(i)));
      genop(s, NIOS2_ldw(2, CTX(lambda), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_RANGE:
      /* A B C   R(A) := range_new(R(B),R(B+1),C) (C=0:'..', C=1:'...') */
      allocseq(s, 6);
      genop(s, NIOS2_addi(4, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_addi(5, (GETARG_B(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(6, GETARG_C(i)));
      genop(s, NIOS2_ldw(2, CTX(range_new), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_OCLASS:
      /* A       R(A) := ::Object */
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, CTX(object_class), NIOS2_VMCTX_REG));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_CLASS:
      /* A B     R(A) := newclass(R(A),mSym(B),R(A+1)) */
      allocseq(s, 6);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(5, GETARG_B(i)));
      genop(s, NIOS2_ldw(6, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(newclass), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_MODULE:
      /* A B     R(A) := newmodule(R(A),mSym(B)) */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(5, GETARG_B(i)));
      genop(s, NIOS2_ldw(2, CTX(newmodule), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_EXEC:
      /* A Bx    R(A) := blockexec(R(A),SEQ[Bx]) */
      allocseq(s, 5);
      genop(s, NIOS2_ldw(2, CTX(blockexec), NIOS2_VMCTX_REG));
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_movui(5, GETARG_Bx(i)));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_METHOD:
      /* A B     R(A).newmethod(mSym(B),R(A+1)) */
      allocseq(s, 8);
      genop(s, NIOS2_movui(4, GETARG_B(i)));
      genop(s, NIOS2_ldw(2, CTX(load_sym), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_mov(5, 2));
      genop(s, NIOS2_ldw(6, (GETARG_A(i)+1)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(newmethod), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_SCLASS:
      /* A B     R(A) := R(B).singleton_class */
      allocseq(s, 4);
      genop(s, NIOS2_ldw(4, GETARG_B(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(singleton_class), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_TCLASS:
      /* A       R(A) := target_class */
      allocseq(s, 3);
      genop(s, NIOS2_ldw(2, CTX(target_class), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_stw(2, GETARG_A(i)*4, NIOS2_STACK_REG));
      break;
    case OP_DEBUG:
      /* A       print R(A) */
      allocseq(s, 3);
      genop(s, NIOS2_ldw(4, GETARG_A(i)*4, NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(raise), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_STOP:
      /*         stop VM */
      allocseq(s, 2);
      genop(s, NIOS2_ldw(2, CTX(stop_vm), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    case OP_ERR:
      /* Bx      raise RuntimeError with message Lit(Bx) */
      allocseq(s, 6);
      genop(s, NIOS2_ldw(4, GETARG_Bx(i), NIOS2_STACK_REG));
      genop(s, NIOS2_ldw(2, CTX(load_lit), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      genop(s, NIOS2_mov(4, 2));
      genop(s, NIOS2_ldw(2, CTX(raise_err), NIOS2_VMCTX_REG));
      genop(s, NIOS2_callr(2));
      break;
    default:
      return "unknown instruction";
    }
  }

  /* fill relative jumps */
  for(src_pc = s->rite_pc, src_ilen = s->rite_ilen;
      src_ilen > 0;
      --src_ilen, ++src_pc) {
    int pos;
    uint16_t from_pc, to_pc;
    int16_t sbx;
    mrb_code *inst_update;
    i = *src_pc;

    pos = GETARG_Ax(i) & ((1<<JUMPOFFSET_BITS)-1);
    if (pos == 0) {
      continue;
    }

    from_pc = (GETARG_Ax(i) >> JUMPOFFSET_BITS) + pos;
    inst_update = &s->new_iseq[from_pc - 1];
    sbx = NIOS2_GET_IMM16(*inst_update);
    to_pc = GETARG_Ax(src_pc[sbx]) >> JUMPOFFSET_BITS;

    *inst_update = (*inst_update & ~NIOS2_IMM16(0xffff)) |
      NIOS2_IMM16(to_pc - from_pc);
  }

  return NULL;
}

static const char *
convert_irep(mrb_state *mrb, mrb_irep *irep)
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

  errmsg = convert_iseq(&scope);

  if (errmsg == NULL) {
    mrb_free(mrb, irep->iseq);
    irep->iseq = scope.new_iseq;
    irep->ilen = scope.pc;
    irep->lines = 0;  /* TODO */
  }
  else {
    mrb_free(mrb, scope.new_iseq);
  }
  return errmsg;
}

static void
codedump(mrb_state *mrb, int n)
{
#ifdef ENABLE_STDIO
  mrb_irep *irep = mrb->irep[n];
  int i;
  mrb_code c;
  const struct nios2_disasm_map *map;

  if (!irep) return;
  printf("irep %d nregs=%d nlocals=%d pools=%d syms=%d\n", n,
         irep->nregs, irep->nlocals, (int)irep->plen, (int)irep->slen);
  for (i=0; i<irep->ilen; i++) {
    c = irep->iseq[i];
    printf("%03d 0x%08x ", i, c);
    map = disasm_map + NIOS2_GET_OP(c);
L_RETRY:
    switch (map->format) {
    case fmt_none:
      if (map->mnemonic) {
        printf("%s\n", map->mnemonic);
      }
      else {
        printf("unknown\n");
      }
      break;
    case fmt_opx:
      map = disasm_mapx + NIOS2_GET_OPX(c);
      goto L_RETRY;
    case fmt_cab:
      printf("%-8sr%d, r%d, r%d\n", map->mnemonic,
        NIOS2_GET_C(c), NIOS2_GET_A(c), NIOS2_GET_B(c));
      break;
    case fmt_basv:
      printf("%-8sr%d, r%d, %d\n", map->mnemonic,
        NIOS2_GET_B(c), NIOS2_GET_A(c), (int16_t)NIOS2_GET_IMM16(c));
      break;
    case fmt_bauv:
      printf("%-8sr%d, r%d, 0x%04x\n", map->mnemonic,
        NIOS2_GET_B(c), NIOS2_GET_A(c), NIOS2_GET_IMM16(c));
      break;
    case fmt_absv:
      printf("%-8sr%d, r%d, %d\n", map->mnemonic,
        NIOS2_GET_A(c), NIOS2_GET_B(c), (int16_t)NIOS2_GET_IMM16(c));
      break;
    case fmt_sv:
      printf("%-8s%d\n", map->mnemonic, (int16_t)NIOS2_GET_IMM16(c));
      break;
    case fmt_i5:
      printf("%-8s%d\n", map->mnemonic, NIOS2_GET_IMM5(c));
      break;
    case fmt_i26:
      printf("%-8s0x%07x\n", map->mnemonic, NIOS2_GET_IMM26(c));
      break;
    case fmt_a:
      printf("%-8sr%d\n", map->mnemonic, NIOS2_GET_A(c));
      break;
    case fmt_cust:
      /* TODO */
      break;
    case fmt_sva:
      printf("%-8s%d, r%d\n", map->mnemonic,
        (int16_t)NIOS2_GET_IMM16(c), NIOS2_GET_A(c));
      break;
    case fmt_ldst:
      printf("%-8sr%d, %d(r%d)\n", map->mnemonic,
        NIOS2_GET_B(c), (int16_t)NIOS2_GET_IMM16(c), NIOS2_GET_A(c));
      break;
    case fmt_ca:
      printf("%-8sr%d, r%d\n", map->mnemonic,
        NIOS2_GET_C(c), NIOS2_GET_A(c));
      break;
    case fmt_buv:
      printf("%-8sr%d, 0x%04x\n", map->mnemonic,
        NIOS2_GET_B(c), NIOS2_GET_IMM16(c));
      break;
    case fmt_bsv:
      printf("%-8sr%d, %d\n", map->mnemonic,
        NIOS2_GET_B(c), (int16_t)NIOS2_GET_IMM16(c));
      break;
    case fmt_c:
      printf("%-8sr%d\n", map->mnemonic, NIOS2_GET_C(c));
      break;
    case fmt_cn:
      break;
    case fmt_cai5:
      break;
    case fmt_na:
      break;
    }
  }
  printf("\n");
#endif
}

#endif  /* MRB_CONVERTER_NIOS2 */
