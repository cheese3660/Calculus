
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>

#include "common/fs.h"
#include "common/command.h"



bool fs_exists(const char* path)
{
    struct stat buf;
    return stat(path, &buf) == 0;
}

bool fs_isdir(const char* path)
{
    struct stat buf;
    if (stat(path, &buf) == -1)
        return false;
    return S_ISDIR(buf.st_mode);
}

bool fs_isreg(const char* path)
{
    struct stat buf;
    if (stat(path, &buf) == -1)
        return false;
    return S_ISREG(buf.st_mode);
}

int fs_ensure_dir(const char* path)
{
    if (!fs_isdir(path)) {
        // Some day we will not do this
        const char* command[] = {"mkdir", "-p", path, nullptr};
        return command_run(command);
    }
    return 0;
}