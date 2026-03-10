#ifndef _FS_BUF_H_
#define _FS_BUF_H_

#include <sync/sleeplock.h>
#include <fs/fs.h>

struct buf
{
  int valid; // has data been read from disk?
  int disk;  // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

#endif /* _FS_BUF_H_ */
