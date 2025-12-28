// Smart Chair ESP32 Sketch
// This sketch controls the smart chair with pressure, vibration, and flex sensors

// Pin definitions for ESP32 38-pin board (GPIO numbers)
// Avoiding pins that conflict with WiFi, boot, or flash
#define PRESSURE_SENSOR_1 32  // ADC1, bidirectional
#define PRESSURE_SENSOR_2 33  // ADC1, bidirectional
#define PRESSURE_SENSOR_3 34  // ADC1, input only (ok for analog read)
#define PRESSURE_SENSOR_4 35  // ADC1, input only (ok for analog read)
#define PRESSURE_SENSOR_5 36  // ADC1, input only (ok for analog read)

#define VIBRATION_SENSOR_1 16  // Digital, safe
#define VIBRATION_SENSOR_2 17  // Digital, safe
#define VIBRATION_SENSOR_3 18  // Digital, safe
#define VIBRATION_SENSOR_4 19  // Digital, safe

#define FLEX_SENSOR_1 37  // ADC1, input only (ok for analog read)
#define FLEX_SENSOR_2 38  // ADC1, input only (ok for analog read)

#define LED_PIN 2   // Safe for output
#define BUZZER_PIN 4  // Safe for output

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  Serial.println("Smart Chair Initialized");

  // Set pin modes
  pinMode(PRESSURE_SENSOR_1, INPUT);
  pinMode(PRESSURE_SENSOR_2, INPUT);
  pinMode(PRESSURE_SENSOR_3, INPUT);
  pinMode(PRESSURE_SENSOR_4, INPUT);
  pinMode(PRESSURE_SENSOR_5, INPUT);

  pinMode(VIBRATION_SENSOR_1, INPUT);
  pinMode(VIBRATION_SENSOR_2, INPUT);
  pinMode(VIBRATION_SENSOR_3, INPUT);
  pinMode(VIBRATION_SENSOR_4, INPUT);

  pinMode(FLEX_SENSOR_1, INPUT);
  pinMode(FLEX_SENSOR_2, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Initial LED blink to indicate setup complete
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Remove any trailing whitespace

    if (command == "p1") {
      int pressure1 = analogRead(PRESSURE_SENSOR_1);
      Serial.print("Pressure Sensor 1: ");
      Serial.println(pressure1);
    } else if (command == "p2") {
      int pressure2 = analogRead(PRESSURE_SENSOR_2);
      Serial.print("Pressure Sensor 2: ");
      Serial.println(pressure2);
    } else if (command == "p3") {
      int pressure3 = analogRead(PRESSURE_SENSOR_3);
      Serial.print("Pressure Sensor 3: ");
      Serial.println(pressure3);
    } else if (command == "p4") {
      int pressure4 = analogRead(PRESSURE_SENSOR_4);
      Serial.print("Pressure Sensor 4: ");
      Serial.println(pressure4);
    } else if (command == "p5") {
      int pressure5 = analogRead(PRESSURE_SENSOR_5);
      Serial.print("Pressure Sensor 5: ");
      Serial.println(pressure5);
    } else if (command == "v1") {
      int vibration1 = digitalRead(VIBRATION_SENSOR_1);
      Serial.print("Vibration Sensor 1: ");
      Serial.println(vibration1);
    } else if (command == "v2") {
      int vibration2 = digitalRead(VIBRATION_SENSOR_2);
      Serial.print("Vibration Sensor 2: ");
      Serial.println(vibration2);
    } else if (command == "v3") {
      int vibration3 = digitalRead(VIBRATION_SENSOR_3);
      Serial.print("Vibration Sensor 3: ");
      Serial.println(vibration3);
    } else if (command == "v4") {
      int vibration4 = digitalRead(VIBRATION_SENSOR_4);
      Serial.print("Vibration Sensor 4: ");
      Serial.println(vibration4);
    } else if (command == "f1") {
      int flex1 = analogRead(FLEX_SENSOR_1);
      Serial.print("Flex Sensor 1: ");
      Serial.println(flex1);
    } else if (command == "f2") {
      int flex2 = analogRead(FLEX_SENSOR_2);
      Serial.print("Flex Sensor 2: ");
      Serial.println(flex2);
    } else if (command == "led on") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("LED turned ON");
    } else if (command == "led off") {
      digitalWrite(LED_PIN, LOW);
      Serial.println("LED turned OFF");
    } else if (command == "buzz") {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Buzzer beeped");
    } else if (command == "all") {
      // Read all sensors
      int pressure1 = analogRead(PRESSURE_SENSOR_1);
      int pressure2 = analogRead(PRESSURE_SENSOR_2);
      int pressure3 = analogRead(PRESSURE_SENSOR_3);
      int pressure4 = analogRead(PRESSURE_SENSOR_4);
      int pressure5 = analogRead(PRESSURE_SENSOR_5);

      int vibration1 = digitalRead(VIBRATION_SENSOR_1);
      int vibration2 = digitalRead(VIBRATION_SENSOR_2);
      int vibration3 = digitalRead(VIBRATION_SENSOR_3);
      int vibration4 = digitalRead(VIBRATION_SENSOR_4);

      int flex1 = analogRead(FLEX_SENSOR_1);
      int flex2 = analogRead(FLEX_SENSOR_2);

      Serial.println("All Sensor Readings:");
      Serial.print("Pressure: ");
      Serial.print(pressure1); Serial.print(", ");
      Serial.print(pressure2); Serial.print(", ");
      Serial.print(pressure3); Serial.print(", ");
      Serial.print(pressure4); Serial.print(", ");
      Serial.println(pressure5);

      Serial.print("Vibration: ");
      Serial.print(vibration1); Serial.print(", ");
      Serial.print(vibration2); Serial.print(", ");
      Serial.print(vibration3); Serial.print(", ");
      Serial.println(vibration4);

      Serial.print("Flex: ");
      Serial.print(flex1); Serial.print(", ");
      Serial.println(flex2);
    } else if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("p1-p5: Read pressure sensors 1-5");
      Serial.println("v1-v4: Read vibration sensors 1-4");
      Serial.println("f1-f2: Read flex sensors 1-2");
      Serial.println("led on/off: Control LED");
      Serial.println("buzz: Beep buzzer");
      Serial.println("all: Read all sensors");
      Serial.println("help: Show this help");
    } else {
      Serial.println("Unknown command. Type 'help' for available commands.");
    }
  }
}
