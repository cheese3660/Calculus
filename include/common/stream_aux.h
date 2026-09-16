/******************************************************************************
 *
 *  stream_aux.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/16/2026
 *
 *  Auxilliary methods for streams that interface with the other utilities in
 *  the codebase, e.g. strings and SHA256 hashes
 *
 *****************************************************************************/

#pragma once

#include "common/crypto.h"
#include "common/stream.h"
#include "common/string.h"

/// @brief Write a SHA256 hash to a stream
/// @param stream The stream
/// @param hash The hash
/// @return The status of the stream after the write
enum stream_status sm_wsha(stream_t stream, sha256_t hash) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Write a string to a stream
/// @param stream The stream
/// @param str The string
/// @return The status of the stream after the write
/// @details Writes the length as a u32, then copies the data directly into the stream
enum stream_status sm_wstr(stream_t stream, const string_t *str) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Write a C string to a stream (as a regular string would be written)
/// @param stream The stream
/// @param str The string
/// @return The status of the stream after the write
enum stream_status sm_wcstr(stream_t stream, const char *str) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Read a SHA256 hash from a stream
/// @param stream The stream
/// @param hash Where to store the hash
/// @param truncated A flag (if non-null) that gets set if the stream read is short
/// @return The status of the tream after the read
enum stream_status sm_rsha(stream_t stream, sha256_t *hash, bool *truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));

/// @brief Read a string from a stream
/// @param stream The stream
/// @param str Where to store the string (note as always this string does need to be free()'d)
/// @param truncated A flag (if non-null) that gets set if the stream read is short
/// @return The status of the stream after the read
enum stream_status sm_rstr(stream_t stream, string_t *str, bool *truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));

/// @brief Read a C string from a stream
/// @param stream
/// @param str Where to store the string (allocated using malloc())
/// @param truncated A flag (if non-null) that gets set if the stream read is short
/// @return The status of the stream after the read
enum stream_status sm_rcstr(stream_t stream, char **str, bool *truncated) __attribute__((access(read_write, 1), access(write_only, 2), access(write_only, 3), nonnull(1, 2)));
