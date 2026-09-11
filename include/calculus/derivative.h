/******************************************************************************
 *
 *  derivative.h
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/11/2026
 *
 *****************************************************************************/

#pragma once

#include <common/crypto.h>

// This represents a general "derivative", i.e. a final package
// Of which there are 2 forms

// Fixed - Which we verify hashes of and what not
// Built - Which get the chroot'd jail
// We will start with a very simple fixed derivative for now

typedef enum
{
    // Used for derivations that just run a script on the system
    // The standard type of derivation
    DT_STANDARD,
    // Used for derivations that fetch a tarball
    DT_FETCH_TARBALL,
    // Other types will go here later but this is what is needed for right now
} derivative_type_t;

typedef struct
{
    derivative_type_t dtype;
    sha256_t dhash;
} derivative_header_t;

typedef struct
{
    derivative_header_t dheader;
    // These are all the build inputs used to make this derivative, order matters here as it does change the environment variables if changed
    // The order here defines the order of the $DEPS environment variable, which will affect the order of the $PATH environment variable as well
    size_t num_dependencies;
    derivative_header_t **dependencies;
    // This is what goes after the hash- part, a name for the derivative, and the name that is used when constructing the $DEP_... environment variable in the build scripts
    char *name;
    // This is the actual build script that will be run using bash (if available) or sh (when bootstrapping) if neither are then that is an error as there always needs to be at least one shell
    char *build;
} standard_derivative_t;

/// @brief Create a standard derivative recipe (or return one if it already exists)
/// @param num_dependencies The length of the dependencies array
/// @param dependencies The derivatives that need to be integrated to make this derivative, will get free()'d by this function or ownership taken over
/// @param name The name of the derivative (used for environment variables) will be strdup'd
/// @param build The build script that actually builds the derivative will be strdup'd
/// @return A derivative recipe for the given derivative
standard_derivative_t *create_standard_derivative(
    size_t num_dependencies,
    derivative_header_t **dependencies,
    const char *name,
    const char *build);

// Tarballs don't have a name, they are always created as a file with the <hash> in the store
// when extract is set to true they are then automatically extracted into a folder named <hash> in the store instead
//
// This allows for 2 tarballs with the same hash downloaded from 2 different sources to become one, as these are content addressed
typedef struct
{
    derivative_header_t dheader;
    char *url;
    bool extract;
} fetch_tarball_derivative_t;

/// @brief Create a tarball derivative recipe (or return one if it already exists)
/// @param url The url of the tarball to download
/// @param hash The expected SHA256 sum of the tarball as a hex string
/// @param extract Whether the tarball should be pre-extracted to a folder - this almost certainly should only be used for the bootstrap tarball
/// @return A derivative recipe to download the given tarball
fetch_tarball_derivative_t *create_fetch_tarball_derivative(
    const char *url,
    const char *hash,
    bool extract);

/// @brief Request a derivative to be built by the system
/// @param derivative The derivative to request to be built
void request_derivative(derivative_header_t *derivative);

/// @brief Get all the requested derivatives to be built by the system
/// @param len A pointer to where the length of the allocated array will be stored
/// @return An array containing all of the requested derivatives
derivative_header_t **get_requested_derivatives(size_t *len);

/// @brief Get the node name a string
/// @param derivative The derivative to get the node name
/// @return A string representing the node name this can get clobbered in successive calls
const char* get_derivative_node_name(derivative_header_t* derivative);

/// @brief Get the store path as a string
/// @param derivative The derivative to get the store path of
/// @return A string representing the store path, this can get clobbered in successive calls
const char* get_derivative_store_path(derivative_header_t* derivative);

/// @brief Gets the set of derivatives that actually need to be built
/// @param wanted The wanted set of derivatives
/// @param len The length of the wanted set of derivatives
/// @return An stb_ds array of derivatives to be built
derivative_header_t **get_buildstack(derivative_header_t **wanted, size_t len);

/// @brief Get the amount of steps in the build stack
/// @param stack The stack
/// @return The length of the build stack
size_t buildstack_len(derivative_header_t** stack);

/// @brief Get the next item from a build stack
/// @param stack The stack
/// @return The next item, or nullptr if there isn't one
derivative_header_t *buildstack_next(derivative_header_t **stack);

/// @brief Free a build stack
/// @param stack The build stack to free
void buildstack_free(derivative_header_t** stack);