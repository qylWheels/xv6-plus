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

void drivers_virtio_gpu_init(void);
void drivers_virtio_gpu_get_display_info(struct virtio_gpu_display_one* arr);
void drivers_virtio_gpu_create_2d_resource(void);
void drivers_virtio_gpu_attach_backing(void);
void drivers_virtio_gpu_transfer_to_host_2d(void);

#endif  // _DRIVERS_VIRTIO_GPU_H_
