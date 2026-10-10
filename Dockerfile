# Use Ubuntu 20.04 as the base image
FROM ubuntu:20.04

# Set timezone and disable interactive package prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Use Aliyun package mirrors
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# Install required packages
RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    bison \
    flex \
    git \
    libprotobuf-dev \
    libnl-route-3-dev \
    libtool \
    pkg-config \
    protobuf-compiler \
    can-utils \
    iproute2 \
    python3 \
    python3-pip \
    openssh-server \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Configure pip to use the Aliyun mirror
RUN mkdir -p ~/.pip && \
    echo "[global]" > ~/.pip/pip.conf && \
    echo "index-url = https://mirrors.aliyun.com/pypi/simple/" >> ~/.pip/pip.conf && \
    echo "trusted-host = mirrors.aliyun.com" >> ~/.pip/pip.conf

# Install python-can libraries
RUN pip3 install python-can can-isotp

# Create the CTF user and home layout
RUN useradd -m -s /bin/bash tester && \
    echo "tester:tester" | chpasswd && \
    mkdir -p /home/tester/backups /home/tester/work

# Shared-account challenge: keep SSH password fixed for all players.
# SSH password authentication still works because sshd checks /etc/shadow itself,
# but tester cannot use setuid account helpers to change the password or shell.
RUN chmod a-s /usr/bin/passwd /usr/bin/chsh /usr/bin/chfn /usr/bin/newgrp /usr/bin/su 2>/dev/null || true && \
    rm -f /usr/bin/expiry /usr/bin/chage 2>/dev/null || true

# Configure SSH service
RUN mkdir -p /var/run/sshd && \
    echo "PasswordAuthentication yes" >> /etc/ssh/sshd_config && \
    echo "PermitRootLogin no" >> /etc/ssh/sshd_config && \
    echo "AllowUsers tester" >> /etc/ssh/sshd_config

# Copy challenge files to a temporary build directory
COPY *.c *.h Makefile /tmp/build/
WORKDIR /tmp/build

# Build nsjail from upstream source
RUN git clone --depth 1 --branch 3.4 https://github.com/google/nsjail.git /tmp/nsjail && \
    make -C /tmp/nsjail && \
    cp /tmp/nsjail/nsjail /usr/local/bin/nsjail && \
    chmod 755 /usr/local/bin/nsjail && \
    rm -rf /tmp/nsjail

# Build the UDS server
RUN make clean && make

# Place the live executable in a protected directory and create a readable backup artifact
RUN mkdir -p /opt/udsctf /home/tester/backups && \
    cp uds_server /opt/udsctf/ && \
    printf '%s\n' \
        'UDSCTF{VINAXURA13370000}' \
        'UDSCTF{27_AXURA_X0r_C1C2}' \
        'UDSCTF{D1D_M0re_Dead6eef_4AXURA}' \
        'UDSCTF{7he_F1n@l_Labyrinth_5ecret_H@s_been_ReC0ver!}' \
        'UDSCTF{Re@dMem0ry_from_5had0w_under_Labyrinth}' \
        > /opt/udsctf/flags.txt && \
    chmod 700 /opt/udsctf/uds_server && \
    chown root:root /opt/udsctf/uds_server && \
    chown root:root /opt/udsctf/flags.txt && \
    chmod 600 /opt/udsctf/flags.txt && \
    python3 -m zipfile -c /home/tester/backups/uds_server_20260702.zip uds_server && \
    chown -R root:root /home/tester/backups && \
    chmod 755 /home/tester/backups && \
    chmod 644 /home/tester/backups/uds_server_20260702.zip && \
    rm -rf /tmp/build

# Create startup script
COPY start.sh /usr/local/bin/start.sh
RUN chmod +x /usr/local/bin/start.sh

# Create cleanup script
COPY cleanup.sh /usr/local/bin/cleanup.sh
RUN chmod +x /usr/local/bin/cleanup.sh

# Create permission setup script
COPY setup_permissions.sh /usr/local/bin/setup_permissions.sh
RUN chmod +x /usr/local/bin/setup_permissions.sh

# Create jailed SSH login wrapper
COPY udsctf_jail.sh /usr/local/bin/udsctf_jail.sh
RUN chmod +x /usr/local/bin/udsctf_jail.sh && \
    echo "ForceCommand /usr/local/bin/udsctf_jail.sh" >> /etc/ssh/sshd_config && \
    echo "PermitTunnel no" >> /etc/ssh/sshd_config && \
    echo "AllowTcpForwarding no" >> /etc/ssh/sshd_config && \
    echo "X11Forwarding no" >> /etc/ssh/sshd_config

# Set user permissions and login banner
RUN chown root:root /home/tester && \
    chmod 755 /home/tester && \
    chown -R root:root /home/tester/backups && \
    chmod 755 /home/tester/backups && \
    chown -R tester:tester /home/tester/work && \
    chmod 750 /home/tester/work && \
    echo "export PATH=/usr/local/bin:/usr/bin:/bin" >> /home/tester/.bashrc && \
    echo "cd /home/tester/work" >> /home/tester/.bashrc && \
    echo "" >> /home/tester/.bashrc && \
    echo "if [ -f /home/tester/.welcome ]; then" >> /home/tester/.bashrc && \
    echo "    cat /home/tester/.welcome" >> /home/tester/.bashrc && \
    echo "fi" >> /home/tester/.bashrc && \
    echo "echo \"Current working directory: \$(pwd)\"" >> /home/tester/.bashrc && \
    echo "echo \"Available commands: python3, vim, cansend, candump, cangen, isotpdump, isotpsend\"" >> /home/tester/.bashrc && \
    echo "echo \"\"" >> /home/tester/.bashrc

# Remove build tools as a security measure
RUN apt-get remove -y build-essential gcc g++ make autoconf bison flex git libtool pkg-config protobuf-compiler && \
    apt-get autoremove -y && \
    rm -f /usr/bin/wget /bin/wget 2>/dev/null && \
    apt-get clean

# Expose SSH port
EXPOSE 22

# Startup script
ENTRYPOINT ["/usr/local/bin/start.sh"]
