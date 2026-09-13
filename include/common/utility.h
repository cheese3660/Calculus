/******************************************************************************
 *
 *  utility.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/13/2026
 *
 *  Basic C utility macros that the C standard library does not define
 *
 *****************************************************************************/

#pragma once

// min/max defined using expression statements because C doesn't have these built in
#define max(a, b) \
    ({ typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a > _b ? _a : _b; })

#define min(a, b) \
    ({ typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a < _b ? _a : _b; })
