#!/usr/bin/env python3

import argparse
import csv
import signal
import struct
import string
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import serial


HEADER = b"\x55\xAA"
CMD_ADC_RAW = 0x00
DEFAULT_PORT = "/dev/tty.usbmodem59090456051"
DEFAULT_BAUD = 38400


@dataclass(frozen=True)
class DecodedFrame:
    address: int
    payload_length: int
    command: int
    adc_raw: int
    timestamp_ms: int
    crc_received: int
    crc_calculated: int
    raw_frame_hex: str


def calculate_custom_crc(buffer: bytes) -> int:
    crc = 0xA5A5

    for index, value in enumerate(buffer):
        crc = (crc + value) & 0xFFFF
        crc = ((crc << 1) | (crc >> 15)) & 0xFFFF
        crc ^= (0x1021 + index) & 0xFFFF

    return crc


def parse_hex_frame_line(line: str) -> bytes | None:
    stripped = line.strip()
    if not stripped:
        return None

    parts = stripped.split()
    if len(parts) < 2 or parts[0:2] != ["55", "AA"]:
        return None

    if any(len(part) != 2 or any(ch not in string.hexdigits for ch in part) for part in parts):
        raise ValueError(f"Invalid hex byte sequence: {stripped}")

    frame = bytes(int(part, 16) for part in parts)
    if len(frame) < 4:
        raise ValueError(f"Frame too short: {len(frame)} bytes")

    expected_length = frame[3] + 6
    if len(frame) != expected_length:
        raise ValueError(
            f"Frame length mismatch: got {len(frame)} bytes, expected {expected_length}"
        )

    return frame


def decode_frame(frame: bytes) -> DecodedFrame:
    if len(frame) < 13:
        raise ValueError(f"Frame too short: {len(frame)} bytes")

    address = frame[2]
    payload_length = frame[3]
    command = frame[4]
    adc_raw = struct.unpack_from("<H", frame, 5)[0]
    timestamp_ms = struct.unpack_from("<I", frame, 7)[0]
    crc_received = struct.unpack_from("<H", frame, len(frame) - 2)[0]
    crc_calculated = calculate_custom_crc(frame[:-2])

    return DecodedFrame(
        address=address,
        payload_length=payload_length,
        command=command,
        adc_raw=adc_raw,
        timestamp_ms=timestamp_ms,
        crc_received=crc_received,
        crc_calculated=crc_calculated,
        raw_frame_hex=" ".join(f"{byte:02X}" for byte in frame),
    )


def build_output_path(output: str | None) -> Path:
    if output:
        return Path(output).expanduser().resolve()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path.cwd() / f"serial_record_{timestamp}.csv"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Record STM32 ADC UART frames, validate CRC, and save decoded data to CSV."
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial device path")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument("--output", help="Output CSV path")
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.2,
        help="Serial read timeout in seconds",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Disable per-frame console output",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_path = build_output_path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    stop_requested = False

    def handle_stop(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGTERM, handle_stop)

    rows_written = 0
    crc_failures = 0
    ignored_lines = 0

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser, output_path.open(
        "w", newline="", encoding="utf-8"
    ) as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            [
                "host_time_us",
                "address",
                "payload_length",
                "command",
                "adc_raw",
                "device_timestamp_ms",
                "crc_received",
                "crc_calculated",
                "raw_frame_hex",
            ]
        )

        print(f"Listening on {args.port} @ {args.baud} baud")
        print(f"Writing decoded frames to {output_path}")
        print("Expecting ASCII hex frames like: 55 AA 01 07 00 34 12 78 56 34 12 AB CD")

        while not stop_requested:
            raw_line = ser.readline()
            if not raw_line:
                continue

            line = raw_line.decode("ascii", errors="ignore")

            try:
                frame = parse_hex_frame_line(line)
            except ValueError as exc:
                crc_failures += 1
                if not args.quiet:
                    print(f"Skipping malformed frame: {exc}", file=sys.stderr)
                continue

            if frame is None:
                ignored_lines += 1
                if not args.quiet and line.strip():
                    print(f"Ignoring non-frame line: {line.strip()}", file=sys.stderr)
                continue

            decoded = decode_frame(frame)

            if decoded.crc_received != decoded.crc_calculated:
                crc_failures += 1
                if not args.quiet:
                    print(
                        "CRC mismatch "
                        f"addr=0x{decoded.address:02X} "
                        f"rx=0x{decoded.crc_received:04X} "
                        f"calc=0x{decoded.crc_calculated:04X} "
                        f"frame={decoded.raw_frame_hex}",
                        file=sys.stderr,
                    )
                continue

            if decoded.command != CMD_ADC_RAW:
                if not args.quiet:
                    print(
                        f"Skipping unsupported command 0x{decoded.command:02X}: {decoded.raw_frame_hex}",
                        file=sys.stderr,
                    )
                continue

            host_time_us = time.time_ns() // 1000
            writer.writerow(
                [
                    host_time_us,
                    decoded.address,
                    decoded.payload_length,
                    decoded.command,
                    decoded.adc_raw,
                    decoded.timestamp_ms,
                    f"0x{decoded.crc_received:04X}",
                    f"0x{decoded.crc_calculated:04X}",
                    decoded.raw_frame_hex,
                ]
            )
            csv_file.flush()
            rows_written += 1

            if not args.quiet:
                print(
                    f"{host_time_us} addr=0x{decoded.address:02X} "
                    f"adc={decoded.adc_raw} tick_ms={decoded.timestamp_ms}"
                )

    print(f"Stopped. Saved {rows_written} valid frames to {output_path}")
    if ignored_lines:
        print(f"Ignored {ignored_lines} non-frame text lines", file=sys.stderr)
    if crc_failures:
        print(f"Ignored {crc_failures} malformed or CRC-failed frames", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())