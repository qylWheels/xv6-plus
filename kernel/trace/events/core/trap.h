#ifndef _TRACE_EVENTS_TRAP_H_
#define _TRACE_EVENTS_TRAP_H_

#include <trace/tracepoint.h>

// 跟踪进程缺页异常
TRACE_DEFINE(pgfault, TRACE_PROTO(), TRACE_ARGS());

// 跟踪进程主动让出
TRACE_DEFINE(volun_switch, TRACE_PROTO(), TRACE_ARGS());

// 跟踪进程被动抢占
TRACE_DEFINE(involun_switch, TRACE_PROTO(), TRACE_ARGS());

// 跟踪进程运行时长
TRACE_DEFINE(tick, TRACE_PROTO(), TRACE_ARGS());

#endif  // _TRACE_EVENTS_TRAP_H_
