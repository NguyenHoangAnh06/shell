#ifndef PROCESS_LIST_H
#define PROCESS_LIST_H

#include "shell.h"

/* Print all background processes (PID, status, cmd) */
void list_processes(void);

/* Terminate a background process by PID */
int kill_process(DWORD pid);

/* Suspend a background process by PID */
int stop_process(DWORD pid);

/* Resume a suspended background process by PID */
int resume_process(DWORD pid);

/* Clean up finished processes from the list */
void reap_processes(void);

/* Kill all background processes (called on shell exit) */
void kill_all_processes(void);

#endif /* PROCESS_LIST_H */
