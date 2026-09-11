/******************************************************************************
 *
 *  archive.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

/*
 * Used for content addressing when needed, and for otherwise moving folders around deterministically
 */

#pragma once

#include <stdio.h>

#include "common/crypto.h"


/// @brief Gets the hash of a directory in the same manner that `car` archiving it will give
/// @param path The directory to hash
/// @param result Where the resulting hash gets written to
/// @return 0 on success, -1 on failure with the errno number kept the same from the failure, and a warning printed to stderr
int car_hash(const char* path, sha256_t *result);


/// @brief Write the calculus archive of a directory to a file
/// @param path The directory to archive
/// @param target The file to write the archive to
/// @return 0 on success, -1 on failure with the errno number kept the same from the failure, and a warning printed to stderr using perror
int car_archive(const char *path, FILE *target);


/// @brief Write the calculus archive of a directory to a file, putting a umask on the permission bits to make it readonly
/// @param path The directory to archive
/// @param target The file to write the archive to
/// @return 0 on success, -1 on failure with the errno number kept the same from the failure, and a warning printed to stderr using perror
int car_archive_ro(const char *path, FILE *target);

/// @brief Extract a calculus archive into a directory
/// @param car_file The archive
/// @param path The directory to write the files out to, must already exist
/// @return 0 on success, -1 on failure with the errno number kept the same from the failure, and a warning printed to stderr using perror
int car_extract(FILE* car_file, const char* path);