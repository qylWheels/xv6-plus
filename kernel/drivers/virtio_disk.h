#ifndef _DRIVERS_VIRTIO_DISK_H_
#define _DRIVERS_VIRTIO_DISK_H_

#include <fs/buf.h>

void virtio_disk_init(void);
void virtio_disk_rw(struct buf *, int);
void virtio_disk_intr(void);

#endif // _DRIVERS_VIRTIO_DISK_H_
