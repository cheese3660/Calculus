/******************************************************************************
 *
 *  stream.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/14/2026
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

static ssize_t file_read(struct stream_header *stream, void *buffer, size_t buffer_size)
{
    // Assume if we get here, that the stream is asserted to be okay
    struct file_stream *fstream = (struct file_stream *)stream;
    ssize_t result = fread(buffer, 1, buffer_size, fstream->file);
    if (result < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
    if (feof(fstream->file))
    {
        stream->status = STREAM_END;
    }
    return result;
}

static ssize_t file_write(struct stream_header *stream, void *buffer, size_t buffer_size)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    ssize_t result = fwrite(buffer, 1, buffer_size, fstream->file);
    if (result < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
    return result;
}

static void file_flush(struct stream_header *stream)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    if (fflush(fstream->file) < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
}

static void file_close(struct stream_header *stream)
{
    struct file_stream *fstream = (struct file_stream *)stream;
    if (fclose(fstream->file) < 0)
    {
        fstream->stored_errno = errno;
        stream->status = STREAM_ERRORED;
    }
}

static const char *file_error(struct stream_header *stream)
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
 * sm_mewrite() - Create a new memory write stream
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

static ssize_t mw_read(struct stream_header *stream, void *buffer, size_t buffer_size)
{
    stream->status = STREAM_ERRORED;
    ((struct mw_stream *)stream)->stored_errno = MW_ATTEMPTED_READ;
    return -1;
}

static ssize_t mw_write(struct stream_header *stream, void *buffer, size_t buffer_size)
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

    if (memcpy(((uint8_t)*mstream->buffer) + *mstream->cursor, buffer, buffer_size) == nullptr)
    {
        stream->status = STREAM_ERRORED;
        mstream->stored_errno = errno;
        return -1;
    }

    return (ssize_t)buffer_size;
}

static void mw_flush(struct stream_header *stream)
{
}

static void mw_close(struct stream_header *stream)
{
    free(*((struct mw_stream *)stream)->buffer);
}

static const char *mw_error(struct stream_header *stream)
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
        *buffer = malloc(buffer_size);
        if (*buffer == nullptr)
            panic("Unable to initially create memory write stream: %s", strerror(errno));
        stream->buffer = buffer;
        stream->capacity = *buffer_size;
    }
    *buffer_size = 0;
    stream->cursor = buffer_size;
    stream->stored_errno = 0;
    return stream;
}

/******************************************************************************
 *
 * MEMORY READ STREAMS
 *
 *****************************************************************************/


/// @brief Read from a stream
/// @param stream The stream to read from
/// @param buffer The buffer to read into
/// @param buffer_size The amount of data to read
/// @return The amount of data read, -1 if failed to read
ssize_t sm_read(stream_t stream, void *buffer, size_t buffer_size);

/// @brief Write to a stream
/// @param stream The stream to write to
/// @param buffer The buffer to write
/// @param buffer_size The amount of data to write
/// @return The amount of data written, -1 if failed to write
ssize_t sm_write(stream_t stream, void *buffer, size_t buffer_size);

/// @brief Flush a stream
/// @param stream The stream to flush
enum stream_status sm_flush(stream_t stream);

/// @brief Close a stream
/// @param stream The stream to close
enum stream_status sm_close(stream_t stream);

// Now time for why I wanted a stream library

// I wish I could make this more generic

/// @brief Write an 8 bit number to the stream
/// @param stream The stream to write the number to
/// @param u8 The 8 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w8(stream_t stream, uint8_t u8);

/// @brief Write a 16 bit number to the stream
/// @param stream The stream to write the number to
/// @param u16 The 16 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w16(stream_t stream, uint16_t u16);

/// @brief Write an 32 bit number to the stream
/// @param stream The stream to write the number to
/// @param u32 The 32 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w32(stream_t stream, uint32_t u32);

/// @brief Write an 64 bit number to the stream
/// @param stream The stream to write the number to
/// @param u64 The 64 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w64(stream_t stream, uint64_t u64);

/// @brief Read an 8 bit number from the stream
/// @param stream The stream to read the number from
/// @param u8 An out parameter for the 8 bit number
/// @return The new status of the stream after the call
enum stream_status sm_r8(stream_t stream, uint8_t *u8);

/// @brief Read a 16 bit number from the stream
/// @param stream The stream to read the number from
/// @param u16 An out parameter for the 16 bit number
/// @return The new status of the stream after the call
enum stream_status sm_r16(stream_t stream, uint16_t *u16);

/// @brief Read a 32 bit number from the stream
/// @param stream The stream to read the number from
/// @param u32 An out parameter for the 32 bit number
/// @return The new status of the stream after the call
enum stream_status sm_r32(stream_t stream, uint32_t *u32);

/// @brief Read a 64 bit number from the stream
/// @param stream The stream to read the number from
/// @param u64 An out parameter for the 64 bit number
/// @return The new status of the stream after the call
enum stream_status sm_r64(stream_t stream, uint64_t *u64);

/// @brief Get the current error for a stream
/// @param stream The stream to get the error from
/// @return A string representing the current error for the stream
const char *stream_error(stream_t stream);
