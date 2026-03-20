// ═══════════════════════════════════════════════════════════════════════════════
//  Smart Chair V2 – FSR Edition
//  MCU       : ESP32 38-pin Development Board
//  Seat      : 5× FSR (Force-Sensitive Resistor) – pressure distribution
//  Backrest  : 4× Flex Sensor – posture / curvature detection
//  Actuators : Buzzer, 2× LED, 5× Vibration Motor, 2× Tactile Button
//
//  Sensor → Actuator Feedback Mapping:
//    FSR_RIGHT  (Right)   → VIBRATION_PIN  (GPIO 19) – Vibration Motor 1
//    FSR_LEFT   (Left)    → VIBRATION_PIN2 (GPIO 18) – Vibration Motor 2
//    FSR_FRONT  (Front)   → VIBRATION_PIN3 (GPIO 13) – Vibration Motor 3
//    FSR_BACK   (Back)    → VIBRATION_PIN4 (GPIO 14) – Vibration Motor 4
//    FSR_MIDDLE (Middle)  → RED_LED_PIN    (GPIO 12) – Red LED indicator
//    Any flex > threshold → BUZZER_PIN     (GPIO 23) – Backrest alert
//    Any flex > threshold → VIBRATION_PIN5 (GPIO  5) – Backrest vibration
//    Any flex > threshold → GREEN_LED_PIN  (GPIO 17) – Green LED backrest alert
//  Buttons   : BUTTON1_PIN (GPIO 21) | BUTTON2_PIN (GPIO 22) – debounced inputs
//
//  Noise reduction: 8-sample averaged ADC reads + exponential moving average
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <SPIFFS.h>

// ── Actuator Pins ─────────────────────────────────────────────────────────────
#define BUZZER_PIN      23   // Backrest-posture audio alert
#define RED_LED_PIN     12   // Middle FSR → red LED visual indicator
#define GREEN_LED_PIN   17   // Green LED → backrest alert visual indicator
#define VIBRATION_PIN   19   // Vibration Motor 1 → FSR_RIGHT feedback
#define VIBRATION_PIN2  18   // Vibration Motor 2 → FSR_LEFT feedback
#define VIBRATION_PIN3  13   // Vibration Motor 3 → FSR_FRONT feedback
#define VIBRATION_PIN4  14   // Vibration Motor 4 → FSR_BACK feedback
#define VIBRATION_PIN5   5   // Vibration Motor 5 → Backrest alert feedback

// ── Button Pins ───────────────────────────────────────────────────────────────
#define BUTTON1_PIN     21   // Tactile button 1 (INPUT_PULLUP, active LOW)
#define BUTTON2_PIN     22   // Tactile button 2 (INPUT_PULLUP, active LOW)

// ── Flex Sensor Pins (ADC1, input-only, no boot conflict) ─────────────────────
// Voltage divider: VCC(3.3V) – R_pull(10kΩ) – GPIO – Flex – GND
#define FLEX_PIN1  34   // ADC1_CH6 – backrest sensor 1
#define FLEX_PIN2  35   // ADC1_CH7 – backrest sensor 2
#define FLEX_PIN3  36   // ADC1_CH0 – backrest sensor 3
#define FLEX_PIN4  39   // ADC1_CH3 – backrest sensor 4

// ── FSR Seat Pressure Sensor Pins ─────────────────────────────────────────────
// Each FSR is wired as a voltage divider: VCC(3.3V) – R_pull(10kΩ) – GPIO – FSR – GND

// ADC1 is preferred (GPIO 32-39). GPIO 25-27 are ADC2 but safe when WiFi is unused.
// WARNING: GPIO 3 is UART0_RX – avoid using Serial while reading FSR_BACK.
#define FSR_RIGHT_PIN   32   // ADC1_CH4 – Right of diamond
#define FSR_LEFT_PIN    27   // ADC2_CH7 – Left of diamond
#define FSR_FRONT_PIN   26   // ADC2_CH9 – Front of diamond
#define FSR_MIDDLE_PIN  25   // ADC2_CH8 – Center of diamond
#define FSR_BACK_PIN    33   // ADC1_CH5 – Back of diamond

// ── Averaging / Filtering Configuration ───────────────────────────────────────
#define ADC_SAMPLES  100        // Number of analogRead samples averaged per reading
#define EMA_ALPHA    0.20f    // EMA smoothing factor: higher = more responsive,
                              //   lower = smoother. Range: 0 < α ≤ 1.

// ── Threshold Configuration ───────────────────────────────────────────────────
// Sensors are inverted: MORE force / bend = LOWER ADC value.
// At boot, tare values (fsrTare[], flexTare[]) are captured with the chair empty.
// Each raw reading is mapped to 0-100 % relative to its own tare:
//   fsrPct[i]  = (fsrTare[i] - current) / fsrTare[i] * 100  (0=no load, 100=full)
//   flexPct[i] = (flexTare[i] - current) / flexTare[i] * 100 (0=straight, 100=bent)
//
// A sitting check gates balance logic so the chair is silent when empty.
// Balance alerts fire on the heavier side when paired sensors differ by > BALANCE_THRESHOLD %.
// Backrest balance: Left group (Flex1+Flex3) vs Right group (Flex2+Flex4).
//   If the group delta sums differ by > FLEX_BALANCE_THRESHOLD ADC counts → Vib5+buzzer.
#define SITTING_THRESHOLD      25   // % average seat load → someone is sitting
#define BALANCE_THRESHOLD      20   // % difference between paired FSRs → imbalance vibration (legacy, unused)
#define DIST_THRESHOLD       0.10f  // normalized share difference between paired FSRs → imbalance vibration
#define FLEX_PCT_THRESHOLD     10   // % flex bend from tare → diagnostic display
#define FLEX_NOISE_FLOOR        8   // ADC counts: delta below this is electrical noise; ignored
#define FLEX_ADC_THRESHOLD     20   // ADC count drop from tare per sensor (used in flexActive)
#define FLEX_BALANCE_THRESHOLD 15   // ADC count difference between Left(Flex1+Flex3) and
                                    // Right(Flex2+Flex4) group sums → backrest balance alert
                                    // Lower = more sensitive; raise if false triggers occur

// ── Beep / Pulse Configuration ────────────────────────────────────────────────
// Buzzer and vibration motors pulse ON/OFF instead of staying continuously active.
// Adjust these durations to taste.
#define BEEP_ON_MS   200   // ms the actuator stays ON per pulse
#define BEEP_OFF_MS  300   // ms the actuator stays OFF between pulses

// ── EMA Filter State ──────────────────────────────────────────────────────────
float emaFsr[5]  = { 0, 0, 0, 0, 0 };  // filtered FSR values
float emaFlex[4] = { 0, 0, 0, 0 };     // filtered flex values
bool  emaInit    = false;               // seed EMA on first loop pass

// ── Flex Adaptive Range (per-sensor running minimum) ──────────────────────────
// Seeded to flexTare[] at tare time; decremented whenever a deeper bend is seen.
// Once the range spans > FLEX_NOISE_FLOOR counts, flexPct is auto-scaled so that
// 0% = straight (tare) and 100% = deepest bend observed since last tare.
float flexRangeMin[4] = { 0, 0, 0, 0 };

// ── Boot-time Tare (no-load baseline) ─────────────────────────────────────────
// Captured in setup() with the chair empty. Load/bend is computed as the DELTA
// from these values so each sensor self-calibrates regardless of its offset.
float fsrTare[5]  = { 0, 0, 0, 0, 0 };
float flexTare[4] = { 0, 0, 0, 0 };

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

// ── FSR Test Mode Flags ────────────────────────────────────────────────────────
bool          testingFsr[5]   = { false, false, false, false, false };
unsigned long fsrTestStart[5] = { 0, 0, 0, 0, 0 };
unsigned long fsrLastPrint[5] = { 0, 0, 0, 0, 0 };

// ── Vibration Motor Test State ────────────────────────────────────────────────
// vibTestMask bits 0-4 correspond to Vib1-Vib5. Only masked motors are turned
// ON during a test; all others are forced OFF for the test duration.
bool          vibTestActive = false;
uint8_t       vibTestMask   = 0;
unsigned long vibTestStart  = 0;

// ── Vibration Pin Array ───────────────────────────────────────────────────────
// Central lookup used by handleVibrationCommand() and the posture feedback loop.
// Index: 0=Vib1(RIGHT) 1=Vib2(LEFT) 2=Vib3(FRONT) 3=Vib4(BACK) 4=Vib5(FLEX)
const uint8_t vibrationPins[5] = {
  VIBRATION_PIN, VIBRATION_PIN2, VIBRATION_PIN3, VIBRATION_PIN4, VIBRATION_PIN5
};

// ── Control Mode ──────────────────────────────────────────────────────────────
// AUTO   : posture feedback drives vibration motors automatically
// MANUAL : motors explicitly commanded via serial; posture does not override
// TEST   : dedicated motor test active; posture feedback suppressed
enum ControlMode { AUTO, MANUAL, TEST };
ControlMode controlMode = AUTO;
// ── User Profile System ──────────────────────────────────────────────────────
// Up to 5 user profiles (ID 0–4), each storing per-user calibration baselines.
// Profiles are persisted to SPIFFS as binary files: /user0.dat … /user4.dat.
// The active user's tare values are loaded into the global fsrTare[], flexTare[],
// and flexRangeMin[] arrays so all existing posture logic works unchanged.
#define MAX_USERS 5

struct UserProfile {
  float fsrTare[5];
  float flexTare[4];
  float flexRangeMin[4];
};

uint8_t currentUserID = 0;   // active user (0–4), default = 0
// ── Button Debounce State ─────────────────────────────────────────────────────
#define DEBOUNCE_MS       50
bool          btn1Raw      = HIGH, btn2Raw      = HIGH;
bool          btn1Stable   = HIGH, btn2Stable   = HIGH;
unsigned long btn1ChangeAt = 0,    btn2ChangeAt = 0;

// ── Button Press-Timing State (User Profile Control) ─────────────────────────
// Button1 SHORT press (<500 ms)  → cycle to next user profile
// Button2 LONG  press (≥2000 ms) → calibrate + save current user profile
#define BTN_SHORT_PRESS_MS  500    // max duration considered a short press
#define BTN_LONG_PRESS_MS  2000   // min duration to trigger calibration

unsigned long btn1PressAt    = 0;   // millis() when Button1 went LOW (stable)
unsigned long btn2PressAt    = 0;   // millis() when Button2 went LOW (stable)
bool          btn2CalibDone  = false;  // prevents re-triggering during one long press

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
// handleVibrationCommand()
//   Independently controls a single vibration motor by 1-based index (1-5).
//   Switches to MANUAL mode so the posture auto-feedback loop does not
//   overwrite the commanded state on subsequent passes. Send "vib auto"
//   to return to automatic posture-driven control.
// ─────────────────────────────────────────────────────────────────────────────
void handleVibrationCommand(int motorIndex, bool state) {
  if (motorIndex < 1 || motorIndex > 5) {
    Serial.println(F("Usage: vib <1-5> on/off"));
    return;
  }
  controlMode = MANUAL;
  digitalWrite(vibrationPins[motorIndex - 1], state ? HIGH : LOW);
  Serial.print(F("Vibration")); Serial.print(motorIndex);
  Serial.println(state ? F(" ON") : F(" OFF"));
}

// ── User Profile Storage Functions ────────────────────────────────────────────

// Build the SPIFFS path for a given user ID: "/user0.dat" … "/user4.dat"
static void profilePath(uint8_t id, char* buf, size_t len) {
  snprintf(buf, len, "/user%u.dat", id);
}

// Returns true if a saved profile exists for the given user ID.
bool profileExists(uint8_t id) {
  if (id >= MAX_USERS) return false;
  char path[16];
  profilePath(id, path, sizeof(path));
  return SPIFFS.exists(path);
}

// Save the current global tare values as a binary profile for the given user ID.
void saveUserProfile(uint8_t id) {
  if (id >= MAX_USERS) { Serial.println(F("Invalid user ID.")); return; }
  char path[16];
  profilePath(id, path, sizeof(path));
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) { Serial.println(F("Profile save failed (SPIFFS).")); return; }
  UserProfile p;
  memcpy(p.fsrTare,     fsrTare,     sizeof(p.fsrTare));
  memcpy(p.flexTare,    flexTare,    sizeof(p.flexTare));
  memcpy(p.flexRangeMin,flexRangeMin,sizeof(p.flexRangeMin));
  f.write((const uint8_t*)&p, sizeof(p));
  f.close();
  Serial.print(F("Profile saved for User ")); Serial.println(id);
}

// Load a binary profile for the given user ID into the global tare arrays.
// Returns true on success, false if the file does not exist or read fails.
bool loadUserProfile(uint8_t id) {
  if (id >= MAX_USERS) return false;
  char path[16];
  profilePath(id, path, sizeof(path));
  if (!SPIFFS.exists(path)) return false;
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) return false;
  UserProfile p;
  size_t n = f.read((uint8_t*)&p, sizeof(p));
  f.close();
  if (n != sizeof(p)) return false;
  memcpy(fsrTare,     p.fsrTare,     sizeof(fsrTare));
  memcpy(flexTare,    p.flexTare,    sizeof(flexTare));
  memcpy(flexRangeMin,p.flexRangeMin,sizeof(flexRangeMin));
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// setup()
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // ── Actuator pins – all off at start ────────────────────────────────────────
  const uint8_t actuators[] = {
    BUZZER_PIN, RED_LED_PIN, GREEN_LED_PIN,
    VIBRATION_PIN, VIBRATION_PIN2, VIBRATION_PIN3, VIBRATION_PIN4, VIBRATION_PIN5
  };
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(actuators[i], OUTPUT);
    digitalWrite(actuators[i], LOW);
  }

  // ── Button pins – internal pull-up ──────────────────────────────────────────
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // ── Boot-time tare: capture no-load baselines for all sensors ───────────────
  // IMPORTANT: chair must be empty (no person, no objects on seat or backrest).
  // Both FSR (seat) and Flex (backrest) tare values are captured here.
  // Send "retare" via Serial to re-run without rebooting.
  Serial.println(F("Capturing tare – keep chair empty..."));
  delay(1000);  // allow ADC to settle after power-on
  {
    const uint8_t fp[5] = { FSR_RIGHT_PIN, FSR_LEFT_PIN, FSR_FRONT_PIN, FSR_BACK_PIN, FSR_MIDDLE_PIN };
    const uint8_t xp[4] = { FLEX_PIN1, FLEX_PIN2, FLEX_PIN3, FLEX_PIN4 };
    for (uint8_t i = 0; i < 5; i++) {
      fsrTare[i] = (float)averagedRead(fp[i], 32);
      Serial.print(F("  FSR")); Serial.print(i + 1);
      Serial.print(F(" tare: ")); Serial.println(fsrTare[i], 0);
    }
    for (uint8_t i = 0; i < 4; i++) {
      flexTare[i]     = (float)averagedRead(xp[i], 16);
      flexRangeMin[i] = flexTare[i];  // seed adaptive range minimum to tare value
      Serial.print(F("  Flex")); Serial.print(i + 1);
      Serial.print(F(" tare: ")); Serial.println(flexTare[i], 0);
    }
  }
  Serial.println(F("Tare complete. Chair is ready."));

  // ── SPIFFS init + attempt to load User 0 profile ────────────────────────────
  if (!SPIFFS.begin(true)) {
    Serial.println(F("SPIFFS mount failed – profiles disabled."));
  } else {
    if (loadUserProfile(0)) {
      Serial.println(F("User 0 profile loaded."));
    } else {
      Serial.println(F("No saved profile for User 0 – using boot tare."));
    }
  }
  Serial.print(F("Active User: ")); Serial.println(currentUserID);

  Serial.println(F("\n── Serial Command Reference ────────────────────────────────"));
  Serial.println(F("  Actuators : buzzer on/off | red led on/off | green led on/off"));
  Serial.println(F("              vib <1-5> on/off | vib all on/off | vib auto"));
  Serial.println(F("  Tare      : retare  (re-capture baselines without reboot)"));
  Serial.println(F("  Flex test : test flex<1-4>"));
  Serial.println(F("  FSR test  : test fsr <right|left|front|back|mid>"));
  Serial.println(F("  Vib test  : test vibration <1|2|3|4|5|all>  (10 s; others off)"));
  Serial.println(F("  Logging   : log start | log stop | log rate <ms> | label <name>"));
  Serial.println(F("  Profiles  : user <0-4> | calibrate | save user | list users"));
  Serial.println(F("────────────────────────────────────────────────────────────\n"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// loop()
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
  const uint8_t fsrPins[5] = { FSR_RIGHT_PIN, FSR_LEFT_PIN, FSR_FRONT_PIN, FSR_BACK_PIN, FSR_MIDDLE_PIN };

  // ── Button debounce ────────────────────────────────────────────────────────
  {
    unsigned long now = millis();
    bool r1 = digitalRead(BUTTON1_PIN);
    bool r2 = digitalRead(BUTTON2_PIN);
    if (r1 != btn1Raw) { btn1Raw = r1; btn1ChangeAt = now; }
    if (r2 != btn2Raw) { btn2Raw = r2; btn2ChangeAt = now; }
    if ((now - btn1ChangeAt >= DEBOUNCE_MS) && (btn1Raw != btn1Stable)) {
      btn1Stable = btn1Raw;
      Serial.println(btn1Stable == LOW ? "Button1 pressed." : "Button1 released.");
      // ── Record press start time ──────────────────────────────────────────
      if (btn1Stable == LOW) btn1PressAt = now;
    }
    if ((now - btn2ChangeAt >= DEBOUNCE_MS) && (btn2Raw != btn2Stable)) {
      btn2Stable = btn2Raw;
      Serial.println(btn2Stable == LOW ? "Button2 pressed." : "Button2 released.");
      // ── Record press start / reset long-press flag on release ────────────
      if (btn2Stable == LOW)  { btn2PressAt = now; btn2CalibDone = false; }
    }
  }

  // ── Button1: short press → cycle to next user ─────────────────────────────
  // Detected on release so we can measure duration accurately.
  {
    static bool btn1WasLow = false;
    if (btn1Stable == LOW) {
      btn1WasLow = true;
    } else if (btn1WasLow) {
      // Button just released
      btn1WasLow = false;
      unsigned long pressDuration = millis() - btn1PressAt;
      if (pressDuration < BTN_SHORT_PRESS_MS) {
        // Cycle to next user (0→1→2→3→4→0)
        currentUserID = (currentUserID + 1) % MAX_USERS;
        Serial.print(F("Active User: ")); Serial.println(currentUserID);
        if (loadUserProfile(currentUserID)) {
          Serial.println(F("Profile loaded."));
        } else {
          Serial.print(F("User ")); Serial.print(currentUserID);
          Serial.println(F(" not calibrated."));
        }
        emaInit = false;  // re-seed EMA after tare change
      }
    }
  }

  // ── Button2: long press (≥2 s) → calibrate current user ──────────────────
  // Triggered while still held so there is no wait on release.
  {
    if (btn2Stable == LOW && !btn2CalibDone) {
      if ((millis() - btn2PressAt) >= BTN_LONG_PRESS_MS) {
        btn2CalibDone = true;  // arm: fire exactly once per press
        Serial.print(F("Calibrating User ")); Serial.print(currentUserID);
        Serial.println(F(" (button)..."));
        const uint8_t fp[5] = { FSR_RIGHT_PIN, FSR_LEFT_PIN, FSR_FRONT_PIN,
                                 FSR_BACK_PIN, FSR_MIDDLE_PIN };
        const uint8_t xp[4] = { FLEX_PIN1, FLEX_PIN2, FLEX_PIN3, FLEX_PIN4 };
        for (uint8_t i = 0; i < 5; i++) {
          fsrTare[i] = (float)averagedRead(fp[i], 32);
        }
        for (uint8_t i = 0; i < 4; i++) {
          flexTare[i]     = (float)averagedRead(xp[i], 16);
          flexRangeMin[i] = flexTare[i];
        }
        saveUserProfile(currentUserID);
        Serial.println(F("Calibration saved."));
        emaInit = false;  // re-seed EMA after tare change
      }
    }
  }

  // ── Serial command handler ──────────────────────────────────────────────────
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    // ── Actuator manual control ────────────────────────────────────────────────
    if      (command == "buzzer on")          { digitalWrite(BUZZER_PIN,     HIGH); Serial.println("Buzzer ON"); }
    else if (command == "buzzer off")         { digitalWrite(BUZZER_PIN,     LOW);  Serial.println("Buzzer OFF"); }
    else if (command == "red led on")         { digitalWrite(RED_LED_PIN,    HIGH); Serial.println("Red LED ON"); }
    else if (command == "red led off")        { digitalWrite(RED_LED_PIN,    LOW);  Serial.println("Red LED OFF"); }
    else if (command == "green led on")       { digitalWrite(GREEN_LED_PIN,  HIGH); Serial.println("Green LED ON"); }
    else if (command == "green led off")      { digitalWrite(GREEN_LED_PIN,  LOW);  Serial.println("Green LED OFF"); }
    // ── Vibration motor control: "vib <1-5> on/off" | "vib all on/off" | "vib auto" ──
    // Each motor is isolated via handleVibrationCommand() which switches the
    // system to MANUAL mode, preventing the posture loop from overriding it.
    // "vib auto" restores automatic posture-driven feedback.
    else if (command.startsWith("vib ")) {
      String args = command.substring(4);
      args.trim();
      if (args == "auto") {
        controlMode = AUTO;
        Serial.println(F("Vibration: AUTO mode restored."));
      } else if (args == "all on") {
        controlMode = MANUAL;
        for (uint8_t i = 0; i < 5; i++) digitalWrite(vibrationPins[i], HIGH);
        Serial.println(F("All motors ON"));
      } else if (args == "all off") {
        controlMode = MANUAL;
        for (uint8_t i = 0; i < 5; i++) digitalWrite(vibrationPins[i], LOW);
        Serial.println(F("All motors OFF"));
      } else {
        int space = args.indexOf(' ');
        if (space > 0) {
          int    idx      = args.substring(0, space).toInt();
          String stateStr = args.substring(space + 1);
          stateStr.trim();
          if      (stateStr == "on")  handleVibrationCommand(idx, true);
          else if (stateStr == "off") handleVibrationCommand(idx, false);
          else                        Serial.println(F("Usage: vib <1-5> on/off | vib all on/off | vib auto"));
        } else {
          Serial.println(F("Usage: vib <1-5> on/off | vib all on/off | vib auto"));
        }
      }
    }

    // ── Re-tare on command ─────────────────────────────────────────────────────
    // Re-captures no-load baselines live, without a reboot.
    // Ensure the chair is empty before sending this command.
    // Saves updated tare values to the current user's SPIFFS profile.
    else if (command == "retare") {
      Serial.println(F("Re-taring – keep chair empty..."));
      const uint8_t fp[5] = { FSR_RIGHT_PIN, FSR_LEFT_PIN, FSR_FRONT_PIN, FSR_BACK_PIN, FSR_MIDDLE_PIN };
      const uint8_t xp[4] = { FLEX_PIN1, FLEX_PIN2, FLEX_PIN3, FLEX_PIN4 };
      for (uint8_t i = 0; i < 5; i++) {
        fsrTare[i] = (float)averagedRead(fp[i], 32);
        Serial.print(F("  FSR")); Serial.print(i + 1);
        Serial.print(F(" tare: ")); Serial.println(fsrTare[i], 0);
      }
      for (uint8_t i = 0; i < 4; i++) {
        flexTare[i]     = (float)averagedRead(xp[i], 16);
        flexRangeMin[i] = flexTare[i];  // reset adaptive range minimum on re-tare
        Serial.print(F("  Flex")); Serial.print(i + 1);
        Serial.print(F(" tare: ")); Serial.println(flexTare[i], 0);
      }
      saveUserProfile(currentUserID);
      Serial.println(F("Re-tare complete."));
    }

    // ── Flex sensor timed test (10 s) ─────────────────────────────────────────
    else if (command == "test flex1") { testingFlex1 = true; testStartTime1 = lastPrintTime1 = millis(); Serial.println("Testing Flex1 for 10 s..."); }
    else if (command == "test flex2") { testingFlex2 = true; testStartTime2 = lastPrintTime2 = millis(); Serial.println("Testing Flex2 for 10 s..."); }
    else if (command == "test flex3") { testingFlex3 = true; testStartTime3 = lastPrintTime3 = millis(); Serial.println("Testing Flex3 for 10 s..."); }
    else if (command == "test flex4") { testingFlex4 = true; testStartTime4 = lastPrintTime4 = millis(); Serial.println("Testing Flex4 for 10 s..."); }

    // ── FSR sensor timed test (10 s) ──────────────────────────────────────────
    else if (command == "test fsr right") { testingFsr[0] = true; fsrTestStart[0] = fsrLastPrint[0] = millis(); Serial.println("Testing FSR-RIGHT for 10 s..."); }
    else if (command == "test fsr left")  { testingFsr[1] = true; fsrTestStart[1] = fsrLastPrint[1] = millis(); Serial.println("Testing FSR-LEFT for 10 s...");  }
    else if (command == "test fsr front") { testingFsr[2] = true; fsrTestStart[2] = fsrLastPrint[2] = millis(); Serial.println("Testing FSR-FRONT for 10 s..."); }
    else if (command == "test fsr back")  { testingFsr[3] = true; fsrTestStart[3] = fsrLastPrint[3] = millis(); Serial.println("Testing FSR-BACK for 10 s...");  }
    else if (command == "test fsr mid")   { testingFsr[4] = true; fsrTestStart[4] = fsrLastPrint[4] = millis(); Serial.println("Testing FSR-MID for 10 s...");   }

    // ── Vibration motor timed test (10 s) ─────────────────────────────────────
    // All motors are forced OFF first; only the target motor(s) are turned ON.
    else if (command.startsWith("test vibration ")) {
      for (uint8_t i = 0; i < 5; i++) digitalWrite(vibrationPins[i], LOW);  // all OFF first
      String target = command.substring(15);
      target.trim();
      if (target == "all") {
        vibTestMask = 0x1F;  // bits 0-4
        for (uint8_t i = 0; i < 5; i++) digitalWrite(vibrationPins[i], HIGH);
        Serial.println("Testing ALL vibration motors for 10 s...");
      } else {
        int n = target.toInt();
        if (n >= 1 && n <= 5) {
          vibTestMask = (1 << (n - 1));
          digitalWrite(vibrationPins[n - 1], HIGH);
          Serial.print("Testing Vibration"); Serial.print(n); Serial.println(" for 10 s (others OFF)...");
        } else {
          Serial.println("Usage: test vibration <1|2|3|4|5|all>");
          vibTestMask = 0;
        }
      }
      if (vibTestMask) { vibTestActive = true; vibTestStart = millis(); controlMode = TEST; }
    }

    // ── Dataset logging control ────────────────────────────────────────────────
    else if (command == "log start") {
      loggingEnabled = true;
      lastLogTime    = millis();
      // CSV header – Python logger keyed on "DATA_HEADER:" prefix
      Serial.println("DATA_HEADER:timestamp_ms,flex1,flex2,flex3,flex4,"
                     "fsr_right,fsr_left,fsr_front,fsr_back,fsr_mid,"
                     "total_fsr,front_back_ratio,left_right_ratio,label");
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

    // ── User Profile Commands ────────────────────────────────────────────────
    // Select active user: "user 0" … "user 4"
    else if (command.startsWith("user ") && command.length() >= 6
             && command.charAt(5) >= '0' && command.charAt(5) <= '4') {
      uint8_t id = command.charAt(5) - '0';
      currentUserID = id;
      if (loadUserProfile(id)) {
        Serial.print(F("Active User: ")); Serial.println(id);
        Serial.println(F("Profile loaded."));
      } else {
        Serial.print(F("Active User: ")); Serial.println(id);
        Serial.println(F("User not calibrated."));
      }
      emaInit = false;  // re-seed EMA after tare change
    }

    // Calibrate current user: live sensor tare + save to SPIFFS
    else if (command == "calibrate") {
      Serial.print(F("Calibrating User ")); Serial.print(currentUserID);
      Serial.println(F(" – keep chair empty..."));
      const uint8_t fp[5] = { FSR_RIGHT_PIN, FSR_LEFT_PIN, FSR_FRONT_PIN, FSR_BACK_PIN, FSR_MIDDLE_PIN };
      const uint8_t xp[4] = { FLEX_PIN1, FLEX_PIN2, FLEX_PIN3, FLEX_PIN4 };
      for (uint8_t i = 0; i < 5; i++) {
        fsrTare[i] = (float)averagedRead(fp[i], 32);
        Serial.print(F("  FSR")); Serial.print(i + 1);
        Serial.print(F(" tare: ")); Serial.println(fsrTare[i], 0);
      }
      for (uint8_t i = 0; i < 4; i++) {
        flexTare[i]     = (float)averagedRead(xp[i], 16);
        flexRangeMin[i] = flexTare[i];
        Serial.print(F("  Flex")); Serial.print(i + 1);
        Serial.print(F(" tare: ")); Serial.println(flexTare[i], 0);
      }
      saveUserProfile(currentUserID);
      Serial.println(F("Calibration complete."));
      emaInit = false;  // re-seed EMA after tare change
    }

    // Explicitly save current user profile
    else if (command == "save user") {
      saveUserProfile(currentUserID);
    }

    // List which user profiles exist on SPIFFS
    else if (command == "list users") {
      Serial.println(F("── User Profiles ──"));
      for (uint8_t i = 0; i < MAX_USERS; i++) {
        Serial.print(F("  User ")); Serial.print(i);
        Serial.print(F(": "));
        Serial.print(profileExists(i) ? F("saved") : F("empty"));
        if (i == currentUserID) Serial.print(F("  [active]"));
        Serial.println();
      }
      Serial.println(F("────────────────────"));
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
  const float fsrRight = emaFsr[0], fsrLeft  = emaFsr[1];
  const float fsrFront = emaFsr[2], fsrBack  = emaFsr[3];
  const float fsrMid   = emaFsr[4];

  // ── Distribution-based balance: normalize each sensor's share of total load ───
  // Robust against saturation: ratios remain meaningful even when all values are low.
  float totalFsr  = fsrRight + fsrLeft + fsrFront + fsrBack + fsrMid;
  float distRight = totalFsr > 0 ? fsrRight / totalFsr : 0.0f;
  float distLeft  = totalFsr > 0 ? fsrLeft  / totalFsr : 0.0f;
  float distFront = totalFsr > 0 ? fsrFront / totalFsr : 0.0f;
  float distBack  = totalFsr > 0 ? fsrBack  / totalFsr : 0.0f;

  // ── Percentage mapping (0 = no load/straight tare, 100 = full load/max bend) ───
  // Clamped to [0, 100] and available for both diagnostic prints and control logic.
  float fsrPct[5], flexPct[4];
  for (uint8_t i = 0; i < 5; i++)
    fsrPct[i]  = (fsrTare[i]  > 0.0f)
                 ? constrain((fsrTare[i]  - emaFsr[i])  / fsrTare[i]  * 100.0f, 0.0f, 100.0f)
                 : 0.0f;
  // ── Flex absolute delta + adaptive auto-range ─────────────────────────────
  // Update per-sensor running minimum (deepest bend seen since last tare).
  for (uint8_t i = 0; i < 4; i++)
    if (emaFlex[i] < flexRangeMin[i]) flexRangeMin[i] = emaFlex[i];

  // flexDelta[i]: ADC count drop from tare with noise-floor deadband.
  // Using an absolute count sidesteps the large-denominator problem that
  // limits tare-relative percentages to ~1-3% even on noticeable bends.
  float flexDelta[4];
  for (uint8_t i = 0; i < 4; i++) {
    float d      = flexTare[i] - emaFlex[i];
    flexDelta[i] = (d >= (float)FLEX_NOISE_FLOOR) ? d : 0.0f;
  }

  // flexPct[i]: auto-scaled to the widest bend observed since last tare.
  // Once span > FLEX_NOISE_FLOOR, 0% = straight, 100% = deepest-ever bend,
  // giving meaningful diagnostics at any physical sensitivity level.
  // Before any bend is recorded the original tare-relative % is used as fallback.
  for (uint8_t i = 0; i < 4; i++) {
    if (flexTare[i] <= 0.0f) { flexPct[i] = 0.0f; continue; }
    float span = flexTare[i] - flexRangeMin[i];
    flexPct[i] = constrain(
      (span > (float)FLEX_NOISE_FLOOR)
        ? flexDelta[i] / span       * 100.0f   // auto-ranged: reliable 0-100 %
        : flexDelta[i] / flexTare[i] * 100.0f, // fallback before range is learned
      0.0f, 100.0f
    );
  }

  // ── Flex sensor timed test output ─────────────────────────────────────────────
  {
    const float    fv[4]  = { flex1, flex2, flex3, flex4 };
    bool*          tf[4]  = { &testingFlex1, &testingFlex2, &testingFlex3, &testingFlex4 };
    unsigned long* ts[4]  = { &testStartTime1, &testStartTime2, &testStartTime3, &testStartTime4 };
    unsigned long* tp[4]  = { &lastPrintTime1, &lastPrintTime2, &lastPrintTime3, &lastPrintTime4 };
    const char*    nm[4]  = { "Flex1", "Flex2", "Flex3", "Flex4" };
    unsigned long  now    = millis();
    for (uint8_t i = 0; i < 4; i++) {
      if (*tf[i]) {
        if (now - *tp[i] >= 200) { Serial.print(nm[i]); Serial.print(": "); Serial.println(fv[i], 0); *tp[i] = now; }
        if (now - *ts[i] >= 10000) { *tf[i] = false; Serial.print(nm[i]); Serial.println(" test done."); }
      }
    }
  }

  // ── FSR sensor timed test output ──────────────────────────────────────────────
  {
    const float   fv[5]  = { fsrRight, fsrLeft, fsrFront, fsrBack, fsrMid };
    const char*   nm[5]  = { "FSR-RIGHT", "FSR-LEFT", "FSR-FRONT", "FSR-BACK", "FSR-MID" };
    unsigned long now    = millis();
    for (uint8_t i = 0; i < 5; i++) {
      if (testingFsr[i]) {
        if (now - fsrLastPrint[i] >= 200) { Serial.print(nm[i]); Serial.print(": "); Serial.println(fv[i], 0); fsrLastPrint[i] = now; }
        if (now - fsrTestStart[i] >= 10000) { testingFsr[i] = false; Serial.print(nm[i]); Serial.println(" test done."); }
      }
    }
  }

  // ── Step 3: Diagnostic serial output ─────────────────────────────────────────

  // Flex sensors – print every 1 s (% bend from tare)
  if (millis() - lastSensorPrintTime > 1000) {
    Serial.print("Flex: ");
    for (uint8_t i = 0; i < 4; i++) {
      Serial.print(flexPct[i], 1);
      Serial.print(i < 3 ? "%  " : "%");
    }
    // Group balance diagnostic: Left = Flex1+Flex3, Right = Flex2+Flex4
    float pL = flexDelta[0] + flexDelta[2];
    float pR = flexDelta[1] + flexDelta[3];
    Serial.print("  | L-grp:"); Serial.print(pL, 0);
    Serial.print(" R-grp:");    Serial.print(pR, 0);
    Serial.print(" diff:");     Serial.print(fabsf(pL - pR), 0);
    Serial.print(" ALERT:");    Serial.println(fabsf(pL - pR) >= FLEX_BALANCE_THRESHOLD ? "YES" : "no");
    lastSensorPrintTime = millis();
  }

  // FSR seat pressure – print every 2 s (% load from tare)
  if (millis() - lastFsrPrintTime > 2000) {
    float avgLoad = (fsrPct[0] + fsrPct[1] + fsrPct[2] + fsrPct[3] + fsrPct[4]) / 5.0f;
    bool  sitting = avgLoad >= SITTING_THRESHOLD;
    Serial.println("── FSR Seat Pressure (% load) ────────────");
    Serial.print("  R:");   Serial.print(fsrPct[0], 1); Serial.print("%");
    Serial.print("  L:");   Serial.print(fsrPct[1], 1); Serial.print("%");
    Serial.print("  F:");   Serial.print(fsrPct[2], 1); Serial.print("%");
    Serial.print("  B:");   Serial.print(fsrPct[3], 1); Serial.print("%");
    Serial.print("  MID:"); Serial.print(fsrPct[4], 1); Serial.println("%");
    Serial.print("  Avg: "); Serial.print(avgLoad, 1);
    Serial.print("%  Sitting: "); Serial.println(sitting ? "YES" : "NO");
    if (sitting) {
      Serial.print("  L/R diff: "); Serial.print(fsrPct[0] - fsrPct[1], 1);
      Serial.print("%  F/B diff: "); Serial.print(fsrPct[2] - fsrPct[3], 1);
      Serial.println("%");
    }
    Serial.println("─────────────────────────────────────────");
    lastFsrPrintTime = millis();
  }

  // ── Step 4: Dataset logging (CSV row) ────────────────────────────────────────
  // Output prefixed with "DATA:" so logger.py can filter debug lines.
  // Column order matches DATA_HEADER sent on "log start".
  if (loggingEnabled && (millis() - lastLogTime >= logInterval)) {
    lastLogTime = millis();

    // Distribution ratios (center excluded from ratio calc)
    float totalFsr = fsrRight + fsrLeft + fsrFront + fsrBack; // middle excluded from ratio calc
    float frRatio  = fsrBack  > 1.0f ? fsrFront / fsrBack  : 0.0f;
    float lrRatio  = fsrRight > 1.0f ? fsrLeft  / fsrRight : 0.0f;

    Serial.print("DATA:");
    Serial.print(millis());        Serial.print(",");
    Serial.print(flex1,  0);       Serial.print(",");
    Serial.print(flex2,  0);       Serial.print(",");
    Serial.print(flex3,  0);       Serial.print(",");
    Serial.print(flex4,  0);       Serial.print(",");
    Serial.print(fsrRight, 0);     Serial.print(",");
    Serial.print(fsrLeft,  0);     Serial.print(",");
    Serial.print(fsrFront, 0);     Serial.print(",");
    Serial.print(fsrBack,  0);     Serial.print(",");
    Serial.print(fsrMid,   0);     Serial.print(",");
    Serial.print(totalFsr, 0);   Serial.print(",");
    Serial.print(frRatio,  4);   Serial.print(",");
    Serial.print(lrRatio,  4);   Serial.print(",");
    Serial.println(logLabel);
  }

  // ── Vibration motor test auto-off ───────────────────────────────────────────
  if (vibTestActive && (millis() - vibTestStart >= 10000)) {
    for (uint8_t i = 0; i < 5; i++) digitalWrite(vibrationPins[i], LOW);
    vibTestActive = false;
    vibTestMask   = 0;
    controlMode   = AUTO;
    Serial.println("Vibration motor test done.");
  }

  // ── Step 5: Posture detection + actuator feedback ────────────────────────────
  //
  //  Per-sensor load/bend percentages (fsrPct[], flexPct[]) were computed above.
  //
  //  SEAT logic:
  //    1. Confirm someone is sitting: average of all 5 FSR % >= SITTING_THRESHOLD.
  //       → Prevents actuators from firing on an empty chair (tare drift is irrelevant).
  //    2. Check left/right balance: vibrate the *heavier* side to cue the person.
  //    3. Check front/back balance: same approach.
  //    4. Middle FSR drives the red LED (sitting indicator).
  //
  //  BACKREST logic:
  //    Any flex sensor % > FLEX_PCT_THRESHOLD → buzzer + Vib5 + green LED.
  //
  //  NOTE: AUTO mode only; MANUAL/TEST modes suppress actuator writes.

  // Pulse clock – shared by all pulsed actuators in AUTO mode.
  bool beepOn = (millis() % (BEEP_ON_MS + BEEP_OFF_MS)) < BEEP_ON_MS;

  // Sitting detection: gate all seat feedback on confirmed occupancy.
  float avgSeatLoad = (fsrPct[0] + fsrPct[1] + fsrPct[2] + fsrPct[3] + fsrPct[4]) / 5.0f;
  bool  isSitting   = (avgSeatLoad >= SITTING_THRESHOLD);

  // Seat balance feedback (AUTO mode only; MANUAL/TEST suppress this)
  if (controlMode == AUTO) {
    if (isSitting) {
      // Left / Right imbalance: distribution-based (robust at any load level)
      bool vibRight = (distRight > distLeft  + DIST_THRESHOLD);  // right heavier → cue shift left
      bool vibLeft  = (distLeft  > distRight + DIST_THRESHOLD);  // left  heavier → cue shift right
      // Front / Back imbalance: distribution-based
      bool vibFront = (distFront > distBack  + DIST_THRESHOLD);  // front heavier → cue sit back
      bool vibBack  = (distBack  > distFront + DIST_THRESHOLD);  // back  heavier → cue lean forward

      // Distribution debug print (every 2 s, aligned with FSR print cadence)
      if (millis() - lastFsrPrintTime > 2000) {
        Serial.print("Dist F:"); Serial.print(distFront, 3);
        Serial.print(" B:");     Serial.print(distBack,  3);
        Serial.print(" L:");     Serial.print(distLeft,  3);
        Serial.print(" R:");     Serial.println(distRight, 3);
      }

      digitalWrite(vibrationPins[0], vibRight && beepOn ? HIGH : LOW);  // Vib1 → RIGHT
      digitalWrite(vibrationPins[1], vibLeft  && beepOn ? HIGH : LOW);  // Vib2 → LEFT
      digitalWrite(vibrationPins[2], vibFront && beepOn ? HIGH : LOW);  // Vib3 → FRONT
      digitalWrite(vibrationPins[3], vibBack  && beepOn ? HIGH : LOW);  // Vib4 → BACK
    } else {
      // Chair is empty – silence all seat motors
      for (uint8_t i = 0; i < 4; i++) digitalWrite(vibrationPins[i], LOW);
    }
  }

  // Middle seat zone → red LED (sitting indicator, stays solid)
  digitalWrite(RED_LED_PIN, isSitting ? HIGH : LOW);

  // Backrest left / right group balance → buzzer + Vib5 + green LED
  //
  // Sensor layout:
  //   Left  group : Flex1 (index 0, bottom-left) + Flex3 (index 2, top-left)
  //   Right group : Flex2 (index 1, top-right)   + Flex4 (index 3, bottom-right)
  //
  // flexDelta[i] is the noise-deadbanded ADC drop from tare (0 when within noise floor).
  // Summing each group and comparing the absolute difference against FLEX_BALANCE_THRESHOLD
  // detects left/right lean without firing on symmetric full-back bends.
  bool flexActive[4];
  for (uint8_t i = 0; i < 4; i++)
    flexActive[i] = (flexDelta[i] >= (float)FLEX_ADC_THRESHOLD);

  float groupLeft  = flexDelta[0] + flexDelta[2];   // Flex1 + Flex3 (left side)
  float groupRight = flexDelta[1] + flexDelta[3];   // Flex2 + Flex4 (right side)
  bool  backrestAlert = fabsf(groupLeft - groupRight) >= (float)FLEX_BALANCE_THRESHOLD;
  digitalWrite(BUZZER_PIN,    backrestAlert && beepOn ? HIGH : LOW);   // pulsed
  digitalWrite(GREEN_LED_PIN, backrestAlert ? HIGH : LOW);              // stays solid
  if (controlMode == AUTO) {
    digitalWrite(vibrationPins[4], backrestAlert && beepOn ? HIGH : LOW);  // Vib5 pulsed
  }

  delay(50);
}