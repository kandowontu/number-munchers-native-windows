#!/usr/bin/env python3
"""Read a seeded Word Munchers guest image from a headless DOSBox-X process."""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import struct
import time


ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "analysis/WM-unpacked-image.bin"
# Startup relocates DS to load-segment + 0x22C8, so DS-relative addresses
# begin 0x22C80 bytes into the normalized load image.
DGROUP_IMAGE_OFFSET = 0x22C80

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
PAGE_GUARD = 0x100
PAGE_NOACCESS = 0x01


class MemoryBasicInformation(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", wintypes.DWORD),
        ("PartitionId", wintypes.WORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", wintypes.DWORD),
        ("Protect", wintypes.DWORD),
        ("Type", wintypes.DWORD),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.VirtualQueryEx.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.POINTER(MemoryBasicInformation),
    ctypes.c_size_t,
]
kernel32.VirtualQueryEx.restype = ctypes.c_size_t
kernel32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
kernel32.ReadProcessMemory.restype = wintypes.BOOL
kernel32.WriteProcessMemory.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
kernel32.WriteProcessMemory.restype = wintypes.BOOL


def read_memory(handle: int, address: int, size: int) -> bytes | None:
    buffer = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(
        handle, ctypes.c_void_p(address), buffer, size, ctypes.byref(read)
    ):
        return None
    return buffer.raw[: read.value] if read.value == size else None


def write_memory(handle: int, address: int, data: bytes) -> None:
    buffer = ctypes.create_string_buffer(data)
    written = ctypes.c_size_t()
    if not kernel32.WriteProcessMemory(
        handle, ctypes.c_void_p(address), buffer, len(data), ctypes.byref(written)
    ) or written.value != len(data):
        raise ctypes.WinError(ctypes.get_last_error())


def find_guest_image(handle: int, image: bytes) -> int:
    # The DOS loader relocates far-call segment words.  Match only stretches
    # containing local control flow/data references so the unpacked normalized
    # image and the resident image remain byte-identical.
    primary_offset = 0x9222
    primary = image[primary_offset : primary_offset + 80]
    checks = ((0xAD50, 40), (0x21E90, 16))
    address = 0
    maximum = (1 << 47) - 1
    while address < maximum:
        info = MemoryBasicInformation()
        if not kernel32.VirtualQueryEx(
            handle, ctypes.c_void_p(address), ctypes.byref(info), ctypes.sizeof(info)
        ):
            break
        base = int(info.BaseAddress or 0)
        size = int(info.RegionSize)
        if (
            info.State == MEM_COMMIT
            and not (info.Protect & PAGE_GUARD)
            and info.Protect != PAGE_NOACCESS
            and size > len(primary)
        ):
            chunk_size = 1 << 20
            overlap = len(primary) - 1
            cursor = base
            tail = b""
            while cursor < base + size:
                wanted = min(chunk_size, base + size - cursor)
                data = read_memory(handle, cursor, wanted)
                if data is None:
                    break
                combined = tail + data
                search_from = 0
                while True:
                    found = combined.find(primary, search_from)
                    if found < 0:
                        break
                    match = cursor - len(tail) + found
                    guest_base = match - primary_offset
                    if guest_base >= 0 and all(
                        read_memory(handle, guest_base + offset, length)
                        == image[offset : offset + length]
                        for offset, length in checks
                    ):
                        return guest_base
                    search_from = found + 1
                tail = combined[-overlap:]
                cursor += wanted
        next_address = base + max(size, 0x1000)
        if next_address <= address:
            break
        address = next_address
    raise RuntimeError("could not locate the loaded Word Munchers guest image")


def random_states(seed: int, count: int) -> dict[int, int]:
    state = seed & 0xFFFF
    result: dict[int, int] = {state: 0}
    for call in range(1, count + 1):
        state = (state * 0x015A4E35 + 1) & 0xFFFFFFFF
        result[state] = call
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0x8E74)
    parser.add_argument("--read-only", action="store_true")
    parser.add_argument("--seconds", type=float, default=100.0)
    args = parser.parse_args()

    handle = kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION
        | PROCESS_VM_READ
        | PROCESS_VM_WRITE,
        False,
        args.pid,
    )
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        image = IMAGE.read_bytes()
        guest_base = find_guest_image(handle, image)
        rng_address = guest_base + DGROUP_IMAGE_OFFSET + 0x532E
        before = read_memory(handle, rng_address, 4)
        if before is None:
            raise RuntimeError("could not read the guest RNG")
        if args.read_only:
            status = read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x5C00, 0x300
            )
            if status is None:
                raise RuntimeError("could not read guest status block")
            current = struct.unpack_from("<h", status, 0x1C2)[0]
            pressure = struct.unpack_from("<h", status, 0x2BC)[0]
            level = struct.unpack_from("<h", status, 0x00E)[0]
            demo = status[0x174]
            cells = struct.unpack_from("<30h", status, 0x21E)
            print(
                f"guest_base=0x{guest_base:X} "
                f"rng_current=0x{struct.unpack('<I', before)[0]:08X} "
                f"demo={demo} level={level} pressure={pressure} current={current} "
                f"nonzero_cells={sum(value != 0 for value in cells)}",
                flush=True,
            )
        else:
            write_memory(handle, rng_address, struct.pack("<I", args.seed & 0xFFFF))
            after = read_memory(handle, rng_address, 4)
            if after != struct.pack("<I", args.seed & 0xFFFF):
                raise RuntimeError("seed write did not persist")
            print(
                f"guest_base=0x{guest_base:X} rng_before=0x{struct.unpack('<I', before)[0]:08X} "
                f"rng_seeded=0x{args.seed & 0xFFFF:04X}",
                flush=True,
            )

        calls_by_state = random_states(args.seed, 1000)
        last_reported: tuple[int | None, int, int, int, int] | None = None
        started = time.monotonic()
        last_enemy_periods: tuple[int, ...] | None = None
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            rng = read_memory(handle, rng_address, 4)
            cell_words = read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x5E1E, 60
            )
            cell_flags = read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x5E5A, 30
            )
            current_raw = read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x5DC2, 2
            )
            scheduler = read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x30FC, 16 * 20
            )
            if (
                rng is None
                or cell_words is None
                or cell_flags is None
                or current_raw is None
                or scheduler is None
            ):
                raise RuntimeError("guest process memory became unreadable")
            state = struct.unpack("<I", rng)[0]
            call = calls_by_state.get(state)
            current = struct.unpack("<h", current_raw)[0]
            cell0 = struct.unpack_from("<h", cell_words, 0)[0]
            cell6 = struct.unpack_from("<h", cell_words, 12)[0]
            demo_record = "missing"
            enemy_records: list[str] = []
            enemy_periods: list[int] = []
            for slot in range(16):
                record = scheduler[slot * 20 : (slot + 1) * 20]
                record_id, selector = struct.unpack_from("<hh", record)
                period, countdown = struct.unpack_from("<ii", record, 8)
                if selector == 5:
                    demo_record = (
                        f"slot{slot}:id{record_id}:period{period}:countdown{countdown}"
                    )
                if selector == 1:
                    enemy_records.append(
                        f"slot{slot}:id{record_id}:period{period}:countdown{countdown}"
                    )
                    enemy_periods.append(period)
            snapshot = (call, current, cell0, cell6, cell_flags[6])
            if snapshot != last_reported and (
                (call is not None and 330 <= call <= 380)
                or (last_reported is not None and snapshot[1:] != last_reported[1:])
            ) or tuple(enemy_periods) != last_enemy_periods:
                print(
                    f"elapsed={time.monotonic() - started:.6f} call={call} "
                    f"state=0x{state:08X} current={current} "
                    f"cell0={cell0} cell6={cell6} flag6=0x{cell_flags[6]:02X}",
                    f"demo_record={demo_record} "
                    f"enemy_records={','.join(enemy_records)}",
                    flush=True,
                )
                last_reported = snapshot
                last_enemy_periods = tuple(enemy_periods)
            time.sleep(0.002)
        return 0
    finally:
        kernel32.CloseHandle(handle)


if __name__ == "__main__":
    raise SystemExit(main())
