#!/bin/bash
# PAM runs this as root when a tester SSH session opens and closes. The lock and
# counter live outside the tester-writable work directory.
set -euo pipefail

state=/run/udsctf
work=/home/tester/work
umask 077
mkdir -p "$state" "$work"
chmod 700 "$state"
exec 9>"$state/sessions.lock"
flock -x 9

clear_work() {
    find "$work" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    rm -f /opt/udsctf/.boot_window
}

restart_ecu() {
    local old current attempt
    old=$(pgrep -xo uds_server || true)
    if [ -n "$old" ]; then
        kill -TERM "$old" || true
    fi
    for attempt in {1..50}; do
        current=$(pgrep -xo uds_server || true)
        if [ -n "$current" ] && [ "$current" != "$old" ]; then
            return 0
        fi
        sleep 0.2
    done
    echo 'UDS server did not restart after cleanup' >&2
    return 1
}

if [ "${1:-}" = startup ]; then
    printf '0\n' > "$state/active.count"
    clear_work
    exit 0
fi

if [ "${PAM_USER:-}" != tester ]; then
    exit 0
fi
count=$(cat "$state/active.count")
[[ "$count" =~ ^[0-9]+$ ]] || exit 1
case "${PAM_TYPE:-}" in
    open_session)
        if [ "$count" -eq 0 ]; then
            clear_work
            restart_ecu
        fi
        printf '%s\n' "$((count + 1))" > "$state/active.count"
        ;;
    close_session)
        if [ "$count" -le 0 ]; then
            echo 'SSH session count underflow' >&2
            exit 1
        fi
        count=$((count - 1))
        printf '%s\n' "$count" > "$state/active.count"
        if [ "$count" -eq 0 ]; then
            # Background tools from previous SSH sessions must not keep working
            # after everyone has left.
            pkill -TERM -u tester || true
            clear_work
            restart_ecu
        fi
        ;;
esac
