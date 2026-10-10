#!/bin/bash
# UDSCTF one-command deployment script

set -e

echo "UDSCTF deployment script started..."
echo "================================"

if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed. Install Docker first."
    exit 1
fi

if command -v docker-compose &> /dev/null; then
    echo "docker-compose detected; deploying with docker-compose"
    DEPLOY_METHOD="docker-compose"
else
    echo "docker-compose not found; deploying directly with Docker"
    DEPLOY_METHOD="docker"
fi

block_container_egress() {
    local cid
    cid=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' udsctf-container 2>/dev/null || true)
    if [ -n "$cid" ]; then
        iptables -C DOCKER-USER -s "$cid" -m conntrack --ctstate NEW -j DROP 2>/dev/null || \
            iptables -I DOCKER-USER 1 -s "$cid" -m conntrack --ctstate NEW -j DROP
        echo "Blocked outbound connections from container IP: $cid"
    fi
}

if ! docker info &> /dev/null; then
    echo "Error: current user cannot access Docker. Add the user to the docker group or use sudo."
    exit 1
fi

if [ "$DEPLOY_METHOD" = "docker-compose" ]; then
    echo "Building and starting the service with docker-compose..."
    docker-compose up -d --build
    block_container_egress

    echo "Deployment complete!"
    echo ""
    echo "Connection:"
    echo "   SSH: ssh tester@localhost -p 2222"
    echo "   password: tester"
    echo ""
    echo "Management commands:"
    echo "   View logs: docker-compose logs -f"
    echo "   Stop service: docker-compose down"
    echo "   Restart service: docker-compose restart"
else
    echo "Building Docker image..."
    docker build -t udsctf:latest .

    echo "Starting container directly with Docker..."
    docker stop udsctf-container 2>/dev/null || true
    docker rm udsctf-container 2>/dev/null || true

    docker run -d \
        --name udsctf-container \
        --cap-add NET_ADMIN \
        --cap-add NET_RAW \
        --security-opt seccomp=unconfined \
        --memory 512m \
        --cpus 1 \
        --pids-limit 256 \
        -p 2222:22 \
        --tmpfs /home/tester/work:rw,size=64m,uid=1000,gid=1000,mode=750 \
        -e UDS_RESET_INTERVAL=10800 \
        --restart unless-stopped \
        udsctf:latest

    block_container_egress

    echo "Deployment complete!"
    echo ""
    echo "Connection:"
    echo "   SSH: ssh tester@localhost -p 2222"
    echo "   password: tester"
    echo ""
    echo "Management commands:"
    echo "   View logs: docker logs -f udsctf-container"
    echo "   Stop service: docker stop udsctf-container"
    echo "   Restart service: docker restart udsctf-container"
    echo "   Remove container: docker rm -f udsctf-container"
fi

echo ""
echo "Challenge notes:"
echo "   1. Connect to the environment over SSH"
echo "   2. Use python-can or can-isotp to interact with the CAN bus"
echo "   3. Explore the UDS protocol to recover flags"
echo "   4. Available tools: python3, vim, cansend, candump, cangen, isotpdump, isotpsend"
echo ""
echo "Security measures:"
echo "   - Source code and the live executable are protected"
echo "   - The tester user receives a read-only backup artifact under /home/tester/backups"
echo "   - Player scripts are limited to /home/tester/work"
echo "   - Password-changing helpers are disabled for the shared tester account"
echo "   - SSH login is wrapped by nsjail with resource limits"
echo "   - File permissions keep /opt/udsctf/uds_server unreadable from tester"
echo "   - Docker limits memory, CPU, and process count"
echo "   - The UDS server is periodically reset with UDS_RESET_INTERVAL"
echo ""
echo "Challenge environment deployed!"
echo "==============================="
