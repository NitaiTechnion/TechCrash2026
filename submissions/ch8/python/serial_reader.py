import argparse
import re
import time

HEX_FRAME_RE = re.compile(r"^[0-9A-Fa-f]{3}$")


def decode_packed_hex(frame: str) -> dict:
    packed = int(frame, 16)
    sw10 = (packed >> 2) & 0x03FF
    key1_pressed = bool((packed >> 1) & 0x1)
    key0_pressed = bool(packed & 0x1)

    sw_bits = "".join("1" if (sw10 >> bit) & 1 else "0" for bit in range(9, -1, -1))

    return {
        "raw": frame.upper(),
        "packed": packed,
        "sw10": sw10,
        "sw_bits": sw_bits,
        "key0_pressed": key0_pressed,
        "key1_pressed": key1_pressed,
    }


def open_serial(port: str, baud: int):
    import serial

    return serial.Serial(port=port, baudrate=baud, timeout=0.5)


def run_reader(port: str, baud: int, reconnect: bool, reconnect_delay: float) -> None:
    print("[INFO] Opening serial port {} @ {}...".format(port, baud))

    while True:
        ser = None
        try:
            ser = open_serial(port, baud)
            print("[INFO] Connected. Waiting for frames (format HHH)...")

            while True:
                line = ser.readline()
                if not line:
                    continue

                text = line.decode("utf-8", errors="ignore").strip()
                if not text:
                    continue

                if not HEX_FRAME_RE.match(text):
                    print("[WARN] Ignoring malformed frame: {}".format(repr(text)))
                    continue

                decoded = decode_packed_hex(text)
                print(
                    "RAW={} | SW={} | KEY1={} KEY0={}".format(
                        decoded["raw"],
                        decoded["sw_bits"],
                        "P" if decoded["key1_pressed"] else "-",
                        "P" if decoded["key0_pressed"] else "-",
                    )
                )

        except Exception as exc:
            # Keep compatibility with Python environments missing pyserial,
            # and handle runtime serial disconnect errors.
            if exc.__class__.__name__ == "SerialException":
                print("[ERROR] Serial error: {}".format(exc))
            elif exc.__class__.__name__ == "ModuleNotFoundError" or (
                exc.__class__.__name__ == "ImportError" and "serial" in str(exc)
            ):
                print("[ERROR] Missing dependency: pyserial")
                print("[INFO] Install with: python -m pip install pyserial")
                break
            else:
                raise
            if not reconnect:
                break
            print("[INFO] Reconnecting in {:.1f}s...".format(reconnect_delay))
            time.sleep(reconnect_delay)
        except KeyboardInterrupt:
            print("\n[INFO] Stopped by user.")
            break
        finally:
            if ser is not None and ser.is_open:
                ser.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read compact FPGA controller frames (HHH) from ESP32 USB serial.",
    )
    parser.add_argument("--port", required=True, help="Serial port (example: COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--reconnect",
        action="store_true",
        help="Auto-reconnect if the serial device disconnects",
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=1.0,
        help="Seconds between reconnect attempts",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    run_reader(
        port=args.port,
        baud=args.baud,
        reconnect=args.reconnect,
        reconnect_delay=args.reconnect_delay,
    )


if __name__ == "__main__":
    main()
