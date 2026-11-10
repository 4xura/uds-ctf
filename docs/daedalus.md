# Daedalus integration backup

This branch preserves the UDSCTF adaptation deployed in Daedalus. Its base is
`fa18541fb353366f30110c31a4d233c046bb733e`; the original `main` branch stays intact.

Copy this branch's working tree, excluding `.git`, into
`Daedalus/chambers/0x02-udsctf/`. The compose file points at `docker/Dockerfile`.
The supporting Daedalus release includes SSH chambers and independently scored
objectives (platform commit `9a414ef` or later).

## Adaptation

- Five generated flags use `DAEDALUS{...}`, with independent awards of
  100, 200, 300, 400 and 500 points. All five are required to pwn the chamber.
- Each instance receives five unique environment variables through the platform.
  The startup script writes the protected flag file and removes those variables
  before launching the challenge. No real flags are baked into the image.
- The loader accepts the Daedalus prefix; legacy UDSCTF-prefixed values remain
  readable solely to preserve already-issued private-instance flags on reset.
- The readable backup binary retains testing placeholders for reversing.
- The image creates its own virtual CAN interface, then drops NET_ADMIN before
  SSH and ECU startup. It retains Docker's default seccomp policy.
- The SSH wrapper uses nsjail clone/exec mode (`-Mo`) rather than `unshare(0)`.
- Daedalus supplies per-instance network isolation, bounded resources and gate
  access; shared SSH is authorized by signed-in account addresses, and private
  SSH is restricted to the owner's addresses. Docker host ports are ignored.

## Validate and check

Prepare the Docker host with the running kernel's SocketCAN modules (`vcan`,
`can_raw`, `can_isotp`) and install `sshpass` for the reference check. Then run
Daedalus validation and its chamber check for `0x02-udsctf`. The reference solver
uses the ordinary `tester` account over SSH and recovers all five real flags
through the virtual CAN interface. It does not read the protected flag file.

The initial SSH login remains `tester` / `tester`. Each login gets a fresh working
directory inside the bounded 64 MB work tmpfs. The Lab instance resets every two
hours; an extended private copy keeps the ECU's three-hour internal reset. Original standalone deployment instructions
under `docs/deployment.md` describe `main`; they are not the platform's deployment
procedure for this branch.

## Shared SSH cleanup

PAM runs `docker/scripts/cleanup.sh` under root when SSH sessions open and close.
Each session uses a fresh working directory. When the final session ends, cleanup
removes leftover files, ends detached tester processes, clears the boot marker,
and restarts the ECU. Sessions that overlap keep working while another player
leaves. They share the `tester` Unix account and can still inspect each other's
active files; cleanup protects the next session from abandoned work.

## Source mapping

The Daedalus repository keeps the Dockerfile and platform scripts at the chamber
root, and the inherited C source under `upstream/`. This branch maps them back to
the original repository's `docker/`, `src/` and `third_party/` layout for review.
The challenge logic, credit and bundled iso14229 MIT notice are retained.
