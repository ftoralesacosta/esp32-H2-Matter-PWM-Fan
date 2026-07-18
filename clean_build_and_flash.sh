#!/bin/bash
set -e
set -o pipefail

echo "🔧 Sourcing ESP-IDF and ESP-Matter environments..."
source "$HOME/Projects/esp32-H2/esp-idf/export.sh"
source "$HOME/Projects/esp32-H2/esp-matter/export.sh"

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
idf.py build 2>&1 | tee build_log.txt

echo "⚡ Erasing flash and flashing device..."
idf.py erase-flash
idf.py flash monitor 2>&1 | tee monitor_log.txt
