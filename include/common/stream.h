/******************************************************************************
 *
 *  stream.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/15/2026
 *
 *  Generic data stream library, can be used to read/write data from a "stream"
 *  of data, like a file or memory
 *
 *****************************************************************************/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>


struct stream_header;

struct stream_info
{
    ssize_t (*read)(struct stream_header *stream, void *buffer, size_t buffer_size)
        __attribute__((access(write_only, 2, 3), nonnull(1, 2)));
    ssize_t (*write)(struct stream_header *stream, const void *buffer, size_t buffer_size)
        __attribute__((access(read_only, 2, 3), nonnull(1, 2)));
    void (*flush)(struct stream_header *stream)
        __attribute__((nonnull(1)));
    void (*close)(struct stream_header *stream)
        __attribute__((nonnull(1)));
    const char *(*error)(const struct stream_header *stream)
        __attribute__((access(read_only, 1), nonnull(1), returns_nonnull));
};

enum stream_status
{
    STREAM_OK,
    STREAM_END,
    STREAM_ERRORED
};

typedef struct stream_header
{
    struct stream_info *vtable;
    enum stream_status status;
} *stream_t;

typedef const struct stream_header *const_stream_t;

// Forward declare this for the malloc attributes
void sm_close(stream_t stream);

/// @brief Wrap a stream around a file
/// @param file The file to wrap
/// @return A stream for reading/writing from the file
stream_t sm_file(FILE *file) __attribute__((malloc, malloc(sm_close, 1), access(read_write, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Create a stream for writing to memory
/// @param buffer A pointer to a buffer that will be allocated with malloc, if set, it already has to be mallocated
/// @param buffer_size A pointer the amount of data that has been written to the stream, if set going in it is the initial capacity of the buffer
/// @return A stream for writing to memory that will be pointed to by buffer, and sized by buffer size
/// @warning Closing the stream will free the memory buffer that backs it, memcpy the data before closing it if you need to keep it
stream_t sm_memwrite(void **buffer, size_t *buffer_size) __attribute__((malloc, malloc(sm_close, 1), access(read_write, 1), access(read_write, 2), nonnull(1, 2), returns_nonnull, warn_unused_result));

/// @brief Create a stream for reading from (fixed size) memory
/// @param buffer The buffer being read from
/// @param buffer_size How much data is contained in the buffer
/// @return A stream for reading from the buffer
stream_t sm_memread(const void *buffer, size_t buffer_size) __attribute__((malloc, malloc(sm_close, 1), access(read_only, 1, 2), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Read from a stream
/// @param stream The stream to read from
/// @param buffer The buffer to read into
/// @param buffer_size The amount of data to read
/// @return The amount of data read, -1 if failed to read
ssize_t sm_read(stream_t stream, void *buffer, size_t buffer_size) __attribute__((access(read_write, 1), access(write_only, 2, 3), nonnull(1, 2), warn_unused_result));

/// @brief Write to a stream
/// @param stream The stream to write to
/// @param buffer The buffer to write
/// @param buffer_size The amount of data to write
/// @return The amount of data written, -1 if failed to write
ssize_t sm_write(stream_t stream, const void *buffer, size_t buffer_size) __attribute__((access(read_write, 1), access(read_only, 2, 3), nonnull(1, 2)));

/// @brief Flush a stream
/// @param stream The stream to flush
/// @return The status of the stream after the flush
enum stream_status sm_flush(stream_t stream) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Close a stream
/// @param stream The stream to close, this also free()'s the stream
void sm_close(stream_t stream) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Get the current error for a stream
/// @param stream The stream to get the error from
/// @return A string representing the current error for the stream
const char *sm_error(const_stream_t stream) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

// Now time for why I wanted a stream library

// I wish I could make this more generic

/// @brief Write an 8 bit number to the stream
/// @param stream The stream to write the number to
/// @param u8 The 8 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w8(stream_t stream, uint8_t u8) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Write a 16 bit number to the stream
/// @param stream The stream to write the number to
/// @param u16 The 16 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w16(stream_t stream, uint16_t u16) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Write an 32 bit number to the stream
/// @param stream The stream to write the number to
/// @param u32 The 32 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w32(stream_t stream, uint32_t u32) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Write an 64 bit number to the stream
/// @param stream The stream to write the number to
/// @param u64 The 64 bit number
/// @return The new status of the stream after the call
enum stream_status sm_w64(stream_t stream, uint64_t u64) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Read an 8 bit number from the stream
/// @param stream The stream to read the number from
/// @param u8 An out parameter for the 8 bit number
/// @param truncated An out parameter for whether or not the read was truncated, can be nullptr
/// @return The new status of the stream after the call
enum stream_status sm_r8(stream_t stream, uint8_t *u8, bool* truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));

/// @brief Read a 16 bit number from the stream
/// @param stream The stream to read the number from
/// @param u16 An out parameter for the 16 bit number
/// @param truncated An out parameter for whether or not the read was truncated, can be nullptr
/// @return The new status of the stream after the call
enum stream_status sm_r16(stream_t stream, uint16_t *u16, bool* truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));

/// @brief Read a 32 bit number from the stream
/// @param stream The stream to read the number from
/// @param u32 An out parameter for the 32 bit number
/// @param truncated An out parameter for whether or not the read was truncated, can be nullptr
/// @return The new status of the stream after the call
enum stream_status sm_r32(stream_t stream, uint32_t *u32, bool* truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));

/// @brief Read a 64 bit number from the stream
/// @param stream The stream to read the number from
/// @param u64 An out parameter for the 64 bit number
/// @param truncated An out parameter for whether or not the read was truncated, can be nullptr
/// @return The new status of the stream after the call
enum stream_status sm_r64(stream_t stream, uint64_t *u64, bool* truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));
