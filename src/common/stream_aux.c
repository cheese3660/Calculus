/******************************************************************************
 *
 *  stream_aux.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/15/2026
 *
 *****************************************************************************/

#include <errno.h>

#include "common/debug.h"
#include "common/stream_aux.h"

enum stream_status sm_wsha(stream_t stream, sha256_t hash)
{
    (void)sm_write(stream, &hash, sizeof(sha256_t));
    return stream->status;
}

enum stream_status sm_wstr(stream_t stream, const string_t *str)
{
    if (sm_w32(stream, str->length) != STREAM_OK)
        return stream->status;
    (void)sm_write(stream, str->cstring, str->length);
    return stream->status;
}

enum stream_status sm_wcstr(stream_t stream, const char *str)
{
    uint32_t len = strlen(str);
    if (sm_w32(stream, len) != STREAM_OK)
        return stream->status;
    (void)sm_write(stream, str, len);
    return stream->status;
}

enum stream_status sm_rsha(stream_t stream, sha256_t *hash, bool *truncated)
{
    if (truncated)
        *truncated = false;
    ssize_t read = sm_read(stream, hash, sizeof(sha256_t));
    if (read < 0)
        return stream->status;
    if (read != sizeof(sha256_t) && truncated)
        *truncated = true;
    return stream->status;
}

enum stream_status sm_rstr(stream_t stream, string_t *str, bool *truncated)
{
    uint32_t len;
    bool len_truncated;
    if (sm_r32(stream, &len, &len_truncated) == STREAM_ERRORED)
        return stream->status;

    if (len_truncated)
    {
        if (truncated)
            *truncated = true;
        return stream->status;
    }

    *str = s_new_a(len);
    ssize_t read = sm_read(stream, str->cstring, len);
    if (read < 0)
    {
        s_free(str);
        return stream->status;
    }
    if (read != sizeof(len) && truncated)
        *truncated = true;
    str->length = (uint32_t)read;
    str->cstring[str->length] = 0;
    return stream->status;
}

enum stream_status sm_rcstr(stream_t stream, char **str, bool *truncated)
{

    uint32_t len;
    bool len_truncated;
    if (sm_r32(stream, &len, &len_truncated) == STREAM_ERRORED)
        return stream->status;

    if (len_truncated)
    {
        if (truncated)
            *truncated = true;
        return stream->status;
    }

    *str = malloc(len);
    if (*str == nullptr)
        panic("error allocating buffer to read string into: %s", strerror(errno));

    ssize_t read = sm_read(stream, *str, len);
    if (read < 0)
    {
        free(*str);
        return stream->status;
    }
    (*str)[read] = 0;
    return stream->status;
}
