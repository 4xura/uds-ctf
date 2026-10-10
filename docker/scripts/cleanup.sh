#!/bin/bash
# Security cleanup script - remove source files and keep the intended backup artifact

echo "Running security cleanup..."

echo "Removing accidental source/build files from player home..."
find /home/tester -path /home/tester/backups -prune -o -name "*.c" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "*.h" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "Makefile" -delete 2>/dev/null || true
find /home/tester -path /home/tester/backups -prune -o -name "*.o" -delete 2>/dev/null || true

echo "Build tools are removed in the image build stage"

echo "Setting UDS server permissions..."
chmod 700 /opt/udsctf/uds_server 2>/dev/null || true
chown root:root /opt/udsctf/uds_server 2>/dev/null || true

echo "Setting tester home permissions..."
chown root:root /home/tester 2>/dev/null || true
chmod 755 /home/tester 2>/dev/null || true
chown -R root:root /home/tester/backups 2>/dev/null || true
chmod 755 /home/tester/backups 2>/dev/null || true
chmod 644 /home/tester/backups/uds_server_*.zip 2>/dev/null || true
mkdir -p /home/tester/work
chown -R tester:tester /home/tester/work 2>/dev/null || true
chmod 750 /home/tester/work 2>/dev/null || true

echo "Security cleanup complete"
