/******************************************************************************
 *
 *  build_verb.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/16/2026
 *
 *  Implements the `calculus build` command which builds either the requested
 *  set when given a `.lua` file, or builds a specific derivation recipe when
 *  given a bare file without the lua extension, if that derivation recipe is
 *  not already in the calculus cookbook `/calc/recipes` then it will copy it
 *  in there and index it into the state database beforehand, otherwise
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include "calculus/derivative.h"
#include "calculus/luaenv.h"
#include "common/string.h"

static const char *optstring = "";

void usage()
{
    fprintf(stderr, "usage: calculus build [options...] <filenames...>\n\n");
    fprintf(stderr, "   filenames: The list of filenames to build, can be lua files or recipes\n"
                    "              will build the superset of all recipes requested from them\n");
    fprintf(stderr, "\nOPTIONS\n");
    exit(EXIT_FAILURE);
}

static const string_t postfix_lua = S(".lua");
static const string_t postfix_recipe = S(".recipe");

int build_verb(int argc, char **argv)
{
    char opt;
    while ((opt = getopt(argc, argv, optstring)) != -1)
    {
        // Parse arguments here once we add some, just putting this here
    }
    if (optind == argc)
    {
        usage();
    }

    // Now we need to get every single string from the the arguments
    env_setup();
    for (int arg = optind; arg < argc; arg++)
    {
        string_t view = s_view(argv[arg]);
        if (s_endswiths(&view, &postfix_lua))
        {
            env_run(view.cstring);
        }
        if (s_endswiths(&view, &postfix_recipe))
        {
            // We request a derivative here
        }
    }
    env_teardown();

    exit(EXIT_SUCCESS);
}
