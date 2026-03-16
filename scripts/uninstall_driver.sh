#!/bin/bash
set -e

DRIVER_NAME="NoiseAI.driver"
INSTALL_DIR="/Library/Audio/Plug-Ins/HAL"

echo "Uninstalling $DRIVER_NAME..."

if [ -d "$INSTALL_DIR/$DRIVER_NAME" ]; then
    sudo rm -rf "$INSTALL_DIR/$DRIVER_NAME"
    echo "Restarting coreaudiod..."
    sudo killall -9 coreaudiod || true
    echo "Done! Driver removed."
else
    echo "Driver not found at $INSTALL_DIR/$DRIVER_NAME"
fi
