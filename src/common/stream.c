/******************************************************************************
 *
 *  stream.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/15/2026
 *
 *  Implementation of the generic data stream library for files and memory
 *
 *****************************************************************************/

#include <errno.h>
#include <stdbit.h>
#include <string.h>

#include "common/debug.h"
#include "common/stream.h"
#include "common/utility.h"

/******************************************************************************
 *
 * FILE STREAMS
 *
 * struct file_stream - The backing type for a file stream
 * file_read() - The read implementation for a file stream
 * file_write() - The write implementation for a file stream
 * file_flush() - The flush implementation for a file stream
 * file_close() - The close implementation for a file stream
 * file_error() - The error implementation for a file stream
 * file_vtable - The vtable for file streams
 * sm_file() - Create a new file stream
 *
 *****************************************************************************/

struct file_stream
{
    struct stream_header header;
    FILE *file;
    int stored_errno;
};

static ssize_t file_read(stream_t stream, void *buffer, size_t buffer_size)
{
    // Assume if we get here, that the stream is asserted to be okay
    struct file_stream *fstream = (struct file_stream *)stream;
    ssize_t result = fread(buffer, 1, buffer_size, fstream->file);
    if (ferror(fstream->file))
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
        result = -1;
    }
    else if (feof(fstream->file))
    {
        stream->status = STREAM_END;
    }
    return result;
}

static ssize_t file_write(stream_t stream, const void *buffer, size_t buffer_size)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    ssize_t result = fwrite(buffer, 1, buffer_size, fstream->file);
    if (ferror(fstream->file))
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
        result = -1;
    }
    return result;
}

static void file_flush(stream_t stream)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    if (fflush(fstream->file) < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
}

static void file_close(stream_t stream)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    if (fclose(fstream->file) < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
}

static const char *file_error(const_stream_t stream)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    return strerror(fstream->stored_errno);
}

static struct stream_info file_vtable = {
    file_read,
    file_write,
    file_flush,
    file_close,
    file_error};

stream_t sm_file(FILE *file)
{
    struct file_stream *stream = malloc(sizeof(struct file_stream));
    if (stream == nullptr)
        panic("failed to allocate file stream: %s", strerror(errno));
    stream->header.vtable = &file_vtable;
    stream->header.status = feof(file) ? STREAM_END : STREAM_OK;
    stream->file = file;
    stream->stored_errno = 0;
    return (stream_t)stream;
}

/******************************************************************************
 *
 * MEMORY WRITE STREAMS
 *
 * struct mw_stream - The memory write stream state
 * mw_read() - A stubbed function for reading from a memory write stream,
 *             immediately sets an error
 * mw_write() - The write implementation of a memory write stream
 * mw_flush() - An empty flush implementation for a memory write stream
 * mw_close() - The close implementation for a memory write stream
 * mw_error() - The error implementation for a memory write stream
 * mw_vtable - The vtable for a memory write stream
 * sm_memwrite() - Create a new memory write stream
 *
 *****************************************************************************/

const int MW_ATTEMPTED_READ = -1;

struct mw_stream
{
    struct stream_header header;
    void **buffer;
    size_t *cursor;
    size_t capacity;
    int stored_errno;
    bool owned;
};

static ssize_t mw_read(stream_t stream, void *buffer, size_t buffer_size)
{
    (void)buffer;
    (void)buffer_size;
    stream->status = STREAM_ERRORED;
    ((struct mw_stream *)stream)->stored_errno = MW_ATTEMPTED_READ;
    return -1;
}

static ssize_t mw_write(stream_t stream, const void *buffer, size_t buffer_size)
{
    struct mw_stream *mstream = (struct mw_stream *)stream;

    // First check if we need to resize the stream's data buffer
    if (*mstream->cursor + buffer_size > mstream->capacity)
    {
        // If we do, then we go for the next power of 2 that contains our data
        mstream->capacity = stdc_bit_ceil(*mstream->cursor + buffer_size);
        *mstream->buffer = realloc(*mstream->buffer, mstream->capacity);
        if (*mstream->buffer == nullptr)
        {
            stream->status = STREAM_ERRORED;
            mstream->stored_errno = errno;
            return -1;
        }
    }

    memcpy(((uint8_t *)*mstream->buffer) + *mstream->cursor, buffer, buffer_size);
    *mstream->cursor += buffer_size;
    return (ssize_t)buffer_size;
}

static void mw_flush(stream_t stream)
{
    (void)stream;
}

static void mw_close(stream_t stream)
{
    free(*((struct mw_stream *)stream)->buffer);
}

static const char *mw_error(const_stream_t stream)
{

    struct mw_stream *mstream = (struct mw_stream *)stream;
    if (mstream->stored_errno == MW_ATTEMPTED_READ)
    {
        return "cannot read from a memwrite stream";
    }
    else
    {
        return strerror(mstream->stored_errno);
    }
}

static struct stream_info mw_vtable = {
    mw_read,
    mw_write,
    mw_flush,
    mw_close,
    mw_error};

stream_t sm_memwrite(void **buffer, size_t *buffer_size)
{
    struct mw_stream *stream = malloc(sizeof(struct mw_stream));
    if (stream == nullptr)
        panic("failed to allocate memwrite stream: %s", strerror(errno));
    stream->header.vtable = &mw_vtable;
    stream->header.status = STREAM_OK;

    if (*buffer != nullptr)
    {
        stream->buffer = buffer;
        stream->capacity = *buffer_size;
    }
    else
    {
        *buffer_size = stdc_bit_ceil(max(*buffer_size, (size_t)64));
        *buffer = malloc(*buffer_size);
        if (*buffer == nullptr)
            panic("Unable to initially create memory write stream buffer: %s", strerror(errno));
        stream->buffer = buffer;
        stream->capacity = *buffer_size;
    }
    *buffer_size = 0;
    stream->cursor = buffer_size;
    stream->stored_errno = 0;
    return (stream_t)stream;
}

/******************************************************************************
 *
 * MEMORY READ STREAMS
 *
 * struct mr_stream - The memory read stream state
 * mr_read() - The read implementation for a memory read stream
 * mr_write() - A stubbed write implementation for a memory read stream
 *              that sets an error
 * mr_flush() - An empty flush implementation for a memory read stream
 * mr_close() - An empty close implementation for a memory read stream
 * mr_error() - The error implementation for a memory read stream
 * mr_vtable - The vtable for a memory read stream
 * sm_memread() - Create a new memory read stream
 *
 *****************************************************************************/

struct mr_stream
{
    struct stream_header header;
    const void *buffer;
    size_t buffer_size;
    size_t cursor;
    bool was_written_to;
};

static ssize_t mr_read(stream_t stream, void *buffer, size_t buffer_size)
{
    struct mr_stream *mstream = (struct mr_stream *)stream;
    size_t remaining = mstream->buffer_size - mstream->cursor;
    size_t toread = min(remaining, buffer_size);
    memcpy(buffer, ((char *)mstream->buffer) + mstream->cursor, toread);
    mstream->cursor += toread;
    if (mstream->cursor == mstream->buffer_size)
    {
        stream->status = STREAM_END;
    }
    return (ssize_t)toread;
}

static ssize_t mr_write(stream_t stream, const void *buffer, size_t buffer_size)
{
    (void)buffer;
    (void)buffer_size;
    struct mr_stream *mstream = (struct mr_stream *)stream;
    mstream->was_written_to = true;
    stream->status = STREAM_ERRORED;
    return -1;
}

static void mr_flush(stream_t stream)
{
    (void)stream;
}

static void mr_close(stream_t stream)
{
    (void)stream;
}

static const char *mr_error(const_stream_t stream)
{
    struct mr_stream *mstream = (struct mr_stream *)stream;
    return mstream->was_written_to ? "cannot write to a memread stream" : "unknown error";
}

static struct stream_info mr_vtable = {
    mr_read,
    mr_write,
    mr_flush,
    mr_close,
    mr_error};

stream_t sm_memread(const void *buffer, size_t buffer_size)
{
    struct mr_stream *stream = malloc(sizeof(struct mr_stream));
    if (stream == nullptr)
        panic("failed to allocate memread stream: %s", strerror(errno));
    stream->header.vtable = &mr_vtable;
    stream->header.status = STREAM_OK;
    stream->buffer = buffer;
    stream->buffer_size = buffer_size;
    stream->cursor = 0;
    stream->was_written_to = false;
    return (stream_t)stream;
}

/******************************************************************************
 *
 * STREAM BASIC METHODS
 *
 * sm_read() - Read a block of data from a stream
 * sm_write() - Write a block of data to a stream
 * sm_flush() - Flush a stream
 * sm_close() - Close a stream
 * sm_error() - Get a streams error
 *
 *****************************************************************************/

ssize_t sm_read(stream_t stream, void *buffer, size_t buffer_size)
{
    if (stream->status == STREAM_END /* Anywhere that would be truncated is also here, truncation is a virtual status*/)
        return 0;
    if (stream->status == STREAM_ERRORED)
        return -1;
    return stream->vtable->read(stream, buffer, buffer_size);
}

ssize_t sm_write(stream_t stream, const void *buffer, size_t buffer_size)
{
    if (stream->status == STREAM_ERRORED)
        return -1;
    return stream->vtable->write(stream, buffer, buffer_size);
}

enum stream_status sm_flush(stream_t stream)
{
    if (stream->status == STREAM_OK)
        stream->vtable->flush(stream);
    return stream->status;
}

void sm_close(stream_t stream)
{
    stream->vtable->close(stream);
    free(stream);
}

const char *sm_error(const_stream_t stream)
{
    if (stream->status == STREAM_OK)
        return "no error";
    if (stream->status == STREAM_END)
        return "end of stream reached";
    if (stream->status == STREAM_ERRORED)
        return stream->vtable->error(stream);
    return "unknown error";
}

/******************************************************************************
 *
 * STREAM SIZED READ/WRITE METHODS
 *
 * sm_wN() - Write N bit data to a stream
 * sm_rN() - Read N bit data from a stream
 *
 *****************************************************************************/

enum stream_status sm_w8(stream_t stream, uint8_t u8)
{
    (void)sm_write(stream, &u8, sizeof(uint8_t));
    return stream->status;
}

enum stream_status sm_w16(stream_t stream, uint16_t u16)
{
    (void)sm_write(stream, &u16, sizeof(uint16_t));
    return stream->status;
}

enum stream_status sm_w32(stream_t stream, uint32_t u32)
{
    (void)sm_write(stream, &u32, sizeof(uint32_t));
    return stream->status;
}

enum stream_status sm_w64(stream_t stream, uint64_t u64)
{
    (void)sm_write(stream, &u64, sizeof(uint64_t));
    return stream->status;
}

enum stream_status sm_r8(stream_t stream, uint8_t *u8, bool *truncated)
{
    if (truncated)
        *truncated = false;
    ssize_t read = sm_read(stream, u8, sizeof(uint8_t));
    if (read < 0)
        return stream->status;
    if (read != sizeof(uint8_t) && truncated)
        *truncated = true;
    return stream->status;
}

enum stream_status sm_r16(stream_t stream, uint16_t *u16, bool *truncated)
{
    if (truncated)
        *truncated = false;
    ssize_t read = sm_read(stream, u16, sizeof(uint16_t));
    if (read < 0)
        return stream->status;
    if (read != sizeof(uint16_t) && truncated)
        *truncated = true;
    return stream->status;
}

enum stream_status sm_r32(stream_t stream, uint32_t *u32, bool *truncated)
{
    if (truncated)
        *truncated = false;
    ssize_t read = sm_read(stream, u32, sizeof(uint32_t));
    if (read < 0)
        return stream->status;
    if (read != sizeof(uint32_t) && truncated)
        *truncated = true;
    return stream->status;
}

enum stream_status sm_r64(stream_t stream, uint64_t *u64, bool *truncated)
{
    if (truncated)
        *truncated = false;
    ssize_t read = sm_read(stream, u64, sizeof(uint64_t));
    if (read < 0)
        return stream->status;
    if (read != sizeof(uint64_t) && truncated)
        *truncated = true;
    return stream->status;
}
