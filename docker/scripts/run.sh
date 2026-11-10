#!/bin/bash
set -eu
/usr/local/bin/setup_permissions.sh
/usr/local/bin/cleanup.sh startup
mkdir -p /run/sshd
ssh-keygen -A
cd /opt/udsctf

(
  while true; do
    sleep "${UDS_RESET_INTERVAL:-10800}"
    rm -f /opt/udsctf/.boot_window
    pkill -TERM -x uds_server || true
  done
) &
(
  while true; do
    ./uds_server || true
    sleep 3
  done
) &
exec /usr/sbin/sshd -D -e
