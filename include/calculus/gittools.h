/******************************************************************************
 *
 *  gittools.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

#pragma once


/// @brief Fetch a tag into an *already* existing empty directory
/// @param path The already existing directory
/// @param remote The remote path
/// @param tag The tag to fetch 
/// @return 0 on success, -1 on failure
int fetch_tag(const char* path, const char* remote, const char* tag);

/// @brief Fetch a commit into an *already* existing empty directory
/// @param path The already existing directory
/// @param remote The remote path
/// @param sha The commit SHA1 to fetch
/// @return 0 on success, -1 on failure
int fetch_sha(const char* path, const char* remote, const char* sha);


extern const char* stored_git_error;