#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    // Each of these are stored in little endian to make the computation quick
    uint32_t h[8];
} sha256_t;


/// @brief Hash a block of data as sha256 - Used for content addressing
/// @param data the block of data to hash
/// @param len the length of the data being hashed
/// @return the sha256 hash of the data
sha256_t sha256_hash(const void* data, size_t len);

/// @brief Hash a file as sha256 - Used for content addressing
/// @param file the file to hash
/// @return the sha256 hash of the data
sha256_t sha256_hashf(FILE* file);

#define sha256_string(str) sha256_hash(str,strlen(str))

/// @brief Convert a hash to a hex string, the returned data is in a preallocated buffer and will be overwritten on the next call (different than the b64 buffer)
/// @param hash the sha256 hash
/// @return a 64 character long string of the sha256 digest in hexadecimal
const char* sha256_to_hex(sha256_t* hash);

/// @brief Convert a hash to a base64 string, the returned data is in a preallocated buffer and will be overwritten on the next call (different than the hex buffer)
/// @param hash the sha256 hash
/// @return a 44 character long string of the sha256 digest in base64
const char* sha256_to_b64(sha256_t* hash);


typedef struct {
    uint8_t chunk[64]; // 512 bit chunks;
    uint8_t chunk_ptr;
    uint64_t chunk_count;
    sha256_t result;
} sha256_ingest_t;

/// @brief Append data to an in progress ingest
/// @param to_ingest The in progress ingest, should be zero initialized to start
/// @param data The data to ingest
/// @param len The length of the data to ingest
void sha256_append(sha256_ingest_t* to_ingest, const void* data, size_t len);

/// @brief Append a file to an in progress ingest
/// @param to_ingest The in progress ingest, should be zero initialized to start
/// @param file The file to read the contents of and append
/// @details This reads 4 kilobyte chunks of the file until the end of its stream
void sha256_appendf(sha256_ingest_t* to_ingest, FILE* file);

/// @brief Finalize an in progress ingest and get the digest
/// @param ingest The ingest to finalize
/// @return The sha256 digest
sha256_t sha256_finalize(sha256_ingest_t* ingest);