#!/bin/bash
set -euo pipefail

# NET_ADMIN is used only here, in this container's isolated network namespace.
ip link add dev vcan0 type vcan
ip link set vcan0 up

umask 077
mkdir -p /opt/udsctf
# The upstream loader expects reset before memory, not the player stage order.
printf '%s\n' "$UDS_FLAG_PUBLIC" "$UDS_FLAG_LEVEL1" "$UDS_FLAG_LEVEL3" \
  "$UDS_FLAG_RESET" "$UDS_FLAG_MEMORY" > /opt/udsctf/flags.txt
chmod 600 /opt/udsctf/flags.txt
unset UDS_FLAG_PUBLIC UDS_FLAG_LEVEL1 UDS_FLAG_LEVEL3 UDS_FLAG_RESET UDS_FLAG_MEMORY
unset DAEDALUS_FLAG

# Neither SSH sessions nor the ECU retain permission to reconfigure the network.
exec setpriv --bounding-set=-net_admin --inh-caps=-net_admin /usr/local/bin/run-challenge.sh
