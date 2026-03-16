#!/usr/bin/env bash
set -euo pipefail

echo "=== NoiseAI: Installing dependencies ==="

# Check for Homebrew
if ! command -v brew &>/dev/null; then
    echo "Error: Homebrew is required. Install from https://brew.sh"
    exit 1
fi

install_if_missing() {
    local cmd="$1"
    local pkg="${2:-$1}"
    if command -v "$cmd" &>/dev/null; then
        echo "  [OK] $cmd already installed"
    else
        echo "  [INSTALL] Installing $pkg via Homebrew..."
        brew install "$pkg"
    fi
}

install_if_missing cmake cmake
install_if_missing ninja ninja
install_if_missing xcodegen xcodegen

echo "=== All dependencies installed ==="
