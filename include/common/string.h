/******************************************************************************
 *
 *  string.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  A very basic dynamic string library
 *
 *  Also allows wrapping constantly sized buffers for strings, and will call
 *  panic() if methods involving those fail as the user themselves should take
 *  care that they do not and use the dynamic versions elsewise
 *
 *****************************************************************************/

#include <stdint.h>
#include <stdlib.h>

// A basic allocation table that is used for creating new strings, resizing them
// and releasing them
struct string_allocator
{
    void *(*alloc)(void *old, size_t new_size);
    void (*dealloc)(void *ptr);
};

extern struct string_allocator *s_current_alloc;

typedef struct string
{
    // This is the allocator for the string, if it is nullptr then this string
    // cannot under any circumstances be resized or freed, which allows for
    // easy owning of the string
    struct string_allocator *allocator;
    uint32_t length;
    uint32_t capacity;
    // This always has a null byte at the end to be able to efficiently convert
    // to C functions that need it
    char *cstring;
} string_t;

/// @brief Clear a string, settings its length to 0 and making it's cstring a nullbyte
/// @param string The string to clear
static inline void s_clear(string_t *string)
{
    string->cstring[0] = 0;
    string->length = 0;
}

// Strings in the string pool will be reallocated upon release if the capacity is above this to prevent huge memory leakage
// This ends up in ~4 Megabytes of max held string capacity at the default value of 64 KiB,
#define STRINGPOOL_MAX_HELD_CAPACITY 65536

#define STRING_DEFAULT_CAPACITY 128

/// @brief Free a string, releasing it's memory if it has an allocator
/// @param string The string to free
void s_free(string_t *string);

/// @brief Allocate a new string with a given capacity
/// @param capacity The initial capacity, use STRING_DEFAULT_CAPACITY if you don't know what to use
/// @return A newly allocated string
string_t s_new_a(uint32_t capacity);

/// @brief Take a string with the given capacity from the string pool
/// @param capacity The initial capacity, use STRING_DEFAULT_CAPACITY if you don't know what to use
/// @return A string taken from the string pool
string_t *s_new_p(uint32_t capacity);

/// @brief Take ownership of a string, reallocating it
/// @param original The string to take ownership of
/// @return A string that contains `original`
string_t s_own_a(const char *original);

/// @brief Take ownership of a string, using the string pool, panicking if it can't
/// @param original The string to take ownership of
/// @return A string that contains `original`
string_t *s_own_p(const char *original);

/// @brief Take ownership of a string, reallocating it
/// @param original The string to take ownership of
/// @param len The original string length,
/// @return A string that contains `original`
string_t s_ownl_a(const char *original, uint32_t len);

/// @brief Take ownership of a string, attempting to put it into the string pool instead of reallocating
/// @param original The string to take ownership of
/// @param len The original string length,
/// @return A string that contains `original`
string_t *s_ownl_p(const char *original, uint32_t len);

/// @brief Create a new string via a format string
/// @param format The format string
/// @param ... The format parameters
/// @return A new string allocated with the given format characters
string_t s_fmt_a(const char *format, ...) __attribute__((format(printf,1,2)));

/// @brief Create a new string via a format string
/// @param max_len The maximum length of the string from the string pool
/// @param format The format string
/// @param ... The format parameters
/// @return A new string from the string pool with the given format characters
string_t *s_fmt_p(uint32_t max_len, const char *format, ...) __attribute__((format(printf,2,3)));

/// @brief Create a view over a string, allowing modification but no reallocating
/// @param original The string being viewed
/// @return A view unto the string, with a capacity of the string length and no reallocation
string_t s_view(char *original);

/// @brief Create a view over a string, allowing modification but no reallocating
/// @param original The string being viewed
/// @param len The length of the string being viewed (excluding the null terminator)
/// @return A view unto the string, with a capacity of the string length and no reallocation
string_t s_viewl(char *original, uint32_t len);

/// @brief Wrap a buffer as a temporary string that cannot be resized
/// @param buffer The buffer
/// @param buffer_capacity The capacity of the buffer (including the null terminator)
/// @return A wrapping of the buffer
string_t s_wrap(char *buffer, uint32_t buffer_capacity);

/// @brief Sets the value of a string to the given string
/// @param s The string
/// @param value The given string
void s_set(string_t *s, const char* value);

/// @brief Sets the value of a string to the given string (with a known length)
/// @param s The string
/// @param value The given string
/// @param len The known length
void s_setl(string_t *s, const char* value, uint32_t len);

/// @brief Sets the value of a string to the given string
/// @param s The string
/// @param value The given string
void s_sets(string_t *s, string_t* value);

/// @brief Make sure the string has the capacity for len bytes
/// @param s The string
/// @param len The length to reserve
void s_reserve(string_t *s, uint32_t len);

/// @brief Checks if a string starts with a given prefix
/// @param s The string
/// @param prefix The prefix
/// @return True if it does, false otherwise
bool s_startswith(string_t *s, const char *prefix);

/// @brief Checks if a string starts with a given prefix
/// @param s
/// @param prefix The prefix
/// @return True if it does, false otherwise
bool s_startswiths(string_t *s, string_t *prefix);

/// @brief Checks if a string ends with a given postfix
/// @param s The string
/// @param postfix The postfix
/// @return True if it does, false otherwise
bool s_endswith(string_t *s, const char *postfix);

/// @brief Checks if a string ends with a given posstfix
/// @param s The string
/// @param prefix The postfix
/// @return True if it does, false otherwise
bool s_endswiths(string_t *s, string_t *postfix);

/// @brief Concatenate s with addition in place
/// @param s The string
/// @param addition The addition to the string
void s_cat(string_t *s, const char *addition);

/// @brief Concatenate s with addition and returns a new string
/// @param s The string
/// @param addition The addition to the string
/// @return A string that is the result of concatenating s with addition
string_t s_cat_a(string_t *s, const char *addition);

/// @brief Concatenate s with addition and returns a new string (using the string pool)
/// @param s The string
/// @param addition The addition to the string
/// @return A string that is the result of concatenating s with addition
string_t *s_cat_p(string_t *s, const char *addition);

/// @brief Concatenate s with addition in place
/// @param s The string
/// @param addition The addition to the string
void s_cats(string_t *s, string_t *addition);

/// @brief Concatenate s with addition and returns a new string
/// @param s The string
/// @param addition The addition to the string
/// @return A string that is the result of concatenating s with addition
string_t s_cats_a(string_t *s, string_t *addition);

/// @brief Concatenate s with addition and returns a new string (using the string pool)
/// @param s The string
/// @param addition The addition to the string
/// @return A string that is the result of concatenating s with addition
string_t *s_cats_p(string_t *s, string_t *addition);

/// @brief Concatenates s with a format string in place (using asprintf internally)
/// @param s The string
/// @param format The format string
/// @param ... The format arguments
void s_catfa(string_t *s, const char* format, ...) __attribute__((format(printf,2,3)));

/// @brief Concatenates s with a format string in place (using snprintf internally for less allocations)
/// @param s The string
/// @param max_format The maximum length of the format result
/// @param format The format string
/// @param ... The format arguments
void s_catfn(string_t *s, uint32_t max_format, const char* format, ...) __attribute__((format(printf,3,4)));

/// @brief Concatenates s with a format string and returns that (using asprintf internally)
/// @param s The string
/// @param format The format string
/// @param ... The format arguments
/// @return A newly allocated string that is the result of the concatenation
string_t s_catfa_a(string_t *s, const char* format, ...) __attribute__((format(printf,2,3)));

/// @brief Concatenates s with a format string and returns that (using snprintf internally for less allocations)
/// @param s The string
/// @param max_format The maximum length of the format result
/// @param format The format string
/// @param ... The format arguments
/// @return A newly allocated string that is the result of the concatenation
string_t s_catfn_a(string_t *s, uint32_t max_format, const char* format, ...) __attribute__((format(printf,3,4)));

/// @brief Concatenates s with a format string and returns a new string from the string pool (using asprintf internally)
/// @param s The string
/// @param format The format string
/// @param ... The format arguments
/// @return A new string from the string pool that is the result of the concatenation
string_t *s_catfa_p(string_t *s, const char* format, ...) __attribute__((format(printf,2,3)));

/// @brief Concatenates s with a format string and returns a new string from the string pool (using snprintf internally for less allocations)
/// @param s The string
/// @param max_format The maximum length of the format result
/// @param format The format string
/// @param ... The format arguments
/// @return A new string from the string pool that is the result of the concatenation
string_t *s_catfn_p(string_t *s, uint32_t max_format, const char* format, ...) __attribute__((format(printf,3,4)));

/// @brief Allocates a new string using the current allocator from s->cstring and frees the string original
/// @param s The string to take and free
/// @return A pointer to a copy of the strings data
char *s_take(string_t *s);

/// @brief Copies a string from s->cstring into the buffer and frees the string original
/// @param s The string to take and free
/// @return A pointer to a copy of the strings data
char *s_taken(string_t* s, char* buffer, uint32_t n);

/// @brief Allocates a new string using the current allocator from s->cstring and returns that
/// @param s The string to copy
/// @return A strdup'd c string
char *s_copy_c(string_t *s);

/// @brief Allocates a new string using the current allocator from s->cstring and returns that
/// @param s The string to copy
/// @return The c string of s copied into the buffer
char *s_copy_cn(string_t* s, char* buffer, uint32_t n);

/// @brief Copies a string and allocates it using the current allocator
/// @param s The string to copy
/// @return The allocated copy
string_t s_copy_a(string_t *s);

/// @brief Copies a string to a temporary string
/// @param s The string to copy
/// @return The temporary string
string_t *s_copy_p(string_t *s);

/// @brief Trims all characters in trimmed from the right of s in place
/// @param s The string
/// @param trimmed The characters to be trimmed
void s_trimr(string_t* s, const char* trimmed);

/// @brief Trims all characters in trimmed from the right of s and returns a new string
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The newly allocated string with the characters trimmed
string_t s_trimr_a(string_t *s, const char* trimmed);

/// @brief Trims all characters in trimmed from the right of s and returns a string from the string pool
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The string from the string pool with all the characters trimmed
string_t *s_trimr_p(string_t *s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left of s in place
/// @param s The string
/// @param trimmed The characters to be trimmed
void s_triml(string_t* s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left of s and returns a new string
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The newly allocated string with the characters trimmed
string_t s_triml_a(string_t *s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left of s and returns a string from the string pool
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The string from the string pool with all the characters trimmed
string_t *s_triml_p(string_t *s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left and right of s in place
/// @param s The string
/// @param trimmed The characters to be trimmed
void s_trimlr(string_t* s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left and right of s and returns a new string
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The newly allocated string with the characters trimmed
string_t s_trimlr_a(string_t *s, const char* trimmed);

/// @brief Trims all characters in trimmed from the left and right of s and returns a string from the string pool
/// @param s The string
/// @param trimmed The characters to be trimmed
/// @return The string from the string pool with all the characters trimmed
string_t *s_trimlr_p(string_t *s, const char* trimmed);

// Utility macros

#define s_stack(BUFFER) (s_wrap(BUFFER, sizeof(BUFFER)))
#define TRIM_WHITSPACE " \t\n\r\v";