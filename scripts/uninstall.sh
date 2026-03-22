#!/bin/bash
set -e

echo "=== NoiseAI Uninstaller ==="
echo ""

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "This script requires administrator privileges."
    echo "Please run: sudo $0"
    exit 1
fi

# Remove the application
APP_PATH="/Applications/NoiseAI.app"
if [ -d "$APP_PATH" ]; then
    echo "Removing $APP_PATH..."
    rm -rf "$APP_PATH"
    echo "  Done."
else
    echo "Application not found at $APP_PATH (skipping)"
fi

# Remove the audio driver
DRIVER_PATH="/Library/Audio/Plug-Ins/HAL/NoiseAI.driver"
if [ -d "$DRIVER_PATH" ]; then
    echo "Removing $DRIVER_PATH..."
    rm -rf "$DRIVER_PATH"
    echo "  Done."
else
    echo "Driver not found at $DRIVER_PATH (skipping)"
fi

# Restart coreaudiod to unload the driver
echo "Restarting coreaudiod..."
killall -9 coreaudiod 2>/dev/null || true
echo "  Done."

# Optionally remove user preferences
PLIST_PATH="$HOME/Library/Preferences/com.noiseai.app.plist"
if [ -f "$PLIST_PATH" ]; then
    echo "Removing user preferences..."
    rm -f "$PLIST_PATH"
    echo "  Done."
fi

echo ""
echo "NoiseAI has been completely uninstalled."
