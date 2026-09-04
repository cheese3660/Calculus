#pragma once
#include <stdio.h>
#include <stdlib.h>

#define panic(fmt, ...) do { \
    fprintf(stderr, "PANIC [%s:%d]: " fmt "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__); \
    abort(); \
} while(0)

#define debug(fmt, ...) fprintf(stderr, "DEBUG [%s:%d]: " fmt "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__);