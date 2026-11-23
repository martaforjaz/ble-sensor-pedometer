#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arduino-cli core update-index
arduino-cli core install arduino:mbed_nano@4.2.1
arduino-cli lib install ArduinoBLE@1.4.1 Arduino_LSM9DS1@1.1.0
arduino-cli compile --fqbn arduino:mbed_nano:nano33ble ist-submission/entrega
arduino-cli compile --fqbn arduino:mbed_nano:nano33ble firmware/pedometer
