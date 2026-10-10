# UDSCTF VPS Deployment

Minimal setup for an empty Ubuntu VPS.

## Ports

Open these TCP ports in the cloud firewall:

- `22`: administrator SSH
- `2222`: public CTF SSH

## Server Setup

SSH into the VPS as root:

```bash
ssh root@<vps-ip>
```

Install Docker and Docker Compose:

```bash
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl gnupg docker.io docker-compose-v2
systemctl enable --now docker
```

Clone the published challenge repository:

```bash
apt-get install -y git
git clone https://github.com/4xura/uds-ctf.git /opt/udsctf-src
cd /opt/udsctf-src
modprobe can
modprobe can_raw
modprobe vcan
```

Build and run:

```bash
docker build -t udsctf:latest .

docker rm -f udsctf-container 2>/dev/null || true
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
```

Block outbound connections initiated from the challenge container. This keeps players from using the shared VPS as a traffic source while preserving inbound SSH on port `2222`:

```bash
CID=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' udsctf-container)
iptables -C DOCKER-USER -s "$CID" -m conntrack --ctstate NEW -j DROP 2>/dev/null || \
  iptables -I DOCKER-USER 1 -s "$CID" -m conntrack --ctstate NEW -j DROP
```

## Verification

Check the container:

```bash
docker ps
docker logs --tail=80 udsctf-container
ss -ltnp | grep -E ':22|:2222'
```

Player login:

```bash
ssh tester@<vps-ip> -p 2222
# password: tester
```

Expected player environment:

- working directory: `/home/tester/work`
- `/home/tester/work` is a `64m` tmpfs so player files cannot fill the VPS disk
- backup artifact: `/home/tester/backups/uds_server_20260702.zip`
- CAN interface: `vcan0`
- UDS request CAN ID: `0x7E0`
- UDS response CAN ID: `0x7E8`

Check basic hardening:

```bash
ssh tester@<vps-ip> -p 2222
# password: tester

pwd                         # /home/tester/work
ls -ld /home/tester/backups # root-owned, read-only for tester
command -v wget             # should print nothing
```

## Reset

The UDS service resets every 3 hours by default:

```bash
UDS_RESET_INTERVAL=10800
```

To manually reset the challenge state:

```bash
docker restart udsctf-container
```
