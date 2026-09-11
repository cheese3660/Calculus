/******************************************************************************
 *
 *  command.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/command.h"
int command_run(const char **args)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        // TODO: add in a reusable PANIC_PERROR macro
        perror("fork");
        return -1;
    }
    else if (pid == 0)
    {
        // We are the child process now
        execvp(args[0], (char *const *)args);
        perror("execvp");
        // We should never get to this point as normally we'd be *replaced* by execvp
        exit(EXIT_FAILURE);
    }
    else
    {
        // pid is the pid of the child process
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        return -1;
    }
}