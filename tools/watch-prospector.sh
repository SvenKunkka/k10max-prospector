#!/usr/bin/env bash
# Watch the Prospector's BLE observation console for a given device name and
# report whether its advertisement carries the Prospector payload.
#
# Usage:  ./watch-prospector.sh [seconds] [name-regex]
#   e.g.  ./watch-prospector.sh 30 'K10|Keychron'
#
# The Prospector's ZMK console runs at debug level and logs EVERY advertisement
# it observes, so this is the instrument that settles whether the keyboard's
# advertisement contains the 26-byte Prospector manufacturer data.

set -uo pipefail

DURATION="${1:-30}"
PATTERN="${2:-Keychron|K10|Prospector}"

PORT=""
for p in /dev/cu.usbmodem*; do
    [ -e "$p" ] || continue
    # The console-capable interface is the one that produces data.
    if python3 - "$p" <<'EOF' >/dev/null 2>&1
import sys, serial, time
try:
    s = serial.Serial(sys.argv[1], 115200, timeout=0.2)
except Exception:
    sys.exit(1)
t = time.time()
while time.time() - t < 1.5:
    if s.read(256):
        s.close(); sys.exit(0)
s.close(); sys.exit(1)
EOF
    then PORT="$p"; break; fi
done

if [ -z "$PORT" ]; then
    echo "No Prospector console found on /dev/cu.usbmodem*." >&2
    echo "Check the Prospector is plugged in:  ls /dev/cu.usbmodem*" >&2
    exit 1
fi

OUT="$(dirname "$0")/../evidence/prospector-console-$(date +%Y%m%d-%H%M%S).log"
echo "Listening on $PORT for ${DURATION}s (pattern: $PATTERN)"
echo "Logging to $OUT"

python3 - "$PORT" "$DURATION" > "$OUT" 2>&1 <<'EOF'
import sys, serial, time
port, dur = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.3)
buf, t = b"", time.time()
while time.time() - t < dur:
    d = s.read(8192)
    if d:
        buf += d
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            sys.stdout.write(line.decode("utf-8", "replace") + "\n")
            sys.stdout.flush()
s.close()
EOF

echo
echo "================ RESULT ================"
TOTAL=$(wc -l < "$OUT" | tr -d ' ')
echo "advertisements logged : $TOTAL"
echo "Prospector payloads   : $(grep -c 'Prospector data found' "$OUT" || true)"
echo "manufacturer too short: $(grep -c 'Manufacturer data too short' "$OUT" || true)"
echo "non-Prospector device : $(grep -c 'Non-Prospector device' "$OUT" || true)"
echo
echo "--- lines matching '$PATTERN' ---"
grep -iE "$PATTERN" "$OUT" | head -20 || echo "(no advertisement from a device matching the pattern)"

if grep -qiE "$PATTERN" "$OUT" && ! grep -q 'Prospector data found' "$OUT"; then
    echo
    echo ">>> The keyboard IS advertising, and the scanner sees it, but NO Prospector"
    echo ">>> manufacturer data was present. This is radio-level confirmation of the"
    echo ">>> blocker documented in docs/BLOCKER.md."
fi
