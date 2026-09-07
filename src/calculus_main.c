/*
    Entry point for `calculus` the main part of the package manager

    Separated from some other programs that rely on other entry points (mainly car)
*/

#include <lua.h>
#include <stdio.h>
#include "common/crypto.h"
#include "calculus/luaenv.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "calculus/derivative.h"
#include "calculus/build.h"

#define SHA256(X)                                            \
    do                                                       \
    {                                                        \
        sha256_t x = sha256_string(X);                       \
        printf("SHA256('" X "') = %s\n", sha256_to_hex(&x)); \
    } while (0)

int main(int argc, const char** argv)
{
    // printf("My euid is %d\n", geteuid());
    // uid_t euid = geteuid();
    // if (euid != 0) {
    //     fprintf(stderr, "The calculus package manager must be run as root.\n");
    //     exit(EXIT_FAILURE);
    // }

    if (argc != 2)
    {
        fprintf(stderr, "Incorrect argument count\n");
        exit(EXIT_FAILURE);
    }

    env_setup();
    env_run(argv[1]);
    env_teardown();

    size_t len;
    derivative_header_t** derivs = get_requested_derivatives(&len);

    printf("Requested %ld derivatives\n", len);
    for (size_t i = 0; i < len; i++)
    {
        printf("- %s\n", get_derivative_store_path(derivs[i]));
    }

    derivative_header_t** stack = get_buildstack(derivs, len);
    printf("Integrating %ld derivatives to achieve goal\n", buildstack_len(stack));
    size_t i2 = 0;
    for (derivative_header_t* deriv = buildstack_next(stack); deriv != nullptr; deriv = buildstack_next(stack)) 
    {
        printf("%ld - %s\n", i2++, get_derivative_store_path(deriv));
        build_derivative(deriv);
    }

    buildstack_free(stack);
}