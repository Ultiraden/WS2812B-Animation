#!/usr/bin/env python3
"""
Teensy Sync Broadcaster (USB-only scan)

- Scans ONLY USB serial ports (skips Bluetooth/modem/virtual COM ports that cause write timeouts)
- Sends DISCOVER? to each candidate and parses: "ID <BOARD_ID> FW <FW_VERSION> CAPS <...>"
- Broadcasts sync commands to all discovered Teensies near-simultaneously

Usage:
  python teensy_sync.py list
  python teensy_sync.py sync-waveall --delay 800 --period 30 --speed 25
  python teensy_sync.py sync-wave --map 3 --delay 800 --period 30 --speed 25
  python teensy_sync.py sync-row0 --map 1 --delay 800
  python teensy_sync.py sync-stop --delay 200
  python teensy_sync.py sync-waveall --delay 900 --only B1_BACK_LEFT B2_BACK_RIGHT

Notes (Windows):
- Close Arduino Serial Monitor / any serial tools before running.
- If your Teensy shows a different description string, run `python teensy_sync.py ports`
  to see what Windows reports and we can tweak the filter.
"""

from __future__ import annotations

import argparse
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import serial
from serial.tools import list_ports

DISCOVER_CMD = "DISCOVER?\n"
BAUD = 115200


@dataclass
class Board:
    port: str
    board_id: str
    fw: str
    caps: str
    desc: str


def open_port(port: str, timeout: float = 0.25, write_timeout: float = 0.6) -> serial.Serial:
    # Short timeouts to keep scanning snappy; write_timeout prevents hangs.
    return serial.Serial(
        port=port,
        baudrate=BAUD,
        timeout=timeout,
        write_timeout=write_timeout,
    )


def read_lines_for(ser: serial.Serial, duration_s: float) -> List[str]:
    end = time.time() + duration_s
    lines: List[str] = []
    while time.time() < end:
        try:
            raw = ser.readline()
        except Exception:
            break
        if not raw:
            continue
        try:
            line = raw.decode(errors="ignore").strip()
        except Exception:
            continue
        if line:
            lines.append(line)
    return lines


def parse_id_line(line: str) -> Optional[Tuple[str, str, str]]:
    # Expected: "ID <BOARD_ID> FW <FW_VERSION> CAPS <CAPS...>"
    if not line.startswith("ID "):
        return None
    parts = line.split()
    try:
        i_fw = parts.index("FW")
        i_caps = parts.index("CAPS")
    except ValueError:
        return None
    if len(parts) < 2 or i_fw + 1 >= len(parts):
        return None
    board_id = parts[1]
    fw = parts[i_fw + 1]
    caps = " ".join(parts[i_caps + 1 :]) if (i_caps + 1) < len(parts) else ""
    return board_id, fw, caps


def is_usb_serial_port(p: list_ports.ListPortInfo) -> bool:
    """
    USB-only filter (Windows-friendly):
    Accept ports that look like USB CDC serial devices. Reject bluetooth/modem/virtual ports.
    """
    desc = (p.description or "").lower()
    hwid = (p.hwid or "").lower()
    manuf = (getattr(p, "manufacturer", "") or "").lower()

    # Strong accepts
    if "teensy" in desc or "teensy" in manuf or "teensy" in hwid:
        return True
    if "usb serial device" in desc:
        return True
    if "usb" in desc and "bluetooth" not in desc:
        return True
    if "usb" in hwid and "bluetooth" not in hwid:
        return True

    # Strong rejects
    if "bluetooth" in desc or "bluetooth" in hwid:
        return False
    if "modem" in desc or "modem" in hwid:
        return False
    if "standard serial over bluetooth" in desc:
        return False

    return False


def list_all_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        print(f"{p.device:8}  {p.description}  | hwid={p.hwid}")


def discover_boards(
    scan_timeout_s: float = 0.35,
    settle_s: float = 0.05,
) -> List[Board]:
    ports_all = list(list_ports.comports())
    ports = [p for p in ports_all if is_usb_serial_port(p)]

    boards: List[Board] = []

    for p in ports:
        port_name = p.device
        desc = p.description or ""

        try:
            ser = open_port(port_name, timeout=scan_timeout_s, write_timeout=0.6)
        except Exception:
            continue

        try:
            # Clear buffers
            try:
                ser.reset_input_buffer()
                ser.reset_output_buffer()
            except Exception:
                pass

            time.sleep(settle_s)

            # Ask for ID
            try:
                ser.write(DISCOVER_CMD.encode("utf-8"))
                ser.flush()
            except serial.SerialTimeoutException:
                # Busy/locked/odd port despite filter. Skip.
                continue
            except Exception:
                continue

            # Read responses briefly
            lines = read_lines_for(ser, duration_s=scan_timeout_s)

            id_line = None
            for ln in lines:
                if ln.startswith("ID "):
                    id_line = ln
                    break

            if not id_line:
                # Try once more (some boards may print HELLO and take a moment)
                lines2 = read_lines_for(ser, duration_s=scan_timeout_s)
                for ln in lines2:
                    if ln.startswith("ID "):
                        id_line = ln
                        break

            if id_line:
                parsed = parse_id_line(id_line)
                if parsed:
                    bid, fw, caps = parsed
                    boards.append(Board(port=port_name, board_id=bid, fw=fw, caps=caps, desc=desc))
        finally:
            try:
                ser.close()
            except Exception:
                pass

    # De-dupe by board_id
    seen: set[str] = set()
    uniq: List[Board] = []
    for b in boards:
        if b.board_id in seen:
            continue
        seen.add(b.board_id)
        uniq.append(b)
    return uniq


def broadcast_command(boards: List[Board], cmd: str, per_port_timeout: float = 0.20) -> None:
    # Open all ports first, then fire writes back-to-back.
    sers: Dict[str, serial.Serial] = {}

    try:
        for b in boards:
            try:
                sers[b.board_id] = open_port(b.port, timeout=per_port_timeout, write_timeout=0.8)
            except Exception as e:
                print(f"[WARN] Could not open {b.board_id} @ {b.port}: {e}")

        if not sers:
            print("No ports could be opened.")
            return

        time.sleep(0.03)

        payload = (cmd.strip() + "\n").encode("utf-8")

        for bid, ser in sers.items():
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            try:
                ser.write(payload)
            except serial.SerialTimeoutException:
                print(f"[WARN] Write timeout to {bid} ({ser.port})")
            except Exception as e:
                print(f"[WARN] Write error to {bid} ({ser.port}): {e}")

        for ser in sers.values():
            try:
                ser.flush()
            except Exception:
                pass

        # Best-effort ACK read
        time.sleep(0.06)
        for bid, ser in sers.items():
            try:
                lines = read_lines_for(ser, duration_s=0.18)
            except Exception:
                lines = []
            if lines:
                print(f"\n[{bid}]")
                for ln in lines[-8:]:
                    print(" ", ln)

    finally:
        for ser in sers.values():
            try:
                ser.close()
            except Exception:
                pass


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ports", help="List all serial ports (debug).")
    sub.add_parser("list", help="Discover Teensies (USB-only) and print IDs/ports.")

    def add_only(p):
        p.add_argument("--only", nargs="*", default=None, help="Limit to these BOARD_IDs")

    sp_waveall = sub.add_parser("sync-waveall", help="Broadcast: sync waveall <delayMs> [period] [speedMs]")
    sp_waveall.add_argument("--delay", type=int, default=800)
    sp_waveall.add_argument("--period", type=int, default=30)
    sp_waveall.add_argument("--speed", type=int, default=25)
    add_only(sp_waveall)

    sp_wave = sub.add_parser("sync-wave", help="Broadcast: sync wave <mapId> <delayMs> [period] [speedMs]")
    sp_wave.add_argument("--map", type=int, required=True)
    sp_wave.add_argument("--delay", type=int, default=800)
    sp_wave.add_argument("--period", type=int, default=30)
    sp_wave.add_argument("--speed", type=int, default=25)
    add_only(sp_wave)

    sp_row0 = sub.add_parser("sync-row0", help="Broadcast: sync row0 <mapId> <delayMs>")
    sp_row0.add_argument("--map", type=int, required=True)
    sp_row0.add_argument("--delay", type=int, default=800)
    add_only(sp_row0)

    sp_stop = sub.add_parser("sync-stop", help="Broadcast: sync stop <delayMs>")
    sp_stop.add_argument("--delay", type=int, default=200)
    add_only(sp_stop)

    args = ap.parse_args()

    if args.cmd == "ports":
        list_all_ports()
        return 0

    boards = discover_boards()
    if not boards:
        print("No boards discovered (USB-only scan).")
        print("Try: python teensy_sync.py ports  (and ensure no Serial Monitor is open)")
        return 2

    if args.cmd == "list":
        print("Discovered boards:")
        for b in boards:
            print(f"  {b.board_id:16}  port={b.port:8}  fw={b.fw:12}  desc={b.desc}")
        return 0

    # Filter by --only
    if getattr(args, "only", None):
        want = set(args.only)
        boards = [b for b in boards if b.board_id in want]
        if not boards:
            print("No matching boards for --only.")
            return 2

    if args.cmd == "sync-waveall":
        cmd = f"sync waveall {args.delay} {args.period} {args.speed}"
    elif args.cmd == "sync-wave":
        cmd = f"sync wave {args.map} {args.delay} {args.period} {args.speed}"
    elif args.cmd == "sync-row0":
        cmd = f"sync row0 {args.map} {args.delay}"
    elif args.cmd == "sync-stop":
        cmd = f"sync stop {args.delay}"
    else:
        print("Unknown command.")
        return 2

    print("Target boards:")
    for b in boards:
        print(f"  {b.board_id} @ {b.port}  ({b.desc})")

    print(f"\nBroadcasting: {cmd}")
    broadcast_command(boards, cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
