#!/bin/bash
set -e

echo "==========================================="
echo " Matter Fan - Clean Build & Flash Script"
echo "==========================================="

echo "🔄 Pulling latest changes from Git..."
git pull

echo "🧹 Cleaning old configuration and build files..."
rm -f sdkconfig
rm -rf build

echo "🎯 Setting target to Seeed Studio XIAO ESP32-C6..."
idf.py set-target esp32c6

echo "🔨 Building the firmware..."
idf.py build

echo "⚡ Erasing flash and flashing device..."
idf.py erase-flash
idf.py flash monitor
