#include <Arduino.h>
#include "HX711.h"

// ── Actuator Pins ──────────────────────────────────────────────────────────────
#define BUZZER_PIN      23
#define LED_PIN         12
#define VIBRATION_PIN   19
#define VIBRATION_PIN2  18
#define VIBRATION_PIN3  13
#define VIBRATION_PIN4  14

// ── Flex Sensor Pins (ADC1, input-only, no boot conflict) ─────────────────────
#define FLEX_PIN1 34
#define FLEX_PIN2 35
#define FLEX_PIN3 36
#define FLEX_PIN4 39

// ── HX711 Pin Assignments ──────────────────────────────────────────────────────
// HX711 #1 – front-left load cell
#define HX711_1_DOUT  5
#define HX711_1_SCK  25
// HX711 #2 – front-right load cell
#define HX711_2_DOUT 26
#define HX711_2_SCK  27
// HX711 #3 – rear-left load cell
#define HX711_3_DOUT 32
#define HX711_3_SCK  33
// HX711 #4 – rear-right load cell
#define HX711_4_DOUT 21
#define HX711_4_SCK  22

// ── Pressure Calculation Constants ────────────────────────────────────────────
// Seat pad area per sensor: 15 cm × 15 cm = 0.0225 m²
const float SENSOR_AREA_M2 = 0.15f * 0.15f;  // 0.0225 m²
const float GRAVITY        = 9.81f;           // m/s²

// ── HX711 Objects ─────────────────────────────────────────────────────────────
HX711 scale1, scale2, scale3, scale4;

// Calibration scale factors (raw ADC units per gram).
// Default values – run 'calibrate hx N' to update live.
float scaleFactors[4] = { 2280.0f, 2280.0f, 2280.0f, 2280.0f };

// ── State Variables ───────────────────────────────────────────────────────────
bool testingFlex1 = false;
bool testingFlex2 = false;
bool testingFlex3 = false;
bool testingFlex4 = false;

unsigned long testStartTime1 = 0, testStartTime2 = 0;
unsigned long testStartTime3 = 0, testStartTime4 = 0;
unsigned long lastPrintTime1 = 0, lastPrintTime2 = 0;
unsigned long lastPrintTime3 = 0, lastPrintTime4 = 0;
unsigned long lastToggleTime = 0;

bool beepState = false;
unsigned long lastSensorPrintTime  = 0;
unsigned long lastHX711PrintTime   = 0;

// ── Helper: initialise one HX711 ──────────────────────────────────────────────
void initScale(HX711 &scale, uint8_t dout, uint8_t sck, float factor, const char *name) {
  scale.begin(dout, sck);
  scale.set_scale(factor);
  scale.tare();
  Serial.print(name);
  if (scale.is_ready()) {
    Serial.println(": ready, tare done.");
  } else {
    Serial.println(": NOT detected – check wiring.");
  }
}

// ── Pressure from one scale (returns Pa) ──────────────────────────────────────
float getPressurePa(HX711 &scale) {
  if (!scale.is_ready()) return 0.0f;
  float mass_g   = scale.get_units(5);       // grams (after set_scale + tare)
  float mass_kg  = mass_g / 1000.0f;
  float force_N  = mass_kg * GRAVITY;
  return force_N / SENSOR_AREA_M2;           // Pascals
}

void setup() {
  Serial.begin(115200);

  // ── Actuator pins ──────────────────────────────────────────────────────────
  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(LED_PIN,       OUTPUT);
  pinMode(VIBRATION_PIN,  OUTPUT);
  pinMode(VIBRATION_PIN2, OUTPUT);
  pinMode(VIBRATION_PIN3, OUTPUT);
  pinMode(VIBRATION_PIN4, OUTPUT);

  // ── Flex sensor baseline ───────────────────────────────────────────────────
  Serial.println("Calibrating flex sensors...");
  delay(1000);
  Serial.print("Flex1 baseline: "); Serial.println(analogRead(FLEX_PIN1));
  Serial.print("Flex2 baseline: "); Serial.println(analogRead(FLEX_PIN2));
  Serial.print("Flex3 baseline: "); Serial.println(analogRead(FLEX_PIN3));
  Serial.print("Flex4 baseline: "); Serial.println(analogRead(FLEX_PIN4));

  // ── HX711 initialisation ───────────────────────────────────────────────────
  Serial.println("Initialising HX711 load cells...");
  initScale(scale1, HX711_1_DOUT, HX711_1_SCK, scaleFactors[0], "HX711-1 (front-left)");
  initScale(scale2, HX711_2_DOUT, HX711_2_SCK, scaleFactors[1], "HX711-2 (front-right)");
  initScale(scale3, HX711_3_DOUT, HX711_3_SCK, scaleFactors[2], "HX711-3 (rear-left)");
  initScale(scale4, HX711_4_DOUT, HX711_4_SCK, scaleFactors[3], "HX711-4 (rear-right)");

  Serial.println("\nCalibration tip:");
  Serial.println("  Place a known mass (e.g. 1 kg) on one sensor.");
  Serial.println("  F = m*g = 1 * 9.81 = 9.81 N   |   P = F/A = 9.81/0.0225 = 436 Pa");
  Serial.println("  Use 'calibrate hx <1-4> <known_grams>' to set the scale factor.");
  Serial.println("\nCommands: buzzer on/off | led on/off | vibration[2|3|4] on/off");
  Serial.println("          test flex[1-4] | calibrate hx <1-4> <grams> | tare hx <1-4>");
}

void loop() {
  // ── Serial command handler ─────────────────────────────────────────────────
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    // Actuator commands
    if (command == "buzzer on")       { digitalWrite(BUZZER_PIN, HIGH); Serial.println("Buzzer ON"); }
    else if (command == "buzzer off") { digitalWrite(BUZZER_PIN, LOW);  Serial.println("Buzzer OFF"); }
    else if (command == "led on")     { digitalWrite(LED_PIN,    HIGH); Serial.println("LED ON"); }
    else if (command == "led off")    { digitalWrite(LED_PIN,    LOW);  Serial.println("LED OFF"); }
    else if (command == "vibration on")  { digitalWrite(VIBRATION_PIN,  HIGH); Serial.println("Vibration ON"); }
    else if (command == "vibration off") { digitalWrite(VIBRATION_PIN,  LOW);  Serial.println("Vibration OFF"); }
    else if (command == "vibration2 on") { digitalWrite(VIBRATION_PIN2, HIGH); Serial.println("Vibration2 ON"); }
    else if (command == "vibration2 off"){ digitalWrite(VIBRATION_PIN2, LOW);  Serial.println("Vibration2 OFF"); }
    else if (command == "vibration3 on") { digitalWrite(VIBRATION_PIN3, HIGH); Serial.println("Vibration3 ON"); }
    else if (command == "vibration3 off"){ digitalWrite(VIBRATION_PIN3, LOW);  Serial.println("Vibration3 OFF"); }
    else if (command == "vibration4 on") { digitalWrite(VIBRATION_PIN4, HIGH); Serial.println("Vibration4 ON"); }
    else if (command == "vibration4 off"){ digitalWrite(VIBRATION_PIN4, LOW);  Serial.println("Vibration4 OFF"); }

    // Flex test commands
    else if (command == "test flex1") { testingFlex1 = true; testStartTime1 = lastPrintTime1 = millis(); Serial.println("Testing Flex1 for 10 s..."); }
    else if (command == "test flex2") { testingFlex2 = true; testStartTime2 = lastPrintTime2 = millis(); Serial.println("Testing Flex2 for 10 s..."); }
    else if (command == "test flex3") { testingFlex3 = true; testStartTime3 = lastPrintTime3 = millis(); Serial.println("Testing Flex3 for 10 s..."); }
    else if (command == "test flex4") { testingFlex4 = true; testStartTime4 = lastPrintTime4 = millis(); Serial.println("Testing Flex4 for 10 s..."); }

    // HX711 tare: "tare hx 2"
    else if (command.startsWith("tare hx ")) {
      int idx = command.substring(8).toInt();
      HX711 *scales[4] = { &scale1, &scale2, &scale3, &scale4 };
      if (idx >= 1 && idx <= 4) {
        scales[idx-1]->tare();
        Serial.print("HX711-"); Serial.print(idx); Serial.println(" tared.");
      } else { Serial.println("Usage: tare hx <1-4>"); }
    }

    // HX711 calibration: "calibrate hx 1 1000"  (place known_grams on the sensor first)
    else if (command.startsWith("calibrate hx ")) {
      String args = command.substring(13);
      int spaceIdx = args.indexOf(' ');
      if (spaceIdx > 0) {
        int idx          = args.substring(0, spaceIdx).toInt();
        float knownGrams = args.substring(spaceIdx + 1).toFloat();
        HX711 *scales[4] = { &scale1, &scale2, &scale3, &scale4 };
        if (idx >= 1 && idx <= 4 && knownGrams > 0) {
          HX711 &s = *scales[idx-1];
          s.set_scale();      // remove current factor
          s.tare();
          Serial.print("Place "); Serial.print(knownGrams); Serial.println(" g on the sensor and wait...");
          delay(5000);
          long rawReading = s.get_units(20);
          float newFactor = (float)rawReading / knownGrams;
          s.set_scale(newFactor);
          scaleFactors[idx-1] = newFactor;
          Serial.print("HX711-"); Serial.print(idx);
          Serial.print(" new scale factor: "); Serial.println(newFactor, 4);
          // Show resulting pressure for verification
          float p = getPressurePa(s);
          Serial.print("Verification pressure: "); Serial.print(p, 2); Serial.println(" Pa");
        } else { Serial.println("Usage: calibrate hx <1-4> <known_grams>"); }
      } else { Serial.println("Usage: calibrate hx <1-4> <known_grams>"); }
    }

    else { Serial.println("Unknown command"); }
  }

  // ── Flex sensor monitoring ────────────────────────────────────────────────
  int flex1Value = analogRead(FLEX_PIN1);
  int flex2Value = analogRead(FLEX_PIN2);
  int flex3Value = analogRead(FLEX_PIN3);
  int flex4Value = analogRead(FLEX_PIN4);

  if (millis() - lastSensorPrintTime > 1000) {
    Serial.print("Flex1: "); Serial.print(flex1Value);
    Serial.print("  Flex2: "); Serial.print(flex2Value);
    Serial.print("  Flex3: "); Serial.print(flex3Value);
    Serial.print("  Flex4: "); Serial.println(flex4Value);
    lastSensorPrintTime = millis();
  }

  // ── HX711 pressure monitoring (every 2 s to avoid ADC conflicts) ──────────
  if (millis() - lastHX711PrintTime > 2000) {
    float p1 = getPressurePa(scale1);
    float p2 = getPressurePa(scale2);
    float p3 = getPressurePa(scale3);
    float p4 = getPressurePa(scale4);
    float totalP = p1 + p2 + p3 + p4;

    Serial.println("── HX711 Pressure (Pa) ──────────────────");
    Serial.print("  FL: "); Serial.print(p1, 2);
    Serial.print("  FR: "); Serial.print(p2, 2);
    Serial.print("  RL: "); Serial.print(p3, 2);
    Serial.print("  RR: "); Serial.println(p4, 2);
    Serial.print("  Total: "); Serial.print(totalP, 2);
    Serial.println(" Pa");
    Serial.println("─────────────────────────────────────────");

    lastHX711PrintTime = millis();
  }

  // ── Actuator logic (flex threshold) ──────────────────────────────────────
  bool actuateAll = (flex1Value > 2200 || flex2Value > 2200 ||
                     flex3Value > 2200 || flex4Value > 2200);

  digitalWrite(BUZZER_PIN,    actuateAll ? HIGH : LOW);
  digitalWrite(LED_PIN,       actuateAll ? HIGH : LOW);
  digitalWrite(VIBRATION_PIN,  actuateAll ? HIGH : LOW);
  digitalWrite(VIBRATION_PIN2, actuateAll ? HIGH : LOW);
  digitalWrite(VIBRATION_PIN3, actuateAll ? HIGH : LOW);
  digitalWrite(VIBRATION_PIN4, actuateAll ? HIGH : LOW);

  delay(50);
}