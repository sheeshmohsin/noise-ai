#!/bin/bash
set -e

DRIVER_NAME="NoiseAI.driver"
BUILD_DIR="build/driver"
INSTALL_DIR="/Library/Audio/Plug-Ins/HAL"

echo "Installing $DRIVER_NAME..."

if [ ! -d "$BUILD_DIR/$DRIVER_NAME" ]; then
    echo "Error: Driver not found at $BUILD_DIR/$DRIVER_NAME"
    echo "Run 'make driver' first."
    exit 1
fi

# Remove old version if exists
if [ -d "$INSTALL_DIR/$DRIVER_NAME" ]; then
    echo "Removing old driver..."
    sudo rm -rf "$INSTALL_DIR/$DRIVER_NAME"
fi

# Copy new driver
echo "Copying driver to $INSTALL_DIR..."
sudo cp -R "$BUILD_DIR/$DRIVER_NAME" "$INSTALL_DIR/"

# Restart coreaudiod to pick up the new driver
# Note: launchctl kickstart is blocked by SIP, but killall works
# because launchd automatically restarts coreaudiod
echo "Restarting coreaudiod..."
sudo killall -9 coreaudiod || true

echo "Done! 'NoiseAI Microphone' should now appear in System Settings -> Sound."
