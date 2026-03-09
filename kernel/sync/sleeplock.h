#ifndef _SLEEPLOCK_H_
#define _SLEEPLOCK_H_

// Long-term locks for processes
struct sleeplock
{
  uint locked;        // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock

  // For debugging:
  char *name; // Name of lock.
  int pid;    // Process holding lock
};

void acquiresleep(struct sleeplock *);
void releasesleep(struct sleeplock *);
int holdingsleep(struct sleeplock *);
void initsleeplock(struct sleeplock *, char *);

#endif /* _SLEEPLOCK_H_ */
