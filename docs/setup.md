# Setup and repository guide

See the [main README](../README.md) for the challenge introduction and stages.

## Quick start

Use a Linux host with Docker, Bash, iptables, and kernel support for SocketCAN and virtual CAN. The deployment script needs root access to configure the container's outbound firewall rule.

```bash
git clone https://github.com/4xura/uds-ctf.git
cd uds-ctf
sudo modprobe can
sudo modprobe can_raw
sudo modprobe vcan
sudo bash ./scripts/deploy.sh
```

Connect to the local challenge:

```bash
ssh tester@localhost -p 2222
# password: tester
```

The container creates `vcan0` and starts the simulated ECU automatically. Write player scripts in `/home/tester/work`; that directory is a 64 MB temporary filesystem and is cleared when the container is recreated.

| Setting | Value |
| --- | --- |
| SSH port | `2222` on the host |
| Player credentials | `tester:tester` |
| CAN interface | `vcan0` |
| Tester → ECU CAN ID | `0x7E0` |
| ECU → tester CAN ID | `0x7E8` |
| Reversing artifact | `/home/tester/backups/uds_server_20260702.zip` |
| Flag format | `UDSCTF{...}` |
| Service reset interval | `10800` seconds (3 hours) |

Available tools include `python3`, `vim`, `cansend`, `candump`, `cangen`, `isotpdump`, and `isotpsend`, plus the `python-can` and `can-isotp` libraries.

For manual Docker deployment and VPS setup, see [docs/deployment.md](deployment.md).

## Challenge stages

| Stage | Focus |
| --- | --- |
| 1. Public DID read | UDS requests and ISO-TP response handling |
| 2. Level 1 SecurityAccess | Reversing a seed-key exchange |
| 3. Programming session | Diagnostic session state and level 3 SecurityAccess |
| 4. Protected memory read | Level 5 SecurityAccess and a mapped ELF image |
| 5. Silent reset | ECU reset behavior and a short post-reset window |

The readable backup binary uses `TEST{FAKE_FLAGS_IN_TESTING_...}` placeholders. The live service loads the `UDSCTF{...}` flags from `/opt/udsctf/flags.txt`, created during the Docker image build. For a new event, replace the five flag values in [docker/Dockerfile](../docker/Dockerfile) before building.

Players share one ECU state. `UDS_RESET_INTERVAL` controls the periodic service reset; set it in `docker-compose.yml` or pass it through `docker run` when deploying manually.

## Build the binary

For a Linux source build, install GCC and Make, then run:

```bash
make
```

This builds `build/uds_server`, with object files kept under `build/`. Run `make clean` to remove the generated files. The Docker build packages the binary into the read-only player backup and installs the live service separately.

## Manage the container

```bash
sudo docker logs --tail=80 udsctf-container
sudo docker restart udsctf-container
sudo docker stop udsctf-container
```

If startup cannot create `vcan0`, check that the host supports virtual CAN and that the modules were loaded before deployment. The default SSH credentials are part of the challenge setup.

## Repository layout

```text
uds-ctf/
├── Makefile                 # Linux binary build
├── docker-compose.yml       # Container configuration
├── src/
│   └── uds_server.c         # Simulated ECU and challenge state machine
├── scripts/
│   └── deploy.sh            # Build, launch, and outbound firewall setup
├── docker/
│   ├── Dockerfile           # Challenge image and player backup
│   └── scripts/
│       ├── start.sh         # CAN, service, SSH, and reset loop startup
│       ├── cleanup.sh       # Runtime cleanup
│       ├── setup_permissions.sh
│       └── udsctf_jail.sh    # SSH session wrapper
├── docs/
│   ├── setup.md             # Build and repository guide
│   └── deployment.md        # VPS deployment guide
└── third_party/
    └── iso14229/
        ├── iso14229.c       # Bundled UDS protocol implementation
        ├── iso14229.h       # Protocol definitions
        └── LICENSE          # Protocol library's MIT notice
```

## Credits

- **Original challenge:** [yichen115/UDSCTF](https://github.com/yichen115/UDSCTF), the basis for this adaptation.
- **Adaptation and solution writeup:** [Axura — Car Hacking CTF: Reversing UDS over CAN](https://4xura.com/car-hacking/car-hacking-ctf-reversing-uds-over-can/).
- **UDS protocol library:** [driftregion/iso14229](https://github.com/driftregion/iso14229). Its MIT notice is preserved in [third_party/iso14229/LICENSE](../third_party/iso14229/LICENSE).
- **SSH session wrapper:** [google/nsjail](https://github.com/google/nsjail).
