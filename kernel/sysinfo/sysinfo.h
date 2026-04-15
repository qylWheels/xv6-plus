#ifndef _SYSINFO_SYSINFO_H_
#define _SYSINFO_SYSINFO_H_

#include <uapi/sysinfo/sysinfo.h>

int ksysinfo(const char* path, struct sysinfo* p);

#endif  // _SYSINFO_SYSINFO_H_
