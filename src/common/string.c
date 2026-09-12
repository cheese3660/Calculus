/******************************************************************************
 *
 *  string.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  Implementation of a basic dynamic string library with a string pool
 *
 *****************************************************************************/

#include <errno.h>
#include <stdarg.h>
#include <stdbit.h>
#include <string.h>

#include "common/debug.h"
#include "common/string.h"

// Let's set it up so that we just have a default allocator for now
// We don't need much more than this
static struct string_allocator default_allocator = {
    realloc,
    free};

struct string_allocator *s_current_alloc = &default_allocator;

// We have an empty pool of 64 strings for temporary use
static string_t pool[64];
static uint64_t used_slots;

static inline size_t get_free_pool_index()
{
    size_t index = stdc_trailing_zeros(used_slots);

    if (index > 64)
        panic("ran out of string pool slots!");

    return index;
}

void s_free(string_t *string)
{
    string->length = 0;
    string->cstring[0] = 0;

    if (string > &pool[0] && string - &pool[0] < 64)
    {
        size_t index = string - &pool[0];

        if (string->capacity > STRINGPOOL_MAX_HELD_CAPACITY)
        {
            // We reallocate this to the max held block size in the string pool
            string->cstring = string->allocator->alloc(string->cstring, STRINGPOOL_MAX_HELD_CAPACITY + 1);
            if (string->cstring == nullptr)
                panic("error allocating string of size %d: %s", STRINGPOOL_MAX_HELD_CAPACITY, strerror(errno));
            string->capacity = STRINGPOOL_MAX_HELD_CAPACITY;
        }

        used_slots &= ~(1 << index);
    }
    else
    {
        if (string->allocator != nullptr)
            string->allocator->dealloc(string->cstring);
    }
}

string_t s_new_a(uint32_t capacity)
{
    // Always convert the capacity to the next power of 2
    capacity = stdc_bit_ceil(capacity);

    string_t result;
    result.allocator = s_current_alloc;
    result.length = 0;
    result.capacity = capacity;
    result.cstring = s_current_alloc->alloc(nullptr, capacity + 1 /* for the nullptr */);
    if (result.cstring == nullptr)
        panic("error allocating string of size %d: %s", capacity, strerror(errno));
    result.cstring[0] = 0;
    return result;
}

string_t *s_new_p(uint32_t capacity)
{
    size_t index = get_free_pool_index();

    if (pool[index].allocator == nullptr)
    {
        pool[index].allocator = s_current_alloc;
    }
    pool[index].length = 0;

    if (pool[index].capacity < capacity)
    {
        capacity = stdc_bit_ceil(capacity);
        pool[index].capacity = capacity;
        pool[index].cstring = s_current_alloc->alloc(nullptr, capacity + 1);
        if (pool[index].cstring == nullptr)
            panic("error allocating string of size %d: %s", capacity, strerror(errno));
    }
    pool[index].cstring[0] = 0;
    used_slots |= (1 << index);
    return &pool[index];
}

string_t s_own_a(const char *original)
{
    size_t len = strlen(original);
    return s_ownl_a(original, len);
}

string_t *s_own_p(const char *original)
{
    size_t len = strlen(original);
    return s_ownl_p(original, len);
}

string_t s_ownl_a(const char *original, uint32_t len)
{
    string_t result = s_new_a(len);
    memcpy(result.cstring, original, len);
    result.length = len;
    result.cstring[len + 1] = 0;
    return result;
}

string_t *s_ownl_p(const char *original, uint32_t len)
{
    string_t *result = s_new_p(len);
    memcpy(result->cstring, original, len);
    result->length = len;
    result->cstring[len + 1] = 0;
    return result;
}

string_t s_fmt_a(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // This always uses the default allocator
    string_t result;
    result.allocator = &default_allocator; // Because it uses malloc we have to use free
    int len = vasprintf(&result.cstring, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    result.capacity = result.length = len;
    return result;
}

string_t *s_fmt_p(uint32_t max_len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // This always uses the default allocator
    string_t *result = s_new_p(max_len);
    int len = vsnprintf(result->cstring, max_len + 1, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    result->length = len;
    return result;
}

string_t s_view(char *original)
{
    return s_viewl(original, strlen(original));
}

string_t s_viewl(char *original, uint32_t len)
{
    string_t result;
    result.allocator = nullptr;
    result.capacity = len;
    result.length = len;
    result.cstring = original;
    return result;
}

string_t s_wrap(char *buffer, uint32_t buffer_capacity)
{
    string_t result;
    result.allocator = nullptr;
    result.capacity = buffer_capacity - 1;
    result.length = 0;
    result.cstring = buffer;
    buffer[0] = 0;
    return result;
}

void s_set(string_t *s, const char* value)
{
    s_setl(s, value, strlen(value));
}

void s_setl(string_t *s, const char* value, uint32_t len)
{
    s_reserve(s, len);
    s->length = len;
    memcpy(s->cstring, value, len + 1 /* Null byte */);
}

void s_sets(string_t *s, string_t* value)
{
    s_setl(s, value->cstring, value->length);
}

void s_reserve(string_t *s, uint32_t len)
{
    if (s->capacity >= len)
        return;
    if (s->allocator == nullptr)
        panic("Attempting to reserve extra space on a string with no allocator!");

    // Make sure we are a power of 2
    len = stdc_bit_ceil(len);
    s->capacity = len;
    s->cstring = s->allocator->alloc(s->cstring, len + 1);
    if (s->cstring == nullptr)
        panic("error reallocating string of size %d: %s", len, strerror(errno));
}

bool s_startswith(string_t *s, const char *prefix)
{
    // <= so that we get the null byte
    for (uint32_t i = 0; i <= s->length; i++)
    {
        // In this case we know it to be the case that it does
        if (prefix[i] == 0)
            return true;
        if (prefix[i] != s->cstring[i])
            return false;
    }
    return false;
}

bool s_startswiths(string_t *s, string_t *prefix)
{
    if (s->length < prefix->length)
        return false;
    return s_startswith(s, prefix->cstring);
}

bool s_endswith(string_t *s, const char *postfix)
{
    uint32_t postfix_length = strlen(postfix);
    if (s->length < postfix_length)
        return false;
    return memcmp(s->cstring + s->length - postfix_length, postfix, postfix_length) == 0;
}

bool s_endswiths(string_t *s, string_t *postfix)
{
    if (s->length < postfix->length)
        return false;
    return memcmp(s->cstring + s->length - postfix->length, postfix->cstring, postfix->length) == 0;
}

void s_cat(string_t *s, const char *addition)
{
    uint32_t addition_len = strlen(addition);
    s_reserve(s, s->length + addition_len);
    memcpy(s->cstring + s->length, addition, addition_len + 1 /* Null byte */);
    s->length += addition_len;
}

string_t s_cat_a(string_t *s, const char *addition)
{
    uint32_t addition_len = strlen(addition);
    string_t result = s_new_a(s->length + addition_len);
    memcpy(result.cstring, s->cstring, s->length);
    memcpy(result.cstring + s->length, addition, addition_len + 1 /* Null byte */);
    result.length = s->length + addition_len;
    return result;
}

string_t *s_cat_p(string_t *s, const char *addition)
{
    uint32_t addition_len = strlen(addition);
    string_t *result = s_new_p(s->length + addition_len);
    memcpy(result->cstring, s->cstring, s->length);
    memcpy(result->cstring + s->length, addition, addition_len + 1 /* Null byte */);
    result->length = s->length + addition_len;
    return result;
}

void s_cats(string_t *s, string_t *addition)
{
    s_reserve(s, s->length + addition->length);
    memcpy(s->cstring + s->length, addition->cstring, addition->length + 1 /* Null byte */);
    s->length += addition->length;
}

string_t s_cats_a(string_t *s, string_t *addition)
{
    string_t result = s_new_a(s->length + addition->length);
    memcpy(result.cstring, s->cstring, s->length);
    memcpy(result.cstring + s->length, addition->cstring, addition->length + 1 /* Null byte */);
    result.length = s->length + addition->length;
    return result;
}

string_t *s_cats_p(string_t *s, string_t *addition)
{
    string_t *result = s_new_p(s->length + addition->length);
    memcpy(result->cstring, s->cstring, s->length);
    memcpy(result->cstring + s->length, addition->cstring, addition->length + 1 /* Null byte */);
    result->length = s->length + addition->length;
    return result;
}

void s_catfa(string_t *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *result;
    int len = vasprintf(&result, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    s_reserve(s, s->length + len);
    memcpy(s->cstring + s->length, result, len + 1 /* Null byte */);
    s->length += len;
}

void s_catfn(string_t *s, uint32_t max_format, const char *format, ...)
{
    s_reserve(s, s->length + max_format);
    va_list args;
    va_start(args, format);
    int len = vsnprintf(s->cstring + s->length, max_format + 1 /* Null byte */, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    s->length += len;
}

string_t s_catfa_a(string_t *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *res;
    int len = vasprintf(&res, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    string_t result = s_new_a(s->length + len);
    memcpy(result.cstring, s->cstring, s->length);
    memcpy(result.cstring + s->length, res, len + 1 /* Null byte */);
    result.length = s->length + len;
    return result;
}

string_t s_catfn_a(string_t *s, uint32_t max_format, const char *format, ...)
{
    string_t result = s_new_a(s->length + max_format);
    memcpy(result.cstring, s->cstring, s->length);
    va_list args;
    va_start(args, format);
    int len = vsnprintf(result.cstring + s->length, max_format + 1 /* Null byte */, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    result.length = s->length + len;
    return result;
}

string_t *s_catfa_p(string_t *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *res;
    int len = vasprintf(&res, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    string_t *result = s_new_p(s->length + len);
    memcpy(result->cstring, s->cstring, s->length);
    memcpy(result->cstring + s->length, res, len + 1 /* Null byte */);
    result->length = s->length + len;
    return result;
}

string_t *s_catfn_p(string_t *s, uint32_t max_format, const char *format, ...)
{
    string_t *result = s_new_p(s->length + max_format);
    memcpy(result->cstring, s->cstring, s->length);
    va_list args;
    va_start(args, format);
    int len = vsnprintf(result->cstring + s->length, max_format + 1 /* Null byte */, format, args);
    va_end(args);
    if (len == -1)
        panic("Error creating formatted string: %s", strerror(errno));
    result->length = s->length + len;
    return result;
}

char *s_take(string_t *s)
{
    char *copy = s_current_alloc->alloc(nullptr, s->length + 1 /* Null byte */);
    if (copy == nullptr)
        panic("Error creating buffer to take string: %s", strerror(errno));
    memcpy(copy, s->cstring, s->length + 1 /* Null byte */);
    s_free(s);
    return copy;
}

char *s_taken(string_t* s, char* buffer, uint32_t n)
{
    uint32_t n1 = s->length + 1;
    n = n > n1 ? n1 : n;
    memcpy(buffer, s->cstring, n);
    // Make sure it's null terminated
    buffer[n - 1] = 0;
    s_free(s);
    return buffer;
}

char *s_copy_c(string_t *s)
{
    char *copy = s_current_alloc->alloc(nullptr, s->length + 1 /* Null byte */);
    if (copy == nullptr)
        panic("Error creating buffer to take string: %s", strerror(errno));
    memcpy(copy, s->cstring, s->length + 1 /* Null byte */);
    return copy;
}

char *s_copy_cn(string_t *s, char* buffer, uint32_t n)
{
    uint32_t n1 = s->length + 1;
    n = n > n1 ? n1 : n;
    memcpy(buffer, s->cstring, n);
    // Make sure it's null terminated
    buffer[n - 1] = 0;
    return buffer;
}

string_t s_copy_a(string_t *s)
{
    string_t copy = s_new_a(s->length);
    memcpy(copy.cstring, s->cstring, s->length + 1 /* Null byte */);
    return copy;
}

string_t *s_copy_p(string_t *s)
{
    string_t *copy = s_new_p(s->length);
    memcpy(copy->cstring, s->cstring, s->length + 1 /* Null byte */);
    return copy;
}

static uint32_t trimr_length(string_t *s, const char *trimmed)
{
    uint32_t l = s->length;
    while (l != 0)
    {
        for (const char *c = trimmed; *c != 0; c++)
        {
            if (*c == s->cstring[l - 1])
            {
                l -= 1;
                break;
            }
        }
    }
    return l;
}

void s_trimr(string_t *s, const char *trimmed)
{
    s->cstring[s->length = trimr_length(s, trimmed)] = 0;
}

string_t s_trimr_a(string_t *s, const char *trimmed)
{
    uint32_t l = trimr_length(s, trimmed);
    string_t new = s_new_a(l);
    memcpy(new.cstring, s->cstring, l);
    new.cstring[new.length = l] = 0;
    return new;
}

string_t *s_trimr_p(string_t *s, const char *trimmed)
{
    uint32_t l = trimr_length(s, trimmed);
    string_t *new = s_new_p(l);
    memcpy(new->cstring, s->cstring, l);
    new->cstring[new->length = l] = 0;
    return new;
}

static uint32_t triml_offset(string_t *s, const char *trimmed)
{
    uint32_t o = 0;
    while (o < s->length)
    {
        for (const char *c = trimmed; *c != 0; c++)
        {
            if (*c == s->cstring[o])
            {
                o += 1;
                break;
            }
        }
    }
    return o;
}

void s_triml(string_t *s, const char *trimmed)
{
    uint32_t o = triml_offset(s, trimmed);
    memmove(s->cstring, s->cstring + o, (s->length - o) + 1 /* null byte */);
    s->length -= o;
}

string_t s_triml_a(string_t *s, const char *trimmed)
{
    uint32_t o = triml_offset(s, trimmed);
    string_t new = s_new_a(s->length - o);
    memcpy(new.cstring, s->cstring + o, (s->length - o) + 1 /* null byte */);
    new.length = s->length - o;
    return new;
}

string_t *s_triml_p(string_t *s, const char *trimmed)
{
    uint32_t o = triml_offset(s, trimmed);
    string_t *new = s_new_p(s->length - o);
    memcpy(new->cstring, s->cstring + o, (s->length - o) + 1 /* null byte */);
    new->length = s->length - o;
    return new;
}

void s_trimlr(string_t *s, const char *trimmed)
{
    s_trimr(s, trimmed);
    // We do the right trim first to reduce the amount moved in the left trim
    s_triml(s, trimmed);
}

string_t s_trimlr_a(string_t *s, const char *trimmed)
{
    uint32_t l = trimr_length(s, trimmed);
    uint32_t o = triml_offset(s, trimmed);
    if (o >= l)
        return s_new_a(0);
    string_t new = s_new_a(l - o);
    memcpy(new.cstring, s->cstring + o, l - o);
    new.cstring[new.length = (l - o)] = 0;
    return new;
}

string_t *s_trimlr_p(string_t *s, const char *trimmed)
{
    uint32_t l = trimr_length(s, trimmed);
    uint32_t o = triml_offset(s, trimmed);
    if (o >= l)
        return s_new_p(0);
    string_t *new = s_new_p(l - o);
    memcpy(new->cstring, s->cstring + o, l - o);
    new->cstring[new->length = (l - o)] = 0;
    return new;
}
