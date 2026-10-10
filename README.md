# UDSCTF Public Introduction

UDSCTF is a five-stage automotive security CTF built around UDS, the diagnostic protocol used by testers to talk to ECUs. The target behaves like a small simulated vehicle controller: it receives diagnostic requests over a virtual CAN interface, keeps session and security state, and exposes different data only after the right protocol path is reached.

The challenge is designed for players who are comfortable with reversing and scripting, but new to car hacking. The main work is not car trivia. It is protocol analysis, state-machine reasoning, and binary reversing.

## References

- **Full solution writeup:** [Car Hacking CTF: Reversing UDS over CAN](https://4xura.com/car-hacking/car-hacking-ctf-reversing-uds-over-can/).
- **Original challenge by yichen115:** [yichen115/UDSCTF](https://github.com/yichen115/UDSCTF). 

> The CTF challenge is not a firmware exploit against a real car, but a simulated automotive diagnostic environment.
>

## Build and deployment

The [deployment guide](docs/deployment.md) explains how to build the binary, run the challenge locally, configure an event, and host it on an Ubuntu VPS. Local deployment exposes SSH on port `2222`.

## Access

Players connect to a shared SSH environment:

```bash
ssh tester@<host> -p <port>
# password: tester
```

Inside the environment, the CAN interface and UDS service are already running:

```text
CAN interface: vcan0
UDS request ID: 0x7E0
UDS response ID: 0x7E8
```

Useful tools are available in the container:

```text
python3, vim, cansend, candump, cangen, isotpdump, isotpsend
```

Player scripts should be written under `/home/tester/work`. A readable backup binary is provided under `/home/tester/backups` as the intended reversing artifact.

## Challenge Model

The simulated ECU accepts UDS requests and returns UDS responses. Each flag is tied to a different diagnostic feature:

- public DID reading
- SecurityAccess seed-key exchange
- diagnostic session switching
- protected memory reading
- reset-time state recovery

The live service protects the real flags. The backup binary is meant for reversing and contains obvious fake testing placeholders instead of prize flags.

The shared server periodically resets the UDS service every 3 hours. This prevents stale state from lasting forever while still giving enough time for normal solving.

## Flag Format

All real flags use:

```text
UDSCTF{...}
```

## Challenge 1: Public DID Read

**Difficulty**: ★

The first stage introduces the basic request/response model. A public diagnostic identifier can be read without authentication. The goal is to recognize the UDS service format, send a valid request, and decode the response correctly.

This stage is mainly about getting comfortable with:

- CAN IDs versus UDS payload bytes
- `ReadDataByIdentifier`
- single-frame versus multi-frame responses

## Challenge 2: Level 1 SecurityAccess

**Difficulty**: ★★

The second stage introduces UDS `SecurityAccess`. The ECU provides a seed, and the tester must return the matching key before a protected diagnostic identifier becomes readable.

This stage requires:

- requesting a seed
- reversing the level 1 key logic from the backup binary
- sending the derived key in the correct byte order
- reading the newly unlocked diagnostic data

## Challenge 3: Level 3 SecurityAccess

**Difficulty**: ★★★

The third stage adds diagnostic session state. A stronger SecurityAccess level is not reachable from the default session, so the solve must first move the ECU into the correct diagnostic mode.

This stage focuses on:

- interpreting negative responses as state-machine feedback
- switching diagnostic sessions
- reversing a stronger seed-key algorithm
- using the unlocked state to access a protected DID

## Challenge 4: Level 5 SecurityAccess and Memory Read

**Difficulty**: ★★★★

The fourth stage combines the previous state chain with a memory-read service. After the required SecurityAccess level is unlocked, the ECU allows controlled reads from a mapped program image.

This stage requires:

- completing the higher-level seed-key chain
- understanding the `ReadMemoryByAddress` request format
- choosing addresses and sizes carefully
- scripting a memory scan to recover the hidden data

## Challenge 5: ECU Reset State Recovery

**Difficulty**: ★★★★★

The final stage uses `ECUReset`. A normal reset is not enough. The useful path depends on prior session state, SecurityAccess state, reset subfunction parsing, and a short post-reset window.

This stage is intended as the capstone:

- reverse the reset handler
- identify the state required before reset
- trigger the correct reset behavior
- reconnect quickly after restart
- recover the final diagnostic data before the window expires
