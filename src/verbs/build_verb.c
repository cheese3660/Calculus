/******************************************************************************
 *
 *  build_verb.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  Implements the `calculus build` command which builds either the requested
 *  set when given a `.lua` file, or builds a specific derivation recipe when
 *  given a bare file without the lua extension, if that derivation recipe is 
 *  not already in the calculus cookbook `/calc/recipes` then it will copy it 
 *  in there and index it into the state database beforehand, otherwise 
 * 
 *****************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static const char* optstring = "";

void usage()
{
    
    exit(EXIT_FAILURE);
}

int build_verb(int argc, const char** argv)
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
}