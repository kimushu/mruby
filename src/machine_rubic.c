/*
** machine_rubic.c - RubivVM machine definition
*/

#ifdef MACHINE_RUBIC
#include "mruby.h"
#include "opcode.h"
#include <stdio.h>
#include <string.h>

/*         (Rubic rubic VM)          */
/* instructions: packed into rubic's */
/*               custom instruction */
/* -------------------------------  */
/*     A:B:C:OP:uOP = 5: 5: 5: 7: 6 */
/*      A:Bx:OP:uOP =    5:10: 7: 6 */
/*        Ax:OP:uOP =      15: 7: 6 */
/*   A:Bz:Cz:OP:uOP = 5: 8: 2: 7: 6 */
/* -------------------------------  */
/* jump instructions are consist of */
/* custom instruction and rubic stan-*/
/* dard instruction.                */

#define COMPILER_NAME     "RbcN"  /* Rubic Nios2 */
#define COMPILER_VERSION  "0000"

#define MAXARG_Bx         (0x3ff)
#define MAXARG_sBx        ((MAXARG_Bx+1)>>1)

#define RUBIC_SMALL_A     0x1f
#define RUBIC_SMALL_B     0x1f
#define RUBIC_SMALL_C     0x1f

#define NIOS2_A(i)        ((mrb_code)((i)&0x1f)<<27)
#define NIOS2_B(i)        ((mrb_code)((i)&0x1f)<<22)
#define NIOS2_C(i)        ((mrb_code)((i)&0x1f)<<17)
#define NIOS2_readra      ((mrb_code)1<<16)
#define NIOS2_readrb      ((mrb_code)1<<15)
#define NIOS2_writerc     ((mrb_code)1<<14)
#define NIOS2_OPX(i)      ((mrb_code)((i)&0x3f)<<11)
#define NIOS2_IMM16(i)    ((mrb_code)((i)&0xffff)<<6)
#define NIOS2_n(i)        ((mrb_code)((i)&0xff)<<6)
#define NIOS2_OP(i)       ((mrb_code)((i)&0x7f))

#define NIOS2_GETOP(c)    (((c)&0x7f)|((((c)&0x7f)==0x3a)?(((c)>>11)&0x3f):0))

#define NIOS2_OP_bne      (NIOS2_OP(0x1e))
#define NIOS2_OP_callr    (NIOS2_OP(0x3a)|NIOS2_OPX(0x1d))
#define NIOS2_OP_custom   (NIOS2_OP(0x32))
#define NIOS2_OP_jmp      (NIOS2_OP(0x3a)|NIOS2_OPX(0x0d))
#define NIOS2_OP_nextpc   (NIOS2_OP(0x3a)|NIOS2_OPX(0x1c))
#define NIOS2_OP_orhi     (NIOS2_OP(0x34))
#define NIOS2_OP_ori      (NIOS2_OP(0x14))
#define NIOS2_OP_ret      (NIOS2_OP(0x3a)|NIOS2_OPX(0x05))

#define RUBIC_JUMP        1
#define RUBIC_CALL        2
#define RUBIC_RET         4
#define RUBIC_ALWAYS      8

static const uint8_t jump_flags[128] =
{
  [OP_NOP]        = 0,                      /*                                                 */
  [OP_MOVE]       = 0,                      /* A B     R(A) := R(B)                            */
  [OP_LOADL]      = RUBIC_JUMP|RUBIC_ALWAYS,/* A Bx    R(A) := Lit(Bx)                         */
  [OP_LOADI]      = 0,                      /* A sBx   R(A) := sBx                             */
  [OP_LOADSYM]    = 0,                      /* A Bx    R(A) := Sym(Bx)                         */
  [OP_LOADNIL]    = 0,                      /* A       R(A) := nil                             */
  [OP_LOADSELF]   = 0,                      /* A       R(A) := self                            */
  [OP_LOADT]      = 0,                      /* A       R(A) := true                            */
  [OP_LOADF]      = 0,                      /* A       R(A) := false                           */

  [OP_GETGLOBAL]  = 0,                      /* A Bx    R(A) := getglobal(Sym(Bx))              */
  [OP_SETGLOBAL]  = 0,                      /* A Bx    setglobal(Sym(Bx), R(A))                */
  [OP_GETSPECIAL] = 0,                      /* A Bx    R(A) := Special[Bx]                     */
  [OP_SETSPECIAL] = 0,                      /* A Bx    Special[Bx] := R(A)                     */
  [OP_GETIV]      = 0,                      /* A Bx    R(A) := ivget(Sym(Bx))                  */
  [OP_SETIV]      = 0,                      /* A Bx    ivset(Sym(Bx),R(A))                     */
  [OP_GETCV]      = 0,                      /* A Bx    R(A) := cvget(Sym(Bx))                  */
  [OP_SETCV]      = 0,                      /* A Bx    cvset(Sym(Bx),R(A))                     */
  [OP_GETCONST]   = 0,                      /* A Bx    R(A) := constget(Sym(Bx))               */
  [OP_SETCONST]   = 0,                      /* A Bx    constset(Sym(Bx),R(A))                  */
  [OP_GETMCNST]   = 0,                      /* A Bx    R(A) := R(A)::Sym(B)                    */
  [OP_SETMCNST]   = 0,                      /* A Bx    R(A+1)::Sym(B) := R(A)                  */
  [OP_GETUPVAR]   = 0,                      /* A B C   R(A) := uvget(B,C)                      */
  [OP_SETUPVAR]   = 0,                      /* A B C   uvset(B,C,R(A))                         */

  [OP_JMP]        = RUBIC_JUMP|RUBIC_ALWAYS,/* sBx     pc+=sBx                                 */
  [OP_JMPIF]      = RUBIC_JUMP,             /* A sBx   if R(A) pc+=sBx                         */
  [OP_JMPNOT]     = RUBIC_JUMP,             /* A sBx   if !R(A) pc+=sBx                        */
  [OP_ONERR]      = 0,                      /* sBx     rescue_push(pc+sBx)                     */
  [OP_RESCUE]     = 0,                      /* A       clear(exc); R(A) := exception (ignore when A=0) */
  [OP_POPERR]     = 0,                      /* A       A.times{rescue_pop()}                   */
  [OP_RAISE]      = RUBIC_JUMP|RUBIC_ALWAYS,/* A       raise(R(A))                             */
  [OP_EPUSH]      = 0,                      /* Bx      ensure_push(SEQ[Bx])                    */
  [OP_EPOP]       = 0,/* special */         /* A       A.times{ensure_pop().call}              */

  [OP_SEND]       = RUBIC_CALL|RUBIC_ALWAYS,/* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C)) */
  [OP_SENDB]      = RUBIC_CALL|RUBIC_ALWAYS,/* A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C),&R(A+C+1)) */
  [OP_FSEND]      = 0,                      /* A B C   R(A) := fcall(R(A),mSym(B),R(A+1),...,R(A+C-1)) */
  [OP_CALL]       = RUBIC_JUMP|RUBIC_ALWAYS,/* A B C   R(A) := self.call(R(A),.., R(A+C))      */
  [OP_SUPER]      = RUBIC_CALL|RUBIC_ALWAYS,/* A B C   R(A) := super(R(A+1),... ,R(A+C-1))     */
  [OP_ARGARY]     = 0,                      /* A Bx    R(A) := argument array (16=6:1:5:4)     */
  [OP_ENTER]      = 0,                      /* Ax      arg setup according to flags (24=5:5:1:5:5:1:1) */
  [OP_KARG]       = 0,                      /* A B C   R(A) := kdict[mSym(B)]; if C kdict.rm(mSym(B)) */
  [OP_KDICT]      = 0,                      /* A C     R(A) := kdict                           */

  [OP_RETURN]     = RUBIC_RET|RUBIC_ALWAYS, /* A B     return R(A) (B=normal,in-block return/break) */
  [OP_TAILCALL]   = 0,                      /* A B C   return call(R(A),mSym(B),*R(C))         */
  [OP_BLKPUSH]    = 0,                      /* A Bx    R(A) := block (16=6:1:5:4)              */

  [OP_ADD]        = RUBIC_CALL,             /* A B C   R(A) := R(A)+R(A+1) (mSyms[B]=:+,C=1)   */
  [OP_ADDI]       = RUBIC_CALL,             /* A B C   R(A) := R(A)+C (mSyms[B]=:+)            */
  [OP_SUB]        = RUBIC_CALL,             /* A B C   R(A) := R(A)-R(A+1) (mSyms[B]=:-,C=1)   */
  [OP_SUBI]       = RUBIC_CALL,             /* A B C   R(A) := R(A)-C (mSyms[B]=:-)            */
  [OP_MUL]        = RUBIC_CALL,             /* A B C   R(A) := R(A)*R(A+1) (mSyms[B]=:*,C=1)   */
  [OP_DIV]        = RUBIC_CALL,             /* A B C   R(A) := R(A)/R(A+1) (mSyms[B]=:/,C=1)   */
  [OP_EQ]         = RUBIC_CALL,             /* A B C   R(A) := R(A)==R(A+1) (mSyms[B]=:==,C=1) */
  [OP_LT]         = RUBIC_CALL,             /* A B C   R(A) := R(A)<R(A+1)  (mSyms[B]=:<,C=1)  */
  [OP_LE]         = RUBIC_CALL,             /* A B C   R(A) := R(A)<=R(A+1) (mSyms[B]=:<=,C=1) */
  [OP_GT]         = RUBIC_CALL,             /* A B C   R(A) := R(A)>R(A+1)  (mSyms[B]=:>,C=1)  */
  [OP_GE]         = RUBIC_CALL,             /* A B C   R(A) := R(A)>=R(A+1) (mSyms[B]=:>=,C=1) */

  [OP_ARRAY]      = 0,                      /* A B C   R(A) := ary_new(R(B),R(B+1)..R(B+C))    */
  [OP_ARYCAT]     = 0,                      /* A B     ary_cat(R(A),R(B))                      */
  [OP_ARYPUSH]    = 0,                      /* A B     ary_push(R(A),R(B))                     */
  [OP_AREF]       = 0,                      /* A B C   R(A) := R(B)[C]                         */
  [OP_ASET]       = 0,                      /* A B C   R(B)[C] := R(A)                         */
  [OP_APOST]      = 0,                      /* A B C   *R(A),R(A+1)..R(A+C) := R(A)            */

  [OP_STRING]     = 0,                      /* A Bx    R(A) := str_dup(Lit(Bx))                */
  [OP_STRCAT]     = 0,                      /* A B     str_cat(R(A),R(B))                      */

  [OP_HASH]       = 0,                      /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C))   */
  [OP_LAMBDA]     = 0,                      /* A Bz Cz R(A) := lambda(SEQ[Bz],Cm)              */
  [OP_RANGE]      = 0,                      /* A B C   R(A) := range_new(R(B),R(B+1),C)        */

  [OP_OCLASS]     = 0,                      /* A       R(A) := ::Object                        */
  [OP_CLASS]      = 0,                      /* A B     R(A) := newclass(R(A),mSym(B),R(A+1))   */
  [OP_MODULE]     = 0,                      /* A B     R(A) := newmodule(R(A),mSym(B))         */
  [OP_EXEC]       = 0,                      /* A Bx    R(A) := blockexec(R(A),SEQ[Bx])         */
  [OP_METHOD]     = 0,                      /* A B     R(A).newmethod(mSym(B),R(A+1))          */
  [OP_SCLASS]     = 0,                      /* A B     R(A) := R(B).singleton_class            */
  [OP_TCLASS]     = 0,                      /* A       R(A) := target_class                    */

  [OP_DEBUG]      = 0,                      /* A       print R(A)                              */
  [OP_STOP]       = 0,                      /* stop VM                                         */
  [OP_ERR]        = 0,                      /* Bx      raise RuntimeError with message Lit(Bx) */

  [OP_REGMOVE]    = 0,                      /* C rA    [Type1] rubic:R(C) := nios2:r(A)        */
  /*                                           rC A    [Type2] nios2:r(C) := rubic:R(A)        */

  [OP_RSVD1]      = 0,                      /* reserved instruction #1                         */
  [OP_RSVD2]      = 0,                      /* reserved instruction #2                         */
  [OP_RSVD3]      = 0,                      /* reserved instruction #3                         */
  [OP_RSVD4]      = 0,                      /* reserved instruction #4                         */
  [OP_RSVD5]      = 0,                      /* reserved instruction #5                         */
};

static int
get_opcode(const mrb_code *i)
{
  return (*i >> 6) & 0x7f;
}

static int
getarg_A(const mrb_code *i)
{
  return (*i >> 27) & 0x1f;
}

static int
getarg_B(const mrb_code *i)
{
  return (*i >> 22) & 0x1f;
}

static int
getarg_C(const mrb_code *i)
{
  return (*i >> 17) & 0x1f;
}

static int
getarg_Bx(const mrb_code *i)
{
  return (*i >> 17) & 0x3ff;
}

static int
getarg_sBx(const mrb_code *i)
{
  return getarg_Bx(i) - MAXARG_sBx;
}

static int32_t
getarg_Ax(const mrb_code *i)
{
  return (*i >> 17) & 0x7fff;
}

static int
getarg_b(const mrb_code *i)
{
  return (*i >> 19) & 0xff;
}

static int
getarg_c(const mrb_code *i)
{
  return (*i >> 17) & 0x3;
}

static int
prologue(void *genop, void *p, uint8_t flags, mrb_code *i)
{
  if (flags & (RUBIC_JUMP|RUBIC_CALL)) {
    *i |= NIOS2_C(2)|NIOS2_writerc;
  }
  else if (flags & RUBIC_RET) {
    *i |= NIOS2_C(31)|NIOS2_writerc;
  }

  return 0;
}

static int
epilogue(void *genop, void *p, uint8_t flags)
{
  int len = 0;

  if (flags & RUBIC_ALWAYS) {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_bne|NIOS2_A(2)|NIOS2_B(0)|NIOS2_IMM16(4));
  }

  if (flags & RUBIC_JUMP) {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_jmp|NIOS2_A(2));
  }
  else if (flags & RUBIC_CALL) {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_callr|NIOS2_A(2)|NIOS2_C(31));
  }
  else if (flags & RUBIC_RET) {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ret|NIOS2_A(31));
  }

  return len;
}

static mrb_code *
prev_op(mrb_code *i)
{
  mrb_code c;
  int op;

  do {
    c = *--i;
    op = NIOS2_GETOP(c);
  } while (op == NIOS2_OP_bne || op == NIOS2_OP_callr || op == NIOS2_OP_jmp);

  if (op != NIOS2_OP_custom) {
    return NULL;
  }

  if (c & NIOS2_readrb) {
    --i;
  }
  if (c & NIOS2_readra) {
    --i;
  }
  return i;
}

static int
mkop_A(void *genop, void *p, int op, int a)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  if (a <= RUBIC_SMALL_A) {
    i |= NIOS2_A(a);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(a));
    i |= NIOS2_A(len)|NIOS2_readra;
  }

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_AB(void *genop, void *p, int op, int a, int b)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  if (a <= RUBIC_SMALL_A) {
    i |= NIOS2_A(a);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(a<<7));
    i |= NIOS2_A(len)|NIOS2_readra;
  }
  if (b <= RUBIC_SMALL_B) {
    i |= NIOS2_B(b);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(b&0x1ff));
    i |= NIOS2_B(len)|NIOS2_readrb;
  }

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_ABC(void *genop, void *p, int op, int a, int b, int c)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  if (a <= RUBIC_SMALL_A && c <= RUBIC_SMALL_C && flags == 0) {
    i |= NIOS2_A(a)|NIOS2_C(c);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16((a<<7)|(c&0x7f)));
    i |= NIOS2_A(len)|NIOS2_readra;
  }
  if (b <= RUBIC_SMALL_B) {
    i |= NIOS2_B(b);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(b&0x1ff));
    i |= NIOS2_B(len)|NIOS2_readrb;
  }

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_ABx(void *genop, void *p, int op, int a, int bx)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  if (a <= RUBIC_SMALL_A) {
    i |= NIOS2_A(a);
  }
  else {
    ++len;
    (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(a<<7));
    i |= NIOS2_A(len)|NIOS2_readra;
  }

  ++len;
  (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(bx));
  i |= NIOS2_B(len)|NIOS2_readrb;

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_Bx(void *genop, void *p, int op, int bx)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  ++len;
  (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(bx));
  i |= NIOS2_B(len)|NIOS2_readrb;

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_sBx(void *genop, void *p, int op, int sbx)
{
  return mkop_Bx(genop, p, op, sbx + (MAXARG_Bx - MAXARG_sBx));
}

static int
mkop_AsBx(void *genop, void *p, int op, int a, int sbx)
{
  return mkop_ABx(genop, p, op, a, sbx + (MAXARG_Bx - MAXARG_sBx));
}

static int
mkop_Ax(void *genop, void *p, int op, int32_t ax)
{
  int len = 1;
  mrb_code i = NIOS2_OP_custom|NIOS2_n(op);
  uint8_t flags = jump_flags[op & 0x7f];

  ++len;
  (*(GENOP)genop)(p, NIOS2_OP_ori|NIOS2_A(0)|NIOS2_B(len)|NIOS2_IMM16(ax));
  i |= NIOS2_A(len)|NIOS2_readra;

  if (ax > 0xffff) {
    (*(GENOP)genop)(p, NIOS2_OP_orhi|NIOS2_A(len)|NIOS2_B(len)|NIOS2_IMM16(ax>>16));
    ++len;
  }

  len += prologue(genop, p, flags, &i);
  (*(GENOP)genop)(p, i);
  len += epilogue(genop, p, flags);
  return len;
}

static int
mkop_Abc(void *genop, void *p, int op, int a, int b, int c)
{
  return mkop_ABx(genop, p, op, a, (b<<2)|(c&3));
}

struct mrb_machine machine_rubic =
{
  .compiler_name    = COMPILER_NAME,
  .compiler_version = COMPILER_VERSION,

  .maxarg_Bx  = MAXARG_Bx,
  .maxarg_sBx = MAXARG_sBx,

  .get_opcode = get_opcode,
  .getarg_A   = getarg_A,
  .getarg_B   = getarg_B,
  .getarg_C   = getarg_C,
  .getarg_Bx  = getarg_Bx,
  .getarg_sBx = getarg_sBx,
  .getarg_Ax  = getarg_Ax,
  .getarg_b   = getarg_b,
  .getarg_c   = getarg_c,

  .prev_op   = prev_op,

  .mkop_A    = mkop_A,
  .mkop_AB   = mkop_AB,
  .mkop_ABC  = mkop_ABC,
  .mkop_ABx  = mkop_ABx,
  .mkop_Bx   = mkop_Bx,
  .mkop_sBx  = mkop_sBx,
  .mkop_AsBx = mkop_AsBx,
  .mkop_Ax   = mkop_Ax,
  .mkop_Abc  = mkop_Abc,
};

#endif  /* MACHINE_RUBIC */
