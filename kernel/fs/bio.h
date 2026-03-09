#ifndef _FS_BIO_H_
#define _FS_BIO_H_

#include <core/types.h>

void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

#endif // _FS_BIO_H_
