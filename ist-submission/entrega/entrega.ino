#include <Wire.h>
#include <ArduinoBLE.h>

// ----- LSM9DS1 DEFINES -----
extern TwoWire Wire1; 
#define LSM9DS1_AG     0x6B
#define CTRL_REG6_XL   0x20
#define OUT_X_L_XL     0x28
const float ACC_SENS_2G = 16384.0;
// ----------------------------

// Ultrasonic pins
const int trigPin = 9;
const int echoPin = 10;

float duration, distance;

// Filtered distance (median of last 3)
float filteredDistance = 0.0f;
float distWindow[3];      // last 3 distance measurements
int   distWindowCount = 0;

// ISR variables
volatile unsigned long echoStart = 0;
volatile unsigned long echoEnd = 0;
volatile bool echoReady = false;
bool firstEcho = true;

// -----------------------------------

// ----- Step counting variables -----
float accMag      = 0.0f;  // instantaneous magnitude
float accMagFilt  = 0.0f;  // low-pass filtered magnitude
unsigned long lastStepTime = 0;
unsigned long stepCount = 0;

// ---- Dual-peak step detection (using accDyn) ----

// State of the detector
enum StepPhase {
  PHASE_SEEK_POS,  // waiting for a positive bump above posThresh
  PHASE_SEEK_NEG   // we saw a positive bump; now waiting for a negative valley
};

StepPhase stepPhase = PHASE_SEEK_POS;

// Track whether we're currently inside a neg/pos "region"
bool inNegRegion = false;
bool inPosRegion = false;

// Times of first crossing of thresholds for current step
unsigned long negCrossTimeMs = 0;  // when accDyn first goes below negThresh
unsigned long posCrossTimeMs = 0;  // when accDyn first goes above posThresh

// Thresholds in g (after gravity removal). You will tune these.
const float negThresh = -0.05f;   // require at least -0.05 g valley
const float posThresh =  0.1f;   // require at least +0.05 g peak

// Time window between valley and peak (the "step" duration)
const unsigned long minStepIntervalMs = 200;  // ~ steps/s upper bound
const unsigned long maxStepIntervalMs = 700;  // ~1 steps/s lower bound
const unsigned long stepDebounceMs = 150; // interval between steps
// -----------------------------------

// ---- Step Length Variables ----
float totalStepDistance   = 0.0f;
float averageStepLength   = 0.0f;
float stepLen             = 0.0f;
float defaultStepLen      = 60.0f;   // cm, fallback when distance is bad

float prevStepDist        = 0.0f;   // distance at previous step

float minStepLen          = 10.0f;  // ignore tiny “steps” < 15 cm
float maxStepLen          = 150.0f; // ignore absurd “steps” > 100 cm
// -----------------------------------

// ---------- BLE definitions ----------
// Custom service UUID (subject to change)
BLEService pedometerService("12345678-1234-5678-1234-56789abcdef0");

// Single characteristic holding CSV text: "steps,stepLen,distance,filteredAz"
BLECharacteristic pedoDataChar(
  "12345678-1234-5678-1234-56789abcdef1",  // you can reuse a UUID
  BLERead | BLENotify,
  64  // max length of the CSV string
);

unsigned long lastBLEsend = 0;
const unsigned long BLEsendInterval = 1000;
// -------------------------------------

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  Serial.begin(9600);

  attachInterrupt(digitalPinToInterrupt(echoPin), echoISR, CHANGE);

  // ---- Accelerometer INIT ----
  Wire1.begin();
  delay(10);
  Wire1.beginTransmission(LSM9DS1_AG);
  Wire1.write(CTRL_REG6_XL);
  Wire1.write(0b01100000);   // 100Hz, ±2g
  Wire1.endTransmission();
  // ----------------------------

  // ---- BLE INIT ----
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);   // fica bloqueado se BLE falhar
  }

  BLE.setLocalName("PM belo");           // nome que aparece no telemóvel
  BLE.setAdvertisedService(pedometerService);

  // Only one characteristic now
  pedometerService.addCharacteristic(pedoDataChar);

  BLE.addService(pedometerService);

  // Initial CSV value
  char initBuf[64];
  snprintf(initBuf, sizeof(initBuf), "0,0.00,0.00,0.000");
  pedoDataChar.writeValue((const unsigned char*)initBuf, strlen(initBuf));

  // começar a anunciar
  BLE.advertise();
  Serial.println("BLE pedometer is advertising...");
}

void loop() {

  BLE.poll();
  
  // ---------- Ultrasonic Trigger ----------
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  

  if (echoReady) {
    echoReady = false;
  
    duration = echoEnd - echoStart;
    distance = (duration * 0.0343) / 2.0;

    // --- Update 3-sample window ---
    distWindow[distWindowCount % 3] = distance;
    if (distWindowCount < 3) {
      distWindowCount++;
    }

    // --- Compute filtered distance (median of last 3) ---
    if (distWindowCount < 3) {
      // Not enough samples yet -> just use raw
      filteredDistance = distance;
    } else {
      filteredDistance = median3(distWindow[0], distWindow[1], distWindow[2]);
    }

    if(firstEcho) {
      firstEcho = false;
      prevStepDist = distance;
    }
  }

  // ---------- Accelerometer Read ----------
  Wire1.beginTransmission(LSM9DS1_AG);
  Wire1.write(OUT_X_L_XL | 0x80); 
  Wire1.endTransmission(false);

  Wire1.requestFrom(LSM9DS1_AG, (uint8_t)6);
  int16_t ax_raw = Wire1.read() | (Wire1.read() << 8);
  int16_t ay_raw = Wire1.read() | (Wire1.read() << 8);
  int16_t az_raw = Wire1.read() | (Wire1.read() << 8);

  float ax = ax_raw / ACC_SENS_2G;      
  float ay = ay_raw / ACC_SENS_2G;
  float az = az_raw / ACC_SENS_2G;

  // Compute acceleration magnitude (orientation independent)
  accMag = sqrtf(ax * ax + ay * ay + az * az);   // in g units, ~1g at rest

  // Low-pass filter on magnitude
  const float alpha = 0.9f;                      // smoothing
  accMagFilt = alpha * accMagFilt + (1.0f - alpha) * accMag;

  // Remove gravity baseline AFTER smoothing
  float accDyn = accMagFilt - 1.0f;   // dynamic acceleration around zero

  unsigned long nowMs = millis();

  // ---------- Dual-peak step detection on accDyn ----------

  switch (stepPhase) {

    case PHASE_SEEK_POS:
        // We are looking for the signal to enter the positive region first
        if (accDyn > posThresh) {
          if (!inPosRegion) {
            // Just entered positive region: store FIRST crossing time
            inPosRegion    = true;
            posCrossTimeMs = nowMs;
          }
          // No need to track max; first crossing time is enough
        } else if (inPosRegion && accDyn <= posThresh) {
          // Left the positive region: positive lobe is done,
          // now look for the negative part of the step.
          inPosRegion = false;
          stepPhase   = PHASE_SEEK_NEG;
        }
        break;

    case PHASE_SEEK_NEG:
        // If it's been too long since the positive crossing, abandon and reset
        if (nowMs - posCrossTimeMs > maxStepIntervalMs) {
          stepPhase   = PHASE_SEEK_POS;
          inNegRegion = false;
          break;
        }

        // Look for the signal to enter the negative region
        if (accDyn < negThresh) {
          if (!inNegRegion) {
            // Just entered negative region: store FIRST crossing time
            inNegRegion    = true;
            negCrossTimeMs = nowMs;
          }
        } else if (inNegRegion && accDyn >= negThresh) {
          // Left the negative region: candidate step completed
          inNegRegion = false;

          // Duration from first POS crossing to first NEG crossing
          unsigned long dt = negCrossTimeMs - posCrossTimeMs;

          bool timeOk     = (dt >= minStepIntervalMs) && (dt <= maxStepIntervalMs);
          bool debounceOk = (nowMs - lastStepTime) > stepDebounceMs;

        if (timeOk && debounceOk) {
          // ---- STEP CONFIRMED HERE ----

          // Default assumption: use fallback length
          float usedStepLen = defaultStepLen;

          float diff = filtererdDistance - prevStepDist;
          if (diff < 0.0f) diff = -diff;

          if (diff >= minStepLen && diff <= maxStepLen) {
            // |distance - prevStepDist|
            usedStepLen = diff;            // distance-based step length
          }

          // Apply chosen step length
          stepLen = usedStepLen;

          stepCount++;
          lastStepTime = nowMs;

          totalStepDistance += stepLen;
          averageStepLength = totalStepDistance / (float)stepCount;

          prevStepDist = filteredDistance;          
        }
        stepPhase = PHASE_SEEK_POS;
      }
      break;
  }
  // ---------- End dual-peak detection ----------
  
  // ----------- BLE updates -----------

  unsigned long now = millis();
  if (now - lastBLEsend >= BLEsendInterval) {

      lastBLEsend = now;

      char csvBuf[64];
      snprintf(csvBuf, sizeof(csvBuf), "S %lu L %.2f D %.2f A %.3f",
              stepCount, stepLen, distance, accDyn);

      pedoDataChar.writeValue((const unsigned char*)csvBuf, strlen(csvBuf));
  }

  // ----------- Prints ---------------------
  
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.print(" cm | ");
   
  Serial.print(accDyn, 4);
  
  Serial.print(" | Steps: ");
  Serial.print(stepCount);

  Serial.print(" | StepLen: ");
  Serial.print(stepLen, 2);
  Serial.print(" cm | AvgStepLen: ");
  Serial.print(averageStepLength, 2);
  Serial.println(" cm");

  // ----------------------------------------
}

void echoISR() {
  if (digitalRead(echoPin) == HIGH) {
    echoStart = micros();
  } else {
    echoEnd = micros();
    echoReady = true;
  }
}

float median3(float a, float b, float c) {
  // return the middle value of a,b,c
  if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
  if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
  return c;
}
