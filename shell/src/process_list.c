#include "process_list.h"
#include <tlhelp32.h>

/* ─────────────────────────────────────────────────────── */
static const char *status_str(ProcStatus s)
{
    switch (s) {
        case PROC_RUNNING: return "Running";
        case PROC_STOPPED: return "Stopped";
        case PROC_DONE:    return "Done";
        default:           return "Unknown";
    }
}

/* ─────────────────────────────────────────────────────── */
void reap_processes(void)
{
    int i;
    for (i = 0; i < MAX_BG_PROCS; i++) {
        DWORD exit_code;
        if (!bg_procs[i].active) continue;
        exit_code = STILL_ACTIVE;
        GetExitCodeProcess(bg_procs[i].hProcess, &exit_code);
        if (exit_code != STILL_ACTIVE) {
            /*
             * Fix #1 — Handle Leak:
             *   Close the process handle as soon as we confirm it has exited.
             *   Waiting until kill() is too late; the process may have already
             *   been gone for many REPL cycles with the handle still open.
             *
             * Fix #2 — Zombie / Array Full:
             *   Mark the slot inactive immediately so it can be reused by the
             *   next background process.  The DONE state used to keep slots
             *   occupied forever with no way to reclaim them.
             */
            printf("\n[%lu] Done (%s)\n",
                   (unsigned long)bg_procs[i].pid,
                   bg_procs[i].cmd);
            CloseHandle(bg_procs[i].hProcess);
            bg_procs[i].hProcess = INVALID_HANDLE_VALUE;
            bg_procs[i].active   = 0;
            bg_count--;
        }
    }
}

/* ─────────────────────────────────────────────────────── */
void list_processes(void)
{
    int i, found = 0;

    reap_processes();

    printf("%-8s %-10s %s\n", "PID", "Status", "Command");
    printf("%-8s %-10s %s\n", "---", "------", "-------");

    for (i = 0; i < MAX_BG_PROCS; i++) {
        if (!bg_procs[i].active) continue;
        printf("%-8lu %-10s %s\n",
               (unsigned long)bg_procs[i].pid,
               status_str(bg_procs[i].status),
               bg_procs[i].cmd);
        found++;
    }
    if (!found) printf("(no background processes)\n");
}

/* ─────────────────────────────────────────────────────── */
static BgProcess *find_proc(DWORD pid)
{
    int i;
    for (i = 0; i < MAX_BG_PROCS; i++) {
        if (bg_procs[i].active && bg_procs[i].pid == pid)
            return &bg_procs[i];
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────── */
int kill_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "kill: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (!TerminateProcess(p->hProcess, 1)) {
        fprintf(stderr, "kill: TerminateProcess failed (error %lu)\n", (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(p->hProcess);
    p->active = 0;
    bg_count--;
    printf("[%lu] Killed\n", (unsigned long)pid);
    return 0;
}

/* ─────────────────────────────────────────────────────── */
/*
 * stop_process / resume_process:
 *   Iterate threads of the target process and Suspend/Resume each one.
 *   Uses CreateToolhelp32Snapshot — no undocumented APIs needed.
 */
static int toggle_process_threads(DWORD pid, int suspend)
{
    HANDLE snap;
    THREADENTRY32 te;
    int found = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;

    te.dwSize = sizeof(te);

    if (Thread32First(snap, &te)) {
        do {
            HANDLE ht;
            if (te.th32OwnerProcessID != pid) continue;
            ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!ht) continue;
            if (suspend)
                SuspendThread(ht);
            else
                ResumeThread(ht);
            CloseHandle(ht);
            found = 1;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return found ? 0 : -1;
}

int stop_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "stop: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (toggle_process_threads(pid, 1) < 0) {
        fprintf(stderr, "stop: failed to suspend process %lu\n", (unsigned long)pid); return -1;
    }
    p->status = PROC_STOPPED;
    printf("[%lu] Stopped\n", (unsigned long)pid);
    return 0;
}

int resume_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "resume: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (toggle_process_threads(pid, 0) < 0) {
        fprintf(stderr, "resume: failed to resume process %lu\n", (unsigned long)pid); return -1;
    }
    p->status = PROC_RUNNING;
    printf("[%lu] Resumed\n", (unsigned long)pid);
    return 0;
}

/* ─────────────────────────────────────────────────────── */
void kill_all_processes(void)
{
    int i;
    for (i = 0; i < MAX_BG_PROCS; i++) {
        if (!bg_procs[i].active) continue;
        TerminateProcess(bg_procs[i].hProcess, 1);
        CloseHandle(bg_procs[i].hProcess);
        bg_procs[i].active = 0;
    }
    bg_count = 0;
}
