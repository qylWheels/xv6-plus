#ifndef _UTILS_PRINTF_H_
#define _UTILS_PRINTF_H_

int printf(char *, ...) __attribute__((format(printf, 1, 2)));
void panic(char *) __attribute__((noreturn));
void printfinit(void);

#endif // _UTILS_PRINTF_H_