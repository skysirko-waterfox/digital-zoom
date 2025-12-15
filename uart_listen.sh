#!/bin/sh

PORT="/dev/ttyS2"
BAUDRATE="57600"

# Configure UART: 57600 8N1, no flow control, no echo
stty -F "$PORT" $BAUDRATE cs8 -cstopb -parenb -ixon -ixoff -crtscts -echo

echo "Listening on $PORT at $BAUDRATE baud..."
echo "Waiting for commands: zoom_in / zoom_out"

# Read lines forever
while IFS= read -r line; do
    case "$line" in
        "zoom_in")
            echo ">> Command received: ZOOM IN"
            # place your actual logic here
            ;;
        "zoom_out")
            echo ">> Command received: ZOOM OUT"
            # place your actual logic here
            ;;
        *)
            # Ignore all other input
            ;;
    esac
done < "$PORT"
