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
char* strstr(const char* hs, const char* ne);
int strcmp(const char* s1, const char* s2);

#endif // _UTILS_STRING_H_
