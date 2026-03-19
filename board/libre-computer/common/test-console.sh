#!/bin/bash
# U-Boot Console Test Runner
#
# Sends test-console.txt commands to a u-boot serial console and
# captures output. Requires the board to be at the u-boot prompt.
#
# Usage:
#   ./test-console.sh /dev/ttyUSB0        # serial UART
#   ./test-console.sh /dev/ttyACM0        # USB ACM
#   ./test-console.sh /dev/ttyUSB0 2>&1 | tee /tmp/uboot-test.log
#
# The script sends one command at a time with delays to avoid
# overwhelming the serial buffer. Output is printed to stdout.

set -e

SERIAL="${1:-/dev/ttyUSB0}"
BAUD="${2:-115200}"
DELAY="${3:-0.2}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_FILE="$SCRIPT_DIR/test-console.txt"

if [ ! -c "$SERIAL" ]; then
    echo "Error: $SERIAL not found or not a character device"
    echo "Usage: $0 <serial-device> [baud] [delay-between-commands]"
    echo "  e.g.: $0 /dev/ttyUSB0 115200 0.2"
    exit 1
fi

if [ ! -f "$TEST_FILE" ]; then
    echo "Error: $TEST_FILE not found"
    exit 1
fi

# Configure serial port
stty -F "$SERIAL" "$BAUD" raw -echo -echoe -echok

# Start background reader to display output
cat "$SERIAL" &
READER_PID=$!
trap "kill $READER_PID 2>/dev/null; exit" INT TERM EXIT

echo "Sending test commands to $SERIAL at ${BAUD}baud..."
echo "Delay between commands: ${DELAY}s"
echo ""

# Send each line with delay
while IFS= read -r line; do
    # Skip empty lines and comments
    [ -z "$line" ] && continue
    [[ "$line" =~ ^# ]] && continue

    # Send command with newline
    echo "$line" > "$SERIAL"
    sleep "$DELAY"

    # Extra delay for slow commands
    case "$line" in
        *"usb start"*|*"sf probe"*|*"mmc dev"*)
            sleep 1
            ;;
    esac
done < "$TEST_FILE"

# Wait for final output
sleep 2
echo ""
echo "Test complete. Check output above for PASS/FAIL/SKIP lines."
echo "Quick summary: grep -c PASS, grep -c FAIL, grep -c SKIP"
