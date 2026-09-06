#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <thirdparty/stb_ds.h>
// Used for the postorder traversal of ftw
#include <ftw.h>

#include "calculus/luaenv.h"
#include "calculus/paths.h"
#include "common/crypto.h"
#include "common/command.h"
#include "common/debug.h"

#define LUA_UNREACHABLE luaL_error(L, "%s:%d should be unreachable!", __FILE__, __LINE__), unreachable()
#define LUA_PERROR(what) luaL_error(L, "[%s:%d] %s: %s", __FILE__, __LINE__, what, strerror(errno));
#define LUA_PERRORN(what, n) luaL_error(L, "[%s:%d] %s: %s", __FILE__, __LINE__, what, strerror(n));

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
    LUA_UNREACHABLE;
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

static char *concat_path(lua_State *L, char *path, char *cat)
{

    // 11 characters we want
    size_t path_len = strlen(path);
    // Just confirm that we can actually do this
    if (path_len + strnlen(cat, PATH_MAX) + 1 >= PATH_MAX)
    {
        luaL_error(L, "module path is too long");
    }
    // Idk how this happens but easy to work around
    if (path[path_len - 1] != '/')
    {
        path[path_len] = '/';
        path_len++;
    }
    strcpy(path + path_len, cat);

    // We don't need to realpath() this as it's already been realpath()'d at least once

    // char *result = realpath(path, path == pathbuf1 ? pathbuf2 : pathbuf1);

    // if (result == nullptr)
    //     LUA_PERROR("realpath");
    return path;
}

// path must always be in a 4kb block, otherwise this is an issue, hence why this is static
static char *get_module_path(lua_State *L, char *path)
{
    return concat_path(L, path, "module.lua");
}

/*
 * We need a way to cache absolute paths
 */

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

        // stack [..., block]
        int before_top = lua_gettop(L) - 1;

        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            luaL_error(L, "Runtime error in running script %s:\t%s", path, lua_tostring(L, -1));
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
        shput(import_cache, path, reg_ref);
    }
    else
    {
        // Already cached
        reg_ref = import_cache[index].value;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref);
    // stack [..., import]
    return 1;
}

// Now we need to do the global library stuff
// As this involves folders with `_pre.lua` and `_post.lua` in them

static struct
{
    char *key;
    // This is meant to be a hashset not a hashmap
    char value;
} *library_set;

// A basic stack of lua files to be run after package resolution
// this is all the _post files that libraries have
static char **teardown_stack;

static char *get_pre_path(lua_State *L, char *path)
{
    return concat_path(L, path, "_pre.lua");
}

static char *get_post_path(lua_State *L, char *path)
{
    return concat_path(L, path, "_post.lua");
}

/* This is the common library load path*/
static void load_library(lua_State *L, char *path)
{
    // Initialize the set
    if (library_set == nullptr)
        sh_new_arena(library_set);

    // Check if this one has been loaded already
    if (shgeti(library_set, path) == -1)
        return;

    // Make sure it can't be loaded again
    shput(library_set, path, 0);


    // Now we stat the prepath
    static char pathclone[PATH_MAX];
                          /* DST! */
    char *clone = strncpy(pathclone, path, PATH_MAX - 1);
    if (clone == nullptr)
    {
        luaL_error(L, "unable to clone library path");
    }

    char *pre = get_pre_path(L, path);


    struct stat stat_buf;
    if (stat(pre, &stat_buf) == -1)
        LUA_PERROR("stat");

    if (!S_ISREG(stat_buf.st_mode))
        luaL_error(L, "library _pre.lua is not a file!");

    if (luaL_loadfile(L, pre) != LUA_OK)
        luaL_error(L, "Failed to load library _pre.lua:\n\t%s", lua_tostring(L, -1));

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        luaL_error(L, "Error running library _pre.lua:\n\t%s", lua_tostring(L, -1));

    char *post = get_post_path(L, pathclone);
    if (stat(post, &stat_buf) == -1)
        // We don't care at this point if it fails
        return;

    if (!S_ISREG(stat_buf.st_mode))
        luaL_error(L, "library _post.lua is not a file!");

    // We push the value to the teardown stack now
    arrpush(teardown_stack, strdup(post));
}

static char *get_gitpath(lua_State *L, sha256_t *input_sha)
{
    static char gitpath[PATH_MAX];
    char *real = realpath(GIT_CACHE_DIRECTORY, gitpath);
    if (real == nullptr)
        LUA_PERROR("realpath");

    size_t len = strlen(real);
    if (len + 65 >= PATH_MAX)
    {
        panic("Git cache path %s is too long to even make a directory in", GIT_CACHE_DIRECTORY);
    }
    if (strcmp(real, "/") == 0)
        panic("Git cache path resolves to root");
    // real always strips off the last '/'
    real[len++] = '/';
    const char *hex = sha256_to_hex(input_sha);
    strcpy(real + len, hex);
    return real;
}

// Returns true if a directory already exists, errors if its not a directory
static bool exists_dir(lua_State *L, const char *directory)
{
    struct stat stat_buf;
    if (stat(directory, &stat_buf) == -1)
    {
        if (errno == ENOENT)
            return false;
        LUA_PERROR("stat");
    }
    if (!S_ISDIR(stat_buf.st_mode))
        luaL_error(L, "error fetching git: %s already exists and is not a directory");
    return true;
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

static void rollback(const char *path)
{
    if (nftw(path, recursive_delete, 64 /* open fds */, FTW_DEPTH | FTW_PHYS) != 0)
    {
        panic("Failed to rollback failed git directory, state may be incorrect!");
    }
}

// Returns false if the direcotory needs to be setup, and automatically runs the initial git commands
static bool git_setup_directory(lua_State *L, const char *remote, const char *path)
{
    if (exists_dir(L, path))
        return true;

    mode_t old_umask = umask(0);
    int status = mkdir(path, 0777);
    umask(old_umask);
    if (status == -1)
        LUA_PERROR("mkdir");

    const char *init_command[] = {"git", "-C", path, "init", nullptr};
    if (command_run(init_command) != 0)
    {
        rollback(path);
        luaL_error(L, "Failed to initialize git repository when importing library");
    }
    const char *remote_command[] = {"git", "-C", path, "remote", "add", "origin", remote, nullptr};
    if (command_run(remote_command) != 0)
    {
        rollback(path);
        luaL_error(L, "Failed to add git remote when importing library");
    }
    return false;
}

static void git_finalize_directory(lua_State *L, const char *path)
{

    const char *checkout_command[] = {"git"
                                      "-C",
                                      path, "checkout", "FETCH_HEAD", nullptr};
    if (command_run(checkout_command) != 0)
    {
        rollback(path);
        luaL_error(L, "Failed to checkout fetch head when importing library");
    }
    // No submodules yet, nor LFS
}

/* Resolve a git directory, cloning and caching it*/
static char *git_commit(lua_State *L, const char *remote, const char *sha)
{
    sha256_ingest_t ingest = {};
    sha256_appends(&ingest, remote);
    sha256_appends(&ingest, "@<sha>(");
    sha256_appends(&ingest, sha);
    sha256_appends(&ingest, ")");
    sha256_t cache_tag = sha256_finalize(&ingest);
    char *path = get_gitpath(L, &cache_tag);
    if (git_setup_directory(L, remote, path))
        return path;

    const char *fetch_command[] = {"git", "-C", path, "fetch", "--depth", "1", "origin", sha, nullptr};
    if (command_run(fetch_command) != 0)
    {
        rollback(path);
        luaL_error(L, "Failed to fetch sha when importing library");
    }
    git_finalize_directory(L, path);
    return path;
}

static char *git_tag(lua_State *L, const char *remote, const char *tag)
{
    sha256_ingest_t ingest = {};
    sha256_appends(&ingest, remote);
    sha256_appends(&ingest, "@<tag>(");
    sha256_appends(&ingest, tag);
    sha256_appends(&ingest, ")");
    sha256_t cache_tag = sha256_finalize(&ingest);
    char *path = get_gitpath(L, &cache_tag);
    if (git_setup_directory(L, remote, path))
        return path;
    char refspec[512];
    snprintf(refspec, sizeof(refspec), "refs/tags/%s:refs/tags/%s", tag, tag);

    const char *fetch_command[] = {"git", "-C", path, "fetch", "--depth", "1", "origin", refspec, nullptr};
    if (command_run(fetch_command) != 0)
    {
        rollback(path);
        luaL_error(L, "Failed to fetch tag when importing library");
    }
    git_finalize_directory(L, path);
    return path;
}

/* Resolve a local folder */
static char *local_folder(lua_State *L, const char *folder)
{
    return caller_relative(L, folder);
}

static int table_get(lua_State *L, int index, const char *name)
{
    // Stack going in
    // [..., t, ...] where t is at an index
    // after pushing the name
    // [..., t, ..., name] where t is still at an index (name is at -1)
    lua_pushstring(L, name);       // Push the key
    return lua_gettable(L, index); // get the value
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

    char *module_path;
    if (table_get(L, 1, "path") == LUA_TSTRING)
    {
        // stack [spec, ..., spec.path]
        module_path = local_folder(L, lua_tostring(L, -1));
    }
    else if (table_get(L, 1, "url") == LUA_TSTRING)
    {
        // stack [spec, ..., spec.url]
        const char *git_url = lua_tostring(L, -1);
        if (table_get(L, 1, "sha") == LUA_TSTRING)
        {
            module_path = git_commit(L, git_url, lua_tostring(L, -1));
        }
        else if (table_get(L, 1, "tag") == LUA_TSTRING)
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

// readfile specifically reads a caller relative file as a string
// This is because we lock down filesystem access in the lua scripts
static int readfile(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc != 1)
        luaL_error(L, "readfile(path): expected 1 argument, got %d", argc);
    const char *relative = lua_tostring(L, 1);
    if (relative == nullptr)
        luaL_error(L, "readfile(path): path must be a string, got a %s", luaL_typename(L, 1));
    char *path = caller_relative(L, relative);

    struct stat stat_buf;
    if (stat(path, &stat_buf) == -1)
        LUA_PERROR("stat");

    if (!S_ISREG(stat_buf.st_mode))
        luaL_error(L, "readfile(path): path must point to a regular file");

    char *buffer = malloc(stat_buf.st_size);
    if (buffer == nullptr)
        LUA_PERROR("malloc");

    FILE *f = fopen(path, "r");
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

// At some point we may want a readjson function, but that'll be another library

static lua_State *state;

static struct
{
    const char *name;
    lua_CFunction function;
} initial_env[] = {
    {"import", import},
    {"use", use_library},
    {"readfile", readfile}};

void env_setup()
{
    if (state != nullptr)
    {
        panic("environmnet has already been setup!!!");
    }
    state = luaL_newstate();
    // Maybe think about extending the sandbox
    luaL_openselectedlibs(state, LUA_GLIBK | LUA_STRLIBK | LUA_UTF8LIBK | LUA_MATHLIBK, 0);
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

    // So now wes et up our own functions
    for (size_t i = 0; i < sizeof(initial_env) / sizeof(initial_env[0]); i++)
    {
        lua_pushcfunction(state, initial_env[i].function);
        lua_setglobal(state, initial_env[i].name);
    }
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