#include <core/types.h>
#include <gui/common.h>
#include <gui/win98ui.h>
#include <gui/basic_draw.h>

static int win98_draw_window(int x, int y) {
  uint8 r1 = 0, g1 = 0, b1 = 128;
  uint8 r2 = 16, g2 = 132, b2 = 208;
  int width = 500, height = 30;

  // 标题栏
  for (int row = y; row < y + height; row++) {
    for (int col = x; col < x + width; col++) {
      uint8 r = r1 + col * (r2 - r1) / (width - 1);
      uint8 g = g1 + col * (g2 - g1) / (width - 1);
      uint8 b = b1 + col * (b2 - b1) / (width - 1);
      gui_draw_pixel(col, row, r, g, b, 0xff);
    }
  }

  // 内容栏
  for (int row = y + height; row < y + height + 400; row++) {
    for (int col = x; col < x + width; col++) {
      gui_draw_pixel(col, row, 192, 192, 192, 0xff);
    }
  }

  return gui_flush();
}

struct gui_window_manager win98_window_manager = {
    .name = "win98_window_manager", .ops = {.draw = win98_draw_window}};
