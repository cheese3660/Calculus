#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include <ftw.h>

#include "common/fs.h"
#include "common/command.h"
#include "common/debug.h"



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

static int recursive_delete(const char *fpath, const struct stat *sb, int type_flag, struct FTW *ftwbuf)
{
    (void)sb;
    (void)type_flag;
    (void)ftwbuf;
    int status = remove(fpath);

    if (status == -1)
        perror(fpath);

    return status;
}

int fs_rmdir(const char *path)
{
    static char check[4096];
    const char* real = realpath(path, check);
    if (strcmp(real,"/") == 0)
    {
        panic("attempting to remove root!");
    }
    return nftw(path, recursive_delete, 64, FTW_DEPTH | FTW_PHYS);
}