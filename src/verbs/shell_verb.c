/******************************************************************************
 *
 *  shell_verb.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  shell_verb.c implements the `calculus shell` verb for the calculus package 
 *  manager, which runs a shell with the requested derivative binaries in PATH
 *
 *  It finds the shell by searching each of the current systems derivatives
 *  (listed in profiles/system-N) `/bin` folders and looks for `bash`, and
 *  executes that if found, and if not found, it will search for `sh` then
 *  execute that. If none are found, then it will error out and print a message
 *  to stderr
 * 
 *****************************************************************************/
