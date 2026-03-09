#ifndef _UAPI_FS_STAT_H_
#define _UAPI_FS_STAT_H_

#define T_DIR 1    // Directory
#define T_FILE 2   // File
#define T_DEVICE 3 // Device

struct stat
{
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};

#endif /* _UAPI_FS_STAT_H_ */
