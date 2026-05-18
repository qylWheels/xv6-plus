#include <gui/basic_draw.h>
#include <gui/common.h>
#include <utils/misc.h>

struct gui_basic_drawer* curr_basic_drawer = NULL;

void gui_set_basic_drawer(struct gui_basic_drawer* drawer) {
  curr_basic_drawer = drawer;
}

int gui_draw_pixel(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a) {
  if (curr_basic_drawer == NULL) {
    return ENOTREGISTERED;
  }
  if (curr_basic_drawer->ops.draw_pixel == NULL) {
    return ENOTIMPLMENTED;
  }
  return curr_basic_drawer->ops.draw_pixel(x, y, r, g, b, a);
}

int gui_flush(void) {
  if (curr_basic_drawer == NULL) {
    return ENOTREGISTERED;
  }
  if (curr_basic_drawer->ops.flush == NULL) {
    return ENOTIMPLMENTED;
  }
  return curr_basic_drawer->ops.flush();
}
