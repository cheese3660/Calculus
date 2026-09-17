/******************************************************************************
 *
 *  path.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/16/2026
 *
 *  General utilities for string path manipulation, like combining directories
 *  and what not
 *
 *****************************************************************************/

#pragma once

#include "string.h"

/// @brief Get the directory from a path in place without the trailing /
/// @param p The path
void p_dir(string_t *p) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Get the directory from a path without the trailing /
/// @param p The path
/// @return The directory of the path as an allocated string
string_t p_dir_a(string_t *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Get the directory from a path without the trailing /
/// @param p The path
/// @return The directory of the path as a string from the string pool
string_t *p_dir_p(string_t *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Get the file (w/ extension) from a path in place
/// @param p The path
void p_file(string_t *p) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Get the file (w/ extension) from a path
/// @param p The path
/// @return The file of the path as an allocated string
string_t p_file_a(string_t *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Get the file (w/ extension) from a path
/// @param p The path
/// @return The file of the path as a string from the string pool
string_t *p_file_p(string_t *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Get the file (w/ extension) from a path
/// @param p The path
/// @return The file name with extension as a substring to the end (no allocations)
const char *p_file_c(string_t *p) __attribute__((pure, access(read_only, 1), nonnull(1), returns_nonnull));

/// @brief Get the filename (w/o extension) from a path in place
/// @param p The path
void p_filename(string_t *p) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Get the filename (w/o extension) from a path
/// @param p The path
/// @return The filename w/o extension as an allocated string
string_t p_filename_a(string_t *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Get the filename (w/o extension) from a path
/// @param p The path
/// @return The filename w/o extension as a string from the string pool
string_t *p_filename_p(string_t *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Get the extension from a path in place (with the dot, so empty string means no extension)
/// @param p The path
void p_extension(string_t *p) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Get the extension from a path (with the dot, so empty string means no extension)
/// @param p The path
/// @return The extension as an allocated string
string_t p_extension_a(string_t *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Get the extension from a path (with the dot, so empty string means no extension)
/// @param p The path
/// @return The extension as a string from the string pool
string_t *p_extension_p(string_t *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Get the extension from a path (with the dot, so empty string means no extension)
/// @param p The path
/// @return The extension as a substring to the end (no allocations)
const char *p_extension_c(string_t *p) __attribute__((pure, access(read_only, 1), nonnull(1), returns_nonnull));

/// @brief Get the CWD
/// @return The CWD as an allocated string
string_t p_cwd_a() __attribute__((warn_unused_result));

/// @brief Get the CWD
/// @return The CWD as a string from the string pool
string_t *p_cwd_p() __attribute__((returns_nonnull, warn_unused_result));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
void p_cat(string_t *p, const char *next) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
void p_pre(string_t *p, const char *prior) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
void p_cats(string_t *p, const string_t *next) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
void p_pres(string_t *p, const string_t *prior) __attribute__((access(read_write, 1), access(read_only, 2), nonnull(1, 2)));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
/// @return The result of the combination as an allocated string
string_t p_cat_a(string_t *p, const char *next) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), warn_unused_result));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
/// @return The result of the combination as an allocated string
string_t p_pre_a(string_t *p, const char *prior) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), warn_unused_result));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
/// @return The result of the combination as an allocated string
string_t p_cats_a(string_t *p, const string_t *next) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), warn_unused_result));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
/// @return The result of the combination as an allocated string
string_t p_pres_a(string_t *p, const string_t *prior) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), warn_unused_result));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
/// @return The result of the combination from the string pool
string_t *p_cat_p(string_t *p, const char *next) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), returns_nonnull, warn_unused_result));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
/// @return The result of the combination from the string pool
string_t *p_pre_p(string_t *p, const char *prior) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), returns_nonnull, warn_unused_result));

/// @brief Combine a path with another path in place
/// @param p The path being modified
/// @param next The next part of the path
/// @return The result of the combination from the string pool
string_t *p_cats_p(string_t *p, const string_t *next) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), returns_nonnull, warn_unused_result));

/// @brief Prefix a path with another path in place
/// @param p The path being modified
/// @param prior The prior part of the path
/// @return The result of the combination from the string pool
string_t *p_pres_p(string_t *p, const string_t *prior) __attribute__((access(read_only, 1), access(read_only, 2), nonnull(1, 2), returns_nonnull, warn_unused_result));

/// @brief Normalize a path in place
/// @param p The path
void p_norm(string_t *p) __attribute__((access(read_write, 1), nonnull(1)));

/// @brief Normalize a path
/// @param p The path
/// @return The normalized path as an allocated string
string_t p_norm_a(string_t *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Normalize a path
/// @param p The path
/// @return The normalized path as a string from the string pool
string_t *p_norm_p(string_t *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));

/// @brief Normalize a path
/// @param p The path
/// @return The normalized path as an allocated string
string_t p_normc_a(const char *p) __attribute__((access(read_only, 1), nonnull(1), warn_unused_result));

/// @brief Normalize a path
/// @param p The path
/// @return The normalized path as a string from the string pool
string_t *p_normc_p(const char *p) __attribute__((access(read_only, 1), nonnull(1), returns_nonnull, warn_unused_result));
