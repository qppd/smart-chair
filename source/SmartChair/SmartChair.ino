// Smart Chair Arduino Sketch
// This sketch controls the smart chair with pressure, vibration, and flex sensors

// Pin definitions (adjust based on wiring)
#define PRESSURE_SENSOR_1 A0
#define PRESSURE_SENSOR_2 A1
#define PRESSURE_SENSOR_3 A2
#define PRESSURE_SENSOR_4 A3
#define PRESSURE_SENSOR_5 A4

#define VIBRATION_SENSOR_1 2
#define VIBRATION_SENSOR_2 3
#define VIBRATION_SENSOR_3 4
#define VIBRATION_SENSOR_4 5

#define FLEX_SENSOR_1 A5
#define FLEX_SENSOR_2 A6

#define LED_PIN 13
#define BUZZER_PIN 6

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
  // Read pressure sensors
  int pressure1 = analogRead(PRESSURE_SENSOR_1);
  int pressure2 = analogRead(PRESSURE_SENSOR_2);
  int pressure3 = analogRead(PRESSURE_SENSOR_3);
  int pressure4 = analogRead(PRESSURE_SENSOR_4);
  int pressure5 = analogRead(PRESSURE_SENSOR_5);

  // Read vibration sensors
  int vibration1 = digitalRead(VIBRATION_SENSOR_1);
  int vibration2 = digitalRead(VIBRATION_SENSOR_2);
  int vibration3 = digitalRead(VIBRATION_SENSOR_3);
  int vibration4 = digitalRead(VIBRATION_SENSOR_4);

  // Read flex sensors
  int flex1 = analogRead(FLEX_SENSOR_1);
  int flex2 = analogRead(FLEX_SENSOR_2);

  // Print sensor readings to serial
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

  // Simple logic: if any pressure sensor detects weight, turn on LED
  if (pressure1 > 100 || pressure2 > 100 || pressure3 > 100 || pressure4 > 100 || pressure5 > 100) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // If vibration detected, beep buzzer
  if (vibration1 || vibration2 || vibration3 || vibration4) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
  }

  delay(1000); // Wait 1 second before next reading
}
