#ifndef BUILTINS_H
#define BUILTINS_H

#include "shell.h"

/*
 * is_builtin:
 *   Returns 1 if cmd->argv[0] is a recognised built-in, else 0.
 */
int is_builtin(const ParsedCmd *cmd);

/*
 * run_builtin:
 *   Executes the built-in command.
 *   Returns 1 if the shell should exit, else 0.
 */
int run_builtin(ParsedCmd *cmd);

#endif /* BUILTINS_H */
