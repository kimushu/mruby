/*
** rubic.h - RiteVM accelerator for Nios II
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_RUBIC_H
#define MRUBY_RUBIC_H

#ifndef ENABLE_RUBIC
#error This header requires ENABLE_RUBIC
#endif

#ifndef __NIOS2__
#error This header supports Nios II only
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define RUBIC_R2N_RATIO   4

#define RUBIC_RNUM_STATE      20
#define RUBIC_RNUM_STACK      21
#define RUBIC_RNUM_LITERAL    22
#define RUBIC_RNUM_SYMBOL     23
#define RUBIC_TO_S(x)         #x
#define RUBIC_REGNAME(num)    "r"RUBIC_TO_S(num)
#define RUBIC_REG_STATE       RUBIC_REGNAME(RUBIC_RNUM_STATE)
#define RUBIC_REG_STACK       RUBIC_REGNAME(RUBIC_RNUM_STACK)
#define RUBIC_REG_LITERAL     RUBIC_REGNAME(RUBIC_RNUM_LITERAL)
#define RUBIC_REG_SYMBOL      RUBIC_REGNAME(RUBIC_RNUM_SYMBOL)

typedef struct rubic_state
{
  int enabled;
  void *inst_base;
  void **ctl_base;
}
rubic_state;

extern void (*mrb_open_rubic)(rubic_state *);

#ifdef MRUBY_H

struct rubic_result;
typedef struct rubic_result (*rubic_native_ptr)(void);

struct rubic_result
{
  rubic_native_ptr next;
  mrb_code inst;
};

void mrb_init_rubic(struct mrb_state *mrb);
rubic_native_ptr rubic_r2n(struct mrb_state *mrb, mrb_code *iseq);
struct rubic_result rubic_enter(rubic_native_ptr ptr);
mrb_code *rubic_n2r(struct mrb_state *mrb, rubic_native_ptr pc);
void rubic_flush_icache(struct mrb_state *mrb, struct mrb_irep *irep);

#endif  /* MRUBY_H */

#if defined(__cplusplus)
}  /* extern "C" { */
#endif

#endif  /* MRUBY_RUBIC_H */
