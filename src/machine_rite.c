/*
** machines.c - VM machines
*/

#include "mruby.h"
#include "opcode.h"
#include <stdio.h>
#include <string.h>

/*            (Rite VM)             */
/* instructions: packed 32 bit      */
/* -------------------------------  */
/*     A:B:C:OP = 9: 9: 7: 7        */
/*      A:Bx:OP =    9:16: 7        */
/*        Ax:OP =      25: 7        */
/*   A:Bz:Cz:OP = 9:14: 2: 7        */

#define RITE_COMPILER_NAME            "MATZ"
#define RITE_COMPILER_VERSION         "0000"

#define RITE_MAXARG_Bx   (0xffff)
#define RITE_MAXARG_sBx  (RITE_MAXARG_Bx>>1)    /* `sBx' is signed */

#define RITE_GET_OPCODE(i) ((int)(((mrb_code)(i)) & 0x7f))
#define RITE_GETARG_A(i)   ((int)((((mrb_code)(i)) >> 23) & 0x1ff))
#define RITE_GETARG_B(i)   ((int)((((mrb_code)(i)) >> 14) & 0x1ff))
#define RITE_GETARG_C(i)   ((int)((((mrb_code)(i)) >>  7) & 0x7f))
#define RITE_GETARG_Bx(i)  ((int)((((mrb_code)(i)) >>  7) & 0xffff))
#define RITE_GETARG_sBx(i) ((int)(RITE_GETARG_Bx(i)-RITE_MAXARG_sBx))
#define RITE_GETARG_Ax(i)  ((int32_t)((((mrb_code)(i)) >>  7) & 0x1ffffff))
#define RITE_GETARG_UNPACK_b(i,n1,n2) ((int)((((mrb_code)(i)) >> (7+(n2))) & (((1<<(n1))-1))))
#define RITE_GETARG_UNPACK_c(i,n1,n2) ((int)((((mrb_code)(i)) >> 7) & (((1<<(n2))-1))))
#define RITE_GETARG_b(i)   RITE_GETARG_UNPACK_b(i,14,2)
#define RITE_GETARG_c(i)   RITE_GETARG_UNPACK_c(i,14,2)

#define RITE_MKOPCODE(op)  ((op) & 0x7f)
#define RITE_MKARG_A(c)    ((mrb_code)((c) & 0x1ff) << 23)
#define RITE_MKARG_B(c)    ((mrb_code)((c) & 0x1ff) << 14)
#define RITE_MKARG_C(c)    (((c) & 0x7f) <<  7)
#define RITE_MKARG_Bx(v)   ((mrb_code)((v) & 0xffff) << 7)
#define RITE_MKARG_sBx(v)  RITE_MKARG_Bx((v)+RITE_MAXARG_sBx)
#define RITE_MKARG_Ax(v)   ((mrb_code)((v) & 0x1ffffff) << 7)
#define RITE_MKARG_PACK(b,n1,c,n2) ((((b) & ((1<<n1)-1)) << (7+n2))|(((c) & ((1<<n2)-1)) << 7))
#define RITE_MKARG_bc(b,c) RITE_MKARG_PACK(b,14,c,2)

static int
rite_get_opcode(mrb_code i)
{
  return RITE_GET_OPCODE(i);
}

static int
rite_getarg_A(mrb_code i)
{
  return RITE_GETARG_A(i);
}

static int
rite_getarg_B(mrb_code i)
{
  return RITE_GETARG_B(i);
}

static int
rite_getarg_C(mrb_code i)
{
  return RITE_GETARG_C(i);
}

static int
rite_getarg_Bx(mrb_code i)
{
  return RITE_GETARG_Bx(i);
}

static int
rite_getarg_sBx(mrb_code i)
{
  return RITE_GETARG_sBx(i);
}

static int32_t
rite_getarg_Ax(mrb_code i)
{
  return RITE_GETARG_Ax(i);
}

static int
rite_getarg_b(mrb_code i)
{
  return RITE_GETARG_b(i);
}

static int
rite_getarg_c(mrb_code i)
{
  return RITE_GETARG_c(i);
}

static mrb_code
rite_mkopcode(int op)
{
  return RITE_MKOPCODE(op);
}

static mrb_code
rite_mkarg_A(int c)
{
  return RITE_MKARG_A(c);
}

static mrb_code
rite_mkarg_B(int c)
{
  return RITE_MKARG_B(c);
}

static mrb_code
rite_mkarg_C(int c)
{
  return RITE_MKARG_C(c);
}

static mrb_code
rite_mkarg_Bx(int v)
{
  return RITE_MKARG_Bx(v);
}

static mrb_code
rite_mkarg_sBx(int v)
{
  return RITE_MKARG_sBx(v);
}

static mrb_code
rite_mkarg_Ax(int32_t v)
{
  return RITE_MKARG_Ax(v);
}

static mrb_code
rite_mkarg_bc(int b, int c)
{
  return RITE_MKARG_bc(b,c);
}

static int
rite_mkop_lit_int(mrb_code *p, int a, mrb_int i)
{
  if (i < RITE_MAXARG_sBx && i > -RITE_MAXARG_sBx) {
    *p = rite_mkopcode(OP_LOADI) | rite_mkarg_A(a) | rite_mkarg_sBx(i);
    return 1;
  }
  return 0;
}

static int
rite_make_OP_CALL(mrb_code *p)
{
  p[0] = rite_mkopcode(OP_CALL) | rite_mkarg_A(0);
  return 1;
}

static struct mrb_machine machine_rite =
{
  .compiler_name    = RITE_COMPILER_NAME,
  .compiler_version = RITE_COMPILER_VERSION,

  .maxarg_Bx  = RITE_MAXARG_Bx,
  .maxarg_sBx = RITE_MAXARG_sBx,

  .get_opcode = rite_get_opcode,
  .getarg_A   = rite_getarg_A,
  .getarg_B   = rite_getarg_B,
  .getarg_C   = rite_getarg_C,
  .getarg_Bx  = rite_getarg_Bx,
  .getarg_sBx = rite_getarg_sBx,
  .getarg_Ax  = rite_getarg_Ax,
  .getarg_b   = rite_getarg_b,
  .getarg_c   = rite_getarg_c,

  .mkopcode   = rite_mkopcode,
  .mkarg_A    = rite_mkarg_A,
  .mkarg_B    = rite_mkarg_B,
  .mkarg_C    = rite_mkarg_C,
  .mkarg_Bx   = rite_mkarg_Bx,
  .mkarg_sBx  = rite_mkarg_sBx,
  .mkarg_Ax   = rite_mkarg_Ax,
  .mkarg_bc   = rite_mkarg_bc,

  .mkop_lit_int = rite_mkop_lit_int,
  .make_OP_CALL = rite_make_OP_CALL,
};

#ifdef MACHINE_RUBIC
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
#define RUBIC_MAXARG_sBx   ((RITE_MAXARG_Bx+1)>>1)
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

static struct mrb_machine machine_rubic =
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

int
mrb_set_machine(mrb_state *mrb, const char *name)
{
  if(strcmp(name, "rite") == 0) {
    mrb->machine = &machine_rite;
    return 0;
  }
#ifdef MACHINE_RUBIC
  else if(strcmp(name, "rubic") == 0) {
    mrb->machine = &machine_rubic;
    return 0;
  }
#endif

  return -1;
}
