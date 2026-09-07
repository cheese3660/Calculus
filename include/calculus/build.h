#pragma once

#include "calculus/derivative.h"

/// @brief Build a derivative
/// @param to_build The derivative to build
/// @return 0 if the build succeeds, the build processes error code otherwise, printing a log to stdout and to the log path
int build_derivative(derivative_header_t* to_build);
