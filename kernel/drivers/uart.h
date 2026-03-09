#ifndef _DRIVERS_UART_H_
#define _DRIVERS_UART_H_

void uartinit(void);
void uartintr(void);
void uartwrite(char[], int);
void uartputc_sync(int);
int uartgetc(void);

#endif // _DRIVERS_UART_H_
