/******************************************************************************
 *
 *  luaenv.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  This contains the implementation of the calculus lua environment, all the
 *  functions that it uses, and how it resolves paths
 *
 *****************************************************************************/

#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <ftw.h>
#include <sys/types.h>
#include <dirent.h>
#include <thirdparty/stb_ds.h>

#include "calculus/luaenv.h"
#include "calculus/paths.h"
#include "calculus/gittools.h"
#include "calculus/derivative.h"

#include "common/crypto.h"
#include "common/command.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/string.h"

#define LUA_UNREACHABLE luaL_error(L, "%s:%d should be unreachable!", __FILE__, __LINE__), unreachable()
#define LUA_PERROR(what) luaL_error(L, "[%s:%d] %s: %s", __FILE__, __LINE__, what, strerror(errno));
#define LUA_PERRORN(what, n) luaL_error(L, "[%s:%d] %s: %s", __FILE__, __LINE__, what, strerror(n));

// This is for s_taken calls for one time calls
static char takebuf[PATH_MAX];

/******************************************************************************
 *
 * INTERNAL UTILITIES
 *
 * dir_cleanup() - cleans up the trailing / from a directory name and puts it
 *                 into a buffer
 * caller_source() - gets the filename of the lua script caller
 * caller_directory() - gets the directory the lua script caller is in
 * caller_relative() - gets the path of a relative file to the caller
 *                     or returns the realpath of the given absolute path
 * concat_path() - concatenates 2 paths together, assuming they have PATH_MAX
 *                 space
 *
 *****************************************************************************/

// Modifies the string passed in directly
static void dir_cleanup(lua_State *L, string_t *path)
{
    size_t len = strlen(path);
    if (len >= PATH_MAX)
        luaL_error(L, "path too long");

    if (len == 1)
    {
        s_setl(path, "/.", strlen("/."));
    }
    else
    {
        s_trimr(path, "/");
    }
}

// Returns a string from the string pool, duplicate it if it needs to be kept
static string_t *caller_source(lua_State *L)
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

        // THIS SHOULD BE OUR SOLE REALPATH CALL AS A SOURCE OF TRUTH

        string_t *s = s_new_p(PATH_MAX);
        char *result = realpath(caller_path, s->cstring);
        if (result == nullptr)
        {
            s_free(s);
            LUA_PERROR("realpath");
        }
        s->length = strlen(result);

        return result;
    }
    LUA_UNREACHABLE;
}

// Still on the string pool here, so temporary-ish
static string_t *caller_directory(lua_State *L)
{
    string_t *path = caller_source(L);
    for (uint32_t i = path->length; i > 0; i--)
    {
        if (path->cstring[i - 1] == '/')
        {
            path->cstring[i] = 0;
            path->length = i;
            break;
        }
    }
    dir_cleanup(L, path);
    return path;
}

// Still on the string pool here
static string_t *caller_relative(lua_State *L, const char *relative)
{
    if (relative[0] == '/')
    {
        string_t *res = s_own_p(relative);
        dir_cleanup(L, res);
        return res;
    }

    string_t *dir = caller_directory(L);

    size_t rel_len = strlen(relative);

    if (dir->length + rel_len >= PATH_MAX)
        luaL_error(L, "relative path is too long");

    s_cat(dir, relative);
    return dir;
}

static void concat_path(lua_State *L, string_t *path, char *cat)
{
    // Just confirm that we can actually do this
    if (path->length + strlen(cat) + 1 >= PATH_MAX)
    {
        luaL_error(L, "module path is too long");
    }
    // Idk how this happens but easy to work around
    if (!s_endswith(path, "/"))
    {
        s_cat(path, "/");
    }

    s_cat(path, cat);

    return path;
}

// path must always be in a 4kb block, otherwise this is an issue, hence why this is static
static void get_module_path(lua_State *L, string_t *path)
{
    concat_path(L, path, "module.lua");
}

/******************************************************************************
 *
 * RELATIVE IMPORT SYSTEM
 *
 * import(path) - Imports a caller relative lua module and returns it's single
 *                result. Cache is keyed on absolute path
 *
 *****************************************************************************/

static struct
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
    string_t *r_path = caller_relative(L, relative);
    // Duplicate it because this function is recursive and we don't want to blow the string pool
    string_t path = s_copy_a(r_path);
    // And release r_path back to the string pool
    s_free(r_path);

    // Now we stat the path to decide what to do with it, as if its a directory we want to instead run `module.lua` inside the directory
    struct stat stat_buf;
    if (stat(path.cstring, &stat_buf) == -1)
    {
        s_free(&path);
        LUA_PERROR("stat");
    }

    mode_t mode = stat_buf.st_mode;
    if (S_ISDIR(mode))
    {
        // path = get_module_path(L, path);
        get_module_path(L, &path);
        if (stat(path.cstring, &stat_buf) == -1)
        {
            // Free is guaranteed to preserve errno and s_free should go to free, see man 3 free:
            // The free() function returns no value, and preserves errno.
            s_free(&path);
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
        s_free(&path);
        luaL_error(L, "import(path): path is not a regular file or directory with module.lua file");
    }

    if (import_cache == nullptr)
        sh_new_arena(import_cache);

    int index = shgeti(import_cache, path.cstring);
    int reg_ref;
    if (index == -1)
    {
        if (luaL_loadfile(L, path.cstring) != LUA_OK)
        {
            luaL_error(L, "Failed to load script %s:\n\t%s", s_taken(&path, takebuf, PATH_MAX), lua_tostring(L, -1));
        }

        // stack [..., block]
        int before_top = lua_gettop(L) - 1;
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            luaL_error(L, "Runtime error in running script %s:\t%s", s_taken(&path, takebuf, PATH_MAX), lua_tostring(L, -1));
        }

        // stack [retval1, retval2, etc...]
        int num_results = lua_gettop(L) - before_top;

        if (num_results == 0)
        {
            reg_ref = LUA_REFNIL;
        }
        else
        {
            lua_settop(L, before_top + 1);
            // stack [retval1]
            reg_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }

        shput(import_cache, path.cstring, reg_ref);
        s_free(&path);
    }
    else
    {
        // Already cached
        reg_ref = import_cache[index].value;
        s_free(&path);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref);
    // stack [..., import]
    return 1;
}

/******************************************************************************
 *
 * GLOBAL LIBRARY SYSTEM
 *
 * use(spec) - either fetches, or finds a given module spec, and runs its
 *             _pre.lua, and adds it's _post.lua (if it exists) to the teardown
 *             stack
 *             modules are cached based off of their final path
 *
 *****************************************************************************/

static struct
{
    char *key;
    // This is meant to be a hashset not a hashmap
    int value;
} *library_set = nullptr;

static char **teardown_stack;

static void get_pre_path(lua_State *L, string_t *path)
{
    concat_path(L, path, "_pre.lua");
}

static void get_post_path(lua_State *L, string_t *path)
{
    concat_path(L, path, "_post.lua");
}

/* This is the common library load path */
/* Path is a heap allocated string that this method modifies and frees */
static void load_library(lua_State *L, string_t path)
{
    if (library_set == nullptr)
        sh_new_arena(library_set);

    // Check if this one has been loaded already
    if (shgeti(library_set, path.cstring) != -1)
        return;

    // Make sure it can't be loaded again
    shput(library_set, path.cstring, 0);

    string_t *pre = s_copy_p(&path);
    get_pre_path(L, pre);

    struct stat stat_buf;
    if (stat(pre->cstring, &stat_buf) == -1)
    {
        s_free(pre);
        s_free(&path);
        LUA_PERROR("stat");
    }

    if (!S_ISREG(stat_buf.st_mode))
    {
        s_free(pre);
        s_free(&path);
        luaL_error(L, "library _pre.lua is not a file!");
    }

    if (luaL_loadfile(L, pre->cstring) != LUA_OK)
    {
        s_free(pre);
        s_free(&path);
        luaL_error(L, "Failed to load library _pre.lua:\n\t%s", lua_tostring(L, -1));
    }

    s_free(pre);

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        luaL_error(L, "Error running library _pre.lua:\n\t%s", lua_tostring(L, -1));

    // Post is now in the original path path
    get_post_path(L, &path);
    if (stat(path.cstring, &stat_buf) == -1)
    {
        s_free(&path);
        return;
    }

    if (!S_ISREG(stat_buf.st_mode))
    {
        s_free(&path);
        luaL_error(L, "library _post.lua is not a file!");
    }

    // We push the value to the teardown stack now
    arrpush(teardown_stack, s_take(&path));
}

static string_t get_gitpath(lua_State *L, sha256_t *input_sha)
{
    // static char gitpath[PATH_MAX];
    string_t result = s_new_a(PATH_MAX);

    // Let's first stat the git cache directory
    struct stat stat_buf;
    if (stat(GIT_CACHE_DIRECTORY, &stat_buf) == -1)
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            // Let's shell out to mkdir for this one so we don't have to handle the recursion ourselves
            // maybe look at this later to not require coreutils, but we'll see
            const char *mkdir_command[] = {"mkdir", "-p", GIT_CACHE_DIRECTORY, nullptr};
            if (command_run(mkdir_command) == -1)
                luaL_error(L, "failed to create git cache directory");
        }
        else
            LUA_PERROR("stat");
    }

    char *real = realpath(GIT_CACHE_DIRECTORY, result.cstring);
    if (real == nullptr)
        LUA_PERROR("realpath");
    result.length = strlen(real);
    if (result.length + 65 >= PATH_MAX)
    {
        panic("Git cache path %s is too long to even make a directory in", GIT_CACHE_DIRECTORY);
    }

    if (strcmp(real, "/") == 0)
        panic("Git cache path resolves to root");

    const char *hex = sha256_to_hex(input_sha);
    s_catfn(&result, "/%s", hex);

    return result;
}

// Returns true if a directory already exists, errors if its not a directory
static bool exists_dir(lua_State *L, const char *directory)
{
    if (!fs_exists(directory))
        return false;
    if (!fs_isdir(directory))
        luaL_error(L, "error fetching git: %s already exists and is not a directory");
    return true;
}

// Returns false if the directory needs to be setup, and automatically runs the initial git commands
static bool git_setup_directory(lua_State *L, const char *path)
{
    if (exists_dir(L, path))
        return true;

    int status = mkdir(path, 0777);
    if (status == -1)
        LUA_PERROR("mkdir");

    return false;
}

/* Resolve a git directory, cloning and caching it*/
static string_t git_commit(lua_State *L, const char *remote, const char *sha)
{
    sha256_ingest_t ingest = {};
    sha256_appends(&ingest, remote);
    sha256_appends(&ingest, "@<sha>(");
    sha256_appends(&ingest, sha);
    sha256_appends(&ingest, ")");
    sha256_t cache_tag = sha256_finalize(&ingest);
    string_t path = get_gitpath(L, &cache_tag);
    if (git_setup_directory(L, path.cstring))
        return path;

    if (fetch_sha(path.cstring, remote, sha))
    {
        s_free(&path);
        fs_rmdir(path.cstring);
        luaL_error(L, "failed to fetch git library: %s", stored_git_error);
    }
    return path;
}

static string_t git_tag(lua_State *L, const char *remote, const char *tag)
{
    sha256_ingest_t ingest = {};
    sha256_appends(&ingest, remote);
    sha256_appends(&ingest, "@<tag>(");
    sha256_appends(&ingest, tag);
    sha256_appends(&ingest, ")");
    sha256_t cache_tag = sha256_finalize(&ingest);
    string_t path = get_gitpath(L, &cache_tag);
    if (git_setup_directory(L, path.cstring))
        return path;

    if (fetch_tag(path.cstring, remote, tag))
    {
        s_free(&path);
        fs_rmdir(path.cstring);
        luaL_error(L, "failed to fetch git library: %s", stored_git_error);
    }
    return path;
}

/* Resolve a local folder */
static string_t local_folder(lua_State *L, const char *folder)
{
    string_t *rel = caller_relative(L, folder);
    string_t copy = s_copy_a(rel);
    return copy;
}

/* the use_library function is meant to be for fetching a library*/
// The way this is used from lua is with a table
// {
//    url = "<git_url>",
//    sha = "<sha>",
//    tag = "<tag>",
//    path = "<path>"
// }
//
// 1 of either url or path has to be set, and if url is set, then 1 of either tag or ref has to be set
static int use_library(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "use(spec): expected 1 argument, got %d", argc);

    if (!lua_istable(L, 1))
        luaL_error(L, "use(spec): spec must be a table, got a %s", luaL_typename(L, 1));

    string_t module_path;
    if (lua_getfield(L, 1, "path") == LUA_TSTRING)
    {
        // stack [spec, ..., spec.path]
        module_path = local_folder(L, lua_tostring(L, -1));
    }
    else if (lua_getfield(L, 1, "url") == LUA_TSTRING)
    {
        // stack [spec, ..., spec.url]
        const char *git_url = lua_tostring(L, -1);
        if (lua_getfield(L, 1, "sha") == LUA_TSTRING)
        {
            module_path = git_commit(L, git_url, lua_tostring(L, -1));
        }
        else if (lua_getfield(L, 1, "tag") == LUA_TSTRING)
        {
            module_path = git_tag(L, git_url, lua_tostring(L, -1));
        }
        else
        {
            luaL_error(L, "use(spec): if spec.url is set, then one of spec.sha or spec.tag must be a string");
            unreachable();
        }
    }
    else
    {
        luaL_error(L, "use(spec): one of spec.url or spec.path must be a string");
        unreachable();
    }

    // So now we need to just import the library
    load_library(L, module_path);

    // And we are *done*
    return 0;
}

/******************************************************************************
 *
 * FILESYSTEM UTILITIES
 *
 * relpath(path) - converts a caller relative path to an absolute path
 * exists(path) - returns a bool indicating if the path exists
 * isdir(path) - returns a bool indicating if the path is a directory
 * isfile(path) - returns a bool indicating if the path is a file
 * readfile(path) - reads an entire file at a path into a string
 * listdir(path) - returns all non-hidden files in a directory
 *
 *****************************************************************************/

static int relpath(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "relpath(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "relpath(path): path must be a string, got a %s", luaL_typename(L, 1));

    string_t *temp = caller_relative(L, relative);
    lua_pushstring(L, temp->cstring);
    s_free(temp);
    return 1;
}

static int exists(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "exists(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "exists(path): path must be a string, got a %s", luaL_typename(L, 1));

    string_t *temp = caller_relative(L, relative);
    lua_pushboolean(L, fs_exists(temp->cstring));
    s_free(temp);
    return 1;
}

static int isdir(lua_State *L)
{

    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "isdir(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "isdir(path): path must be a string, got a %s", luaL_typename(L, 1));

    string_t *temp = caller_relative(L, relative);
    struct stat buf;
    if (stat(temp->cstring, &buf) == -1)
    {
        s_free(temp);
        LUA_PERROR("stat");
    }
    s_free(temp);

    lua_pushboolean(L, S_ISDIR(buf.st_mode));
    return 1;
}

static int isfile(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "isfile(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "isfile(path): path must be a string, got a %s", luaL_typename(L, 1));

    string_t *temp = caller_relative(L, relative);
    struct stat buf;
    if (stat(temp->cstring, &buf) == -1)
    {
        s_free(temp);
        LUA_PERROR("stat");
    }
    s_free(temp);

    lua_pushboolean(L, S_ISREG(buf.st_mode));
    return 1;
}

static int readfile(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "readfile(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "readfile(path): path must be a string, got a %s", luaL_typename(L, 1));
    string_t *path = caller_relative(L, relative);

    struct stat stat_buf;
    if (stat(path->cstring, &stat_buf) == -1)
    {
        s_free(path);
        LUA_PERROR("stat");
    }
        
    if (!S_ISREG(stat_buf.st_mode))
    {
        s_free(path);
        luaL_error(L, "readfile(path): path must point to a regular file");
    }

    char *buffer = malloc(stat_buf.st_size);
    if (buffer == nullptr) {
        s_free(path);
        LUA_PERROR("malloc");
    }

    FILE *f = fopen(path, "r");
    s_free(path);
    if (f == nullptr)
        LUA_PERROR("fopen");

    size_t read = fread(buffer, 1, stat_buf.st_size, f);
    if ((ssize_t)read != stat_buf.st_size)
    {
        int n = errno;
        free(buffer);
        fclose(f);
        LUA_PERRORN("fread", n);
    }

    fclose(f);

    lua_pushlstring(L, buffer, stat_buf.st_size);
    free(buffer);
    return 1;
}

static int listdir(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "listdir(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "listdir(path): path must be a string, got a %s", luaL_typename(L, 1));

    string_t *rel = caller_relative(L, relative);

    struct stat buf;
    if (stat(rel->cstring, &buf) == -1)
    {
        s_free(rel);
        LUA_PERROR("stat");
    }

    if (!S_ISDIR(buf.st_mode))
    {
        s_free(rel);
        luaL_error(L, "listdir(path): path is not a directory");
    }

    DIR *directory = opendir(rel);
    s_free(rel);
    if (directory == nullptr)
        LUA_PERROR("opendir");

    int i = 1;
    lua_createtable(L, buf.st_size / 24 /* Absolute upper limit here */, 0);
    for (struct dirent *ent = readdir(directory); ent != nullptr; ent = readdir(directory))
    {
        if (ent->d_name[0] == '.')
            continue;
        lua_pushstring(L, ent->d_name);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

/******************************************************************************
 *
 * DERIVATIVE REGISTRATION
 *
 * check_derivative() - check if a value is derivative metadata
 * derivative_path() - derivative metamethod that becomes :path,
 *                     resolves the final store path of a derivative for the
 *                     build system
 * derivative_request() - request a derivative to be built this round, is in the derivative mt
 * push_derivative_userdata() - push a derivative as userdata with the proper metatable
 * setup_derivative_mt() - sets up the initial derivative metatable when one is
 *                       first needed
 * fetch_tarball(spec) - creates a tarball derivative recipe
 * derivative(spec) - creates a standard derivative recipe
 *
 *****************************************************************************/

#define DERIVATIVE_METATABLE "derivative"

static derivative_header_t *check_derivative(lua_State *L, int index)
{
    derivative_header_t **ud = (derivative_header_t **)luaL_checkudata(L, index, DERIVATIVE_METATABLE);
    return *ud;
}
static int derivative_path(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc == 0)
        luaL_error(L, "derivative:path() must be called using ':' not '.'");
    if (argc != 1)
        luaL_error(L, "derivative:path() expects no arguments");

    derivative_header_t *ud = check_derivative(L, 1);

    // Very easy now just to get the store path
    lua_pushstring(L, get_derivative_store_path(ud));
    return 1;
}

static int derivative_request(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc == 0)
        luaL_error(L, "derivative:path() must be called using ':' not '.'");
    if (argc != 1)
        luaL_error(L, "derivative:path() expects no arguments");

    derivative_header_t *ud = check_derivative(L, 1);

    request_derivative(ud);
    return 0;
}

static void push_derivative_userdata(lua_State *L, derivative_header_t *raw_ptr)
{
    derivative_header_t **ud = (derivative_header_t **)lua_newuserdata(L, sizeof(derivative_header_t *));
    *ud = raw_ptr;

    luaL_getmetatable(L, DERIVATIVE_METATABLE);
    lua_setmetatable(L, -2);
}

// We are going to use a shared metatable for now
static void setup_derivative_mt(lua_State *L)
{
    // stack after
    // -1: table
    luaL_newmetatable(L, DERIVATIVE_METATABLE);

    // stack after
    // -2: table
    // -1: derivative_path
    lua_pushcfunction(L, derivative_path);

    // stack after
    // -1: table
    lua_setfield(L, -2, "path");

    // stack after
    // -2: table
    // -1: derivative_request
    lua_pushcfunction(L, derivative_request);

    // stack after
    // -1: table
    lua_setfield(L, -2, "request");

    // stack after
    // -2: table
    // -1: table
    lua_pushvalue(L, -1);

    // stack after
    // -1: table (index = table)
    lua_setfield(L, -2, "__index");

    // stack after
    // -2: table
    // -1: luserdata
    lua_pushlightuserdata(L, nullptr);

    // stack after
    // -1: luserdata
    lua_setmetatable(L, -2);

    // stack after
    // empty
    lua_pop(L, 1);
}

static int fetch(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        return luaL_error(L, "fetch(spec): expected 1 argument, got %d", argc);

    if (!lua_istable(L, 1))
        return luaL_error(L, "fetch(spec): spec must be a table, got a %s", luaL_typename(L, 1));

    if (lua_getfield(L, 1, "url") != LUA_TSTRING)
        return luaL_error(L, "fetch(spec): spec must have a string field named url that determines which file to download");

    const char *url = lua_tostring(L, -1);

    if (lua_getfield(L, 1, "hash") != LUA_TSTRING)
        return luaL_error(L, "fetch(spec): spec must have a string field named hash to verify the files hash");

    const char *hash = lua_tostring(L, -1);

    bool extract = false;

    if (lua_getfield(L, 1, "extract") == LUA_TBOOLEAN)
    {
        extract = lua_toboolean(L, -1);
    }
    fetch_tarball_derivative_t *drv = create_fetch_tarball_derivative(url, hash, extract);

    if (drv == nullptr)
        luaL_error(L, "fetch failed to create derivative (likely due to the same file being told extract and not)");

    push_derivative_userdata(L, (derivative_header_t *)drv);

    return 1;
}

static int derivative(lua_State *L)
{

    int argc = lua_gettop(L);
    if (argc != 1)
        return luaL_error(L, "derivative(spec): expected 1 argument, got %d", argc);

    if (!lua_istable(L, 1))
        return luaL_error(L, "derivative(spec): spec must be a table, got a %s", luaL_typename(L, 1));

    if (lua_getfield(L, 1, "name") != LUA_TSTRING)
        return luaL_error(L, "derivative(spec): spec must have a string field named name that determines the name of the derivative");

    const char *name = lua_tostring(L, -1);

    if (lua_getfield(L, 1, "build") != LUA_TSTRING)
        return luaL_error(L, "derivative(spec): spec must have a string field named build that is the build script for the derivative");

    const char *build = lua_tostring(L, -1);

    if (lua_getfield(L, 1, "deps") != LUA_TTABLE)
        return luaL_error(L, "derivative(spec): spec must have a array field named deps that contains the dependencies for the derivative");

    int len = lua_rawlen(L, -1);

    if (len == 0)
        return luaL_error(L, "derivative(spec): a non-fixed derivative must have at least one dependency or it's build script won't be able to run! (%s)", name);

    derivative_header_t **deps = malloc(sizeof(derivative_header_t *) * len);

    for (int i = 1; i <= len; i++)
    {
        // if (lua_rawgeti(L, -1, i) != LUA_TUSERDATA)
        // {
        //     free(deps);
        //     return luaL_error(L, "derivative(spec): deps[%d] is not a derivative", i);
        // }
        lua_rawgeti(L, -1, i);
        deps[i - 1] = check_derivative(L, -1);

        // Pop the userdata pointer now that we've consumed it
        lua_pop(L, 1);
    }

    // Pop the table now that we've consumed it
    lua_pop(L, 1);

    push_derivative_userdata(L, (derivative_header_t *)create_standard_derivative(len, deps, name, build));
    return 1;
}

/******************************************************************************
 *
 * ENVIRONMENT SETUP
 *
 * env_setup() - sets up the global lua state
 * env_run(path) - runs a script in the global lua state from a fresh stack
 * env_teardown() - shuts down the global lua state
 *
 *****************************************************************************/

static lua_State *state;

static struct
{
    const char *name;
    lua_CFunction function;
} initial_env[] = {
    {"import", import},
    {"use", use_library},
    {"readfile", readfile},
    {"relpath", relpath},
    {"exists", exists},
    {"isdir", isdir},
    {"isfile", isfile},
    {"listdir", listdir},
    {"fetch", fetch},
    {"derivative", derivative}};

void env_setup()
{
    if (state != nullptr)
    {
        panic("environmnet has already been setup!!!");
    }
    state = luaL_newstate();
    // Maybe think about extending the sandbox
    luaL_openselectedlibs(state, LUA_GLIBK | LUA_STRLIBK | LUA_UTF8LIBK | LUA_MATHLIBK | LUA_TABLIBK | LUA_DBLIBK, 0);
    const char *blacklist[] = {
        "load",
        "loadfile",
        "dofile",
        "collectgarbage",

        nullptr};

    for (int i = 0; blacklist[i] != nullptr; i++)
    {
        lua_pushnil(state);
        lua_setglobal(state, blacklist[i]);
    }

    // So now we set up our own functions
    for (size_t i = 0; i < sizeof(initial_env) / sizeof(initial_env[0]); i++)
    {
        lua_pushcfunction(state, initial_env[i].function);
        lua_setglobal(state, initial_env[i].name);
    }

    // And metatables
    setup_derivative_mt(state);
}

int env_run(const char *path)
{
    // We don't want any prior teardown state coming here, if this somehow leaks there is a bigger issue
    (void)arrfree(teardown_stack);
    int result = 0;
    if (luaL_dofile(state, path) != LUA_OK)
    {
        const char *error = lua_tostring(state, -1);
        fprintf(stderr, "running %s failed with error: %s\n", path, error);
        result = -1;
    }
    lua_settop(state, 0);
    // Now let's run every single teardown path
    while (arrlen(teardown_stack) != 0)
    {
        char *top = arrlast(teardown_stack);
        // Pop it
        arrdel(teardown_stack, arrlen(teardown_stack) - 1);
        // Then run the teardown script
        if (luaL_dofile(state, top) != LUA_OK)
        {
            const char *error = lua_tostring(state, -1);
            fprintf(stderr, "running %s failed with error: %s\n", top, error);
            result = -1;
        }
        // Free the file
        free(top);
        // Then set the stack back to being empty
        lua_settop(state, 0);
    }
    return result;
}

void env_teardown()
{
    lua_close(state);
}