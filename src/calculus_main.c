/******************************************************************************
 *
 *  calculus_main.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

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
#include <sys/stat.h>
#include "thirdparty/stb_ds.h"
#include "common/debug.h"

#define SHA256(X)                                            \
    do                                                       \
    {                                                        \
        sha256_t x = sha256_string(X);                       \
        printf("SHA256('" X "') = %s\n", sha256_to_hex(&x)); \
    } while (0)

void mermaid(FILE* output, derivative_header_t** wanted, size_t len);

int main(int argc, const char** argv)
{
    // Let's make sure we have no umask issues
    umask(0);
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

    FILE* merm = fopen("mermaid.txt", "w+");
    mermaid(merm, derivs, len);
    fclose(merm);
    printf("Mermaid diagram of build graph is at mermaid.txt\n");

    derivative_header_t** stack = get_buildstack(derivs, len);
    printf("Integrating %ld derivatives to achieve goal\n", buildstack_len(stack));
    size_t i2 = 0;
    for (derivative_header_t* deriv = buildstack_next(stack); deriv != nullptr; deriv = buildstack_next(stack)) 
    {
        printf("%ld - %s\n", i2++, get_derivative_store_path(deriv));
        if (build_derivative(deriv))
            break;
    }

    buildstack_free(stack);
}


derivative_header_t** mermaid_visited;

void mermaid_step1(FILE* output, derivative_header_t* node)
{
    for (ssize_t i = 0; i < arrlen(mermaid_visited); i++)
    {
        if (mermaid_visited[i] == node)
            return;
    }
    arrpush(mermaid_visited, node);

    if (node->dtype == DT_STANDARD)
    {
        standard_derivative_t* n = (standard_derivative_t*)node;
        fprintf(output, "    node_%s[\"%s\"]\n", sha256_to_hex(&node->dhash), n->name);
        for (size_t i = 0; i < n->num_dependencies; i++)
        {
            mermaid_step1(output, n->dependencies[i]);
        }
    }
    else if (node->dtype == DT_FETCH_TARBALL)
    {
        fetch_tarball_derivative_t* n = (fetch_tarball_derivative_t*)node;
        fprintf(output, "    node_%s[\"%s\"]\n", sha256_to_hex(&node->dhash), n->url);
    }
    else
    {
        panic("Unknown node type");
    }
}

void mermaid_step2(FILE* output, derivative_header_t* node)
{
    for (ssize_t i = 0; i < arrlen(mermaid_visited); i++)
    {
        if (mermaid_visited[i] == node)
            return;
    }
    arrpush(mermaid_visited, node);

    if (node->dtype == DT_STANDARD)
    {
        standard_derivative_t* n = (standard_derivative_t*)node;
        char* hash = strdup(sha256_to_hex(&node->dhash));
        for (size_t i = 0; i < n->num_dependencies; i++)
        {
            fprintf(output, "    node_%s --> node_%s\n", sha256_to_hex(&n->dependencies[i]->dhash),hash);
            mermaid_step2(output, n->dependencies[i]);
        }
        free(hash);
    }
    else if (node->dtype == DT_FETCH_TARBALL)
    {
        // Nothing to do here
    }
    else
    {
        panic("Unknown node type");
    }
}

// Let's create a mermaid diagram of the build graph
void mermaid(FILE* output, derivative_header_t** wanted, size_t len)
{
    // We write the header out
    fprintf(output, "flowchart TB\n");
    for (size_t i = 0; i < len; i++)
    {
        mermaid_step1(output, wanted[i]);
    }
    arrfree(mermaid_visited);
    mermaid_visited = nullptr;
    for (size_t i = 0; i < len; i++)
    {
        mermaid_step2(output, wanted[i]);
    }
}