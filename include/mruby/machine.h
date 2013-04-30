/*
** mruby/machine.h - machine (VM type) definition
*/

#ifndef MRUBY_MACHINE_H
#define MRUBY_MACHINE_H

typedef struct mrb_machine {
  const char *compiler_name;
  const char *compiler_version;

  int32_t maxarg_Bx;
  int32_t maxarg_sBx;
  int     maxstk_depth;

  int     (*get_opcode)(const mrb_code*);
  int     (*getarg_A)  (const mrb_code*);
  int     (*getarg_B)  (const mrb_code*);
  int     (*getarg_C)  (const mrb_code*);
  int     (*getarg_Bx) (const mrb_code*);
  int     (*getarg_sBx)(const mrb_code*);
  int32_t (*getarg_Ax) (const mrb_code*);
  int     (*getarg_b)  (const mrb_code*);
  int     (*getarg_c)  (const mrb_code*);

  mrb_code* (*prev_op)(mrb_code*);

  int (*mkop_A)   (void *genop, void *p ,int op, int a);
  int (*mkop_AB)  (void *genop, void *p, int op, int a, int b);
  int (*mkop_ABC) (void *genop, void *p, int op, int a, int b, int c);
  int (*mkop_ABx) (void *genop, void *p, int op, int a, int bx);
  int (*mkop_Bx)  (void *genop, void *p, int op, int bx);
  int (*mkop_sBx) (void *genop, void *p, int op, int sbx);
  int (*mkop_AsBx)(void *genop, void *p, int op, int a, int sbx);
  int (*mkop_Ax)  (void *genop, void *p, int op, int32_t ax);
  int (*mkop_Abc) (void *genop, void *p, int op, int a, int b, int c);

} mrb_machine;

typedef void (*GENOP)(void *p, mrb_code i);

#define MAXLEN_OP_CALL  4
#define MAXLEN_LIT 3

#endif  /* !MRUBY_MACHINE_H */
