/******************************************************************************
 *
 *  paths.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

#pragma once

// This definition here is such that when we eventually want to build stuff on the actual root of the system
// #define USE_RELATIVE_PATHS
// I've decided root of the system is what we want from now on, so we can easily do testing

#define CALCULUS_TRUE_PREFIX "/calc/"

// This all will be changed to our actual paths once we actually have them
#ifdef USE_RELATIVE_PATHS
    #define CALCULUS_PATH_PREFIX "./.rel/"
#else
    #define CALCULUS_PATH_PREFIX "/calc/"
#endif

#define GIT_CACHE_DIRECTORY CALCULUS_PATH_PREFIX "git_cache"
#define CALCULUS_BUILD_DIRECTORY CALCULUS_PATH_PREFIX "build"
#define CALCULUS_STORE_DIRECTORY CALCULUS_PATH_PREFIX "str"
#define CALCULUS_LOGS_DIRECTORY CALCULUS_PATH_PREFIX "logs"
#define CALCULUS_STATE_DIRECTORY CALCULUS_PATH_PREFIX "var"
#define CALCULUS_RECIPES_DIRECTORY CALCULUS_PATH_PREFIX "recipes"

// This is the system index that indexes the current system and what build dependencies are needed
// and/or what transient dependencies are needed
// it is updated every single time a derivation recipe is added to the recipe book
// o
#define CALCULUS_INDEX_FILE CALCULUS_STATE_DIRECTORY "/index"


#define CHROOT_STORE_DIRECTORY CALCULUS_TRUE_PREFIX "str"
