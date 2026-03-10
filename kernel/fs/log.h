#ifndef _FS_LOG_H_
#define _FS_LOG_H_

#include <fs/buf.h>

void initlog(int, struct superblock *);
void log_write(struct buf *);
void begin_op(void);
void end_op(void);

#endif // _FS_LOG_H_