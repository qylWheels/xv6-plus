#include <core/riscv.h>
#include <core/types.h>
#include <drivers/virtio.h>
#include <drivers/virtio_gpu.h>
#include <mm/kalloc.h>
#include <mm/memlayout.h>
#include <utils/printf.h>
#include <utils/string.h>

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

#define MMIO(offset) (*(volatile uint32*)(VIRTIO1 + (offset)))
#define MAX_QUEUE_SIZE 128

// // which=desc/avail/used
// #define QUEUE(which, p) ((struct virtq_##which*)p)

struct virtio_gpu_config {
  uint32 events_read;
  uint32 events_clear;
  uint32 num_scanouts;
  uint32 num_capsets;
};

enum virtio_gpu_shm_id {
  VIRTIO_GPU_SHM_ID_UNDEFINED = 0,
  VIRTIO_GPU_SHM_ID_HOST_VISIBLE = 1,
};

enum virtio_gpu_ctrl_type {
  /* 2d commands */
  VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
  VIRTIO_GPU_CMD_RESOURCE_UNREF,
  VIRTIO_GPU_CMD_SET_SCANOUT,
  VIRTIO_GPU_CMD_RESOURCE_FLUSH,
  VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
  VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
  VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
  VIRTIO_GPU_CMD_GET_CAPSET_INFO,
  VIRTIO_GPU_CMD_GET_CAPSET,
  VIRTIO_GPU_CMD_GET_EDID,
  VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
  VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

  /* 3d commands */
  VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
  VIRTIO_GPU_CMD_CTX_DESTROY,
  VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
  VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
  VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
  VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
  VIRTIO_GPU_CMD_SUBMIT_3D,
  VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
  VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

  /* cursor commands */
  VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
  VIRTIO_GPU_CMD_MOVE_CURSOR,

  /* success responses */
  VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
  VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET,
  VIRTIO_GPU_RESP_OK_EDID,
  VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
  VIRTIO_GPU_RESP_OK_MAP_INFO,

  /* error responses */
  VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
  VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
  VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)
#define VIRTIO_GPU_FLAG_INFO_RING_IDX (1 << 1)

struct virtio_gpu_ctrl_hdr {
  uint32 type;
  uint32 flags;
  uint64 fence_id;
  uint32 ctx_id;
  uint8 ring_idx;
  uint8 padding[3];
};

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_rect {
  uint32 x;
  uint32 y;
  uint32 width;
  uint32 height;
};

struct virtio_gpu_resp_display_info {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    uint32 enabled;
    uint32 flags;
  } pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

struct queue {
  char free_desc[MAX_QUEUE_SIZE];   // 1为空闲，0为正在被使用
  struct virtq_desc* desc_ring;     // desc[]
  struct virtq_avail* driver_ring;  // 只需一个，其中已经包含整个环形队列了
  struct virtq_used* device_ring;   // 依旧只需一个，其中包含整个环形队列
  uint32 last_used_idx;             // 最后一个处理完毕的device_ring的下标
};

struct gpu {
  struct queue controlq;
  struct queue cursorq;
  // TODO: 增加一个锁供os正常工作时使用
} gpu;

// -1代表desc[]中的desc都正在被占用
static int alloc_desc(struct queue* q) {
  for (int i = 0; i < MAX_QUEUE_SIZE; ++i) {
    if (q->free_desc[i]) {
      q->free_desc[i] = 0;  // 设为被占用
      return i;
    }
  }
  return -1;
}

static void free_desc(struct queue* q, int idx) {
  q->desc_ring[idx].addr = 0;
  q->desc_ring[idx].len = 0;
  q->desc_ring[idx].flags = 0;
  q->desc_ring[idx].next = 0;
  q->free_desc[idx] = 1;  // 设为空闲
}

static void free_desc_chain(struct queue* q, int idx) {
  while (1) {
    int flag = q->desc_ring[idx].flags;
    int next = q->desc_ring[idx].next;
    free_desc(q, idx);
    if (flag & VRING_DESC_F_NEXT) {
      idx = next;
    } else {
      break;
    }
  }
}

// 初始化gpu结构体
static void init_gpu_struct(void) {
  gpu.controlq.last_used_idx = 0;
  gpu.cursorq.last_used_idx = 0;
  for (int i = 0; i < MAX_QUEUE_SIZE; ++i) {
    gpu.controlq.free_desc[i] = 1;
    gpu.cursorq.free_desc[i] = 1;
  }
}

void drivers_virtio_gpu_init(void) {
  // 初始化gpu结构体先
  init_gpu_struct();

  // 检查magic和version
  uint32 magic = MMIO(VIRTIO_MMIO_MAGIC_VALUE);
  uint32 version = MMIO(VIRTIO_MMIO_VERSION);
  if (0x74726976 != magic || 2 != version) {
    panic("could not find virtio-gpu-device");
  }

  // 检查deviceid
  uint32 deviceid = MMIO(VIRTIO_MMIO_DEVICE_ID);
  if (0 == deviceid) {
    panic("failed to initialize virtio-gpu-device");
  }

  // 初始化设备：reset
  MMIO(VIRTIO_MMIO_STATUS) = 0;
  while (MMIO(VIRTIO_MMIO_STATUS) != 0);

  // 初始化设备：设置features
  MMIO(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  MMIO(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_DRIVER;
  // uint32 device_features = MMIO(VIRTIO_MMIO_DEVICE_FEATURES);
  // MMIO(VIRTIO_MMIO_DRIVER_FEATURES) = device_features;
  MMIO(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_FEATURES_OK;
  if (!(VIRTIO_CONFIG_S_FEATURES_OK & MMIO(VIRTIO_MMIO_STATUS))) {
    panic("unsupported feature(s)");
  }

  // 初始化controlq
  MMIO(VIRTIO_MMIO_QUEUE_SEL) = 0;
  uint32 max_queue_size_controlq = MMIO(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (0 == max_queue_size_controlq) {
    panic("controlq's size=0");
  }
  if (MAX_QUEUE_SIZE > max_queue_size_controlq) {
    panic("controlq's size is too small");
  }
  MMIO(VIRTIO_MMIO_QUEUE_NUM) = MAX_QUEUE_SIZE;
  gpu.controlq.desc_ring = kalloc();
  gpu.controlq.driver_ring = kalloc();
  gpu.controlq.device_ring = kalloc();
  if (!gpu.controlq.desc_ring || !gpu.controlq.driver_ring ||
      !gpu.controlq.device_ring) {
    panic("kalloc for controlq");
  }
  // printf("max controlq size=%d\n", max_queue_size);
  memset((void*)gpu.controlq.desc_ring, 0, PGSIZE);
  memset((void*)gpu.controlq.driver_ring, 0, PGSIZE);
  memset((void*)gpu.controlq.device_ring, 0, PGSIZE);
  MMIO(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)gpu.controlq.desc_ring;
  MMIO(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)gpu.controlq.desc_ring >> 32;
  MMIO(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)gpu.controlq.driver_ring;
  MMIO(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)gpu.controlq.driver_ring >> 32;
  MMIO(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)gpu.controlq.device_ring;
  MMIO(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)gpu.controlq.device_ring >> 32;
  MMIO(VIRTIO_MMIO_QUEUE_READY) = 1;

  // 初始化cursorq
  MMIO(VIRTIO_MMIO_QUEUE_SEL) = 1;
  uint32 max_queue_size_cursorq = MMIO(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (0 == max_queue_size_cursorq) {
    panic("cursorq's size=0");
  }
  if (MAX_QUEUE_SIZE > max_queue_size_cursorq) {
    panic("cursorq's size is too small");
  }
  // printf("max cursorq size=%d\n", max_queue_size_cursorq);
  MMIO(VIRTIO_MMIO_QUEUE_NUM) = MAX_QUEUE_SIZE;
  gpu.cursorq.desc_ring = kalloc();
  gpu.cursorq.driver_ring = kalloc();
  gpu.cursorq.device_ring = kalloc();
  if (!gpu.cursorq.desc_ring || !gpu.cursorq.driver_ring ||
      !gpu.cursorq.device_ring) {
    panic("kalloc for cursorq");
  }
  memset((void*)gpu.cursorq.desc_ring, 0, PGSIZE);
  memset((void*)gpu.cursorq.driver_ring, 0, PGSIZE);
  memset((void*)gpu.cursorq.device_ring, 0, PGSIZE);
  MMIO(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)gpu.cursorq.desc_ring;
  MMIO(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)gpu.cursorq.desc_ring >> 32;
  MMIO(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)gpu.cursorq.driver_ring;
  MMIO(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)gpu.cursorq.driver_ring >> 32;
  MMIO(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)gpu.cursorq.device_ring;
  MMIO(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)gpu.cursorq.device_ring >> 32;
  MMIO(VIRTIO_MMIO_QUEUE_READY) = 1;

  // 初始化设备：标志设备可用
  MMIO(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_DRIVER_OK;

  // 获取virtio-gpu显示信息
  int head_idx = alloc_desc(&gpu.controlq);    // 第一个空闲描述符索引
  int second_idx = alloc_desc(&gpu.controlq);  // 第二个索引
  printf("head=%d,second=%d\n", head_idx, second_idx);
  struct virtio_gpu_ctrl_hdr cmd_hdr = {0};
  cmd_hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  cmd_hdr.flags = VIRTIO_GPU_FLAG_FENCE;
  gpu.controlq.desc_ring[head_idx].addr = (uint64)&cmd_hdr;
  gpu.controlq.desc_ring[head_idx].len = sizeof(cmd_hdr);
  gpu.controlq.desc_ring[head_idx].flags =
      0 | VRING_DESC_F_NEXT;                           // 0 = 设备只读
  gpu.controlq.desc_ring[head_idx].next = second_idx;  // 指向下一个描述符

  struct virtio_gpu_resp_display_info resp_buf = {0};
  gpu.controlq.desc_ring[second_idx].addr = (uint64)&resp_buf;
  gpu.controlq.desc_ring[second_idx].len = sizeof(resp_buf);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_WRITE;  // 设备可写
  gpu.controlq.desc_ring[second_idx].next = 0;                    // 链结束

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  // panic("break");

  // 驱动在中断处理或轮询中检查 used->idx 是否变化
  while (1) {
    // printf("device->used=%d, last_used=%d\n",
    //        QUEUE(used, device_area_controlq)->idx, last_used_idx);
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % MAX_QUEUE_SIZE;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      if (e->id == head_idx &&
          resp_buf.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        // printf("resp_buf.hdr.type=0x%x\n", resp_buf.hdr.type);
        // 此时 resp_buf 已包含有效的显示信息
        for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
          uint32 ena = resp_buf.pmodes[i].enabled;
          uint32 x = resp_buf.pmodes[i].r.x;
          uint32 y = resp_buf.pmodes[i].r.y;
          uint32 width = resp_buf.pmodes[i].r.width;
          uint32 height = resp_buf.pmodes[i].r.height;
          printf("ena=%u, x=%u, y=%u, width=%u, height=%u\n", ena, x, y, width,
                 height);
        }
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      break;
    }
  }

  return;
}