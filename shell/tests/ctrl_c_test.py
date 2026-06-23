import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path


root = Path(__file__).resolve().parent.parent
exe = root / "myShell.exe"

if not exe.exists():
    raise SystemExit("Build myShell.exe before running this test.")

proc = subprocess.Popen(
    [str(exe)],
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    encoding="utf-8",
    errors="replace",
    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
)

assert proc.stdin is not None
proc.stdin.write("ping -t 127.0.0.1\n")
proc.stdin.flush()
time.sleep(1.5)

# The shell handles both CTRL_C_EVENT and CTRL_BREAK_EVENT. Python can target
# the new process group reliably with CTRL_BREAK_EVENT.
os.kill(proc.pid, signal.CTRL_BREAK_EVENT)
time.sleep(0.8)

proc.stdin.write("date\nexit\n")
proc.stdin.flush()
output, _ = proc.communicate(timeout=10)

if "Goodbye!" not in output:
    print(output)
    raise SystemExit("FAILED: shell did not recover after interrupt")
if not re.search(r"\b\d{4}-\d{2}-\d{2}\b", output):
    sys.stdout.buffer.write(output.encode("utf-8", errors="backslashreplace"))
    raise SystemExit("FAILED: command after interrupt did not run")

print("CTRL+C foreground termination test passed.")
