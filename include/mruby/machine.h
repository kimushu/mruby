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

  int     (*get_opcode)(mrb_code);
  int     (*getarg_A)  (mrb_code);
  int     (*getarg_B)  (mrb_code);
  int     (*getarg_C)  (mrb_code);
  int     (*getarg_Bx) (mrb_code);
  int     (*getarg_sBx)(mrb_code);
  int32_t (*getarg_Ax) (mrb_code);
  int     (*getarg_b)  (mrb_code);
  int     (*getarg_c)  (mrb_code);

  mrb_code (*mkopcode) (int);
  mrb_code (*mkarg_A)  (int);
  mrb_code (*mkarg_B)  (int);
  mrb_code (*mkarg_C)  (int);
  mrb_code (*mkarg_Bx) (int);
  mrb_code (*mkarg_sBx)(int);
  mrb_code (*mkarg_Ax) (int32_t);
  mrb_code (*mkarg_bc) (int, int);

  int (*make_OP_CALL)(mrb_code* p);
} mrb_machine;

#define MAXLEN_OP_CALL  4

#endif  /* !MRUBY_MACHINE_H */
