# myShell

A small interactive Windows shell written in C99 with the Win32 API.

## Features

- Foreground and background process execution.
- Background process management with `list`, `kill`, `stop`, and `resume`.
- Built-ins: `exit`, `help`, `date`, `time`, `cd`, `dir`, `path`, `addpath`.
- UTF-8 console input/output and UTF-16 Win32 filesystem/process APIs.
- Native `dir` implementation; arguments are not passed through `cmd.exe`.
- `.bat`/`.cmd` support through `cmd.exe`, with dangerous CMD
  metacharacters rejected in script arguments.
- Human-readable Win32 errors.
- Overlong input lines are discarded instead of being executed as a second
  command.
- Interactive command suggestions, `Tab` completion, and prefix-filtered
  command history with the Up/Down arrow keys.

## Requirements

- Windows 7 or later.
- MinGW-w64 GCC.
- `mingw32-make` for the Makefile targets, or GCC for direct compilation.

## Build

```bat
mingw32-make
```

Direct compilation:

```bat
gcc -Wall -Wextra -O2 -std=c99 -D_WIN32_WINNT=0x0600 -Isrc ^
  -o myShell.exe src/main.c src/globals.c src/parser.c src/process.c ^
  src/process_list.c src/builtins.c src/line_editor.c ^
  src/shell_utils.c -lkernel32 -luser32
```

## Usage

```text
help
cd "C:\path with spaces"
dir .
notepad &
list
stop <pid>
resume <pid>
kill <pid>
path
addpath "C:\new tools"
test.bat hello
exit
```

`addpath` changes only the current shell process. Child processes started
afterward inherit the updated `PATH`; the Windows Registry is not modified.

Built-ins cannot run in the background.

## Interactive editing

- Type a unique command prefix such as `ki` to see `kill` as a dim suggestion.
- Press `Tab` to accept the suggestion.
- Press Right Arrow at the end of the line to accept one suggested character.
- Type a prefix and press Up Arrow to search older matching commands.
- Press Down Arrow to move toward newer matching commands and eventually
  restore the prefix originally typed.
- Left/Right, Home, End, Backspace, and Delete edit the current line.

History is kept for the current shell session only.

## Tests

Build `myShell.exe`, then run:

```bat
test.bat
```

The batch file runs the visible, sequential demonstration suite. For strict
regression checks, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\regression.ps1
```

If `mingw32-make` is installed:

```bat
mingw32-make test
```

The regression suite checks repeated `addpath`, Unicode paths, native
directory listing, normal and rejected batch arguments, and recovery after an
overlong input line.

To test foreground interruption:

```powershell
python tests\ctrl_c_test.py
```

## Design notes and limitations

- `CTRL+C` forcibly terminates the current foreground process with exit code
  130 while keeping the shell alive. This is predictable for programs such as
  Windows `ping`, but the child does not get an opportunity for graceful
  cleanup.
- `stop`/`resume` enumerate and suspend/resume individual threads. Partial
  failures are rolled back, but suspending arbitrary threads can still
  deadlock a target that holds a lock, and threads created after the snapshot
  can be missed.
- Batch scripts are interpreted by `cmd.exe`. Because an arbitrary script can
  unsafely expand `%1`, arguments containing CMD metacharacters are rejected
  instead of being passed through.
- The shell does not implement its own pipelines or redirection. Operators
  such as `|`, `>`, and `<` are ordinary argument characters for external
  programs.
- Input is limited to 1023 UTF-8 bytes and 63 arguments.
- Completion currently covers built-ins and a small set of common external
  commands; it does not yet complete filenames or arbitrary executables from
  `PATH`.

## Project structure

```text
shell/
|-- Makefile
|-- README.md
|-- test.bat
|-- tests/
|   |-- echo_args.bat
|   |-- regression_input.txt
|   `-- regression.ps1
`-- src/
    |-- main.c
    |-- parser.c
    |-- process.c
    |-- process_list.c
    |-- builtins.c
    |-- line_editor.c
    |-- shell_utils.c
    `-- *.h
```
