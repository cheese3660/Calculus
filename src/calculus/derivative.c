#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>

#include "calculus/derivative.h"
#include "calculus/paths.h"

#include "common/debug.h"
#include "common/fs.h"

#include "thirdparty/stb_ds.h"

static struct
{
    sha256_t key;
    derivative_header_t *value;
} *registered_derivatives = nullptr;

static derivative_header_t **requested_derivatives = nullptr;

standard_derivative_t *create_standard_derivative(
    size_t num_dependencies,
    derivative_header_t **dependencies,
    const char *name,
    const char *build)
{
    // Let's create the hash first
    // This will be the exact hash of the resulting .drv file if we make those
    sha256_ingest_t drv_ingest = {};
    sha256_appends(&drv_ingest, "---- recipe for ");
    sha256_appends(&drv_ingest, name);
    sha256_appends(&drv_ingest, " ----\ningredients:");
    for (size_t i = 0; i < num_dependencies; i++)
    {
        sha256_appends(&drv_ingest, "\n- ");
        sha256_appends(&drv_ingest, get_derivative_node_name(dependencies[i]));
    }
    sha256_appends(&drv_ingest, "\ninstructions:\n");
    sha256_appends(&drv_ingest, build);

    sha256_t hash = sha256_finalize(&drv_ingest);

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

fetch_tarball_derivative_t *create_fetch_tarball_derivative(
    const char *url,
    const char *hash,
    bool extract)
{
    sha256_t sha = hex_to_sha256(hash);

    derivative_header_t *preexisting = hmget(registered_derivatives, sha);

    if (preexisting != nullptr)
    {
        // Sanity check
        if (preexisting->dtype != DT_FETCH_TARBALL)
            panic("possible hash collision detected evaluating derivative for tarball %s, hash %s", url, sha256_to_hex(&sha));
        if (((fetch_tarball_derivative_t *)preexisting)->extract != extract)
            return nullptr; // TODO: add error message here

        return (fetch_tarball_derivative_t *)preexisting;
    }

    fetch_tarball_derivative_t *result = malloc(sizeof(fetch_tarball_derivative_t));
    result->dheader = (derivative_header_t){
        DT_FETCH_TARBALL,
        sha};
    result->url = strdup(url);
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
    case DT_FETCH_TARBALL:
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

// static derivative_header_t **push(derivative_header_t **stack, derivative_header_t *wanted)
// {
//     for (ssize_t i = 0; i < arrlen(stack); i++)
//     {
//         if (stack[i] == wanted)
//             return stack;
//     }

//     static char buffer[PATH_MAX];

//     debug("pushing - %s", get_derivative_node_name(wanted));
//     arrpush(stack, wanted);

//     if (wanted->dtype == DT_STANDARD)
//     {
//         standard_derivative_t *actual = (standard_derivative_t *)wanted;
//         for (size_t i = 0; i < actual->num_dependencies; i++)
//         {
//             stack = push(stack, actual->dependencies[i]);
//         }
//     }

//     return stack;
// }

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