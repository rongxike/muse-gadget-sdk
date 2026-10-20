#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: tools/muse/deploy_box3.sh [--port PORT_OR_SERIAL] [--token MGST_TOKEN] [--flash-only]

Builds and flashes the ESP32-S3-BOX-3 profile.

Options:
  -p, --port PORT_OR_SERIAL  Explicit serial port (or USB serial selector for ports.py)
  -t, --token MGST_TOKEN     Send SDK token over serial after flashing (>sdk_token=...)
      --flash-only           Skip build and flash the current build output
  -h, --help                 Show this help
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PORT_SELECTOR=""
TOKEN=""
FLASH_ONLY=0

while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port)
      [ $# -ge 2 ] || { echo "error: --port needs a value" >&2; exit 2; }
      PORT_SELECTOR="$2"
      shift 2
      ;;
    -t|--token)
      [ $# -ge 2 ] || { echo "error: --token needs a value" >&2; exit 2; }
      TOKEN="$2"
      shift 2
      ;;
    --flash-only)
      FLASH_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument '$1'" >&2
      usage
      exit 2
      ;;
  esac
done

cd "$ROOT"

if [ "$FLASH_ONLY" -eq 0 ]; then
  echo ">> Building BOX-3 firmware"
  tools/muse/board.sh build box3
fi

echo ">> Flashing BOX-3 firmware"
if [ -n "$PORT_SELECTOR" ]; then
  tools/muse/board.sh flash box3 "$PORT_SELECTOR"
else
  tools/muse/board.sh flash box3
fi

if [ -n "$TOKEN" ]; then
  command -v python3 >/dev/null 2>&1 || { echo "error: python3 not found" >&2; exit 1; }

  RESOLVED_PORT="$(python3 "$ROOT/tools/muse/ports.py" box3 "${PORT_SELECTOR:-}")"
  [ -n "$RESOLVED_PORT" ] || { echo "error: failed to resolve BOX-3 serial port" >&2; exit 1; }

  echo ">> Sending SDK token to $RESOLVED_PORT"
  python3 - "$RESOLVED_PORT" "$TOKEN" <<'PY'
import sys
import time

port, token = sys.argv[1], sys.argv[2]

try:
    import serial  # type: ignore
except Exception as exc:
    raise SystemExit(f"error: pyserial is required to send token ({exc})")

with serial.Serial(port, 115200, timeout=1) as ser:
    time.sleep(1.5)
    ser.reset_input_buffer()
    ser.write(f">sdk_token={token}\n".encode("utf-8"))
    ser.flush()

    deadline = time.time() + 12
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        print(line)
        if line.startswith("@sdk_token ok"):
            raise SystemExit(0)
        if line.startswith("@sdk_token error"):
            raise SystemExit("error: device rejected SDK token")

    raise SystemExit("error: no SDK token acknowledgement from device")
PY
fi

echo ">> Done"
