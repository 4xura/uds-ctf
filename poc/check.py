"""Run the reference solver as the ordinary tester SSH account."""

import argparse
import os
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--connection-info")
parser.parse_args()
solver = Path(__file__).with_name("solve.py").read_text()
command = "python3 - <<'DAEDALUS_SOLVER'\n" + solver + "\nDAEDALUS_SOLVER"
ssh = ["sshpass", "-p", "tester", "ssh", "-tt", "-o", "StrictHostKeyChecking=no",
       "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=10",
       "-p", os.environ["DAEDALUS_PORT"], "tester@" + os.environ["DAEDALUS_HOST"]]


def session(script, timeout=30):
    return subprocess.run(ssh, input=script + "\nexit\n", text=True,
                          capture_output=True, timeout=timeout)


# Exercise cleanup through ordinary SSH sessions before the five-stage solve.
first = session('test "$PWD" = "$UDS_SESSION_DIR" && '
                'printf stale > /home/tester/work/cleanup-probe')
if first.returncode:
    print(first.stdout + first.stderr)
    raise SystemExit("SSH did not create a private working directory")
for attempt in range(5):
    clean = session("test ! -e /home/tester/work/cleanup-probe")
    if clean.returncode == 0:
        break
    time.sleep(2)
else:
    print(clean.stdout + clean.stderr)
    raise SystemExit("Leftover work was visible to the next SSH session")

result = session(command, timeout=120)
print(result.stdout)
if result.returncode:
    print(result.stderr)
raise SystemExit(result.returncode)
