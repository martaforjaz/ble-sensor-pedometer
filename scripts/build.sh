#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arduino-cli core update-index
arduino-cli core install arduino:mbed_nano@4.2.1
arduino-cli lib install ArduinoBLE@1.4.1 Arduino_LSM9DS1@1.1.0
scratch=$(mktemp -d)
trap 'rm -r "$scratch"' EXIT
mkdir "$scratch/entrega"
# Preserve the submitted file. Correct its misspelled variable in the build copy.
sed 's/\<filtererdDistance\>/filteredDistance/g' ist-submission/entrega/entrega.ino > "$scratch/entrega/entrega.ino"
arduino-cli compile --fqbn arduino:mbed_nano:nano33ble "$scratch/entrega"
arduino-cli compile --fqbn arduino:mbed_nano:nano33ble firmware/pedometer
