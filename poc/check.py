"""Run the reference solver as the ordinary tester SSH account."""

import argparse
import os
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--connection-info")
parser.parse_args()
solver = Path(__file__).with_name("solve.py").read_text()
command = "python3 - <<'DAEDALUS_SOLVER'\n" + solver + "\nDAEDALUS_SOLVER\nexit\n"
result = subprocess.run(
    ["sshpass", "-p", "tester", "ssh", "-tt", "-o", "StrictHostKeyChecking=no",
     "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=10",
     "-p", os.environ["DAEDALUS_PORT"], "tester@" + os.environ["DAEDALUS_HOST"]],
    input=command, text=True, capture_output=True, timeout=90,
)
print(result.stdout)
if result.returncode:
    print(result.stderr)
raise SystemExit(result.returncode)
