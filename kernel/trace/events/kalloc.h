#ifndef _TRACE_EVENTS_KALLOC_H_
#define _TRACE_EVENTS_KALLOC_H_

#include <trace/tracepoint.h>

// 跟踪kalloc()的调用次数，无论成败
TRACE_DEFINE(kalloc, TRACE_PROTO(), TRACE_ARGS());

// 跟踪kfree()的调用次数，哪怕是freerange()调用的也算
TRACE_DEFINE(kfree, TRACE_PROTO(), TRACE_ARGS());

#endif // _TRACE_EVENTS_KALLOC_H_
