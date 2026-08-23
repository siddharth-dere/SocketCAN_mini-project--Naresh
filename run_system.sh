#!/usr/bin/env bash
set -euo pipefail
IFACE="${1:-vcan0}"
BASE="$(cd "$(dirname "$0")" && pwd)"
cd "$BASE"
mkdir -p runtime
[[ -x ./body_sensor_ecu ]] || make

pids=()
finish() {
    for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done
    wait || true
}
trap finish INT TERM EXIT

./body_diagnostic_ecu --interface "$IFACE" --log body_diagnostic_log.txt > runtime/diagnostic.log 2>&1 & pids+=("$!")
./body_controller_ecu --interface "$IFACE" > runtime/controller.log 2>&1 & pids+=("$!")
./body_actuator_ecu --interface "$IFACE" > runtime/actuator.log 2>&1 & pids+=("$!")
./body_sensor_ecu --interface "$IFACE" > runtime/sensor.log 2>&1 & pids+=("$!")

echo "All four ECUs started on $IFACE. Press Ctrl+C to stop."
wait
