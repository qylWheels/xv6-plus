#ifndef _IPC_PIPE_H_
#define _IPC_PIPE_H_

#include <core/types.h>

int pipealloc(struct file **, struct file **);
void pipeclose(struct pipe *, int);
int piperead(struct pipe *, uint64, int);
int pipewrite(struct pipe *, uint64, int);

#endif // _IPC_PIPE_H_