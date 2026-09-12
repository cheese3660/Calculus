/******************************************************************************
 *
 *  fs.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  Some very basic filesystem abstractions
 * 
 *****************************************************************************/

#pragma once

// Filesystem tools that make checking for very basic things a lot simpler

/// @brief Stats path, and returns true if the stat succeeds
/// @param path The target path
/// @return True if the stat succeeds
bool fs_exists(const char* path);

/// @brief Stats path, and returns true if the stat mode is DIR
/// @param path The target path
/// @return True if the stat succeeds and stat mode is DIR
bool fs_isdir(const char* path);

/// @brief Stats path, and returns true if the stat mode is REG
/// @param path The target path
/// @return True if the stat succeeds and stat mode is REG
bool fs_isreg(const char* path);

/// @brief Checks if the target exists, and is a directory, and if not, shells out to mkdir
/// @param path The target path
/// @return -1 on failure, 0 on success
int fs_ensure_dir(const char* path);


/// @brief DANGEROUS - completely delete a directory, basically rm -rf, make absolutely sure you are passing a proper path into this!!!
/// @param path The path to delete
/// @return -1 on failure, 0 on success
int fs_rmdir(const char *path);