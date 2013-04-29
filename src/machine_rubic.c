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

#define RUBIC_COMPILER_NAME      "RbcN"  /* Rubic Nios2 */
#define RUBIC_COMPILER_VERSION   "0000"

#define RUBIC_MAXARG_Bx    (0x3ff)
#define RUBIC_MAXARG_sBx   ((RUBIC_MAXARG_Bx+1)>>1)
#define NIOS2_CUSTOM_uOP   0x32    /* rubic custom instruction */
#define RUBIC_RETVAL_IDX   2       /* r2: return value */
#define RUBIC_WITH_RETVAL  ((mrb_code)(RUBIC_RETVAL_IDX<<17)|(1<<14)) /* writerc=1 */
#define NIOS2_READRA        (1<<16)
#define NIOS2_READRB        (1<<15)
#define NIOS2_WRITERC       (1<<14)

static int
rubic_get_opcode(mrb_code i)
{
  return (i >> 6) & 0x7f;
}

static int
rubic_getarg_A(mrb_code i)
{
  return (i >> 27) & 0x1f;
}

static int
rubic_getarg_B(mrb_code i)
{
  return (i >> 22) & 0x1f;
}

static int
rubic_getarg_C(mrb_code i)
{
  return (i >> 17) & 0x1f;
}

static int
rubic_getarg_Bx(mrb_code i)
{
  return (i >> 17) & 0x3ff;
}

static int
rubic_getarg_sBx(mrb_code i)
{
  return rubic_getarg_Bx(i) - RUBIC_MAXARG_sBx;
}

static int32_t
rubic_getarg_Ax(mrb_code i)
{
  return (i >> 17) & 0x7fff;
}

static int
rubic_getarg_b(mrb_code i)
{
  return (i >> 19) & 0xff;
}

static int
rubic_getarg_c(mrb_code i)
{
  return (i >> 17) & 0x3;
}

static mrb_code
rubic_mkopcode(int op)
{
  return ((op & 0x7f) << 6) | NIOS2_CUSTOM_uOP;
}

static mrb_code
rubic_mkarg_A(int c)
{
  return (mrb_code)(c & 0x1f) << 27;
}

static mrb_code
rubic_mkarg_B(int c)
{
  return (mrb_code)(c & 0x1f) << 22;
}

static mrb_code
rubic_mkarg_C(int c)
{
  return (mrb_code)(c & 0x1f) << 17;
}

static mrb_code
rubic_mkarg_Bx(int v)
{
  return (mrb_code)(v & 0x3ff) << 17;
}

static mrb_code
rubic_mkarg_sBx(int v)
{
  return rubic_mkarg_Bx(v + RUBIC_MAXARG_sBx);
}

static mrb_code
rubic_mkarg_Ax(int32_t v)
{
  return (mrb_code)(v & 0x7fff) << 17;
}

static mrb_code
rubic_mkarg_bc(int b, int c)
{
  return ((mrb_code)(b & 0xff) << 19) | ((mrb_code)(b & 0x3) << 17);
}

static int
rubic_mkop_lit_int(mrb_code *p, int a, mrb_int i)
{
  int len = 0;
  /* ori r2, r0, (i[14:0]<<1)|1 */
  *p++ = (0 << 27) | (2 << 22) | ((((i & 0x7fff) << 1) + 1) << 6) | 0x14;
  ++len;
  if(i >= (1<<15) || i < -(1<<15)) {
    /* orih r2, r2, i[30:15] */
    *p++ = (2 << 27) | (2 << 22) | ((((i & 0x7fff8000) >> 14) + 1) << 6) | 0x34;
    ++len;
  }
  *p++ = rubic_mkopcode(OP_REGMOVE) | rubic_mkarg_A(2) | NIOS2_READRA | rubic_mkarg_C(a);
  return ++len;
}

static int
rubic_make_OP_CALL(mrb_code* p)
{
  p[0] = rubic_mkopcode(OP_CALL) | RUBIC_WITH_RETVAL;
  p[1] = (RUBIC_RETVAL_IDX << 27) | (0x0d << 11) | (0x3a); // jmp r*
  return 2;
}

struct mrb_machine machine_rubic =
{
  .compiler_name    = RUBIC_COMPILER_NAME,
  .compiler_version = RUBIC_COMPILER_VERSION,

  .maxarg_Bx  = RUBIC_MAXARG_Bx,
  .maxarg_sBx = RUBIC_MAXARG_sBx,

  .get_opcode = rubic_get_opcode,
  .getarg_A   = rubic_getarg_A,
  .getarg_B   = rubic_getarg_B,
  .getarg_C   = rubic_getarg_C,
  .getarg_Bx  = rubic_getarg_Bx,
  .getarg_sBx = rubic_getarg_sBx,
  .getarg_Ax  = rubic_getarg_Ax,
  .getarg_b   = rubic_getarg_b,
  .getarg_c   = rubic_getarg_c,

  .mkopcode   = rubic_mkopcode,
  .mkarg_A    = rubic_mkarg_A,
  .mkarg_B    = rubic_mkarg_B,
  .mkarg_C    = rubic_mkarg_C,
  .mkarg_Bx   = rubic_mkarg_Bx,
  .mkarg_sBx  = rubic_mkarg_sBx,
  .mkarg_Ax   = rubic_mkarg_Ax,
  .mkarg_bc   = rubic_mkarg_bc,

  .mkop_lit_int = rubic_mkop_lit_int,
  .make_OP_CALL = rubic_make_OP_CALL,
};
#endif

