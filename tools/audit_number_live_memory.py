#!/usr/bin/env python3
"""Seed or inspect a live Number Munchers image in a headless DOSBox-X process."""

from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import struct
import time

import audit_word_live_memory as memory


ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "analysis/NM-unpacked-image.bin"
DGROUP_IMAGE_OFFSET = 0x27340
RNG_OFFSET = 0x5076


def find_guest_image(handle: int, image: bytes) -> int:
    """Locate NM's normalized image through relocation-free code stretches."""
    # The on-disk LZEXE image expands to the same static data tables while a
    # few unpacker-adjusted code bytes differ from the normalized audit image.
    # Anchor on the immutable five-species string table, then cross-check two
    # other relocation-free data/code spans.
    primary_offset = 165395
    primary = image[primary_offset : primary_offset + 44]
    checks = ((171584, 24), (163387, 24))
    address = 0
    maximum = (1 << 47) - 1
    while address < maximum:
        info = memory.MemoryBasicInformation()
        if not memory.kernel32.VirtualQueryEx(
            handle,
            ctypes.c_void_p(address),
            ctypes.byref(info),
            ctypes.sizeof(info),
        ):
            break
        base = int(info.BaseAddress or 0)
        size = int(info.RegionSize)
        if (
            info.State == memory.MEM_COMMIT
            and not (info.Protect & memory.PAGE_GUARD)
            and info.Protect != memory.PAGE_NOACCESS
            and size > len(primary)
        ):
            chunk_size = 1 << 20
            overlap = len(primary) - 1
            cursor = base
            tail = b""
            while cursor < base + size:
                wanted = min(chunk_size, base + size - cursor)
                data = memory.read_memory(handle, cursor, wanted)
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
                        memory.read_memory(handle, guest_base + offset, length)
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
    raise RuntimeError("could not locate the loaded Number Munchers guest image")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=6)
    parser.add_argument("--preserve-seed", action="store_true")
    parser.add_argument("--read-only", action="store_true")
    parser.add_argument("--level", type=int)
    parser.add_argument("--score", type=int)
    parser.add_argument("--reserves", type=int)
    parser.add_argument("--wait-seconds", type=float, default=10.0)
    parser.add_argument("--wait-for-demo-seconds", type=float, default=0.0)
    parser.add_argument("--wait-for-user-game-seconds", type=float, default=0.0)
    args = parser.parse_args()
    if args.read_only and any(
        value is not None for value in (args.level, args.score, args.reserves)
    ):
        parser.error("--read-only cannot be combined with state writes")
    if args.level is not None and not 1 <= args.level <= 0x7FFF:
        parser.error("--level must be in 1..32767")
    if args.score is not None and not 0 <= args.score <= 0x7FFFFFFF:
        parser.error("--score must be in 0..2147483647")
    if args.reserves is not None and not 0 <= args.reserves <= 0x7FFF:
        parser.error("--reserves must be in 0..32767")

    handle = memory.kernel32.OpenProcess(
        memory.PROCESS_QUERY_INFORMATION
        | memory.PROCESS_VM_OPERATION
        | memory.PROCESS_VM_READ
        | memory.PROCESS_VM_WRITE,
        False,
        args.pid,
    )
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        image = IMAGE.read_bytes()
        deadline = time.monotonic() + args.wait_seconds
        error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                guest_base = find_guest_image(handle, image)
                break
            except RuntimeError as caught:
                error = caught
                time.sleep(0.05)
        else:
            raise RuntimeError("could not locate the loaded Number Munchers guest image") from error

        rng_address = guest_base + DGROUP_IMAGE_OFFSET + RNG_OFFSET
        before = memory.read_memory(handle, rng_address, 4)
        if before is None:
            raise RuntimeError("could not read the guest RNG")
        if not args.read_only and not args.preserve_seed:
            wanted = struct.pack("<I", args.seed & 0xFFFF)
            memory.write_memory(handle, rng_address, wanted)
            after = memory.read_memory(handle, rng_address, 4)
            if after != wanted:
                raise RuntimeError("seed write did not persist")
        else:
            after = before
        def gameplay_state() -> tuple[int, int, int, tuple[int, int, int]]:
            raw = memory.read_memory(
                handle, guest_base + DGROUP_IMAGE_OFFSET + 0x5868, 0x252
            )
            if raw is None:
                raise RuntimeError("could not read Number gameplay state")
            return (
                raw[0x104],
                struct.unpack_from("<h", raw, 0)[0],
                struct.unpack_from("<h", raw, 0x24A)[0],
                struct.unpack_from("<3h", raw, 0x190),
            )

        if args.wait_for_demo_seconds > 0 and args.wait_for_user_game_seconds > 0:
            parser.error("choose only one gameplay wait mode")
        if args.wait_for_demo_seconds > 0:
            demo_deadline = time.monotonic() + args.wait_for_demo_seconds
            while True:
                demo, level, pressure, enemy_types = gameplay_state()
                if demo and level > 0 and any(enemy_types):
                    after = memory.read_memory(handle, rng_address, 4)
                    if after is None:
                        raise RuntimeError("could not read the post-setup guest RNG")
                    break
                if time.monotonic() >= demo_deadline:
                    raise RuntimeError("timed out waiting for Number Demo initialization")
                time.sleep(0.001)
        elif args.wait_for_user_game_seconds > 0:
            user_deadline = time.monotonic() + args.wait_for_user_game_seconds
            while True:
                demo, level, pressure, enemy_types = gameplay_state()
                if not demo and level > 0:
                    break
                if time.monotonic() >= user_deadline:
                    raise RuntimeError("timed out waiting for Number user-game initialization")
                time.sleep(0.001)
        else:
            demo, level, pressure, enemy_types = gameplay_state()

        state_address = guest_base + DGROUP_IMAGE_OFFSET
        if args.level is not None:
            memory.write_memory(handle, state_address + 0x5868,
                                struct.pack("<h", args.level))
        if args.score is not None:
            memory.write_memory(handle, state_address + 0x5864,
                                struct.pack("<i", args.score))
        if args.reserves is not None:
            memory.write_memory(handle, state_address + 0x5998,
                                struct.pack("<h", args.reserves))
        if any(value is not None for value in (args.level, args.score, args.reserves)):
            demo, level, pressure, enemy_types = gameplay_state()
            score_bytes = memory.read_memory(handle, state_address + 0x5864, 4)
            reserves_bytes = memory.read_memory(handle, state_address + 0x5998, 2)
            if score_bytes is None or reserves_bytes is None:
                raise RuntimeError("could not verify Number HUD-state write")
            installed_score = struct.unpack("<i", score_bytes)[0]
            installed_reserves = struct.unpack("<h", reserves_bytes)[0]
        else:
            installed_score = None
            installed_reserves = None
        print(
            f"guest_base=0x{guest_base:X} "
            f"rng_before=0x{struct.unpack('<I', before)[0]:08X} "
            f"rng_after=0x{struct.unpack('<I', after)[0]:08X} "
            f"demo={demo} level={level} pressure={pressure} "
            f"enemy_types={enemy_types} "
            f"score={installed_score} reserves={installed_reserves}",
            flush=True,
        )
        return 0
    finally:
        memory.kernel32.CloseHandle(handle)


if __name__ == "__main__":
    raise SystemExit(main())
