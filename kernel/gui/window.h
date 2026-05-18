#ifndef _GUI_WINDOW_H_
#define _GUI_WINDOW_H_

struct gui_window_ops {
  int (*draw)(int x, int y);
};

struct gui_window_manager {
  char name[32];
  struct gui_window_ops ops;
};

void gui_set_window_manager(struct gui_window_manager* manager);
int gui_draw_window(int x, int y);

#endif  // _GUI_WINDOW_H_
