#ifndef PROCESS_H
#define PROCESS_H

#include "shell.h"

/*
 * run_process:
 *   Executes the command described by 'cmd'.
 *   - Foreground: waits for child to exit.
 *   - Background: registers child in bg_procs[], returns immediately.
 *   Automatically prepends "cmd /c" for .bat/.cmd files.
 */
void run_process(ParsedCmd *cmd);

#endif /* PROCESS_H */
