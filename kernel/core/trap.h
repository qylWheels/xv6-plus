#ifndef _CORE_TRAP_H_
#define _CORE_TRAP_H_

#include <core/types.h>

extern uint ticks;
void trapinit(void);
void trapinithart(void);
extern struct spinlock tickslock;
void prepare_return(void);

#endif // _CORE_TRAP_H_
