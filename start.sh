#!/bin/bash
# Startup script - prepare the environment and start UDSCTF

echo "Starting the UDSCTF challenge environment..."

/usr/local/bin/cleanup.sh
/usr/local/bin/setup_permissions.sh

# Load CAN modules when modprobe is available. Containers may already have them loaded by the host.
modprobe can 2>/dev/null || true
modprobe can_raw 2>/dev/null || true
modprobe vcan 2>/dev/null || true

ip link add dev vcan0 type vcan 2>/dev/null || true
ip link set up vcan0

echo "CAN interface vcan0 has been created and brought up"

cd /opt/udsctf

UDS_RESET_INTERVAL=${UDS_RESET_INTERVAL:-10800}

periodic_uds_reset() {
    while true; do
        sleep "$UDS_RESET_INTERVAL"
        echo "Periodic UDS server reset requested"
        rm -f /opt/udsctf/.boot_window 2>/dev/null || true
        pkill -TERM -x uds_server 2>/dev/null || true
    done
}

periodic_uds_reset &
RESET_PID=$!
echo "Periodic UDS reset loop started (PID: $RESET_PID, interval: ${UDS_RESET_INTERVAL}s)"

# Remove any possible source files as an additional safety measure.
find /home/tester -path /home/tester/backups -prune -o -name "*.c" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "*.h" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "Makefile" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "*.o" -delete 2>/dev/null || true

while true; do
    echo "Starting UDS server..."
    ./uds_server
    UDS_EXIT_CODE=$?
    echo "UDS server exited, exit code: $UDS_EXIT_CODE"

    if [ $UDS_EXIT_CODE -eq 0 ]; then
        echo "Normal restart request detected; restarting UDS server in 3 seconds..."
        sleep 3
    else
        echo "UDS server exited unexpectedly; restarting in 5 seconds..."
        sleep 5
    fi
done &
UDS_PID=$!

echo "UDS server started (PID: $UDS_PID)"

if [ ! -f /etc/ssh/ssh_host_rsa_key ]; then
    ssh-keygen -A
fi

echo "Starting SSH service..."
/usr/sbin/sshd -D
