// ═══════════════════════════════════════════════════════════════════════════════
//  Smart Chair V2 – FSR Edition
//  MCU       : ESP32 38-pin Development Board
//  Seat      : 5× FSR (Force-Sensitive Resistor) – pressure distribution
//  Backrest  : 4× Flex Sensor – posture / curvature detection
//  Actuators : Buzzer, LED, 4× Vibration Motor
//
//  Sensor → Actuator Feedback Mapping:
//    FSR_RR  (Rear-Right)  → VIBRATION_PIN  (GPIO 19) – Vibration Motor 1
//    FSR_RL  (Rear-Left)   → VIBRATION_PIN2 (GPIO 18) – Vibration Motor 2
//    FSR_FR  (Front-Right) → VIBRATION_PIN3 (GPIO 13) – Vibration Motor 3
//    FSR_FL  (Front-Left)  → VIBRATION_PIN4 (GPIO 14) – Vibration Motor 4
//    FSR_CTR (Center)      → LED_PIN        (GPIO 12) – Visual indicator
//    Any flex > threshold  → BUZZER_PIN     (GPIO 23) – Backrest alert
//
//  Noise reduction: 8-sample averaged ADC reads + exponential moving average
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// ── Actuator Pins (unchanged) ──────────────────────────────────────────────────
#define BUZZER_PIN      23   // Backrest-posture audio alert
#define LED_PIN         12   // Center FSR visual indicator
#define VIBRATION_PIN   19   // Vibration Motor 1 → FSR_RR feedback
#define VIBRATION_PIN2  18   // Vibration Motor 2 → FSR_RL feedback
#define VIBRATION_PIN3  13   // Vibration Motor 3 → FSR_FR feedback
#define VIBRATION_PIN4  14   // Vibration Motor 4 → FSR_FL feedback

// ── Flex Sensor Pins (ADC1, input-only, no boot conflict) ─────────────────────
// Voltage divider: VCC(3.3V) – R_pull(10kΩ) – GPIO – Flex – GND
#define FLEX_PIN1  34   // ADC1_CH6 – backrest sensor 1
#define FLEX_PIN2  35   // ADC1_CH7 – backrest sensor 2
#define FLEX_PIN3  36   // ADC1_CH0 – backrest sensor 3
#define FLEX_PIN4  39   // ADC1_CH3 – backrest sensor 4

// ── FSR Seat Pressure Sensor Pins ─────────────────────────────────────────────
// Each FSR is wired as a voltage divider: VCC(3.3V) – R_pull(10kΩ) – GPIO – FSR – GND
// ADC1 is preferred (GPIO 32-39). GPIO 25-27 are ADC2 but safe when WiFi is unused.
#define FSR_RR_PIN  32   // ADC1_CH4 – Rear-Right  seat quadrant
#define FSR_RL_PIN  33   // ADC1_CH5 – Rear-Left   seat quadrant
#define FSR_FR_PIN  25   // ADC2_CH8 – Front-Right seat quadrant (WiFi off)
#define FSR_FL_PIN  26   // ADC2_CH9 – Front-Left  seat quadrant (WiFi off)
#define FSR_CTR_PIN 27   // ADC2_CH7 – Center      seat zone     (WiFi off)

// ── Averaging / Filtering Configuration ───────────────────────────────────────
#define ADC_SAMPLES  8        // Number of analogRead samples averaged per reading
#define EMA_ALPHA    0.20f    // EMA smoothing factor: higher = more responsive,
                              //   lower = smoother. Range: 0 < α ≤ 1.

// ── Threshold Configuration ───────────────────────────────────────────────────
// Adjust these via Serial or after calibration to match your FSR pull-down setup.
#define FSR_THRESHOLD   1500  // FSR EMA value above this triggers vibration motor
#define FLEX_THRESHOLD  2200  // Flex EMA value above this triggers buzzer alert

// ── FSR Calibration Data ──────────────────────────────────────────────────────
// offset : zero-load ADC count (captured during tare)
// scale  : ADC counts per gram; grams = (reading - offset) / scale
struct FsrCalib {
  float offset;   // zero-load ADC value
  float scale;    // ADC counts per gram (set by 'calibrate fsr N grams')
};

// Index order: 0=RR, 1=RL, 2=FR, 3=FL, 4=CTR
FsrCalib fsrCalib[5] = {
  { 0.0f, 1.0f },   // FSR1 – RR
  { 0.0f, 1.0f },   // FSR2 – RL
  { 0.0f, 1.0f },   // FSR3 – FR
  { 0.0f, 1.0f },   // FSR4 – FL
  { 0.0f, 1.0f },   // FSR5 – CTR
};

// ── EMA Filter State ──────────────────────────────────────────────────────────
float emaFsr[5]  = { 0, 0, 0, 0, 0 };  // filtered FSR values
float emaFlex[4] = { 0, 0, 0, 0 };     // filtered flex values
bool  emaInit    = false;               // seed EMA on first loop pass

// ── Data Logging State ────────────────────────────────────────────────────────
bool          loggingEnabled = false;
unsigned long logInterval    = 100;       // ms between CSV rows
unsigned long lastLogTime    = 0;
char          logLabel[48]   = "unlabeled";

// ── Timing State ──────────────────────────────────────────────────────────────
unsigned long lastSensorPrintTime = 0;  // flex print cadence
unsigned long lastFsrPrintTime    = 0;  // FSR print cadence

// ── Flex Test Mode Flags ───────────────────────────────────────────────────────
bool testingFlex1 = false, testingFlex2 = false;
bool testingFlex3 = false, testingFlex4 = false;
unsigned long testStartTime1 = 0, testStartTime2 = 0;
unsigned long testStartTime3 = 0, testStartTime4 = 0;
unsigned long lastPrintTime1 = 0, lastPrintTime2 = 0;
unsigned long lastPrintTime3 = 0, lastPrintTime4 = 0;

// ─────────────────────────────────────────────────────────────────────────────
// averagedRead()
//   Takes ADC_SAMPLES analogRead samples with a brief inter-sample delay and
//   returns the integer mean. Reduces high-frequency EMI noise on the ADC bus.
// ─────────────────────────────────────────────────────────────────────────────
int averagedRead(uint8_t pin, uint8_t samples = ADC_SAMPLES) {
  long sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);   // allow input capacitor to settle between samples
  }
  return (int)(sum / samples);
}

// ─────────────────────────────────────────────────────────────────────────────
// applyEma()
//   Single-pole IIR (exponential moving average):
//     y[n] = α · x[n] + (1 − α) · y[n−1]
// ─────────────────────────────────────────────────────────────────────────────
float applyEma(float prev, float newVal, float alpha = EMA_ALPHA) {
  return alpha * newVal + (1.0f - alpha) * prev;
}

// ─────────────────────────────────────────────────────────────────────────────
// fsrToGrams()
//   Converts a raw (or filtered) ADC reading to grams using stored calibration.
//   Returns 0 when the reading is at or below the zero offset.
// ─────────────────────────────────────────────────────────────────────────────
float fsrToGrams(float rawAdc, const FsrCalib &cal) {
  float delta = rawAdc - cal.offset;
  if (delta <= 0.0f || cal.scale <= 0.0f) return 0.0f;
  return delta / cal.scale;
}

// ═══════════════════════════════════════════════════════════════════════════════
// setup()
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // ── Actuator pins – all off at start ────────────────────────────────────────
  const uint8_t actuators[] = {
    BUZZER_PIN, LED_PIN,
    VIBRATION_PIN, VIBRATION_PIN2, VIBRATION_PIN3, VIBRATION_PIN4
  };
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(actuators[i], OUTPUT);
    digitalWrite(actuators[i], LOW);
  }

  // ── FSR zero-offset calibration (tare) at power-on ──────────────────────────
  // Ensure no load is applied to the seat during power-on for accurate tare.
  Serial.println("Capturing FSR zero offsets (ensure seat is unloaded)...");
  delay(500);
  const uint8_t fsrPins[5]  = { FSR_RR_PIN, FSR_RL_PIN, FSR_FR_PIN, FSR_FL_PIN, FSR_CTR_PIN };
  const char*   fsrNames[5] = { "FSR-RR", "FSR-RL", "FSR-FR", "FSR-FL", "FSR-CTR" };
  for (uint8_t i = 0; i < 5; i++) {
    fsrCalib[i].offset = (float)averagedRead(fsrPins[i], 16);
    Serial.print("  "); Serial.print(fsrNames[i]);
    Serial.print(" offset: "); Serial.println(fsrCalib[i].offset, 1);
  }

  // ── Flex sensor baselines ────────────────────────────────────────────────────
  Serial.println("Flex sensor baselines (unloaded backrest):");
  Serial.print("  Flex1: "); Serial.println(averagedRead(FLEX_PIN1, 8));
  Serial.print("  Flex2: "); Serial.println(averagedRead(FLEX_PIN2, 8));
  Serial.print("  Flex3: "); Serial.println(averagedRead(FLEX_PIN3, 8));
  Serial.print("  Flex4: "); Serial.println(averagedRead(FLEX_PIN4, 8));

  Serial.println(F("\n── Serial Command Reference ────────────────────────────────"));
  Serial.println(F("  Actuators : buzzer on/off | led on/off | vibration[2|3|4] on/off"));
  Serial.println(F("  Flex test : test flex<1-4>"));
  Serial.println(F("  FSR tare  : tare fsr <1-5>"));
  Serial.println(F("  FSR calib : calibrate fsr <1-5> <known_grams>"));
  Serial.println(F("  Logging   : log start | log stop | log rate <ms> | label <name>"));
  Serial.println(F("────────────────────────────────────────────────────────────\n"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// loop()
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
  const uint8_t fsrPins[5] = { FSR_RR_PIN, FSR_RL_PIN, FSR_FR_PIN, FSR_FL_PIN, FSR_CTR_PIN };

  // ── Serial command handler ──────────────────────────────────────────────────
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    // ── Actuator manual control ────────────────────────────────────────────────
    if      (command == "buzzer on")          { digitalWrite(BUZZER_PIN,     HIGH); Serial.println("Buzzer ON"); }
    else if (command == "buzzer off")         { digitalWrite(BUZZER_PIN,     LOW);  Serial.println("Buzzer OFF"); }
    else if (command == "led on")             { digitalWrite(LED_PIN,        HIGH); Serial.println("LED ON"); }
    else if (command == "led off")            { digitalWrite(LED_PIN,        LOW);  Serial.println("LED OFF"); }
    else if (command == "vibration on")       { digitalWrite(VIBRATION_PIN,  HIGH); Serial.println("Vibration1 ON"); }
    else if (command == "vibration off")      { digitalWrite(VIBRATION_PIN,  LOW);  Serial.println("Vibration1 OFF"); }
    else if (command == "vibration2 on")      { digitalWrite(VIBRATION_PIN2, HIGH); Serial.println("Vibration2 ON"); }
    else if (command == "vibration2 off")     { digitalWrite(VIBRATION_PIN2, LOW);  Serial.println("Vibration2 OFF"); }
    else if (command == "vibration3 on")      { digitalWrite(VIBRATION_PIN3, HIGH); Serial.println("Vibration3 ON"); }
    else if (command == "vibration3 off")     { digitalWrite(VIBRATION_PIN3, LOW);  Serial.println("Vibration3 OFF"); }
    else if (command == "vibration4 on")      { digitalWrite(VIBRATION_PIN4, HIGH); Serial.println("Vibration4 ON"); }
    else if (command == "vibration4 off")     { digitalWrite(VIBRATION_PIN4, LOW);  Serial.println("Vibration4 OFF"); }

    // ── Flex sensor timed test (10 s) ─────────────────────────────────────────
    else if (command == "test flex1") { testingFlex1 = true; testStartTime1 = lastPrintTime1 = millis(); Serial.println("Testing Flex1 for 10 s..."); }
    else if (command == "test flex2") { testingFlex2 = true; testStartTime2 = lastPrintTime2 = millis(); Serial.println("Testing Flex2 for 10 s..."); }
    else if (command == "test flex3") { testingFlex3 = true; testStartTime3 = lastPrintTime3 = millis(); Serial.println("Testing Flex3 for 10 s..."); }
    else if (command == "test flex4") { testingFlex4 = true; testStartTime4 = lastPrintTime4 = millis(); Serial.println("Testing Flex4 for 10 s..."); }

    // ── FSR Tare: "tare fsr 2" ────────────────────────────────────────────────
    // Re-captures the zero-load offset for the specified FSR.
    else if (command.startsWith("tare fsr ")) {
      int idx = command.substring(9).toInt();
      if (idx >= 1 && idx <= 5) {
        fsrCalib[idx-1].offset = (float)averagedRead(fsrPins[idx-1], 16);
        Serial.print("FSR-"); Serial.print(idx);
        Serial.print(" tared. New offset = ");
        Serial.println(fsrCalib[idx-1].offset, 1);
      } else { Serial.println("Usage: tare fsr <1-5>"); }
    }

    // ── FSR Calibration: "calibrate fsr 1 500" ────────────────────────────────
    // Procedure:
    //   1. Send the command (sensor must be unloaded).
    //   2. Wait for the "Place N g..." prompt.
    //   3. Apply known_grams load within 5 seconds.
    //   4. Firmware captures loaded reading and computes scale factor.
    else if (command.startsWith("calibrate fsr ")) {
      String args     = command.substring(14);
      int    spaceIdx = args.indexOf(' ');
      if (spaceIdx > 0) {
        int   idx        = args.substring(0, spaceIdx).toInt();
        float knownGrams = args.substring(spaceIdx + 1).toFloat();
        if (idx >= 1 && idx <= 5 && knownGrams > 0.0f) {
          // Tare first, then wait for load
          fsrCalib[idx-1].offset = (float)averagedRead(fsrPins[idx-1], 16);
          Serial.print("Place "); Serial.print(knownGrams);
          Serial.println(" g on the FSR now – capturing in 5 s...");
          delay(5000);
          float rawLoaded = (float)averagedRead(fsrPins[idx-1], 16);
          float delta     = rawLoaded - fsrCalib[idx-1].offset;
          if (delta > 0.0f) {
            fsrCalib[idx-1].scale = delta / knownGrams;
            Serial.print("FSR-"); Serial.print(idx);
            Serial.print(" calibrated. Scale = ");
            Serial.print(fsrCalib[idx-1].scale, 4);
            Serial.print(" ADC/g  |  Verification: ");
            Serial.print(fsrToGrams(rawLoaded, fsrCalib[idx-1]), 1);
            Serial.println(" g");
          } else {
            Serial.println("Error: no positive ADC delta. Ensure load is applied to the sensor.");
          }
        } else { Serial.println("Usage: calibrate fsr <1-5> <known_grams>"); }
      }   else { Serial.println("Usage: calibrate fsr <1-5> <known_grams>"); }
    }

    // ── Dataset logging control ────────────────────────────────────────────────
    else if (command == "log start") {
      loggingEnabled = true;
      lastLogTime    = millis();
      // CSV header – Python logger keyed on "DATA_HEADER:" prefix
      Serial.println("DATA_HEADER:timestamp_ms,flex1,flex2,flex3,flex4,"
                     "fsr_rr,fsr_rl,fsr_fr,fsr_fl,fsr_ctr,"
                     "total_fsr,front_rear_ratio,left_right_ratio,label");
      Serial.print("Logging started | interval: "); Serial.print(logInterval);
      Serial.print(" ms | label: ");               Serial.println(logLabel);
    }
    else if (command == "log stop") {
      loggingEnabled = false;
      Serial.println("Logging stopped.");
    }
    else if (command.startsWith("log rate ")) {
      int ms = command.substring(9).toInt();
      if (ms >= 50) {
        logInterval = (unsigned long)ms;
        Serial.print("Log interval set to "); Serial.print(logInterval); Serial.println(" ms.");
      } else {
        Serial.println("Minimum interval is 50 ms.");
      }
    }
    else if (command.startsWith("label ")) {
      String lbl = command.substring(6);
      lbl.trim();
      lbl.toCharArray(logLabel, sizeof(logLabel));
      Serial.print("Label set to: "); Serial.println(logLabel);
    }

    else { Serial.println("Unknown command."); }
  }

  // ── Step 1: Averaged ADC reads ──────────────────────────────────────────────
  float rawFlex[4] = {
    (float)averagedRead(FLEX_PIN1),
    (float)averagedRead(FLEX_PIN2),
    (float)averagedRead(FLEX_PIN3),
    (float)averagedRead(FLEX_PIN4)
  };

  float rawFsr[5];
  for (uint8_t i = 0; i < 5; i++) {
    rawFsr[i] = (float)averagedRead(fsrPins[i]);
  }

  // ── Step 2: EMA filter update ────────────────────────────────────────────────
  // First pass: seed EMA with raw values to avoid artificial transient at boot.
  if (!emaInit) {
    for (uint8_t i = 0; i < 4; i++) emaFlex[i] = rawFlex[i];
    for (uint8_t i = 0; i < 5; i++) emaFsr[i]  = rawFsr[i];
    emaInit = true;
  } else {
    for (uint8_t i = 0; i < 4; i++) emaFlex[i] = applyEma(emaFlex[i], rawFlex[i]);
    for (uint8_t i = 0; i < 5; i++) emaFsr[i]  = applyEma(emaFsr[i],  rawFsr[i]);
  }

  // Convenience aliases for filtered sensor values
  const float flex1 = emaFlex[0], flex2 = emaFlex[1];
  const float flex3 = emaFlex[2], flex4 = emaFlex[3];
  const float fsrRR = emaFsr[0],  fsrRL = emaFsr[1];
  const float fsrFR = emaFsr[2],  fsrFL = emaFsr[3];
  const float fsrCTR= emaFsr[4];

  // ── Step 3: Diagnostic serial output ─────────────────────────────────────────

  // Flex sensors – print every 1 s
  if (millis() - lastSensorPrintTime > 1000) {
    Serial.print("Flex1: "); Serial.print(flex1, 0);
    Serial.print("  Flex2: "); Serial.print(flex2, 0);
    Serial.print("  Flex3: "); Serial.print(flex3, 0);
    Serial.print("  Flex4: "); Serial.println(flex4, 0);
    lastSensorPrintTime = millis();
  }

  // FSR seat pressure – print every 2 s
  if (millis() - lastFsrPrintTime > 2000) {
    float totalFsr = fsrRR + fsrRL + fsrFR + fsrFL + fsrCTR;
    float frRatio  = (fsrRL + fsrRR) > 1.0f ? (fsrFL + fsrFR) / (fsrRL + fsrRR) : 0.0f;
    float lrRatio  = (fsrFR + fsrRR) > 1.0f ? (fsrFL + fsrRL) / (fsrFR + fsrRR) : 0.0f;

    Serial.println("── FSR Seat Pressure (EMA ADC) ───────────");
    Serial.print("  RR: ");  Serial.print(fsrRR,  0);
    Serial.print("  RL: ");  Serial.print(fsrRL,  0);
    Serial.print("  FR: ");  Serial.print(fsrFR,  0);
    Serial.print("  FL: ");  Serial.print(fsrFL,  0);
    Serial.print("  CTR: "); Serial.print(fsrCTR, 0);
    Serial.print("  Total: ");Serial.println(totalFsr, 0);
    // Calibrated mass output (grams)
    Serial.print("  RR(g): ");  Serial.print(fsrToGrams(fsrRR,  fsrCalib[0]), 1);
    Serial.print("  RL(g): ");  Serial.print(fsrToGrams(fsrRL,  fsrCalib[1]), 1);
    Serial.print("  FR(g): ");  Serial.print(fsrToGrams(fsrFR,  fsrCalib[2]), 1);
    Serial.print("  FL(g): ");  Serial.print(fsrToGrams(fsrFL,  fsrCalib[3]), 1);
    Serial.print("  CTR(g): "); Serial.println(fsrToGrams(fsrCTR,fsrCalib[4]), 1);
    // Weight distribution ratios
    Serial.print("  Front/Rear ratio: "); Serial.print(frRatio, 3);
    Serial.print("  Left/Right ratio: "); Serial.println(lrRatio, 3);
    Serial.println("─────────────────────────────────────────");
    lastFsrPrintTime = millis();
  }

  // ── Step 4: Dataset logging (CSV row) ────────────────────────────────────────
  // Output prefixed with "DATA:" so logger.py can filter debug lines.
  // Column order matches DATA_HEADER sent on "log start".
  if (loggingEnabled && (millis() - lastLogTime >= logInterval)) {
    lastLogTime = millis();

    // Distribution ratios (center excluded from ratio calc)
    float totalFsr = fsrRR + fsrRL + fsrFR + fsrFL;
    float frRatio  = (fsrRL + fsrRR) > 1.0f ? (fsrFL + fsrFR) / (fsrRL + fsrRR) : 0.0f;
    float lrRatio  = (fsrFR + fsrRR) > 1.0f ? (fsrFL + fsrRL) / (fsrFR + fsrRR) : 0.0f;

    Serial.print("DATA:");
    Serial.print(millis());      Serial.print(",");
    Serial.print(flex1,  0);     Serial.print(",");
    Serial.print(flex2,  0);     Serial.print(",");
    Serial.print(flex3,  0);     Serial.print(",");
    Serial.print(flex4,  0);     Serial.print(",");
    Serial.print(fsrRR,  0);     Serial.print(",");
    Serial.print(fsrRL,  0);     Serial.print(",");
    Serial.print(fsrFR,  0);     Serial.print(",");
    Serial.print(fsrFL,  0);     Serial.print(",");
    Serial.print(fsrCTR, 0);     Serial.print(",");
    Serial.print(totalFsr, 0);   Serial.print(",");
    Serial.print(frRatio,  4);   Serial.print(",");
    Serial.print(lrRatio,  4);   Serial.print(",");
    Serial.println(logLabel);
  }

  // ── Step 5: Posture detection + actuator feedback ────────────────────────────
  //
  //  SEAT (FSR) logic – individual vibration per quadrant:
  //    Each FSR exceeding FSR_THRESHOLD activates its dedicated vibration motor.
  //    This gives the user localised haptic feedback indicating which seat
  //    quadrant is under uneven or excessive pressure.
  //
  //  BACKREST (Flex) logic – buzzer alert:
  //    If any flex sensor exceeds FLEX_THRESHOLD the backrest posture is
  //    considered to require correction and the buzzer sounds.
  //
  //  CENTER FSR – LED indicator:
  //    Center pressure above threshold lights the LED (e.g. forward lean alert).

  // Seat quadrant vibration feedback
  digitalWrite(VIBRATION_PIN,  (fsrRR  > FSR_THRESHOLD) ? HIGH : LOW);  // RR → Vib1
  digitalWrite(VIBRATION_PIN2, (fsrRL  > FSR_THRESHOLD) ? HIGH : LOW);  // RL → Vib2
  digitalWrite(VIBRATION_PIN3, (fsrFR  > FSR_THRESHOLD) ? HIGH : LOW);  // FR → Vib3
  digitalWrite(VIBRATION_PIN4, (fsrFL  > FSR_THRESHOLD) ? HIGH : LOW);  // FL → Vib4

  // Center seat zone → LED
  digitalWrite(LED_PIN, (fsrCTR > FSR_THRESHOLD) ? HIGH : LOW);

  // Backrest posture → buzzer
  bool backrestAlert = (flex1 > FLEX_THRESHOLD || flex2 > FLEX_THRESHOLD ||
                        flex3 > FLEX_THRESHOLD || flex4 > FLEX_THRESHOLD);
  digitalWrite(BUZZER_PIN, backrestAlert ? HIGH : LOW);

  delay(50);
}