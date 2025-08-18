#!/bin/bash

BIN_DIR="./bin"
LOG_INTERVAL=1  # Check every 1 second

# Start main program with all output suppressed
cd $BIN_DIR
"./main" > /dev/null &
MAIN_PID=$!
cd ..

# Function to calculate total byte counts
get_byte_counts() {
    # Sum sizes of all .idx files (bytes)
    idx_bytes=$(find "$BIN_DIR" -name "*.idx" -type f -printf "%s + " 2>/dev/null | sed 's/+ $//')
    idx_total=$(( ${idx_bytes:-0} + 0 ))  # Handle empty case
    
    # Sum sizes of all .dat files (bytes)
    dat_bytes=$(find "$BIN_DIR" -name "*.dat" -type f -printf "%s + " 2>/dev/null | sed 's/+ $//')
    dat_total=$(( ${dat_bytes:-0} + 0 ))
    
    echo "$(date '+%H:%M:%S') - IDX: $(numfmt --to=iec $idx_total) | DAT: $(numfmt --to=iec $dat_total)"
}

# Cleanup on Ctrl+C
cleanup() {
    kill $MAIN_PID 2>/dev/null
    exit 0
}
trap cleanup SIGINT

# Main loop
echo "Monitoring file sizes (Press Ctrl+C to stop)..."
while true; do
    get_byte_counts
    sleep $LOG_INTERVAL
done
