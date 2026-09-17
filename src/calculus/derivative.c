/******************************************************************************
 *
 *  derivative.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/16/2026
 *
 *****************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>

#include "thirdparty/stb_ds.h"

#include "calculus/derivative.h"
#include "calculus/paths.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/stream_aux.h"
#include "common/string.h"

static struct
{
    sha256_t key;
    derivative_header_t *value;
} *registered_derivatives = nullptr;

static derivative_header_t **requested_derivatives = nullptr;

#define CALC_MAGIC 0x434C4143

static void write_standard_derivative(stream_t stream, uint32_t num_dependencies, derivative_header_t **dependencies, const char *name, const char *build)
{
    if (sm_w32(stream, CALC_MAGIC) == STREAM_ERRORED)
        panic("Error writing derivative header to stream: %s", sm_error(stream));
    if (sm_w32(stream, DT_STANDARD) == STREAM_ERRORED)
        panic("Error writing derivative type to stream: %s", sm_error(stream));
    if (sm_wcstr(stream, name) == STREAM_ERRORED)
        panic("Error writing derivative name to stream: %s", sm_error(stream));

    if (sm_w32(stream, num_dependencies) == STREAM_ERRORED)
        panic("Error writing dependency count to stream: %s", sm_error(stream));

    for (uint32_t i = 0; i < num_dependencies; i++)
    {
        if (sm_wsha(stream, dependencies[i]->dhash) == STREAM_ERRORED)
            panic("Error writing dependency hash to stream: %s", sm_error(stream));
    }

    if (sm_wcstr(stream, build) == STREAM_ERRORED)
        panic("Error writing derivative build script to stream: %s", sm_error(stream));
}

standard_derivative_t *create_standard_derivative(
    uint32_t num_dependencies,
    derivative_header_t **dependencies,
    const char *name,
    const char *build)
{
    void *buffer = nullptr;
    size_t buffer_len = 0;
    stream_t memstream = sm_memwrite(&buffer, &buffer_len);
    write_standard_derivative(memstream, num_dependencies, dependencies, name, build);
    sha256_t hash = sha256_hash(buffer, buffer_len);
    sm_close(memstream);

    derivative_header_t *preexisting = hmget(registered_derivatives, hash);
    if (preexisting != nullptr)
    {
        // Sanity check
        if (preexisting->dtype != DT_STANDARD)
            panic("possible hash collision detected evaluating derivative for %s, hash %s", name, sha256_to_hex(&hash));
        free(dependencies);
        return (standard_derivative_t *)preexisting;
    }
    standard_derivative_t *result = malloc(sizeof(standard_derivative_t));
    result->dheader = (derivative_header_t){
        DT_STANDARD,
        hash};
    result->num_dependencies = num_dependencies;
    result->dependencies = dependencies;
    result->name = strdup(name);
    result->build = strdup(build);

    hmput(registered_derivatives, hash, (derivative_header_t *)result);
    return result;
}

static void write_fetch_derivative(stream_t stream, const char *url, sha256_t hash, bool extract)
{
    if (sm_w32(stream, CALC_MAGIC) == STREAM_ERRORED)
        panic("Error writing derivative header to stream: %s", sm_error(stream));
    if (sm_w32(stream, DT_FETCH) == STREAM_ERRORED)
        panic("Error writing derivative type to stream: %s", sm_error(stream));
    if (sm_wcstr(stream, url) == STREAM_ERRORED)
        panic("Error writing fetch file to stream: %s", sm_error(stream));
    if (sm_wsha(stream, hash) == STREAM_ERRORED)
        panic("Error writing fetch hash to stream: %s", sm_error(stream));
    if (sm_w8(stream, extract ? 0xFF : 0x00) == STREAM_ERRORED)
        panic("Error writing extract flag to stream: %s", sm_error(stream));
}

fetch_derivative_t *create_fetch_derivative(
    const char *url,
    const char *hash,
    bool extract)
{
    sha256_t file_sha = hex_to_sha256(hash);
    void *buffer = nullptr;
    size_t buffer_len = 0;
    stream_t memstream = sm_memwrite(&buffer, &buffer_len);
    write_fetch_derivative(memstream, url, file_sha, extract);
    sha256_t sha = sha256_hash(buffer, buffer_len);
    sm_close(memstream);

    derivative_header_t *preexisting = hmget(registered_derivatives, sha);

    if (preexisting != nullptr)
    {
        // Sanity check
        if (preexisting->dtype != DT_FETCH)
            panic("possible hash collision detected evaluating derivative for tarball %s, hash %s", url, hash);
        return (fetch_derivative_t *)preexisting;
    }

    fetch_derivative_t *result = malloc(sizeof(fetch_derivative_t));
    result->dheader = (derivative_header_t){
        DT_STANDARD,
        sha};
    result->url = strdup(url);
    result->filehash = file_sha;
    result->extract = extract;
    hmput(registered_derivatives, sha, (derivative_header_t *)result);
    return result;
}

void request_derivative(derivative_header_t *derivative)
{
    // Let's first check if this derivative has already been requested
    for (ssize_t i = 0; i < arrlen(requested_derivatives); i++)
    {
        if (requested_derivatives[i] == derivative)
            return;
    }
    arrpush(requested_derivatives, derivative);
}

derivative_header_t **get_requested_derivatives(size_t *len)
{
    *len = (size_t)arrlen(requested_derivatives);
    return requested_derivatives;
}

// We want to limit actually how long paths get
// So we do this
// /calc/str/<>-%n
// ^^^^^^^^^^
// And we strnprintf offset into the buffer so the path is always there
static char path_buf[256];
static size_t str_offset = 0;

static void setup_str()
{
    str_offset = strlen(CHROOT_STORE_DIRECTORY) + 1;
    memcpy(path_buf, CHROOT_STORE_DIRECTORY, str_offset);
    path_buf[str_offset - 1] = '/';
}

static void add_str(derivative_header_t *derivative)
{
    if (str_offset == 0)
        setup_str();
    switch (derivative->dtype)
    {
    case DT_STANDARD:
        snprintf(path_buf + str_offset, 256 - str_offset, "%s-%s", sha256_to_hex(&derivative->dhash), ((standard_derivative_t *)derivative)->name);
        break;
    case DT_FETCH:
        snprintf(path_buf + str_offset, 256 - str_offset, "%s", sha256_to_hex(&derivative->dhash));
        break;
    default:
        panic("unknown derivative type");
    }
}

const char *get_derivative_node_name(derivative_header_t *derivative)
{
    add_str(derivative);
    return path_buf + str_offset;
}

const char *get_derivative_store_path(derivative_header_t *derivative)
{
    add_str(derivative);
    return path_buf;
}

// Postorder dependency sort
// Instead of the accidental preorder one I had before
void resolve_dependencies(
    derivative_header_t *node,
    derivative_header_t ***out_queue,
    derivative_header_t ***visited)
{
    for (ssize_t i = 0; i < arrlen(*visited); i++)
    {
        if ((*visited)[i] == node)
            return;
    }
    arrpush(*visited, node);

    static char buffer[PATH_MAX];
    snprintf(buffer, PATH_MAX, "%s/%s", CALCULUS_STORE_DIRECTORY, get_derivative_node_name(node));
    if (fs_exists(buffer))
        return;

    if (node->dtype == DT_STANDARD)
    {
        standard_derivative_t *actual = (standard_derivative_t *)node;
        for (size_t i = 0; i < actual->num_dependencies; i++)
        {
            resolve_dependencies(actual->dependencies[i], out_queue, visited);
        }
    }

    arrpush(*out_queue, node);
}

derivative_header_t **get_buildstack(derivative_header_t **wanted, size_t len)
{

    derivative_header_t **queue = NULL;
    derivative_header_t **visited = NULL;
    for (size_t i = 0; i < len; i++)
    {
        resolve_dependencies(wanted[i], &queue, &visited);
    }
    arrfree(visited);

    return queue;
}

size_t buildstack_len(derivative_header_t **stack)
{
    return (size_t)arrlen(stack);
}

derivative_header_t *buildstack_next(derivative_header_t **stack)
{
    if (arrlen(stack) > 0)
    {
        // return arrpop(stack);
        derivative_header_t *result = stack[0];
        arrdel(stack, 0);
        return result;
    }
    else
    {
        return nullptr;
    }
}

void buildstack_free(derivative_header_t **stack)
{
    arrfree(stack);
}

#define checked_r(N, s, target)                                          \
    do                                                                   \
    {                                                                    \
        if (sm_r##N(s, target, &truncated) == STREAM_ERRORED)            \
        {                                                                \
            fprintf(stderr, "failed to read %s: %s", path, sm_error(s)); \
            goto err;                                                    \
        }                                                                \
        if (truncated)                                                   \
        {                                                                \
            fprintf(stderr, "unexpected eof in %s", path);               \
            goto err;                                                    \
        }                                                                \
    } while (0)

static string_t cookbook_path = S(CALCULUS_RECIPES_DIRECTORY "/");

static derivative_header_t *read_standard_derivative(const char *path, stream_t s)
{
    bool truncated;
    uint32_t n_dependencies;
    char *name = nullptr;
    char *build = nullptr;
    derivative_header_t **deps = nullptr;
    string_t pstr = s_new_a(PATH_MAX);
    pstr.length = strlen(realpath(path, pstr.cstring));
    int32_t last_slash = s_rfindc(&pstr, '/');
    if (last_slash < 0)
        unreachable(); // There should be no way realpath fails here at all
    s_sub(&pstr, 0, last_slash + 1 /* We want to keep in the last slash for easy cat'ing */);
    s_shrink(&pstr);

    checked_r(cstr, s, &name);

    checked_r(32, s, &n_dependencies);

    deps = calloc(n_dependencies, sizeof(derivative_header_t *));
    if (!deps)
        panic("error allocating dependency array: %s", strerror(errno));

    for (uint32_t i = 0; i < n_dependencies; i++)
    {
        sha256_t hash;
        checked_r(sha, s, &hash);
        ssize_t preexisting = hmgeti(registered_derivatives, hash);
        if (preexisting != -1)
        {
            deps[i] = registered_derivatives[preexisting].value;
            continue;
        }

        string_t test = s_cat_a(&pstr, sha256_to_hex(&hash));
        s_cat(&test, ".recipe");
        s_shrink(&test);

        if (fs_exists(test.cstring))
        {
            derivative_header_t *to_add = derivative_read_recipe(test.cstring);
            if (to_add == nullptr)
            {
                fprintf(stderr, "error in dependency for %s: %s", path, test.cstring);
                s_free(&test);
                goto err;
            }
            s_free(&test);
            continue;
        }
        if (s_cmps(&pstr, &cookbook_path) == 0)
        {
            s_free(&test);
            fprintf(stderr, "dependency not found for %s: %s", path, sha256_to_hex(&hash));
            goto err;
        }

        s_setl(&test, CALCULUS_RECIPES_DIRECTORY "/", sizeof(CALCULUS_RECIPES_DIRECTORY) /* no -1 because of the / */);
        s_cat(&test, sha256_to_hex(&hash));
        s_cat(&test, ".recipe");
        s_shrink(&test);

        if (!fs_exists(test.cstring))
        {
            s_free(&test);
            fprintf(stderr, "dependency not found for %s: %s", path, sha256_to_hex(&hash));
            goto err;
        }

        derivative_header_t *to_add = derivative_read_recipe(test.cstring);
        if (to_add == nullptr)
        {
            fprintf(stderr, "error in dependency for %s: %s", path, test.cstring);
            s_free(&test);
            goto err;
        }
        s_free(&test);
    }

    checked_r(cstr, s, &build);

    derivative_header_t *result = create_standard_derivative(n_dependencies, deps, name, build);
    // Now we validate the hash is in the filename

    s_free(&pstr);
    free(deps);
    free(name);
    free(build);
    return result;
err:
    s_free(&pstr);
    if (deps)
        free(deps);
    if (name)
        free(name);
    if (build)
        free(build);
    return nullptr;
}

static derivative_header_t *read_fetch_derivative(const char *path, stream_t s)
{
}

derivative_header_t *derivative_read_recipe(const char *path)
{
    uint32_t magic;
    bool truncated;
    uint32_t type;
    derivative_header_t *result = nullptr;
    stream_t s = nullptr;
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
    {
        fprintf(stderr, "failed to open %s: %s", path, strerror(errno));
        goto err;
    }
    // This now owns the file
    s = sm_file(f);

    checked_r(32, s, &magic);
    if (magic != CALC_MAGIC)
    {
        fprintf(stderr, "incorrect magic number in %s: %x", path, magic);
        goto err;
    }
    checked_r(32, s, &type);

    switch (type)
    {
    case DT_STANDARD:
        result = read_standard_derivative(path, s);
        break;
    case DT_FETCH:
        result = read_fetch_derivative(path, s);
        break;
    default:
        fprintf(stderr, "unknown derivative type in %s: %d", path, type);
        goto err;
    }

    sm_close(s);
    return result;

err:
    if (s)
        sm_close(s);
    return nullptr;
}
