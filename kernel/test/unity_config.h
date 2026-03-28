#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// 禁用标准库
#define UNITY_EXCLUDE_STDINT_H
#define UNITY_EXCLUDE_LIMITS_H
#define UNITY_EXCLUDE_STDDEF_H
#define UNITY_EXCLUDE_SETJMP_H

// 禁用浮点数
#define UNITY_EXCLUDE_FLOAT
#define UNITY_EXCLUDE_DOUBLE

// 适配 xv6-riscv 64位架构
#define UNITY_SUPPORT_64
#define UNITY_INT_WIDTH 32
#define UNITY_LONG_WIDTH 64
#define UNITY_POINTER_WIDTH 64

// 输出重定向到 xv6 串口
extern void consputc(int);
#define UNITY_OUTPUT_CHAR(c) consputc(c)

// 定义NULL，unity会用到
#ifndef NULL
#define NULL ((void*)0)
#endif  // NULL

#endif  // UNITY_CONFIG_H
