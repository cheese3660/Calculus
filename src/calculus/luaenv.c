#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <thirdparty/stb_ds.h>

#include "calculus/luaenv.h"

#define LUA_UNREACHABLE luaL_error(L, "%s:%d should be unreachable!", __FILE__, __LINE__)
#define LUA_PERROR(what) luaL_error(L, "[%s,%d] %s: %s", __FILE__, __LINE__, what, strerror(errno));

static char pathbuf1[PATH_MAX];
static char pathbuf2[PATH_MAX];
/*
 * This is the core primitive that allows for scripts to be run next to eachother
 * The return on this is temporary
 */
static char *caller_source(lua_State *L)
{
    lua_Debug ar;
    // Get the callers stack frame
    if (lua_getstack(L, 1, &ar))
    {
        // And attempt to get the source
        lua_getinfo(L, "S", &ar);
        const char *caller_path = ar.source;
        if (caller_path[0] == '@')
        {
            caller_path++;
        }
        else
        {
            LUA_UNREACHABLE;
        }
        char *result = realpath(caller_path, pathbuf1);
        if (result == nullptr)
            LUA_PERROR("realpath");
        return result;
    }
    else
    {
        LUA_UNREACHABLE;
    }
}

static char *caller_directory(lua_State *L)
{
    char *path = caller_source(L);
    int len = strlen(path);

    // Quickly just convert the character after the last `/` to a `.`, so that stuff
    // can be easily concatenated
    for (int i = len; i > 0; i--)
    {
        if (path[i - 1] == '/')
        {
            path[i] = 0;
            break;
        }
    }
    return path;
}

static char *caller_relative(lua_State *L, const char *relative)
{
    if (relative[0] == '/')
    {
        char *result = realpath(relative, pathbuf1);
        if (result == nullptr)
            LUA_PERROR("realpath");
        return result;
    }

    char *dir = caller_directory(L);

    size_t dir_len = strlen(dir);
    size_t rel_len = strlen(relative);

    if (dir_len + rel_len >= PATH_MAX)
    {
        luaL_error(L, "relative path is too long");
    }

    memcpy(dir + dir_len, relative, rel_len + 1 /* copy the null byte as well */);
    // We want to go to pathbuf2 with this
    char *result = realpath(dir, dir == pathbuf1 ? pathbuf2 : pathbuf1);
    if (result == nullptr)
        LUA_PERROR("realpath");
    return result;
}

// path must always be in a 4kb block, otherwise this is an issue, hence why this is static
static char *get_module_path(lua_State *L, char *path)
{
    // 11 characters we want
    size_t path_len = strlen(path);
    // Just confirm that we can actually do this
    if (path_len + 12 >= PATH_MAX)
    {
        luaL_error(L, "module path is too long");
    }
    // Idk how this happens but easy to work around
    if (path[path_len - 1] != '/')
    {
        path[path_len] = '/';
        path_len++;
    }
    strcpy(path + path_len, "module.lua");
    char *result = realpath(path, path == pathbuf1 ? pathbuf2 : pathbuf1);
    if (result == nullptr)
        LUA_PERROR("realpath");
    return result;
}

/*
 * We need a way to cache absolute paths
 */

struct
{
    char *key;
    int value;
} *import_cache;

static int import(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "import(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "import(path): path must be a string, got a %s", luaL_typename(L, 1));
    char *path = caller_relative(L, relative);

    // Now we stat the path to decide what to do with it, as if its a directory we want to instead run `module.lua` inside the directory
    struct stat stat_buf;
    if (stat(path, &stat_buf) == -1)
        LUA_PERROR("stat");

    mode_t mode = stat_buf.st_mode;
    if (S_ISDIR(mode))
    {
        // Let us concatenate "/"
        path = get_module_path(L, path);
        if (stat(path, &stat_buf) == -1)
        {
            if (errno == ENOENT)
            {
                luaL_error(L, "import(path): path is directoy without module.lua file");
            }
            else
            {
                LUA_PERROR("stat");
            }
        }
        mode = stat_buf.st_mode;
    }

    if (!S_ISREG(mode))
    {
        luaL_error(L, "import(path): path is not a regular file or directory with module.lua file");
    }

    if (import_cache == nullptr)
    {
        sh_new_arena(import_cache);
    }

    int index = shgeti(import_cache, path);
    int reg_ref;
    if (index == -1)
    {
        if (luaL_loadfile(L, path) != LUA_OK)
        {
            luaL_error(L, "Failed to load script %s:\n\t%s", path, lua_tostring(L, -1));
        }

        int before_top = lua_gettop(L) - 1;

        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            luaL_error(L, "Runtime error in running script %s:\t%s", path, lua_tostring(L, -1));
        }

        int num_results = lua_gettop(L) - before_top;

        if (num_results == 0)
        {
            reg_ref = LUA_REFNIL;
        }
        else
        {
            lua_settop(L, before_top + 1);
            reg_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        shput(import_cache, path, reg_ref);
    }
    else
    {
        reg_ref = import_cache[index].value;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref);
    return 1;
}