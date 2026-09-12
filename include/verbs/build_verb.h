/******************************************************************************
 *
 *  build_verb.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 * 
 *  Provides the `calculus build` command interface, see `build_verb.c` for
 *  details about it's implementation
 *
 *****************************************************************************/

/// @brief The `build` verb for calculus
/// @param argc The argument count for the verb
/// @param argv The arguments of the verb, with argv[0] being the verb itself
/// @return The exit code from the verb
int build_verb(int argc, const char** argv);
