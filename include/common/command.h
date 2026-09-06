#pragma once

/// @brief Run a command
/// @param args The args to the command, args[0] is the command itself
/// @return The exit code of the command 
int command_run(const char **args);