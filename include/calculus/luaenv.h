#pragma once
/*
 *  Structures and functions for the Calculus Lua environment
 */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Integration is done by the lua environment
// All this needs is the primitives to set up that lua environment

/// @brief Set up a new lua environment, if one is already setup it errors
void env_setup();

/// @brief Run a script at a given path, returns whether it succeeded or not
/// @param path The script to run
int env_run(const char* path);

/// @brief Teardown a lua environment, nop if nothing is set up
void env_teardown();