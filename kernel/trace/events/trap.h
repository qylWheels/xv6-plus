#ifndef _TRACE_EVENTS_TRAP_H_
#define _TRACE_EVENTS_TRAP_H_

#include <trace/tracepoint.h>

// 跟踪进程缺页异常
TRACE_DEFINE(pgfault, TRACE_PROTO(), TRACE_ARGS());

#endif // _TRACE_EVENTS_TRAP_H_
