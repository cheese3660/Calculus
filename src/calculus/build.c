// This is the main calculus build file

#include "common/fs.h"
#include "common/debug.h"

#include "calculus/build.h"
#include "calculus/paths.h"

static void ensure_paths()
{
    static bool paths_ensured = false;
    if (paths_ensured)
        return;
    paths_ensured = true;

    if (fs_ensure_dir(CALCULUS_LOGS_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");
    
    if (fs_ensure_dir(CALCULUS_BUILD_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");
        
    if (fs_ensure_dir(CALCULUS_STORE_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");
}

int build_derivative(derivative_header_t *to_build)
{
    (void)to_build;
    ensure_paths();
    return 0;
}