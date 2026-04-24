#include <core/riscv.h>
#include <core/types.h>
#include <drivers/virtio.h>
#include <drivers/virtio_gpu.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
#include <mm/memlayout.h>
#include <sync/spinlock.h>
#include <utils/printf.h>
#include <utils/string.h>
#include <utils/uthash.h>

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

#define MMIO(offset) (*(volatile uint32*)(VIRTIO1 + (offset)))

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

struct virtio_gpu_resp_display_info {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

enum virtio_gpu_formats {
  VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
  VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
  VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
  VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,
  VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
  VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,
  VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
  VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};

struct virtio_gpu_resource_create_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 resource_id;
  uint32 format;
  uint32 width;
  uint32 height;
};

struct virtio_gpu_resource_attach_backing {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 resource_id;
  uint32 nr_entries;  // 后续 mem_entry 的数量
};

struct virtio_gpu_mem_entry {
  uint64 addr;    // guest 物理地址
  uint32 length;  // 长度（字节）
  uint32 padding;
};

struct virtio_gpu_transfer_to_host_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;  // 矩形区域
  uint64 offset;             // 资源内偏移（字节）
  uint32 resource_id;
  uint32 padding;
};

struct virtio_gpu_set_scanout {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;  // 显示区域（在 scanout 中的位置和大小）
  uint32 scanout_id;         // 0 到 num_scanouts-1
  uint32 resource_id;
};

struct virtio_gpu_resource_flush {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;  // 需要刷新的区域
  uint32 resource_id;
  uint32 padding;
};

struct queue {
  char free_desc[NUM];              // 1为空闲，0为正在被使用
  struct virtq_desc* desc_ring;     // desc[]
  struct virtq_avail* driver_ring;  // 只需一个，其中已经包含整个环形队列了
  struct virtq_used* device_ring;   // 依旧只需一个，其中包含整个环形队列
  uint32 last_used_idx;             // 最后一个处理完毕的device_ring的下标
};

struct backing_store_item {
  uint32 idx;  // key
  uint32* mem;
  uint64 memsize;
  UT_hash_handle hh;
};

struct gpu {
  uint32 res_id;  // 资源编号
  struct queue controlq;
  struct queue cursorq;
  struct spinlock vgpu_lock;
  struct backing_store_item* backing_store_hashtable;
  struct virtio_gpu_rect r;  // 可显示的区域
} gpu;

// -1代表desc[]中的desc都正在被占用
static int alloc_desc(struct queue* q) {
  for (int i = 0; i < NUM; ++i) {
    if (q->free_desc[i]) {
      q->free_desc[i] = 0;  // 设为被占用
      return i;
    }
  }
  panic("alloc_desc()");
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
  initlock(&gpu.vgpu_lock, "virtio-gpu-lock");
  gpu.res_id = 1;  // XXX: gpu不认0，要从1开始
  for (int i = 0; i < NUM; ++i) {
    gpu.controlq.free_desc[i] = 1;
    gpu.cursorq.free_desc[i] = 1;
  }
  gpu.controlq.last_used_idx = 0;
  gpu.cursorq.last_used_idx = 0;
  gpu.backing_store_hashtable = NULL;
  memset(&gpu.r, 0, sizeof(gpu.r));
}

// 目前只支持一块屏幕
static void drivers_virtio_gpu_get_display_info(void) {
  acquire(&gpu.vgpu_lock);

  int head_idx = alloc_desc(&gpu.controlq);    // 第一个空闲描述符索引
  int second_idx = alloc_desc(&gpu.controlq);  // 第二个索引
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

  // 驱动在中断处理或轮询中检查 used->idx 是否变化
  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      // printf("resp_buf.hdr.type=0x%x\n", resp_buf.hdr.type);
      if (e->id == head_idx &&
          resp_buf.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        gpu.r.x = resp_buf.pmodes[0].r.x;
        gpu.r.y = resp_buf.pmodes[0].r.y;
        gpu.r.width = resp_buf.pmodes[0].r.width;
        gpu.r.height = resp_buf.pmodes[0].r.height;
        // printf("get display info successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

// 创建2d资源
static void drivers_virtio_gpu_create_2d_resource(void) {
  acquire(&gpu.vgpu_lock);

  // 构造请求头
  struct virtio_gpu_ctrl_hdr hdr = {0};
  hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  hdr.flags = VIRTIO_GPU_FLAG_FENCE;  // 可设置 VIRTIO_GPU_FLAG_FENCE 以同步等待

  // 构造请求体
  struct virtio_gpu_resource_create_2d req = {
      .hdr = hdr,
      .resource_id = 1,  // 现在只有一个资源
      .format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
      .width = gpu.r.width,
      .height = gpu.r.height,
  };

  // 构造响应结构体
  struct virtio_gpu_ctrl_hdr resp = {0};

  int head_idx = alloc_desc(&gpu.controlq);
  int second_idx = alloc_desc(&gpu.controlq);

  gpu.controlq.desc_ring[head_idx].addr = (uint64)&req;
  gpu.controlq.desc_ring[head_idx].len = sizeof(req);
  gpu.controlq.desc_ring[head_idx].flags =
      0 | VRING_DESC_F_NEXT;  // 0 = 设备只读
  gpu.controlq.desc_ring[head_idx].next = second_idx;

  gpu.controlq.desc_ring[second_idx].addr = (uint64)&resp;
  gpu.controlq.desc_ring[second_idx].len = sizeof(resp);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_WRITE;
  gpu.controlq.desc_ring[second_idx].next = 0;

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      if (e->id == head_idx && resp.type == VIRTIO_GPU_RESP_OK_NODATA) {
        // printf("create 2D resouce successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

// 分配并将“显存”绑定到2d资源上
// TODO: 权宜之计，扩展kmalloc可分配内存的上限才是正道
#define LIST_SIZE (1280 * 800 * 4 / PGSIZE + 10)  // +10防越界
struct virtio_gpu_mem_entry* entry_list[LIST_SIZE] = {0};
struct virtq_desc indirect_descs[LIST_SIZE] = {0};
static void drivers_virtio_gpu_attach_backing(void) {
  acquire(&gpu.vgpu_lock);

  uint32 width = gpu.r.width;
  uint32 height = gpu.r.height;
  uint32 nr_pixels = width * height;
  uint32 nr_pages = nr_pixels * sizeof(uint32) / PGSIZE + 1;
  uint32 nr_entries = nr_pages;

  // 请求头
  struct virtio_gpu_ctrl_hdr hdr = {
      .type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
      .flags = VIRTIO_GPU_FLAG_FENCE,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0},
  };

  // 请求体
  struct virtio_gpu_resource_attach_backing req = {
      .hdr = hdr,
      // 目前只有一个res_id，为1，就用它
      .resource_id = 1,
      .nr_entries = nr_entries,
  };

  // 内存条目
  for (uint32 i = 0; i < nr_pages; ++i) {
    void* mem = kalloc();
    memset(mem, 0, PGSIZE);

    struct virtio_gpu_mem_entry* entry = kmalloc(sizeof(*entry));
    entry->addr = (uint64)mem;
    entry->length = PGSIZE;
    entry->padding = 0;
    entry_list[i] = entry;

    struct backing_store_item* item = kmalloc(sizeof(*item));
    item->idx = i;
    item->mem = mem;
    item->memsize = PGSIZE;
    HASH_ADD_INT(gpu.backing_store_hashtable, idx, item);
  }

  // 响应体
  struct virtio_gpu_ctrl_hdr resp = {0};

  int head_idx = alloc_desc(&gpu.controlq);
  int second_idx = alloc_desc(&gpu.controlq);  // 存放indirect descriptors
  int last_idx = alloc_desc(&gpu.controlq);

  gpu.controlq.desc_ring[head_idx].addr = (uint64)&req;
  gpu.controlq.desc_ring[head_idx].len = sizeof(req);
  gpu.controlq.desc_ring[head_idx].flags = 0 | VRING_DESC_F_NEXT;
  gpu.controlq.desc_ring[head_idx].next = second_idx;

  for (int i = 0; i < nr_entries; ++i) {
    indirect_descs[i].addr = (uint64)entry_list[i];
    indirect_descs[i].len = sizeof(*entry_list[i]);
    if (i < nr_entries - 1) {
      indirect_descs[i].flags = VRING_DESC_F_NEXT;
      indirect_descs[i].next = i + 1;
    } else {
      indirect_descs[i].flags = 0;
      indirect_descs[i].next = 0;
    }
  }

  // 存indirect descriptors
  gpu.controlq.desc_ring[second_idx].addr = (uint64)indirect_descs;
  gpu.controlq.desc_ring[second_idx].len = sizeof(indirect_descs);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_NEXT;
  gpu.controlq.desc_ring[second_idx].next = last_idx;

  gpu.controlq.desc_ring[last_idx].addr = (uint64)&resp;
  gpu.controlq.desc_ring[last_idx].len = sizeof(resp);
  gpu.controlq.desc_ring[last_idx].flags = 0 | VRING_DESC_F_WRITE;
  gpu.controlq.desc_ring[last_idx].next = 0;

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  // printf("ok\n");
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      // printf("e->id=%d, type=0x%x\n", e->id, resp.type);
      if (e->id == head_idx && resp.type == VIRTIO_GPU_RESP_OK_NODATA) {
        // printf("attach backing memory successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

// 设置要输出到的“屏幕”
static void drivers_virtio_gpu_set_scanout(void) {
  acquire(&gpu.vgpu_lock);

  struct virtio_gpu_ctrl_hdr hdr = {
      .type = VIRTIO_GPU_CMD_SET_SCANOUT,
      .flags = VIRTIO_GPU_FLAG_FENCE,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0},
  };

  struct virtio_gpu_set_scanout req = {
      .hdr = hdr,
      .r =
          {
              .x = gpu.r.x,
              .y = gpu.r.y,
              .width = gpu.r.width,
              .height = gpu.r.height,
          },
      .scanout_id = 0,  // 使用第一个 scanout
      .resource_id = 1,
  };

  struct virtio_gpu_ctrl_hdr resp = {0};

  int head_idx = alloc_desc(&gpu.controlq);
  int second_idx = alloc_desc(&gpu.controlq);

  gpu.controlq.desc_ring[head_idx].addr = (uint64)&req;
  gpu.controlq.desc_ring[head_idx].len = sizeof(req);
  gpu.controlq.desc_ring[head_idx].flags = VRING_DESC_F_NEXT;
  gpu.controlq.desc_ring[head_idx].next = second_idx;

  gpu.controlq.desc_ring[second_idx].addr = (uint64)&resp;
  gpu.controlq.desc_ring[second_idx].len = sizeof(resp);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_WRITE;
  gpu.controlq.desc_ring[second_idx].next = 0;

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      // printf("e->id=%d, type=0x%x\n", e->id, resp.type);
      if (e->id == head_idx && resp.type == VIRTIO_GPU_RESP_OK_NODATA) {
        // printf("set scanout successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

// 初始化gpu
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
  uint32 device_features = MMIO(VIRTIO_MMIO_DEVICE_FEATURES);
  if (device_features & (1 << VIRTIO_RING_F_INDIRECT_DESC)) {
    // printf("indirect desc supported\n");
  } else {
    // printf("indirect desc NOT supported\n");
  }
  MMIO(VIRTIO_MMIO_DRIVER_FEATURES) |=
      VIRTIO_RING_F_INDIRECT_DESC;  // 使用间接描述符
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
  if (NUM > max_queue_size_controlq) {
    panic("controlq's size is too small");
  }
  MMIO(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  gpu.controlq.desc_ring = kalloc();
  gpu.controlq.driver_ring = kalloc();
  gpu.controlq.device_ring = kalloc();
  if (!gpu.controlq.desc_ring || !gpu.controlq.driver_ring ||
      !gpu.controlq.device_ring) {
    panic("kalloc for controlq");
  }
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
  if (NUM > max_queue_size_cursorq) {
    panic("cursorq's size is too small");
  }
  MMIO(VIRTIO_MMIO_QUEUE_NUM) = NUM;
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

  // 其他初始化
  drivers_virtio_gpu_get_display_info();
  drivers_virtio_gpu_create_2d_resource();
  drivers_virtio_gpu_attach_backing();
  drivers_virtio_gpu_set_scanout();

  return;
}

// 将数据传送给宿主机
static void drivers_virtio_gpu_transfer_to_host_2d_screen(int x, int y) {
  acquire(&gpu.vgpu_lock);

  // 构造传输请求
  struct virtio_gpu_ctrl_hdr hdr = {
      .type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
      .flags = VIRTIO_GPU_FLAG_FENCE,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0},
  };

  // 只传输一个像素
  // FIXME:
  // 目前似乎做不到等用户flush时再将整个屏幕的像素传递给宿主，只能一个个传
  struct virtio_gpu_transfer_to_host_2d req = {
      .hdr = hdr,
      .r =
          {
              .x = x,
              .y = y,
              .width = 1,
              .height = 1,
          },
      .offset = 0,
      .resource_id = 1,
      .padding = 0,
  };

  struct virtio_gpu_ctrl_hdr resp = {0};

  int head_idx = alloc_desc(&gpu.controlq);
  int second_idx = alloc_desc(&gpu.controlq);

  gpu.controlq.desc_ring[head_idx].addr = (uint64)&req;
  gpu.controlq.desc_ring[head_idx].len = sizeof(req);
  gpu.controlq.desc_ring[head_idx].flags = VRING_DESC_F_NEXT;
  gpu.controlq.desc_ring[head_idx].next = second_idx;

  gpu.controlq.desc_ring[second_idx].addr = (uint64)&resp;
  gpu.controlq.desc_ring[second_idx].len = sizeof(resp);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_WRITE;
  gpu.controlq.desc_ring[second_idx].next = 0;

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      // printf("e->id=%d, type=0x%x\n", e->id, resp.type);
      if (e->id == head_idx && resp.type == VIRTIO_GPU_RESP_OK_NODATA) {
        // printf("transfer to host 2D successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

// 刷新屏幕
static void drivers_virtio_gpu_flush_screen() {
  acquire(&gpu.vgpu_lock);

  // printf("[flush] driver_ring.idx=%d\n", gpu.controlq.driver_ring->idx);

  struct virtio_gpu_ctrl_hdr hdr = {
      .type = VIRTIO_GPU_CMD_RESOURCE_FLUSH,
      .flags = VIRTIO_GPU_FLAG_FENCE,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0},
  };

  // 刷新整个屏幕
  struct virtio_gpu_resource_flush req = {
      .hdr = hdr,
      .r =
          {
              .x = gpu.r.x,
              .y = gpu.r.y,
              .width = gpu.r.width,
              .height = gpu.r.height,
          },
      .resource_id = 1,
      .padding = 0,
  };

  struct virtio_gpu_ctrl_hdr resp = {0};

  int head_idx = alloc_desc(&gpu.controlq);
  int second_idx = alloc_desc(&gpu.controlq);

  gpu.controlq.desc_ring[head_idx].addr = (uint64)&req;
  gpu.controlq.desc_ring[head_idx].len = sizeof(req);
  gpu.controlq.desc_ring[head_idx].flags = VRING_DESC_F_NEXT;
  gpu.controlq.desc_ring[head_idx].next = second_idx;

  gpu.controlq.desc_ring[second_idx].addr = (uint64)&resp;
  gpu.controlq.desc_ring[second_idx].len = sizeof(resp);
  gpu.controlq.desc_ring[second_idx].flags = VRING_DESC_F_WRITE;
  gpu.controlq.desc_ring[second_idx].next = 0;

  uint16 avail_idx = gpu.controlq.driver_ring->idx;
  gpu.controlq.driver_ring->ring[avail_idx % NUM] =
      head_idx;  // 放入描述符链头索引

  __sync_synchronize();  // 确保 ring 写入完成再更新 idx【spec 2.7.13.3.1】
  // 原子递增，使描述符对设备可见，不需要对NUM取余，因为取下标的时候会取余
  gpu.controlq.driver_ring->idx++;
  gpu.controlq.last_used_idx = gpu.controlq.device_ring->idx;
  __sync_synchronize();  // 确保 idx 更新对设备可见【spec 2.7.13.4.1】
  MMIO(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (1) {
    __sync_synchronize();
    if (gpu.controlq.device_ring->idx != gpu.controlq.last_used_idx) {
      // 内存屏障：确保读取到最新的 used ring 内容【隐含在规范要求中】
      __sync_synchronize();
      uint16 used_pos = gpu.controlq.last_used_idx % NUM;
      struct virtq_used_elem* e = &gpu.controlq.device_ring->ring[used_pos];

      // printf("e->id=%d, type=0x%x\n", e->id, resp.type);
      if (e->id == head_idx && resp.type == VIRTIO_GPU_RESP_OK_NODATA) {
        // printf("flush successfully\n");
      }

      // 更新驱动本地指针，不用取余
      gpu.controlq.last_used_idx++;

      // 处理完毕，释放descs
      free_desc_chain(&gpu.controlq, head_idx);

      release(&gpu.vgpu_lock);

      break;
    }
  }
}

#define RGBA(r, g, b, a) (((r) << 24) | ((g) << 16) | ((b) << 8) | (a))
#define ERANGE 1

// 向指定位置打印一个像素
int drivers_virtio_gpu_draw_pixel(int x, int y, uint8 r, uint8 g, uint8 b,
                                  uint8 a) {
  if (x >= gpu.r.x + gpu.r.width || y >= gpu.r.y + gpu.r.height) {
    return -ERANGE;
  }
  uint32 offset_pixel = y * gpu.r.width + x;
  uint32 page_idx = offset_pixel * sizeof(uint32) / PGSIZE;
  uint32 page_offset_byte = offset_pixel * sizeof(uint32) - page_idx * PGSIZE;
  uint32 page_offset_pixel = page_offset_byte / sizeof(uint32);
  struct backing_store_item* item;
  HASH_FIND_INT(gpu.backing_store_hashtable, &page_idx, item);
  __sync_synchronize();
  *((volatile uint32*)(item->mem + page_offset_pixel)) = RGBA(r, g, b, a);
  __sync_synchronize();
  // FIXME: 目前必须每绘制一个像素就要传输一次，否则屏幕只会显示一条绿色的虚线
  drivers_virtio_gpu_transfer_to_host_2d_screen(x, y);
  return 0;
}

void drivers_virtio_gpu_flush(void) { drivers_virtio_gpu_flush_screen(); }
