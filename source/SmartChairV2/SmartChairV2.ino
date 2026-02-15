#include <Arduino.h>

#define BUZZER_PIN 23
#define LED_PIN 12
#define VIBRATION_PIN 19
#define VIBRATION_PIN2 18
#define VIBRATION_PIN3 13
#define VIBRATION_PIN4 14
#define FLEX_PIN1 34
#define FLEX_PIN2 35

bool testingFlex1 = false;
bool testingFlex2 = false;
unsigned long testStartTime1 = 0;
unsigned long testStartTime2 = 0;
unsigned long lastPrintTime1 = 0;
unsigned long lastPrintTime2 = 0;
unsigned long lastToggleTime = 0;
bool beepState = false;
unsigned long lastSensorPrintTime = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, OUTPUT);
  pinMode(VIBRATION_PIN2, OUTPUT);
  pinMode(VIBRATION_PIN3, OUTPUT);
  pinMode(VIBRATION_PIN4, OUTPUT);

  // Auto-calibration of flex sensors
  Serial.println("Calibrating flex sensors...");
  delay(1000); // Wait for sensors to stabilize
  int baseline1 = analogRead(FLEX_PIN1);
  int baseline2 = analogRead(FLEX_PIN2);
  Serial.print("Flex1 baseline (straight): ");
  Serial.println(baseline1);
  Serial.print("Flex2 baseline (straight): ");
  Serial.println(baseline2);
  Serial.println("Adjust thresholds in code based on these values.");

  Serial.println("Testing ready. Commands: 'buzzer on/off', 'led on/off', 'vibration on/off', 'vibration2 on/off', 'vibration3 on/off', 'vibration4 on/off', 'test flex1', 'test flex2'");
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "buzzer on") {
      digitalWrite(BUZZER_PIN, HIGH);
      Serial.println("Buzzer ON");
    } else if (command == "buzzer off") {
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Buzzer OFF");
    } else if (command == "led on") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("LED ON");
    } else if (command == "led off") {
      digitalWrite(LED_PIN, LOW);
      Serial.println("LED OFF");
    } else if (command == "vibration on") {
      digitalWrite(VIBRATION_PIN, HIGH);
      Serial.println("Vibration ON");
    } else if (command == "vibration off") {
      digitalWrite(VIBRATION_PIN, LOW);
      Serial.println("Vibration OFF");
    } else if (command == "vibration2 on") {
      digitalWrite(VIBRATION_PIN2, HIGH);
      Serial.println("Vibration2 ON");
    } else if (command == "vibration2 off") {
      digitalWrite(VIBRATION_PIN2, LOW);
      Serial.println("Vibration2 OFF");
    } else if (command == "vibration3 on") {
      digitalWrite(VIBRATION_PIN3, HIGH);
      Serial.println("Vibration3 ON");
    } else if (command == "vibration3 off") {
      digitalWrite(VIBRATION_PIN3, LOW);
      Serial.println("Vibration3 OFF");
    } else if (command == "vibration4 on") {
      digitalWrite(VIBRATION_PIN4, HIGH);
      Serial.println("Vibration4 ON");
    } else if (command == "vibration4 off") {
      digitalWrite(VIBRATION_PIN4, LOW);
      Serial.println("Vibration4 OFF");
    } else if (command == "test flex1") {
      testingFlex1 = true;
      testStartTime1 = millis();
      lastPrintTime1 = millis();
      Serial.println("Testing Flex1 for 10 seconds...");
    } else if (command == "test flex2") {
      testingFlex2 = true;
      testStartTime2 = millis();
      lastPrintTime2 = millis();
      Serial.println("Testing Flex2 for 10 seconds...");
    } else {
      Serial.println("Unknown command");
    }
  }

  // Continuous monitoring of flex sensors
  int flex1Value = analogRead(FLEX_PIN1);
  int flex2Value = analogRead(FLEX_PIN2);
  bool actuateAll = false;
  
  if (millis() - lastSensorPrintTime > 1000) {
    Serial.print("Flex1: ");
    Serial.print(flex1Value);
    Serial.print(" Flex2: ");
    Serial.println(flex2Value);
    lastSensorPrintTime = millis();
  }
  
  if (flex1Value > 2200 || flex2Value > 2200) actuateAll = true;
  if (actuateAll) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(VIBRATION_PIN, HIGH);
    digitalWrite(VIBRATION_PIN2, HIGH);
    digitalWrite(VIBRATION_PIN3, HIGH);
    digitalWrite(VIBRATION_PIN4, HIGH);
    delay(1000);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(VIBRATION_PIN, LOW);
    digitalWrite(VIBRATION_PIN2, LOW);
    digitalWrite(VIBRATION_PIN3, LOW);
    digitalWrite(VIBRATION_PIN4, LOW);
    delay(1000);
  }

  
  
}