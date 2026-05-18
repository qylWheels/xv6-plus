#ifndef _DRIVERS_VIRTIO_GPU_H_
#define _DRIVERS_VIRTIO_GPU_H_

#include <core/types.h>

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_rect {
  uint32 x;
  uint32 y;
  uint32 width;
  uint32 height;
};

struct virtio_gpu_display_one {
  struct virtio_gpu_rect r;
  uint32 enabled;
  uint32 flags;
};

int drivers_virtio_gpu_init(void);
int drivers_virtio_gpu_draw_pixel(int x, int y, uint8 r, uint8 g, uint8 b,
                                  uint8 a);
int drivers_virtio_gpu_flush(void);

#endif  // _DRIVERS_VIRTIO_GPU_H_
