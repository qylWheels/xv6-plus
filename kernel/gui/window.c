#include <utils/misc.h>
#include <gui/window.h>
#include <gui/common.h>

static struct gui_window_manager* curr_window_manager = NULL;

void gui_set_window_manager(struct gui_window_manager* manager) {
  curr_window_manager = manager;
}

int gui_draw_window(int x, int y) {
  if (curr_window_manager == NULL) {
    return -ENOTREGISTERED;
  }
  if (curr_window_manager->ops.draw == NULL) {
    return -ENOTIMPLMENTED;
  }
  return curr_window_manager->ops.draw(x, y);
}
