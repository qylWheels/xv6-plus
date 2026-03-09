#ifndef _DRIVERS_PLIC_H_
#define _DRIVERS_PLIC_H_

void plicinit(void);
void plicinithart(void);
int plic_claim(void);
void plic_complete(int);

#endif // _DRIVERS_PLIC_H_