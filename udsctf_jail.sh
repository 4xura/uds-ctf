#!/bin/bash
set -e

export HOME=/home/tester
export USER=tester
export LOGNAME=tester
export PATH=/usr/local/bin:/usr/bin:/bin
cd /home/tester/work

run_nsjail() {
    exec /usr/local/bin/nsjail \
        -Me \
        --quiet \
        --disable_clone_newnet \
        --disable_clone_newuser \
        --disable_clone_newns \
        --disable_clone_newpid \
        --disable_clone_newipc \
        --disable_clone_newuts \
        --disable_clone_newcgroup \
        --cwd /home/tester/work \
        --env HOME=/home/tester \
        --env USER=tester \
        --env LOGNAME=tester \
        --env PATH=/usr/local/bin:/usr/bin:/bin \
        --env TERM=xterm-256color \
        --env UDSCTF_BANNER_SHOWN=1 \
        --rlimit_as 1024 \
        --rlimit_fsize 64 \
        --rlimit_nofile 256 \
        -- \
        "$@"
}

if [ -n "${SSH_ORIGINAL_COMMAND:-}" ]; then
    case "$SSH_ORIGINAL_COMMAND" in
        /usr/lib/openssh/sftp-server|"/usr/lib/openssh/sftp-server"*)
            run_nsjail /usr/lib/openssh/sftp-server -R
            ;;
        "scp -f /home/tester/backups/"*.zip|"scp -f backups/"*.zip|"scp -f ~/backups/"*.zip)
            run_nsjail /bin/sh -c "$SSH_ORIGINAL_COMMAND"
            ;;
        *)
            echo "Interactive login only."
            exit 1
            ;;
    esac
fi

run_nsjail /bin/bash --rcfile /home/tester/.bashrc -i
