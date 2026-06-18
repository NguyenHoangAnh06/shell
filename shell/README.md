# myShell

A tiny interactive shell for Windows, written in C99 using the Win32 API.

## Features

| Feature | Details |
|---|---|
| Foreground execution | Shell waits for the child process to finish |
| Background execution | Append `&` — shell continues immediately |
| Process management | `list`, `kill`, `stop`, `resume` for background processes |
| CTRL+C handling | Handled via `SetConsoleCtrlHandler` + `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)` sent to the child's process group only — shell stays alive |
| Batch file support | `.bat` files are handled by prepending `cmd /c` to the command line |
| Built-in commands | `exit`, `help`, `date`, `time`, `cd`, `dir`, `path`, `addpath` |
| Error reporting | All Win32 errors displayed as human-readable messages via `FormatMessageA` |

## Requirements

- **MinGW-w64** (`gcc` on PATH) — <https://www.mingw-w64.org/>
- Windows 7 or later

## Build

```sh
cd C:\Users\ADMIN\Documents\shell

# Option 1 — compile directly
gcc -Wall -O2 -std=c99 -D_WIN32_WINNT=0x0600 -Isrc -o myShell.exe ^
    src/main.c src/globals.c src/parser.c src/process.c ^
    src/process_list.c src/builtins.c src/shell_utils.c -lkernel32

# Option 2 — via make
mingw32-make
```

## Usage

```
myShell.exe
```

### Built-in commands

```
help                 Show help screen
exit                 Exit the shell (terminates all background processes)
date                 Print current date  (YYYY-MM-DD)
time                 Print current time  (HH:MM:SS)
cd  [path]           Change working directory (built-in; no arg prints CWD)
dir [path]           List directory contents (uses cmd /c dir internally)
list                 List background processes (PID, status, command)
kill  <pid>          Terminate a background process
stop  <pid>          Suspend a background process
resume <pid>         Resume a suspended background process
path                 Show PATH variable (one entry per line)
addpath <dir>        Append <dir> to PATH
```

> **Note on `addpath`**: Changes are local to the current shell session only.
> They are **not** written to the Windows Registry and will be lost when the
> shell exits. Child processes launched after `addpath` inherit the updated
> PATH because they share the shell's environment block.

### Background processes

```
notepad &            # start in background → prints [PID] notepad &
list                 # shows PID, status, command
stop 1234            # suspend all threads of PID 1234
resume 1234          # resume
kill 1234            # terminate
```

### CTRL+C

Pressing CTRL+C while a foreground process is running sends
`CTRL_BREAK_EVENT` to the foreground child's **process group** only.
The shell itself is not affected and returns to the prompt.

### Batch files

Files ending in `.bat` are automatically handled via `cmd /c`, for example:

```
myShell> test.bat
```

### Running the test suite

```sh
# Pipe the test script into myShell
.\myShell.exe < test.bat
```

---

## Project Structure

```
shell/
├── Makefile
├── README.md
├── test.bat              Regression / demonstration test suite
└── src/
    ├── shell.h           Shared types and constants
    ├── globals.c         Single definition of all global variables
    ├── shell_utils.h/.c  Centralised Win32 error reporter (shell_perror)
    ├── parser.h/.c       Command-line tokeniser (quotes, & detection)
    ├── process.h/.c      CreateProcess wrapper (fg/bg + .bat handling)
    ├── process_list.h/.c Background process registry (list/kill/stop/resume)
    ├── builtins.h/.c     Built-in command handlers (3 sections)
    └── main.c            REPL loop, CTRL+C handler
```

---

## Process Status Definitions

| Status | Meaning | Policy |
|---|---|---|
| `Running` | Process is alive and not suspended | Active slot in `bg_procs` |
| `Stopped` | All threads suspended by `stop <pid>` | Active slot; use `resume` to continue |
| `Done` | `GetExitCodeProcess` returned a value other than `STILL_ACTIVE` | **Slot is freed automatically** at the next REPL cycle via `reap_processes()`; handle is closed immediately |

Completed processes are automatically removed at the start of every prompt cycle — you do not need to `kill` them.

---

## Known Limitations

### CTRL+C / signal routing
The current implementation sends `CTRL_BREAK_EVENT` (not `CTRL_C_EVENT`) to the
foreground child's process group. This is intentional: processes created with
`CREATE_NEW_PROCESS_GROUP` have `CTRL_C_EVENT` blocked by Windows, so
`CTRL_BREAK_EVENT` is the correct mechanism for sending an interrupt. Well-behaved
programs handle `CTRL_BREAK_EVENT` and clean up; programs that ignore it may need
to be killed via `kill <pid>` instead.

### stop / resume — potential deadlock risk
`stop` and `resume` are implemented by iterating a `CreateToolhelp32Snapshot`
and calling `SuspendThread` / `ResumeThread` on every thread of the target process.
This approach is acceptable at the academic level but has two known risks in production:

1. **Deadlock**: If a thread is suspended while holding the heap lock or a critical
   section, other threads attempting to allocate memory will deadlock indefinitely.
2. **Missed threads**: If the target process spawns new threads *after* the snapshot
   is taken, those threads will not be suspended and the process may continue running.

These are inherent limitations of the `SuspendThread`-based approach on Windows
(there is no `SIGSTOP` equivalent). A production shell would use Job Objects or
a dedicated debugging API for more reliable process control.

### Architecture note — command dispatcher
The dispatch logic (built-in vs. external, foreground vs. background) is currently
split between `main.c` and `builtins.c`. In a larger project this should be
consolidated into a single `dispatch()` function with a command-table struct
(name + function pointer) to make adding new commands trivial.

### addpath scope
PATH changes via `addpath` only affect the current shell session. They are not
persisted to the Windows Registry (`HKCU\Environment` or `HKLM\SYSTEM\...`).
