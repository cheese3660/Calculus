/******************************************************************************
 *
 *  path.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/16/2026
 *
 *****************************************************************************/

#include <errno.h>
#include <stdbit.h>
#include <string.h>

#include <linux/limits.h>
#include <unistd.h>

#include "common/debug.h"
#include "common/path.h"

void p_dir(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash == 0)
    {
        s_setl(p, "/.", 2);
    }
    else if (slash < 0)
    {
        s_setl(p, ".", 1);
    }
    else
    {
        s_sub(p, 0, slash);
    }
}

string_t p_dir_a(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash == 0)
    {
        return s_ownl_a("/.", 2);
    }
    else if (slash < 0)
    {
        return s_ownl_a(".", 1);
    }
    else
    {
        return s_sub_a(p, 0, slash);
    }
}

string_t *p_dir_p(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash == 0)
    {
        return s_ownl_p("/.", 2);
    }
    else if (slash < 0)
    {
        return s_ownl_p(".", 1);
    }
    else
    {
        return s_sub_p(p, 0, slash);
    }
}

void p_file(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash < 0)
        return;
    s_subend(p, slash + 1);
}

string_t p_file_a(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash < 0)
        return s_copy_a(p);
    return s_subend_a(p, slash + 1);
}

string_t *p_file_p(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash < 0)
        return s_copy_p(p);
    return s_subend_p(p, slash + 1);
}

const char *p_file_c(string_t *p)
{
    int32_t slash = s_rfindc(p, '/');
    if (slash < 0)
        return p->cstring;
    return s_subend_c(p, slash + 1);
}

void p_filename(string_t *p)
{
    p_file(p);
    int32_t dot = s_rfindc(p, '.');
    if (dot < 0)
        return;
    s_sub(p, 0, dot);
}

string_t p_filename_a(string_t *p)
{
    string_t result = p_file_a(p);
    int32_t dot = s_rfindc(&result, '.');
    if (dot < 0)
        return result;
    s_sub(&result, 0, dot);
    return result;
}

string_t *p_filename_p(string_t *p)
{
    string_t *result = p_file_p(p);
    int32_t dot = s_rfindc(result, '.');
    if (dot < 0)
        return result;
    s_sub(result, 0, dot);
    return result;
}

void p_extension(string_t *p)
{
    int32_t dot = s_rfindc(p, '.');
    int32_t slash = s_rfindc(p, '/');
    if (dot < 0 || dot < slash)
        dot = p->length;
    s_subend(p, dot);
}

string_t p_extension_a(string_t *p)
{
    int32_t dot = s_rfindc(p, '.');
    int32_t slash = s_rfindc(p, '/');
    if (dot < 0 || dot < slash)
        dot = p->length;
    return s_subend_a(p, dot);
}

string_t *p_extension_p(string_t *p)
{
    int32_t dot = s_rfindc(p, '.');
    int32_t slash = s_rfindc(p, '/');
    if (dot < 0 || dot < slash)
        dot = p->length;
    return s_subend_p(p, dot);
}

const char *p_extension_c(string_t *p)
{
    int32_t dot = s_rfindc(p, '.');
    int32_t slash = s_rfindc(p, '/');
    if (dot < 0 || dot < slash)
        dot = p->length;
    return s_subend_c(p, dot);
}

string_t p_cwd_a()
{
    string_t result = s_new_a(PATH_MAX);
    result.length = strlen(getcwd(result.cstring, PATH_MAX));
    return result;
}

string_t *p_cwd_p()
{
    string_t *result = s_new_p(PATH_MAX);
    result->length = strlen(getcwd(result->cstring, PATH_MAX));
    return result;
}

void p_cat(string_t *p, const char *next)
{
    while (next[0] == '/')
        next += 1;
    if (!s_endswith(p, "/"))
        s_cat(p, "/");
    s_cat(p, next);
    s_trimr(p, "/");
}

void p_pre(string_t *p, const char *prior)
{
    int32_t prior_len = strlen(prior);
    s_triml(p, "/");
    if (prior[prior_len - 1] != '/')
        s_pre(p, "/");
    s_pre(p, prior);
}

void p_cats(string_t *p, const string_t *next)
{
    p_cat(p, next->cstring);
}

void p_pres(string_t *p, const string_t *prior)
{
    p_pre(p, prior->cstring);
}

string_t p_cat_a(string_t *p, const char *next)
{
    string_t result = s_copy_a(p);
    p_cat(&result, next);
    return result;
}

string_t p_pre_a(string_t *p, const char *prior)
{
    string_t result = s_copy_a(p);
    p_pre(&result, prior);
    return result;
}

string_t p_cats_a(string_t *p, const string_t *next)
{
    return p_cat_a(p, next->cstring);
}

string_t p_pres_a(string_t *p, const string_t *prior)
{
    return p_pre_a(p, prior->cstring);
}

string_t *p_cat_p(string_t *p, const char *next)
{
    string_t *result = s_copy_p(p);
    p_cat(result, next);
    return result;
}

string_t *p_pre_p(string_t *p, const char *prior)
{
    string_t *result = s_copy_p(p);
    p_pre(result, prior);
    return result;
}

string_t *p_cats_p(string_t *p, const string_t *next)
{
    return p_cat_p(p, next->cstring);
}

string_t *p_pres_p(string_t *p, const string_t *prior)
{
    return p_pre_p(p, prior->cstring);
}

void p_norm(string_t *p)
{
    // Let's hope a 256 character path catches the common case with filenamess
    char *stack = malloc(256);
    if (stack == nullptr)
        panic("Failed to allocate stack for normalization: %s", strerror(errno));
    int32_t size = 256;

    int32_t cursor = 0;
    if (!s_startswith(p, "/"))
    {
        string_t *cwd = p_cwd_p();
        p_pres(p, cwd);
        s_free(cwd);
    }

    // Now we basically treat this like a directory stack
    // <...> -> Push
    // . -> Do nothing
    // .. -> Pop

    // si is 1 because we don't care about the leading '/' for this
    int32_t si = 1;
    while (si <= p->length)
    {
        int32_t slash = s_lfindnc(p, '/', si);
        if (slash < si)
            slash = p->length;
        if (slash == si)
        {
            si = slash + 1;
            continue;
        }

        int32_t len = slash - si;

        if (len == 1 && p->cstring[si] == '.')
        {
            // Do nothing case
            si = slash + 1;
            continue;
        }

        if (len == 2 && p->cstring[si] == '.' && p->cstring[si + 1] == '.')
        {
            // This is the pop case
            if (cursor > 0)
            {
                cursor -= 1;
                while (cursor != 0)
                {
                    if (stack[cursor - 1] == 0)
                        break;
                    cursor -= 1;
                }
            }

            si = slash + 1;
            continue;
        }

        // Normal case
        if (len + cursor >= size /* >= because of the 0 byte */)
        {
            stack = realloc(stack, size = stdc_bit_ceil((uint32_t)(size + len + cursor + 1)));
            if (stack == nullptr)
                panic("Failed to allocate stack for normalization: %s", strerror(errno));
        }

        memcpy(stack + cursor, p->cstring + si, len);
        cursor += len;
        stack[cursor++] = 0;

        si = slash + 1;
    }

    // Now that we've gotten the entire path as a stack
    s_setl(p, "", 0);
    int32_t ci = 0;
    while (ci < cursor)
    {
        s_cat(p, "/");
        s_cat(p, stack + ci);
        ci += strlen(stack + ci) + 1;
    }

    if (p->length == 0)
    {
        s_setl(p, "/", 1);
    }
    free(stack);
}

string_t p_norm_a(string_t *p)
{
    string_t result = s_copy_a(p);
    p_norm(&result);
    return result;
}

string_t *p_norm_p(string_t *p)
{
    string_t *result = s_copy_p(p);
    p_norm(result);
    return result;
}

string_t p_normc_a(const char *p)
{
    string_t result = s_own_a(p);
    p_norm(&result);
    return result;
}

string_t *p_normc_p(const char *p)
{
    string_t *result = s_own_p(p);
    p_norm(result);
    return result;
}
