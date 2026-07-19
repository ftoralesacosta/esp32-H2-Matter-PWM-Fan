#!/bin/bash
set -e
set -o pipefail

echo "🔧 Sourcing ESP-IDF and ESP-Matter environments..."
source "$HOME/Projects/esp32-H2/esp-idf/export.sh"
source "$HOME/Projects/esp32-H2/esp-matter/export.sh"

echo "==========================================="
echo " Matter Fan - Monitoring"
echo "==========================================="

echo "🎯 Setting target to Seeed Studio XIAO ESP32-C6..."
idf.py set-target esp32c6

idf.py monitor 2>&1 | tee monitor_log.txt
