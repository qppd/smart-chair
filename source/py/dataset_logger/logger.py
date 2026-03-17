#!/usr/bin/env python3
"""
Smart Chair Dataset Logger
===========================
Connects to the ESP32 over serial, streams sensor CSV rows, and saves them to
a CSV file for posture-classification model training.

Columns written
---------------
timestamp_ms, flex1, flex2, flex3, flex4,
pressure_FL, pressure_FR, pressure_RL, pressure_RR,
total_pressure, front_rear_ratio, left_right_ratio, label

Derived columns (computed on the ESP32)
----------------------------------------
total_pressure    = FL + FR + RL + RR
front_rear_ratio  = (FL + FR) / (RL + RR)
left_right_ratio  = (FL + RL) / (FR + RR)

Usage
-----
  python logger.py                             # interactive setup
  python logger.py -p COM3 -l good            # named port + label
  python logger.py -p COM3 -l slouch -r 50    # 50 ms sample rate
  python logger.py -p COM3 -o my_data.csv     # custom output file

Interactive label change while running
---------------------------------------
  Type a new label and press Enter during logging.
  The ESP32 will be updated and the next DATA: rows will carry the new label.
"""

import argparse
import csv
import os
import queue
import signal
import sys
import threading
import time
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial is not installed.")
    print("       Run:  pip install pyserial")
    sys.exit(1)

# ── Constants ─────────────────────────────────────────────────────────────────
CSV_HEADER = [
    "timestamp_ms",
    "flex1", "flex2", "flex3", "flex4",
    "pressure_FL", "pressure_FR", "pressure_RL", "pressure_RR",
    "total_pressure", "front_rear_ratio", "left_right_ratio",
    "label",
]
DATA_PREFIX = "DATA:"
HEADER_PREFIX = "DATA_HEADER:"
BAUD_DEFAULT = 115200
RATE_DEFAULT = 100   # ms
FLUSH_EVERY  = 20    # rows between CSV flushes


# ── Helpers ───────────────────────────────────────────────────────────────────

def list_ports() -> list[str]:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found. Check your USB connection.")
        return []
    print("\nAvailable serial ports:")
    for i, p in enumerate(ports):
        print(f"  [{i}]  {p.device:<12}  {p.description}")
    return [p.device for p in ports]


def choose_port(port_arg: str | None) -> str:
    if port_arg:
        return port_arg
    ports = list_ports()
    if not ports:
        sys.exit(1)
    if len(ports) == 1:
        print(f"Auto-selecting: {ports[0]}")
        return ports[0]
    while True:
        raw = input("\nSelect port number: ").strip()
        try:
            return ports[int(raw)]
        except (ValueError, IndexError):
            print("Invalid selection, try again.")


def default_filename(label: str) -> str:
    ts   = datetime.now().strftime("%Y%m%d_%H%M%S")
    slug = label.replace(" ", "_") or "unlabeled"
    return f"smart_chair_{slug}_{ts}.csv"


# ── Serial reader thread ──────────────────────────────────────────────────────

def serial_reader(ser: serial.Serial, data_q: queue.Queue, stop_evt: threading.Event):
    """Reads lines from serial and puts them into data_q."""
    while not stop_evt.is_set():
        try:
            raw = ser.readline()
            if raw:
                line = raw.decode("utf-8", errors="replace").strip()
                data_q.put(line)
        except serial.SerialException as exc:
            data_q.put(f"__SERIAL_ERROR__:{exc}")
            break


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Smart Chair Dataset Logger – streams ESP32 sensor data to CSV",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument("-p", "--port",  default=None,
                        help="Serial port  (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud",  type=int, default=BAUD_DEFAULT,
                        help=f"Baud rate  (default {BAUD_DEFAULT})")
    parser.add_argument("-o", "--out",   default=None,
                        help="Output CSV file path  (auto-generated if omitted)")
    parser.add_argument("-r", "--rate",  type=int, default=RATE_DEFAULT,
                        help=f"Sample interval in ms  (default {RATE_DEFAULT}, min 50)")
    parser.add_argument("-l", "--label", default="",
                        help="Posture label for this session  (e.g. good, slouch)")
    args = parser.parse_args()

    # ── Port & label ──────────────────────────────────────────────────────────
    port  = choose_port(args.port)
    label = args.label.strip()
    if not label:
        label = input("\nEnter posture label (e.g. good, slouch, lean_left): ").strip()
    if not label:
        label = "unlabeled"

    out_path = args.out or default_filename(label)
    rate_ms  = max(50, args.rate)

    print(f"\n{'─'*50}")
    print(f"  Port    : {port}")
    print(f"  Baud    : {args.baud}")
    print(f"  Output  : {out_path}")
    print(f"  Rate    : {rate_ms} ms")
    print(f"  Label   : '{label}'")
    print(f"{'─'*50}")
    print("\nConnecting…")

    # ── Open serial port ──────────────────────────────────────────────────────
    try:
        ser = serial.Serial(port, args.baud, timeout=2)
    except serial.SerialException as exc:
        print(f"ERROR: Cannot open {port}: {exc}")
        sys.exit(1)

    time.sleep(2)          # wait for ESP32 boot / reset
    ser.reset_input_buffer()

    # ── Send initial configuration to ESP32 ─────────────────────────────────
    def send(cmd: str):
        ser.write((cmd + "\n").encode())
        time.sleep(0.08)

    send(f"log stop")                  # stop any previous session
    send(f"label {label}")
    send(f"log rate {rate_ms}")
    send("log start")

    # ── Open / create CSV file ────────────────────────────────────────────────
    file_exists = os.path.isfile(out_path)
    csvfile = open(out_path, "a", newline="", encoding="utf-8")
    writer  = csv.writer(csvfile)
    if not file_exists:
        writer.writerow(CSV_HEADER)
        csvfile.flush()

    # ── Threading helpers ─────────────────────────────────────────────────────
    stop_evt  = threading.Event()
    data_q    = queue.Queue()

    reader_thread = threading.Thread(
        target=serial_reader, args=(ser, data_q, stop_evt), daemon=True
    )
    reader_thread.start()

    # ── Graceful shutdown on Ctrl-C ───────────────────────────────────────────
    def shutdown(sig=None, frame=None):
        stop_evt.set()

    signal.signal(signal.SIGINT,  shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── Interactive label-change thread ───────────────────────────────────────
    current_label = label

    def input_thread_func():
        nonlocal current_label
        print("\nTip: type a new label and press Enter to change it mid-session.")
        print("     Press Ctrl-C to stop logging.\n")
        while not stop_evt.is_set():
            try:
                new_lbl = input()
                if stop_evt.is_set():
                    break
                new_lbl = new_lbl.strip()
                if new_lbl:
                    current_label = new_lbl
                    send(f"label {new_lbl}")
                    print(f"[label → '{new_lbl}']")
            except (EOFError, OSError):
                break

    input_thread = threading.Thread(target=input_thread_func, daemon=True)
    input_thread.start()

    # ── Main logging loop ─────────────────────────────────────────────────────
    row_count = 0
    print(f"Logging started. Saving to: {out_path}\n")
    print(",".join(CSV_HEADER))
    print("─" * 100)

    while not stop_evt.is_set():
        try:
            line = data_q.get(timeout=0.5)
        except queue.Empty:
            continue

        # Serial read error from the reader thread
        if line.startswith("__SERIAL_ERROR__:"):
            print(f"\n{line[17:]}")
            stop_evt.set()
            break

        # CSV data row
        if line.startswith(DATA_PREFIX):
            row_str = line[len(DATA_PREFIX):]
            parts   = row_str.split(",")
            if len(parts) == len(CSV_HEADER):
                writer.writerow(parts)
                row_count += 1
                if row_count % FLUSH_EVERY == 0:
                    csvfile.flush()
                # Pretty-print to terminal
                print(row_str)
            # else: malformed row – silently skip

        elif line.startswith(HEADER_PREFIX):
            # Confirm column order matches expectation
            esp_cols = line[len(HEADER_PREFIX):].split(",")
            if esp_cols != CSV_HEADER:
                print(f"[WARN] ESP32 column order differs from expected!\n"
                      f"  ESP32  : {esp_cols}\n"
                      f"  Python : {CSV_HEADER}")

        else:
            # Debug / status lines from ESP32 – print dimly
            if line:
                print(f"[ESP32] {line}")

    # ── Cleanup ───────────────────────────────────────────────────────────────
    try:
        ser.write(b"log stop\n")
        time.sleep(0.1)
        ser.close()
    except Exception:
        pass

    csvfile.flush()
    csvfile.close()

    print(f"\n{'─'*50}")
    print(f"Logging stopped.")
    print(f"  Rows saved : {row_count}")
    print(f"  File       : {out_path}")
    print(f"{'─'*50}")


if __name__ == "__main__":
    main()
