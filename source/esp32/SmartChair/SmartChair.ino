// Smart Chair ESP32 Sketch with TensorFlow Lite Micro
// This sketch controls the smart chair with pressure, vibration motors, and flex sensors
// Includes Random Forest model for posture prediction using TFLite Micro
// Features: Calibration, User Profiles, Multi-class Posture Detection, Data Logging

#include <TensorFlowLite_ESP32.h>

#include <SPIFFS.h>  // For file storage
#include <EEPROM.h>  // For persistent storage

// Pin definitions for ESP32 38-pin board (GPIO numbers)
// Avoiding pins that conflict with WiFi, boot, or flash
#define PRESSURE_SENSOR_1 32  // ADC1, bidirectional
#define PRESSURE_SENSOR_2 33  // ADC1, bidirectional
#define PRESSURE_SENSOR_3 34  // ADC1, input only (ok for analog read)
#define PRESSURE_SENSOR_4 35  // ADC1, input only (ok for analog read)
#define PRESSURE_SENSOR_5 36  // ADC1, input only (ok for analog read)

#define VIBRATION_MOTOR_1 16  // Digital, safe
#define VIBRATION_MOTOR_2 17  // Digital, safe
#define VIBRATION_MOTOR_3 18  // Digital, safe
#define VIBRATION_MOTOR_4 19  // Digital, safe

#define FLEX_SENSOR_1 37  // ADC1, input only (ok for analog read)
#define FLEX_SENSOR_2 38  // ADC1, input only (ok for analog read)

#define LED_PIN 2   // Safe for output
#define BUZZER_PIN 4  // Safe for output

// Data structures
struct CalibrationData {
  int pressureBaseline[5];  // Baseline readings for good posture
  int flexBaseline[2];
  float pressureThreshold;  // Dynamic threshold based on user
  float flexThreshold;
};

struct UserProfile {
  char name[20];
  CalibrationData calib;
};

struct PostureLog {
  unsigned long timestamp;
  int postureClass;  // 0=good, 1=slouch, 2=lean_left, 3=lean_right, etc.
  int sensorData[7];  // Raw sensor readings: 5 pressure + 2 flex
};

// Constants
#define MAX_USERS 5
#define MAX_LOGS 100
#define EEPROM_SIZE 512

// Global variables
UserProfile users[MAX_USERS];
int currentUser = -1;  // No user selected initially
PostureLog logs[MAX_LOGS];
int logIndex = 0;

// TFLite globals
namespace {
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;

  // Arena size for TFLite (adjust based on model size)
  constexpr int kTensorArenaSize = 8 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
}

// Placeholder for Random Forest model (replace with actual trained model)
// This should be the .tflite file content as a byte array
// Generated from: python -c "import numpy as np; model_bytes = open('model.tflite', 'rb').read(); print(', '.join([str(b) for b in model_bytes]))"
const unsigned char model_tflite[] = {
  // PLACEHOLDER: Replace with actual model bytes
  // Example: 0x1C, 0x00, 0x00, 0x00, ... (full model data)
  0x00  // Minimal placeholder - replace with real model
};
const int model_tflite_len = 1;  // Replace with actual length

// Utility functions
void loadUserProfiles() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  
  File file = SPIFFS.open("/profiles.dat", "r");
  if (file) {
    file.read((uint8_t*)users, sizeof(users));
    file.close();
    Serial.println("User profiles loaded");
  } else {
    Serial.println("No user profiles found, using defaults");
  }
}

void saveUserProfiles() {
  File file = SPIFFS.open("/profiles.dat", "w");
  if (file) {
    file.write((uint8_t*)users, sizeof(users));
    file.close();
    Serial.println("User profiles saved");
  } else {
    Serial.println("Failed to save user profiles");
  }
}

void calibrateUser(int userIndex) {
  if (userIndex < 0 || userIndex >= MAX_USERS) return;
  
  Serial.println("Starting calibration... Sit in good posture and send 'calib done' when ready");
  
  // Wait for calibration command
  while (true) {
    if (Serial.available() > 0) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd == "calib done") break;
    }
    delay(100);
  }
  
  // Read baseline values
  users[userIndex].calib.pressureBaseline[0] = analogRead(PRESSURE_SENSOR_1);
  users[userIndex].calib.pressureBaseline[1] = analogRead(PRESSURE_SENSOR_2);
  users[userIndex].calib.pressureBaseline[2] = analogRead(PRESSURE_SENSOR_3);
  users[userIndex].calib.pressureBaseline[3] = analogRead(PRESSURE_SENSOR_4);
  users[userIndex].calib.pressureBaseline[4] = analogRead(PRESSURE_SENSOR_5);
  
  users[userIndex].calib.flexBaseline[0] = analogRead(FLEX_SENSOR_1);
  users[userIndex].calib.flexBaseline[1] = analogRead(FLEX_SENSOR_2);
  
  // Calculate dynamic thresholds (example: 20% deviation from baseline)
  users[userIndex].calib.pressureThreshold = 0.2;  // 20% tolerance
  users[userIndex].calib.flexThreshold = 0.3;      // 30% tolerance for flex
  
  Serial.println("Calibration complete");
  saveUserProfiles();
}

void logPostureData(int postureClass, int* sensorData) {
  logs[logIndex].timestamp = millis();
  logs[logIndex].postureClass = postureClass;
  memcpy(logs[logIndex].sensorData, sensorData, sizeof(int) * 7);
  logIndex = (logIndex + 1) % MAX_LOGS;
}

void exportLogs() {
  Serial.println("Posture Logs:");
  for (int i = 0; i < MAX_LOGS; i++) {
    if (logs[i].timestamp > 0) {
      Serial.print("Time: "); Serial.print(logs[i].timestamp);
      Serial.print(", Class: "); Serial.print(logs[i].postureClass);
      Serial.print(", Data: ");
      for (int j = 0; j < 7; j++) {
        Serial.print(logs[i].sensorData[j]);
        if (j < 6) Serial.print(",");
      }
      Serial.println();
    }
  }
}

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

  pinMode(VIBRATION_MOTOR_1, OUTPUT);
  pinMode(VIBRATION_MOTOR_2, OUTPUT);
  pinMode(VIBRATION_MOTOR_3, OUTPUT);
  pinMode(VIBRATION_MOTOR_4, OUTPUT);

  pinMode(FLEX_SENSOR_1, INPUT);
  pinMode(FLEX_SENSOR_2, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Load user profiles from SPIFFS
  loadUserProfiles();

  // Initialize TensorFlow Lite
  model = tflite::GetModel(model_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.print("Model version mismatch: ");
    Serial.println(model->version());
    return;
  }

  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddFullyConnected();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    Serial.println("AllocateTensors() failed");
    return;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.println("TFLite Model Loaded Successfully");

  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.println("TFLite Model Loaded Successfully");

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
    } else if (command == "f2") {
      int flex2 = analogRead(FLEX_SENSOR_2);
      Serial.print("Flex Sensor 2: ");
      Serial.println(flex2);
    } else if (command == "led on") {
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

      int flex1 = analogRead(FLEX_SENSOR_1);
      int flex2 = analogRead(FLEX_SENSOR_2);

      Serial.println("All Sensor Readings:");
      Serial.print("Pressure: ");
      Serial.print(pressure1); Serial.print(", ");
      Serial.print(pressure2); Serial.print(", ");
      Serial.print(pressure3); Serial.print(", ");
      Serial.print(pressure4); Serial.print(", ");
      Serial.println(pressure5);

      Serial.print("Flex: ");
      Serial.print(flex1); Serial.print(", ");
      Serial.println(flex2);
    } else if (command == "predict") {
      // Read all sensors for ML prediction
      int pressure1 = analogRead(PRESSURE_SENSOR_1);
      int pressure2 = analogRead(PRESSURE_SENSOR_2);
      int pressure3 = analogRead(PRESSURE_SENSOR_3);
      int pressure4 = analogRead(PRESSURE_SENSOR_4);
      int pressure5 = analogRead(PRESSURE_SENSOR_5);

      int flex1 = analogRead(FLEX_SENSOR_1);
      int flex2 = analogRead(FLEX_SENSOR_2);

      // Prepare input tensor (7 features: 5 pressure + 2 flex)
      input->data.f[0] = pressure1 / 4095.0f;  // Normalize to 0-1
      input->data.f[1] = pressure2 / 4095.0f;
      input->data.f[2] = pressure3 / 4095.0f;
      input->data.f[3] = pressure4 / 4095.0f;
      input->data.f[4] = pressure5 / 4095.0f;
      input->data.f[5] = flex1 / 4095.0f;
      input->data.f[6] = flex2 / 4095.0f;

      // Run inference
      TfLiteStatus invoke_status = interpreter->Invoke();
      if (invoke_status != kTfLiteOk) {
        Serial.println("Prediction failed");
        return;
      }

      // Get output (multi-class classification)
      // Assume output tensor has multiple values, find the class with highest probability
      int numClasses = output->dims->data[1];  // Assuming shape [1, num_classes]
      int predictedClass = 0;
      float maxProb = output->data.f[0];
      
      for (int i = 1; i < numClasses; i++) {
        if (output->data.f[i] > maxProb) {
          maxProb = output->data.f[i];
          predictedClass = i;
        }
      }

      // Log the data
      int sensorData[7] = {pressure1, pressure2, pressure3, pressure4, pressure5, 
                           flex1, flex2};
      logPostureData(predictedClass, sensorData);

      Serial.print("ML Prediction - Class: ");
      Serial.print(predictedClass);
      Serial.print(" (Probability: ");
      Serial.print(maxProb);
      Serial.println(") - Logged");
    } else if (command.startsWith("user ")) {
      int userId = command.substring(5).toInt();
      if (userId >= 0 && userId < MAX_USERS) {
        currentUser = userId;
        Serial.print("Selected user: ");
        Serial.println(currentUser);
      } else {
        Serial.println("Invalid user ID (0-4)");
      }
    } else if (command == "calibrate") {
      if (currentUser >= 0) {
        calibrateUser(currentUser);
      } else {
        Serial.println("Select a user first with 'user X'");
      }
    } else if (command == "logs") {
      exportLogs();
    } else if (command == "vib1 on") {
      digitalWrite(VIBRATION_MOTOR_1, HIGH);
      Serial.println("Vibration Motor 1: ON");
    } else if (command == "vib1 off") {
      digitalWrite(VIBRATION_MOTOR_1, LOW);
      Serial.println("Vibration Motor 1: OFF");
    } else if (command == "vib2 on") {
      digitalWrite(VIBRATION_MOTOR_2, HIGH);
      Serial.println("Vibration Motor 2: ON");
    } else if (command == "vib2 off") {
      digitalWrite(VIBRATION_MOTOR_2, LOW);
      Serial.println("Vibration Motor 2: OFF");
    } else if (command == "vib3 on") {
      digitalWrite(VIBRATION_MOTOR_3, HIGH);
      Serial.println("Vibration Motor 3: ON");
    } else if (command == "vib3 off") {
      digitalWrite(VIBRATION_MOTOR_3, LOW);
      Serial.println("Vibration Motor 3: OFF");
    } else if (command == "vib4 on") {
      digitalWrite(VIBRATION_MOTOR_4, HIGH);
      Serial.println("Vibration Motor 4: ON");
    } else if (command == "vib4 off") {
      digitalWrite(VIBRATION_MOTOR_4, LOW);
      Serial.println("Vibration Motor 4: OFF");
    } else if (command == "vib all on") {
      digitalWrite(VIBRATION_MOTOR_1, HIGH);
      digitalWrite(VIBRATION_MOTOR_2, HIGH);
      digitalWrite(VIBRATION_MOTOR_3, HIGH);
      digitalWrite(VIBRATION_MOTOR_4, HIGH);
      Serial.println("All Vibration Motors: ON");
    } else if (command == "vib all off") {
      digitalWrite(VIBRATION_MOTOR_1, LOW);
      digitalWrite(VIBRATION_MOTOR_2, LOW);
      digitalWrite(VIBRATION_MOTOR_3, LOW);
      digitalWrite(VIBRATION_MOTOR_4, LOW);
      Serial.println("All Vibration Motors: OFF");
    } else if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("p1-p5: Read pressure sensors 1-5");
      Serial.println("f1-f2: Read flex sensors 1-2");
      Serial.println("vib1 on/off: Control vibration motor 1");
      Serial.println("vib2 on/off: Control vibration motor 2");
      Serial.println("vib3 on/off: Control vibration motor 3");
      Serial.println("vib4 on/off: Control vibration motor 4");
      Serial.println("vib all on/off: Control all vibration motors");
      Serial.println("led on/off: Control LED");
      Serial.println("buzz: Beep buzzer");
      Serial.println("all: Read all sensors");
      Serial.println("predict: Run ML posture prediction");
      Serial.println("user X: Select user profile (0-4)");
      Serial.println("calibrate: Calibrate current user");
      Serial.println("logs: Export posture logs");
      Serial.println("help: Show this help");
    } else {
      Serial.println("Unknown command. Type 'help' for available commands.");
    }
  }
}
