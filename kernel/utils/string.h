#ifndef _UTILS_STRING_H_
#define _UTILS_STRING_H_

#include <core/types.h>

int memcmp(const void *, const void *, uint);
void *memmove(void *, const void *, uint);
void *memset(void *, int, uint);
char *safestrcpy(char *, const char *, int);
int strlen(const char *);
int strncmp(const char *, const char *, uint);
char *strncpy(char *, const char *, int);

#endif // _UTILS_STRING_H_
