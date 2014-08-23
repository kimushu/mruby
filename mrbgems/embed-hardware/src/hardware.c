/*
** hardware.c - Hardware module
**
** See Copyright Notice in mruby.h
*/

#include <string.h>
#include <stdarg.h>
#include "mruby.h"
#include "mruby/range.h"
#include "mruby/class.h"
#include "mruby/data.h"

extern void usleep(uint32_t usecs);
extern void msleep(uint32_t msecs);

extern uint32_t readl(void *address);
extern void writel(uint32_t value, void *address);

#define REGFLAG_ROOT      0x01

struct hw_reg {
  void      *address;
  uint8_t   flags;
  uint8_t   msb;
  uint8_t   lsb;
  uint8_t   width;
  uint32_t  mask;
  union {
    struct hw_reg *parent;
    mrb_value *bits;
  };
};

static mrb_value
ker_usleep(mrb_state *mrb, mrb_value self)
{
  mrb_int usecs;
  mrb_get_args(mrb, "i", &usecs);

  usleep(usecs);
  return self;
}

static mrb_value
ker_msleep(mrb_state *mrb, mrb_value self)
{
  mrb_int msecs;
  mrb_get_args(mrb, "i", &msecs);

  msleep(msecs);
  return self;
}

static mrb_value
ker_sleep(mrb_state *mrb, mrb_value self)
{
  mrb_int secs;
  mrb_get_args(mrb, "i", &secs);

  msleep(secs * 1000);
  return self;
}

static void
hw_reg_free(mrb_state *mrb, void *ptr)
{
  struct hw_reg *data = (struct hw_reg *)ptr;
  if ((data->flags & REGFLAG_ROOT) && data->bits) {
    mrb_free(mrb, data->bits);
  }
  mrb_free(mrb, ptr);
}

static struct mrb_data_type hw_reg_type = { "Register", hw_reg_free };

static mrb_value
hw_reg_wrap(mrb_state *mrb, struct RClass *cls, struct hw_reg *data)
{
  return mrb_obj_value(Data_Wrap_Struct(mrb, cls, &hw_reg_type, data));
}

static mrb_value
hw_reg_new(mrb_state *mrb, mrb_value self)
{
  mrb_int address = 0, msb = 0, lsb = 0, width;
  struct hw_reg *data;

  mrb_get_args(mrb, "iii", &address, &msb, &lsb);
  width = msb - lsb + 1;

  if ((address & 3) || msb < lsb || msb > 31 || lsb < 0 || width == 32) {
    return mrb_nil_value(); /* TODO */
  }

  data = (struct hw_reg *)mrb_calloc(mrb, 1, sizeof(*data));
  data->address = (void *)address;
  data->flags = REGFLAG_ROOT;
  data->msb = msb;
  data->lsb = lsb;
  data->width = width;
  data->mask = ((1 << width) - 1) << lsb;

  return hw_reg_wrap(mrb, mrb_class_ptr(self), data);
}

static mrb_value
hw_reg_slice(mrb_state *mrb, mrb_value self)
{
  mrb_value arg, ret;
  struct hw_reg *data, *newdata;
  mrb_int msb = -1, lsb = -1, width;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  mrb_get_args(mrb, "o", &arg);

  if (mrb_fixnum_p(arg)) {
    /* single bit */
    msb = lsb = mrb_fixnum(arg);
  }
  else if (mrb_type(arg) == MRB_TT_RANGE) {
    /* multiple bits */
    struct RRange *range = mrb_range_ptr(arg);
    if (range->excl || !mrb_fixnum_p(range->edges->beg) || !mrb_fixnum_p(range->edges->end)) {
      goto arg_error;
    }
    msb = mrb_fixnum(range->edges->beg);
    lsb = mrb_fixnum(range->edges->end);
  }
  else {
    goto arg_error;
  }

  width = msb - lsb + 1;
  if (width < 0 || msb >= data->width) goto arg_error;

  if (!(data->flags & REGFLAG_ROOT)) {
    lsb += data->lsb;
    msb += data->lsb;
    data = data->parent;
  }

  if (width == 1) {
    if (!data->bits) {
      data->bits = (mrb_value *)mrb_calloc(mrb, 1, sizeof(mrb_value) * data->width);
    }
    else {
      ret = data->bits[lsb];
      if (!mrb_nil_p(ret)) {
        return ret;
      }
    }
  }

  newdata = (struct hw_reg *)mrb_calloc(mrb, 1, sizeof(struct hw_reg));
  newdata->address = data->address;
  newdata->msb = msb;
  newdata->lsb = lsb;
  newdata->width = width;
  newdata->mask = ((1 << width) - 1) << lsb;
  newdata->parent = data;
  ret = hw_reg_wrap(mrb, mrb_obj_class(mrb, self), newdata);

  if (width == 1) {
    data->bits[lsb] = ret;
  }

  return ret;

arg_error:
  return mrb_nil_value(); /* TODO */
}

static mrb_value
hw_reg_clear(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  writel(readl(data->address) & ~data->mask, data->address);
  return self;
}

static mrb_value
hw_reg_set(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  writel(readl(data->address) | data->mask, data->address);
  return self;
}

static mrb_value
hw_reg_toggle(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  writel(readl(data->address) ^ data->mask, data->address);
  return self;
}

static mrb_value
hw_reg_iscleared(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  if ((readl(data->address) & data->mask) == 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
hw_reg_isset(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  if ((readl(data->address) & data->mask) != 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
hw_reg_read(mrb_state *mrb, mrb_value self)
{
  struct hw_reg *data;

  data = (struct hw_reg *)mrb_data_get_ptr(mrb, self, &hw_reg_type);
  if (!data) return mrb_nil_value();

  return mrb_fixnum_value((readl(data->address) & data->mask) >> data->lsb);
}

static mrb_value
hw_reg_write(mrb_state *mrb, mrb_value self)
{
  return mrb_nil_value(); /* TODO */
}

static mrb_value
hw_reg_modify(mrb_state *mrb, mrb_value self)
{
  return mrb_nil_value(); /* TODO */
}

void
mrb_embed_hardware_gem_init(mrb_state* mrb)
{
  struct RClass *hw;
  struct RClass *reg;

  mrb_define_class_method(mrb, mrb->kernel_module, "usleep", ker_usleep, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, mrb->kernel_module, "msleep", ker_msleep, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, mrb->kernel_module, "sleep",  ker_sleep,  MRB_ARGS_REQ(1));

  hw = mrb_define_module(mrb, "Hardware");
  reg = mrb_define_class_under(mrb, hw, "Register", mrb->object_class);
  mrb_define_class_method(mrb, reg, "new",  hw_reg_new,       MRB_ARGS_REQ(3));
  mrb_define_method(mrb, reg, "slice",    hw_reg_slice,       MRB_ARGS_REQ(1));
  mrb_define_alias (mrb, reg, "[]", "slice");
  mrb_define_method(mrb, reg, "clear",    hw_reg_clear,       MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "set",      hw_reg_set,         MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "toggle",   hw_reg_toggle,      MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "cleared?", hw_reg_iscleared,   MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "set?",     hw_reg_isset,       MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "read",     hw_reg_read,        MRB_ARGS_NONE());
  mrb_define_method(mrb, reg, "write",    hw_reg_write,       MRB_ARGS_REQ(1));
  mrb_define_method(mrb, reg, "modify",   hw_reg_modify,      MRB_ARGS_ARG(1, 1));
}

void
mrb_embed_hardware_gem_final(mrb_state* mrb)
{
}
