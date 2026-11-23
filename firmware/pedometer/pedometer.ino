#include <ArduinoBLE.h>
#include <Arduino_LSM9DS1.h>
#include <math.h>

constexpr int triggerPin = 9, echoPin = 10;
constexpr float threshold = 0.12f;
BLEService service("93426600-02de-4f17-ac9d-24f41bbfc140");
BLECharacteristic summary("93426601-02de-4f17-ac9d-24f41bbfc140", BLERead | BLENotify, 16);
unsigned long steps = 0, lastStep = 0, lastPing = 0, lastReport = 0;
float baseline = 1.0f, wallDistance = -1.0f;
bool armed = true;

void setup() {
  Serial.begin(115200);
  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  pinMode(echoPin, INPUT);
  if (!IMU.begin() || !BLE.begin()) {
    Serial.println("IMU or BLE initialization failed");
    for (;;) delay(1000);
  }
  BLE.setLocalName("Sensor Pedometer");
  BLE.setAdvertisedService(service);
  service.addCharacteristic(summary);
  BLE.addService(service);
  BLE.advertise();
  Serial.println("steps,wall_distance_cm");
}

void loop() {
  BLE.poll();
  const unsigned long now = millis();
  if (IMU.accelerationAvailable()) {
    float x, y, z;
    if (IMU.readAcceleration(x, y, z)) {
      const float magnitude = sqrtf(x*x + y*y + z*z);
      baseline += 0.03f * (magnitude - baseline);
      const float motion = magnitude - baseline;
      if (motion < threshold / 2) armed = true;
      if (armed && motion > threshold) {
        armed = false;
        if (steps == 0 || now - lastStep >= 300) {
          ++steps;
          lastStep = now;
        }
      }
    }
  }
  if (now - lastPing >= 150) {
    lastPing = now;
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin, LOW);
    const unsigned long echo = pulseIn(echoPin, HIGH, 12000);
    wallDistance = echo ? echo * 0.0343f / 2 : -1.0f;
  }
  if (now - lastReport >= 1000) {
    lastReport = now;
    char packet[17];
    snprintf(packet, sizeof(packet), "%lu,%.1f", steps, wallDistance);
    summary.writeValue(reinterpret_cast<const uint8_t*>(packet), strlen(packet));
    Serial.println(packet);
  }
}
