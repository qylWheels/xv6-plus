// On-disk file system format.
// Both the kernel and user programs use this header file.

#ifndef _UAPI_FS_FS_H_
#define _UAPI_FS_FS_H_

#include <uapi/core/types.h>

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

// The name field may have DIRSIZ characters and not end in a NUL
// character.
struct dirent
{
  ushort inum;
  char name[DIRSIZ] __attribute__((nonstring));
};

#endif /* _UAPI_FS_FS_H_ */
