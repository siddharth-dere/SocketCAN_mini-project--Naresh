#!/usr/bin/env bash
set -euo pipefail
DEVICE="${1:-vcan0}"
echo "[1/3] Loading vcan kernel module..."
sudo modprobe vcan
echo "[2/3] Recreating $DEVICE..."
if ip link show "$DEVICE" >/dev/null 2>&1; then
    sudo ip link set "$DEVICE" down || true
    sudo ip link delete "$DEVICE" type vcan || true
fi
sudo ip link add name "$DEVICE" type vcan
sudo ip link set "$DEVICE" up
echo "[3/3] Interface state:"
ip -details link show "$DEVICE"
