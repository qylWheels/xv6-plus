#ifndef _TRACE_TRACEPOINT_H_
#define _TRACE_TRACEPOINT_H_

#include <utils/misc.h>
#include <utils/string.h>
#include <core/types.h>

// 探针的最大数量
#define MAX_PROBES 16

#define TRACE_PROTO(...) __VA_ARGS__

#define TRACE_ARGS(...) __VA_ARGS__

#define TRACE_DEFINE(event, proto, args)                                          \
    struct _##event##_probe                                                       \
    {                                                                             \
        void (*probe)(proto);                                                     \
        char used;                                                                \
    };                                                                            \
                                                                                  \
    static struct _##event##_probe _##event##_probes[MAX_PROBES] = {0};           \
                                                                                  \
    static inline void trace_##event(proto)                                       \
    {                                                                             \
        do                                                                        \
        {                                                                         \
            for (int i = 0; i < MAX_PROBES; ++i)                                  \
            {                                                                     \
                if (_##event##_probes[i].used)                                    \
                {                                                                 \
                    _##event##_probes[i].probe(args);                             \
                }                                                                 \
            }                                                                     \
        } while (0);                                                              \
    }                                                                             \
                                                                                  \
    static inline void reg_trace_##event##_probe(void (*probe)(proto))            \
    {                                                                             \
        for (int i = 0; i < MAX_PROBES; ++i)                                      \
        {                                                                         \
            if (!_##event##_probes[i].used)                                       \
            {                                                                     \
                _##event##_probes[i].probe = probe;                               \
                _##event##_probes[i].used = 1;                                    \
            }                                                                     \
        }                                                                         \
    }                                                                             \
                                                                                  \
    static inline void unreg_trace_##event##_probe(void (*probe)(proto))          \
    {                                                                             \
        for (int i = 0; i < MAX_PROBES; ++i)                                      \
        {                                                                         \
            if (_##event##_probes[i].used && _##event##_probes[i].probe == probe) \
            {                                                                     \
                _##event##_probes[i].used = 0;                                    \
            }                                                                     \
        }                                                                         \
    }

#endif // _TRACE_TRACEPOINT_H_
