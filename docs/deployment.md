# Build and deployment guide

This guide is for challenge hosts. The [main README](../README.md) contains the player introduction, five challenge stages, solution writeup, and original-project credit.

## Run locally

Use a Linux host with Docker, Bash, iptables, and kernel support for SocketCAN and virtual CAN. Run the deployment script as root so it can configure the container's outbound firewall rule.

```bash
git clone https://github.com/4xura/uds-ctf.git
cd uds-ctf
sudo modprobe can
sudo modprobe can_raw
sudo modprobe vcan
sudo bash ./scripts/deploy.sh
```

The script builds the image, starts the container, and applies the outbound firewall rule. It supports Docker Compose v2, legacy Compose, and direct Docker deployment.

Connect as a player:

```bash
ssh tester@localhost -p 2222
# password: tester
```

Player scripts belong in `/home/tester/work`, a 64 MB temporary filesystem that is cleared when the container is recreated.

## Build the binary

From the repository root on Linux, with GCC and Make installed:

```bash
make
```

The executable is written to `build/uds_server`, and object files stay under `build/`. Use `make clean` to remove the generated files. The Docker build creates the player backup ZIP automatically.

## Configure an event

- Replace the five published flag values in [docker/Dockerfile](../docker/Dockerfile) before building for a new event.
- Set `UDS_RESET_INTERVAL` in [docker-compose.yml](../docker-compose.yml), or pass it through `docker run`. The default is `10800` seconds (3 hours).
- Players share one ECU state. A service reset clears that state.
- The readable backup binary contains `TEST{FAKE_FLAGS_IN_TESTING_...}` placeholders; the live service reads the event flags from `/opt/udsctf/flags.txt`.

## Deploy on an Ubuntu VPS

The following steps assume an empty Ubuntu VPS and a root SSH session.

### Ports

Open these TCP ports in the cloud firewall:

- `22`: administrator SSH
- `2222`: public CTF SSH

### Server Setup

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
docker build -f docker/Dockerfile -t udsctf:latest .

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

### Verification

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

### Reset

The UDS service resets every 3 hours by default:

```bash
UDS_RESET_INTERVAL=10800
```

To manually reset the challenge state:

```bash
docker restart udsctf-container
```

## Third-party components

The bundled UDS protocol implementation comes from [driftregion/iso14229](https://github.com/driftregion/iso14229), with its MIT notice preserved in [third_party/iso14229/LICENSE](../third_party/iso14229/LICENSE). The container's SSH session wrapper uses [google/nsjail](https://github.com/google/nsjail).
