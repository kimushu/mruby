/*
** vm_nios2.c - virtual machine for Nios2
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/irep.h"

#ifdef MRB_MACHINE_NIOS2


mrb_value
mrb_funcall(mrb_state *mrb, mrb_value self, const char *name, int argc, ...)
{
  /* TODO: placeholder */
  return mrb_nil_value();
}

mrb_value
mrb_funcall_with_block(mrb_state *mrb, mrb_value self, mrb_sym mid, int argc, mrb_value *argv, mrb_value blk)
{
  /* TODO: placeholder */
  return mrb_nil_value();
}

mrb_value
mrb_funcall_argv(mrb_state *mrb, mrb_value self, mrb_sym mid, int argc, mrb_value *argv)
{
  return mrb_funcall_with_block(mrb, self, mid, argc, argv, mrb_nil_value());
}

mrb_value
mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, struct RClass *c)
{
  /* TODO: placeholder */
  return mrb_nil_value();
}

mrb_value
mrb_run(mrb_state *mrb, struct RProc *proc, mrb_value self)
{
  /* TODO: placeholder */
  return mrb_nil_value();
}

#endif  /* MRB_MACHINE_NIOS2 */
