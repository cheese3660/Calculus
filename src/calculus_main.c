/*
    Entry point for `calculus` the main part of the package manager

    Separated from some other programs that rely on other entry points (mainly car)
*/

#include <lua.h>
#include <stdio.h>
#include "common/crypto.h"
#include "calculus/luaenv.h"
#include <stdlib.h>

#define SHA256(X)                                            \
    do                                                       \
    {                                                        \
        sha256_t x = sha256_string(X);                       \
        printf("SHA256('" X "') = %s\n", sha256_to_hex(&x)); \
    } while (0)

int main(int argc, const char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Incorrect argument count\n");
        exit(EXIT_FAILURE);
    }

    env_setup();
    env_run(argv[1]);
    env_teardown();
}