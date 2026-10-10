#!/bin/bash
# Permission setup script - configure user permissions at container startup

echo "Setting user permission restrictions..."

chown root:root /home/tester
chmod 755 /home/tester
chown -R root:root /home/tester/backups
chmod 755 /home/tester/backups
chmod 644 /home/tester/backups/uds_server_*.zip 2>/dev/null || true
mkdir -p /home/tester/work
chown -R tester:tester /home/tester/work
chmod 750 /home/tester/work

chmod 700 /opt/udsctf/uds_server
chown root:root /opt/udsctf/uds_server

cat > /home/tester/.welcome << 'EOF'
Welcome to the UDSCTF challenge environment!

Available tools:
- python3: use python-can and can-isotp libraries
- vim: edit Python scripts
- cansend: send CAN frames
- candump: listen for CAN frames
- cangen: generate CAN frames
- isotpdump: listen to ISO-TP traffic
- isotpsend: send ISO-TP traffic

CAN interface: vcan0
UDS request ID: 0x7E0
UDS response ID: 0x7E8

Tip: use vim to write Python scripts for UDS protocol exploration.
EOF

cat > /home/tester/.bashrc << 'EOF'
export HOME=/home/tester
export USER=tester
export LOGNAME=tester
export PATH=/usr/local/bin:/usr/bin:/bin
export TERM=${TERM:-xterm-256color}
export CLICOLOR_FORCE=1
export LS_COLORS='di=01;34:ln=01;36:so=01;35:pi=33:ex=01;32:bd=01;33:cd=01;33:su=37;41:sg=30;43:tw=30;42:ow=34;42'

alias ls='ls --color=auto'
alias ll='ls -alF --color=auto'
alias grep='grep --color=auto'

PS1='\[\e[1;32m\]\u@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
cd /home/tester/work

if [[ $- == *i* ]] && [ -t 1 ] && [ -z "${UDSCTF_BANNER_SHOWN:-}" ]; then
    export UDSCTF_BANNER_SHOWN=1
    printf '\e[1;36mWelcome to the UDSCTF challenge environment!\e[0m\n\n'
    printf '\e[1;33mAvailable tools:\e[0m\n'
    printf '  \e[1;32mpython3\e[0m     use python-can and can-isotp libraries\n'
    printf '  \e[1;32mvim\e[0m         edit Python scripts\n'
    printf '  \e[1;32mcansend\e[0m     send CAN frames\n'
    printf '  \e[1;32mcandump\e[0m     listen for CAN frames\n'
    printf '  \e[1;32mcangen\e[0m      generate CAN frames\n'
    printf '  \e[1;32misotpdump\e[0m   listen to ISO-TP traffic\n'
    printf '  \e[1;32misotpsend\e[0m   send ISO-TP traffic\n\n'
    printf '\e[1;33mCAN interface:\e[0m vcan0\n'
    printf '\e[1;33mUDS request ID:\e[0m 0x7E0\n'
    printf '\e[1;33mUDS response ID:\e[0m 0x7E8\n\n'
fi
EOF

chown root:root /home/tester/.welcome
chown root:root /home/tester/.bashrc
chmod 644 /home/tester/.welcome /home/tester/.bashrc

echo "Permission setup complete"
