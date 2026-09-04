#pragma once
#include <stdio.h>
#include <stdlib.h>

#define panic(fmt, ...)                             \
    do                                              \
    {                                               \
        fprintf(stderr, "PANIC [%s:%d]: " fmt "\n", \
                __FILE__, __LINE__, ##__VA_ARGS__); \
        abort();                                    \
    } while (0)

#define DEBUG_MSGS

#ifdef DEBUG_MSGS
#define debug(fmt, ...) fprintf(stderr, "DEBUG [%s:%d]: " fmt "\n", \
                                __FILE__, __LINE__, ##__VA_ARGS__);
#else
#define debug(fmt, ...)
#endif