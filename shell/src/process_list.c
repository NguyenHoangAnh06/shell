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
        if (!GetExitCodeProcess(bg_procs[i].hProcess, &exit_code)) {
            fprintf(stderr, "list: GetExitCodeProcess failed for PID %lu (error %lu)\n",
                    (unsigned long)bg_procs[i].pid,
                    (unsigned long)GetLastError());
            continue;
        }
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
static int toggle_process_tree_threads(DWORD root_pid, int suspend);
static int terminate_process_tree(BgProcess *p);
int kill_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "kill: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (terminate_process_tree(p) < 0) {
        fprintf(stderr, "kill: TerminateProcess failed for process tree (error %lu)\n", (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(p->hProcess);
    p->hProcess = INVALID_HANDLE_VALUE;
    p->active = 0;
    p->status = PROC_DONE;
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
    DWORD *thread_ids = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t changed = 0;
    int result = -1;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;

    te.dwSize = sizeof(te);

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (count == capacity) {
                size_t new_capacity = capacity ? capacity * 2 : 16;
                DWORD *new_ids = (DWORD *)realloc(
                    thread_ids, new_capacity * sizeof(DWORD));
                if (!new_ids) {
                    CloseHandle(snap);
                    free(thread_ids);
                    return -1;
                }
                thread_ids = new_ids;
                capacity = new_capacity;
            }
            thread_ids[count++] = te.th32ThreadID;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    if (count == 0) {
        free(thread_ids);
        return -1;
    }

    for (changed = 0; changed < count; changed++) {
        HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE,
                               thread_ids[changed]);
        DWORD rc;
        if (!ht) break;
        rc = suspend ? SuspendThread(ht) : ResumeThread(ht);
        CloseHandle(ht);
        if (rc == (DWORD)-1) break;
    }

    if (changed == count) {
        result = 0;
    } else {
        /*
         * Roll back threads already changed so the process and our status
         * table cannot disagree after a partial failure.
         */
        while (changed > 0) {
            HANDLE ht;
            changed--;
            ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE,
                            thread_ids[changed]);
            if (!ht) continue;
            if (suspend)
                ResumeThread(ht);
            else
                SuspendThread(ht);
            CloseHandle(ht);
        }
    }

    free(thread_ids);
    return result;
}


static int pid_in_array(const DWORD *pids, size_t count, DWORD pid)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (pids[i] == pid) return 1;
    }
    return 0;
}

static size_t collect_process_tree(DWORD root_pid, DWORD *pids, size_t max_pids)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    size_t count = 0;
    int changed;

    if (max_pids == 0) return 0;
    pids[count++] = root_pid;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return count;

    do {
        changed = 0;
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (count >= max_pids) break;
                if (pid_in_array(pids, count, pe.th32ProcessID)) continue;
                if (pid_in_array(pids, count, pe.th32ParentProcessID)) {
                    pids[count++] = pe.th32ProcessID;
                    changed = 1;
                }
            } while (Process32Next(snap, &pe));
        }
    } while (changed && count < max_pids);

    CloseHandle(snap);
    return count;
}

static int toggle_process_tree_threads(DWORD root_pid, int suspend)
{
    DWORD pids[256];
    size_t count;
    size_t changed = 0;
    size_t i;

    count = collect_process_tree(root_pid, pids, 256);
    if (count == 0) return -1;

    for (i = 0; i < count; i++) {
        if (toggle_process_threads(pids[i], suspend) < 0) break;
        changed++;
    }

    if (changed == count) return 0;

    while (changed > 0) {
        changed--;
        toggle_process_threads(pids[changed], !suspend);
    }
    return -1;
}

static int terminate_process_tree(BgProcess *p)
{
    DWORD pids[256];
    size_t count;
    size_t i;
    int failed = 0;

    count = collect_process_tree(p->pid, pids, 256);
    if (count == 0) return -1;

    for (i = count; i > 0; i--) {
        DWORD target_pid = pids[i - 1];
        HANDLE hp;

        if (target_pid == p->pid) {
            hp = p->hProcess;
        } else {
            hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, target_pid);
            if (!hp) {
                failed = 1;
                continue;
            }
        }

        if (!TerminateProcess(hp, 1)) failed = 1;
        if (target_pid != p->pid) CloseHandle(hp);
    }

    return failed ? -1 : 0;
}

int stop_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "stop: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (p->status == PROC_STOPPED) {
        fprintf(stderr, "stop: process %lu is already stopped\n", (unsigned long)pid);
        return -1;
    }
    if (toggle_process_tree_threads(pid, 1) < 0) {
        fprintf(stderr, "stop: failed to suspend process tree rooted at %lu\n", (unsigned long)pid); return -1;
    }
    p->status = PROC_STOPPED;
    printf("[%lu] Stopped\n", (unsigned long)pid);
    return 0;
}

int resume_process(DWORD pid)
{
    BgProcess *p = find_proc(pid);
    if (!p) { fprintf(stderr, "resume: no background process with PID %lu\n", (unsigned long)pid); return -1; }
    if (p->status != PROC_STOPPED) {
        fprintf(stderr, "resume: process %lu is not stopped\n", (unsigned long)pid);
        return -1;
    }
    if (toggle_process_tree_threads(pid, 0) < 0) {
        fprintf(stderr, "resume: failed to resume process tree rooted at %lu\n", (unsigned long)pid); return -1;
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
        bg_procs[i].hProcess = INVALID_HANDLE_VALUE;
        bg_procs[i].active = 0;
        bg_procs[i].status = PROC_DONE;
    }
    bg_count = 0;
}
