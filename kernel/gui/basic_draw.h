#ifndef _GUI_BASIC_DRAW_H_
#define _GUI_BASIC_DRAW_H_

#include <core/types.h>

struct gui_basic_draw_ops {
  int (*draw_pixel)(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a);
  int (*flush)(void);
};

struct gui_basic_drawer {
  char name[32];
  struct gui_basic_draw_ops ops;
};

void gui_set_basic_drawer(struct gui_basic_drawer* drawer);
int gui_draw_pixel(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a);
int gui_flush(void);

#endif  // _GUI_BASIC_DRAW_H_
