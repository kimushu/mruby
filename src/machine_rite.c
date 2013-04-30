/*
** machine_rite.c - RiteVM machine definition
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

#define COMPILER_NAME            "MATZ"
#define COMPILER_VERSION         "0000"

#define MAXARG_Bx   (0xffff)
#define MAXARG_sBx  (MAXARG_Bx>>1)    /* `sBx' is signed */

#define GET_OPCODE(i) ((int)(((mrb_code)(i)) & 0x7f))
#define GETARG_A(i)   ((int)((((mrb_code)(i)) >> 23) & 0x1ff))
#define GETARG_B(i)   ((int)((((mrb_code)(i)) >> 14) & 0x1ff))
#define GETARG_C(i)   ((int)((((mrb_code)(i)) >>  7) & 0x7f))
#define GETARG_Bx(i)  ((int)((((mrb_code)(i)) >>  7) & 0xffff))
#define GETARG_sBx(i) ((int)(GETARG_Bx(i)-MAXARG_sBx))
#define GETARG_Ax(i)  ((int32_t)((((mrb_code)(i)) >>  7) & 0x1ffffff))
#define GETARG_UNPACK_b(i,n1,n2) ((int)((((mrb_code)(i)) >> (7+(n2))) & (((1<<(n1))-1))))
#define GETARG_UNPACK_c(i,n1,n2) ((int)((((mrb_code)(i)) >> 7) & (((1<<(n2))-1))))
#define GETARG_b(i)   GETARG_UNPACK_b(i,14,2)
#define GETARG_c(i)   GETARG_UNPACK_c(i,14,2)

#define MKOPCODE(op)  ((op) & 0x7f)
#define MKARG_A(c)    ((mrb_code)((c) & 0x1ff) << 23)
#define MKARG_B(c)    ((mrb_code)((c) & 0x1ff) << 14)
#define MKARG_C(c)    (((c) & 0x7f) <<  7)
#define MKARG_Bx(v)   ((mrb_code)((v) & 0xffff) << 7)
#define MKARG_sBx(v)  MKARG_Bx((v)+MAXARG_sBx)
#define MKARG_Ax(v)   ((mrb_code)((v) & 0x1ffffff) << 7)
#define MKARG_PACK(b,n1,c,n2) ((((b) & ((1<<n1)-1)) << (7+n2))|(((c) & ((1<<n2)-1)) << 7))
#define MKARG_bc(b,c) MKARG_PACK(b,14,c,2)

#define MKOP_A(op,a)        (MKOPCODE(op)|MKARG_A(a))
#define MKOP_AB(op,a,b)     (MKOP_A(op,a)|MKARG_B(b))
#define MKOP_ABC(op,a,b,c)  (MKOP_AB(op,a,b)|MKARG_C(c))
#define MKOP_ABx(op,a,bx)   (MKOP_A(op,a)|MKARG_Bx(bx))
#define MKOP_Bx(op,bx)      (MKOPCODE(op)|MKARG_Bx(bx))
#define MKOP_sBx(op,sbx)    (MKOPCODE(op)|MKARG_sBx(sbx))
#define MKOP_AsBx(op,a,sbx) (MKOP_A(op,a)|MKARG_sBx(sbx))
#define MKOP_Ax(op,ax)      (MKOPCODE(op)|MKARG_Ax(ax))
#define MKOP_Abc(op,a,b,c)  (MKOP_A(op,a)|MKARG_bc(b,c))

static int
get_opcode(const mrb_code *i)
{
  return GET_OPCODE(*i);
}

static int
getarg_A(const mrb_code *i)
{
  return GETARG_A(*i);
}

static int
getarg_B(const mrb_code *i)
{
  return GETARG_B(*i);
}

static int
getarg_C(const mrb_code *i)
{
  return GETARG_C(*i);
}

static int
getarg_Bx(const mrb_code *i)
{
  return GETARG_Bx(*i);
}

static int
getarg_sBx(const mrb_code *i)
{
  return GETARG_sBx(*i);
}

static int32_t
getarg_Ax(const mrb_code *i)
{
  return GETARG_Ax(*i);
}

static int
getarg_b(const mrb_code *i)
{
  return GETARG_b(*i);
}

static int
getarg_c(const mrb_code *i)
{
  return GETARG_c(*i);
}

static mrb_code *
prev_op(mrb_code *i)
{
  return i - 1;
}

static int
mkop_A(void *genop, void *p, int op, int a)
{
  (*(GENOP)genop)(p, MKOPCODE(op)|MKARG_A(a));
  return 1;
}

static int
mkop_AB(void *genop, void *p, int op, int a, int b)
{
  (*(GENOP)genop)(p, MKOP_A(op,a)|MKARG_B(b));
  return 1;
}

static int
mkop_ABC(void *genop, void *p, int op, int a, int b, int c)
{
  (*(GENOP)genop)(p, MKOP_AB(op,a,b)|MKARG_C(c));
  return 1;
}

static int
mkop_ABx(void *genop, void *p, int op, int a, int bx)
{
  (*(GENOP)genop)(p, MKOP_A(op,a)|MKARG_Bx(bx));
  return 1;
}

static int
mkop_Bx(void *genop, void *p, int op, int bx)
{
  (*(GENOP)genop)(p, MKOPCODE(op)|MKARG_Bx(bx));
  return 1;
}

static int
mkop_sBx(void *genop, void *p, int op, int sbx)
{
  (*(GENOP)genop)(p, MKOPCODE(op)|MKARG_sBx(sbx));
  return 1;
}

static int
mkop_AsBx(void *genop, void *p, int op, int a, int sbx)
{
  (*(GENOP)genop)(p, MKOP_A(op,a)|MKARG_sBx(sbx));
  return 1;
}

static int
mkop_Ax(void *genop, void *p, int op, int32_t ax)
{
  (*(GENOP)genop)(p, MKOPCODE(op)|MKARG_Ax(ax));
  return 1;
}

static int
mkop_Abc(void *genop, void *p, int op, int a, int b, int c)
{
  (*(GENOP)genop)(p, MKOP_A(op,a)|MKARG_bc(b,c));
  return 1;
}

struct mrb_machine machine_rite =
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

