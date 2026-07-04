/*
  Pembagian FreeRTOS:

  Core 1:
  - SensorTask : membaca MPU6050
  - LogicTask  : mendeteksi warning, jatuh, dan risiko kecelakaan
  - OutputTask : mengatur LED, buzzer, dan tombol reset

  Core 0:
  - NetworkTask: WiFi, MQTT, telemetry, command, dan alert
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <Preferences.h> // Tambahan Library Preferences

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// =====================================================
// KONFIGURASI NVS & JARINGAN
// =====================================================

Preferences preferences;
String wifiSsid;
String wifiPassword;

const char* MQTT_HOST = "xxxxxxxx.cloud.shiftr.io";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "xxxxxxxx";
const char* MQTT_PASS = "xxxxxxxxx";

const char* DEVICE_ID = "SHM-001";

// =====================================================
// PIN
// =====================================================

constexpr uint8_t PIN_BUZZER = 25;
constexpr uint8_t PIN_LED_GREEN = 18;
constexpr uint8_t PIN_LED_RED = 19;
constexpr uint8_t PIN_LED_YELLOW = 23;
constexpr uint8_t PIN_RESET = 26;

constexpr uint8_t MPU_SDA = 21;
constexpr uint8_t MPU_SCL = 22;

// =====================================================
// PARAMETER DETEKSI
// =====================================================

constexpr float WARNING_TILT_DEG = 55.0f;
constexpr float FALL_TILT_DEG = 70.0f;

constexpr float IMPACT_THRESHOLD_G = 2.5f;
constexpr float HARD_IMPACT_G = 3.5f;

constexpr float STILL_ACCEL_DELTA_G = 0.12f;
constexpr float STILL_GYRO_DPS = 12.0f;

constexpr uint32_t STILL_TIME_MS = 4000;
constexpr uint32_t SEND_INTERVAL = 500;
constexpr uint32_t HTTP_INTERVAL = 5000;
constexpr uint32_t WIFI_RETRY_MS = 5000;
constexpr uint32_t MQTT_RETRY_MS = 3000;

// =====================================================
// OBJEK
// =====================================================

MPU6050 mpu(Wire);

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String topicTelemetry;
String topicStatus;
String topicAlert;
String topicCommand;

// =====================================================
// STATUS SISTEM
// =====================================================

enum HelmetStatus {
  NORMAL,
  WARNING,
  FALL_DETECTED,
  ACCIDENT_RISK
};

struct SensorData {
  float axG;
  float ayG;
  float azG;

  float gx;
  float gy;
  float gz;

  float angleX;
  float angleY;
  float angleZ;

  float accelerationMagnitude;
  float gyroMagnitude;
  float tiltAngle;

  uint32_t updatedAt;
};

struct SystemState {
  HelmetStatus status;

  bool automaticMode;
  bool alarmLatched;
  bool impactPending;

  uint32_t impactAt;
};

struct AlertEvent {
  char name[48];
};

// =====================================================
// DATA BERSAMA ANTARTASK
// =====================================================

SensorData sensorData = {
  0.0f, 0.0f, 1.0f,
  0.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 0.0f,
  1.0f, 0.0f, 0.0f,
  0
};

SystemState systemState = {
  NORMAL,
  true,
  false,
  false,
  0
};

SemaphoreHandle_t sensorMutex;
SemaphoreHandle_t stateMutex;

QueueHandle_t alertQueue;

// =====================================================
// TIMER NETWORK
// =====================================================

uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastTelemetryAt = 0;

// =====================================================
// HELPER
// =====================================================

const char* statusText(HelmetStatus status) {
  switch (status) {
    case NORMAL: return "NORMAL";
    case WARNING: return "WARNING";
    case FALL_DETECTED: return "FALL_DETECTED";
    case ACCIDENT_RISK: return "ACCIDENT_RISK";
    default: return "UNKNOWN";
  }
}

bool copySensorData(SensorData& destination) {
  if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  destination = sensorData;
  xSemaphoreGive(sensorMutex);
  return true;
}

bool copySystemState(SystemState& destination) {
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  destination = systemState;
  xSemaphoreGive(stateMutex);
  return true;
}

void queueAlert(const char* eventName) {
  if (alertQueue == nullptr || eventName == nullptr) return;
  AlertEvent event{};
  strncpy(event.name, eventName, sizeof(event.name) - 1);
  event.name[sizeof(event.name) - 1] = '\0';

  if (xQueueSend(alertQueue, &event, 0) != pdTRUE) {
    Serial.printf("Queue alert penuh, event dilewati: %s\n", eventName);
  }
}

void resetAlarm(bool sendEvent = true) {
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    systemState.alarmLatched = false;
    systemState.impactPending = false;
    systemState.impactAt = 0;
    systemState.status = NORMAL;
    xSemaphoreGive(stateMutex);
  }
  if (sendEvent) {
    queueAlert("ALARM_RESET");
  }
}

// =====================================================
// MQTT CALLBACK
// =====================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int index = 0; index < length; index++) {
    message += static_cast<char>(payload[index]);
  }

  StaticJsonDocument<256> document;
  DeserializationError error = deserializeJson(document, message);
  String command = (!error) ? String(document["command"] | "") : message;
  
  command.trim();
  command.toUpperCase();

  Serial.printf("Command MQTT: %s\n", command.c_str());

  if (command == "RESET_ALARM") { resetAlarm(true); return; }

  if (command == "MODE_AUTO") {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      systemState.automaticMode = true;
      systemState.alarmLatched = false;
      systemState.impactPending = false;
      systemState.status = NORMAL;
      xSemaphoreGive(stateMutex);
    }
    queueAlert("MODE_AUTO"); return;
  }

  if (command == "MODE_MANUAL") {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      systemState.automaticMode = false;
      xSemaphoreGive(stateMutex);
    }
    queueAlert("MODE_MANUAL"); return;
  }

  if (command == "ALARM_ON") {
    bool manualMode = false;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      manualMode = !systemState.automaticMode;
      if (manualMode) {
        systemState.alarmLatched = true;
        systemState.status = ACCIDENT_RISK;
      }
      xSemaphoreGive(stateMutex);
    }
    if (manualMode) queueAlert("MANUAL_ALARM_ON");
    return;
  }

  if (command == "ALARM_OFF") {
    bool manualMode = false;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      manualMode = !systemState.automaticMode;
      xSemaphoreGive(stateMutex);
    }
    if (manualMode) resetAlarm(true);
    return;
  }

  queueAlert("UNKNOWN_COMMAND");
}

// =====================================================
// KONEKSI WIFI
// =====================================================

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  const uint32_t now = millis();
  if (now - lastWifiAttempt < WIFI_RETRY_MS) return;
  lastWifiAttempt = now;

  Serial.printf("Menghubungkan WiFi ke SSID: %s\n", wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Menggunakan SSID & Password dari NVS Preferences
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
}

// =====================================================
// KONEKSI MQTT
// =====================================================

void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  const uint32_t now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = now;

  String clientId = "ESP32-" + String(DEVICE_ID) + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  Serial.printf("Menghubungkan MQTT sebagai %s...\n", clientId.c_str());

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("MQTT terhubung.");
    mqtt.subscribe(topicCommand.c_str());
    queueAlert("DEVICE_CONNECTED");
  } else {
    Serial.printf("MQTT gagal, rc=%d\n", mqtt.state());
  }
}

// =====================================================
// PUBLISH ALERT
// =====================================================

void publishAlertNow(const char* eventName) {
  if (!mqtt.connected() || eventName == nullptr) return;

  SystemState stateSnapshot;
  if (!copySystemState(stateSnapshot)) return;

  StaticJsonDocument<256> document;
  document["device_id"] = DEVICE_ID;
  document["event"] = eventName;
  document["status"] = statusText(stateSnapshot.status);
  document["timestamp_ms"] = millis();

  char payload[256];
  if (serializeJson(document, payload, sizeof(payload)) > 0) {
    mqtt.publish(topicAlert.c_str(), payload, true);
  }
}

void processAlertQueue() {
  if (!mqtt.connected()) return;
  AlertEvent event{};
  while (xQueueReceive(alertQueue, &event, 0) == pdTRUE) {
    publishAlertNow(event.name);
  }
}

// =====================================================
// PUBLISH TELEMETRY
// =====================================================

void publishTelemetry() {
  if (!mqtt.connected()) return;
  SensorData sensorSnapshot;
  SystemState stateSnapshot;

  if (!copySensorData(sensorSnapshot) || !copySystemState(stateSnapshot)) return;

  StaticJsonDocument<768> document;
  document["device_id"] = DEVICE_ID;
  document["status"] = statusText(stateSnapshot.status);
  document["mode"] = stateSnapshot.automaticMode ? "AUTO" : "MANUAL";
  document["alarm"] = stateSnapshot.alarmLatched;
  document["tilt"] = roundf(sensorSnapshot.tiltAngle * 100.0f) / 100.0f;
  document["acceleration_g"] = roundf(sensorSnapshot.accelerationMagnitude * 100.0f) / 100.0f;

  JsonObject accelerometer = document.createNestedObject("accelerometer");
  accelerometer["x"] = roundf(sensorSnapshot.axG * 9.80665f * 100.0f) / 100.0f;
  accelerometer["y"] = roundf(sensorSnapshot.ayG * 9.80665f * 100.0f) / 100.0f;
  accelerometer["z"] = roundf(sensorSnapshot.azG * 9.80665f * 100.0f) / 100.0f;

  JsonObject accelerometerG = document.createNestedObject("accelerometer_g");
  accelerometerG["x"] = roundf(sensorSnapshot.axG * 100.0f) / 100.0f;
  accelerometerG["y"] = roundf(sensorSnapshot.ayG * 100.0f) / 100.0f;
  accelerometerG["z"] = roundf(sensorSnapshot.azG * 100.0f) / 100.0f;

  JsonObject gyroscope = document.createNestedObject("gyroscope");
  gyroscope["x"] = roundf(sensorSnapshot.gx * 100.0f) / 100.0f;
  gyroscope["y"] = roundf(sensorSnapshot.gy * 100.0f) / 100.0f;
  gyroscope["z"] = roundf(sensorSnapshot.gz * 100.0f) / 100.0f;

  JsonObject angles = document.createNestedObject("angles");
  angles["x"] = roundf(sensorSnapshot.angleX * 100.0f) / 100.0f;
  angles["y"] = roundf(sensorSnapshot.angleY * 100.0f) / 100.0f;
  angles["z"] = roundf(sensorSnapshot.angleZ * 100.0f) / 100.0f;

  document["rssi"] = WiFi.RSSI();
  document["uptime_ms"] = millis();

  char payload[768];
  if (serializeJson(document, payload, sizeof(payload)) > 0) {
    mqtt.publish(topicTelemetry.c_str(), payload, false);
    mqtt.publish(topicStatus.c_str(), statusText(stateSnapshot.status), true);
  }
}

// =====================================================
// TASK 1 — SENSOR MPU6050
// =====================================================

void SensorTask(void* parameter) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(10);

  while (true) {
    mpu.update();
    SensorData newData{};
    newData.axG = mpu.getAccX();
    newData.ayG = mpu.getAccY();
    newData.azG = mpu.getAccZ();
    newData.gx = mpu.getGyroX();
    newData.gy = mpu.getGyroY();
    newData.gz = mpu.getGyroZ();
    newData.angleX = mpu.getAngleX();
    newData.angleY = mpu.getAngleY();
    newData.angleZ = mpu.getAngleZ();

    newData.accelerationMagnitude = sqrtf(newData.axG * newData.axG + newData.ayG * newData.ayG + newData.azG * newData.azG);
    newData.gyroMagnitude = sqrtf(newData.gx * newData.gx + newData.gy * newData.gy + newData.gz * newData.gz);
    newData.tiltAngle = max(fabsf(newData.angleX), fabsf(newData.angleY));
    newData.updatedAt = millis();

    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sensorData = newData;
      xSemaphoreGive(sensorMutex);
    }
    vTaskDelayUntil(&lastWakeTime, interval);
  }
}

// =====================================================
// TASK 2 — LOGIKA DETEKSI
// =====================================================

void LogicTask(void* parameter) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(20);

  while (true) {
    SensorData sensorSnapshot;
    if (!copySensorData(sensorSnapshot)) {
      vTaskDelayUntil(&lastWakeTime, interval);
      continue;
    }

    HelmetStatus previousStatus = NORMAL, newStatus = NORMAL;
    bool statusChanged = false, accidentDetected = false;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      previousStatus = systemState.status;

      if (systemState.automaticMode) {
        const bool hardImpact = sensorSnapshot.accelerationMagnitude >= HARD_IMPACT_G;
        const bool impact = sensorSnapshot.accelerationMagnitude >= IMPACT_THRESHOLD_G;
        const bool extremeTilt = sensorSnapshot.tiltAngle >= FALL_TILT_DEG;
        const bool warningTilt = sensorSnapshot.tiltAngle >= WARNING_TILT_DEG;

        if (hardImpact || (impact && extremeTilt)) {
          systemState.impactPending = true;
          systemState.impactAt = millis();
          systemState.status = FALL_DETECTED;
          systemState.alarmLatched = true;
        } else if (impact || extremeTilt) {
          systemState.status = FALL_DETECTED;
          systemState.alarmLatched = true;
        } else if (warningTilt && !systemState.alarmLatched) {
          systemState.status = WARNING;
        } else if (!systemState.alarmLatched) {
          systemState.status = NORMAL;
        }

        if (systemState.impactPending && millis() - systemState.impactAt >= STILL_TIME_MS) {
          const bool accelerationStill = fabsf(sensorSnapshot.accelerationMagnitude - 1.0f) <= STILL_ACCEL_DELTA_G;
          const bool gyroStill = sensorSnapshot.gyroMagnitude <= STILL_GYRO_DPS;
          
          if (accelerationStill && gyroStill) {
            systemState.status = ACCIDENT_RISK;
            systemState.alarmLatched = true;
            accidentDetected = true;
          }
          systemState.impactPending = false;
        }
      }

      newStatus = systemState.status;
      statusChanged = newStatus != previousStatus;
      xSemaphoreGive(stateMutex);
    }

    if (accidentDetected) queueAlert("ACCIDENT_RISK_DETECTED");
    else if (statusChanged) {
      switch (newStatus) {
        case NORMAL: queueAlert("STATUS_NORMAL"); break;
        case WARNING: queueAlert("WARNING_DETECTED"); break;
        case FALL_DETECTED: queueAlert("FALL_DETECTED"); break;
        case ACCIDENT_RISK: queueAlert("ACCIDENT_RISK_DETECTED"); break;
      }
    }
    vTaskDelayUntil(&lastWakeTime, interval);
  }
}

// =====================================================
// TASK 3 — LED, BUZZER, BUTTON
// =====================================================

void OutputTask(void* parameter) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(25);
  uint32_t lastButtonAt = 0;

  while (true) {
    SystemState stateSnapshot;
    if (copySystemState(stateSnapshot)) {
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_YELLOW, LOW);
      digitalWrite(PIN_LED_RED, LOW);

      if (stateSnapshot.alarmLatched || stateSnapshot.status == FALL_DETECTED || stateSnapshot.status == ACCIDENT_RISK) {
        digitalWrite(PIN_LED_RED, HIGH);
        tone(PIN_BUZZER, 2500);
      } else if (stateSnapshot.status == WARNING) {
        digitalWrite(PIN_LED_YELLOW, HIGH);
        if ((millis() / 500) % 2 == 0) tone(PIN_BUZZER, 1500);
        else noTone(PIN_BUZZER);
      } else {
        digitalWrite(PIN_LED_GREEN, HIGH);
        noTone(PIN_BUZZER);
      }
    }

    if (digitalRead(PIN_RESET) == LOW && millis() - lastButtonAt > 500) {
      lastButtonAt = millis();
      resetAlarm(true);
    }
    vTaskDelayUntil(&lastWakeTime, interval);
  }
}

// =====================================================
// TASK 4 — WIFI, MQTT, HTTP
// =====================================================

void NetworkTask(void* parameter) {
  while (true) {
    maintainWiFi();
    if (WiFi.status() == WL_CONNECTED) maintainMQTT();
    if (mqtt.connected()) {
      mqtt.loop();
      processAlertQueue();
    }
    const uint32_t now = millis();
    if (now - lastTelemetryAt >= SEND_INTERVAL) {
      lastTelemetryAt = now;
      publishTelemetry();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  // --- INISIALISASI PREFERENCES ---
  preferences.begin("wifi_config", false); 
  
  // Ambil SSID dan Password dari memori ESP32, berikan nilai default "Munyenyo" dan "jangkrik" jika tidak ditemukan
  wifiSsid = preferences.getString("ssid", "Munyenyo");
  wifiPassword = preferences.getString("password", "jangkrik");

  Serial.println("Memuat konfigurasi WiFi NVS...");
  Serial.printf("Target SSID: %s\n", wifiSsid.c_str());
  // --------------------------------

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_RESET, INPUT_PULLUP);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  noTone(PIN_BUZZER);

  sensorMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();
  alertQueue = xQueueCreate(12, sizeof(AlertEvent));

  if (sensorMutex == nullptr || stateMutex == nullptr || alertQueue == nullptr) {
    Serial.println("Gagal membuat resource FreeRTOS.");
    while (true) {
      digitalWrite(PIN_LED_RED, !digitalRead(PIN_LED_RED));
      delay(300);
    }
  }

  Wire.begin(MPU_SDA, MPU_SCL);
  byte mpuStatus = mpu.begin();

  if (mpuStatus != 0) {
    Serial.println("MPU6050 tidak ditemukan. Periksa wiring.");
    while (true) {
      digitalWrite(PIN_LED_RED, !digitalRead(PIN_LED_RED));
      delay(300);
    }
  }

  Serial.println("Jangan gerakkan helm. Kalibrasi MPU6050...");
  delay(1000);
  mpu.calcOffsets(true, true);
  Serial.println("Kalibrasi MPU6050 selesai.");

  topicTelemetry = "smarthelmet/" + String(DEVICE_ID) + "/telemetry";
  topicStatus = "smarthelmet/" + String(DEVICE_ID) + "/status";
  topicAlert = "smarthelmet/" + String(DEVICE_ID) + "/alert";
  topicCommand = "smarthelmet/" + String(DEVICE_ID) + "/command";

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(3);

  WiFi.mode(WIFI_STA);

  Serial.println("Membuat task FreeRTOS...");

  BaseType_t sensorTaskResult = xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, nullptr, 4, nullptr, 1);
  BaseType_t logicTaskResult = xTaskCreatePinnedToCore(LogicTask, "LogicTask", 4096, nullptr, 3, nullptr, 1);
  BaseType_t outputTaskResult = xTaskCreatePinnedToCore(OutputTask, "OutputTask", 3072, nullptr, 2, nullptr, 1);
  BaseType_t networkTaskResult = xTaskCreatePinnedToCore(NetworkTask, "NetworkTask", 8192, nullptr, 2, nullptr, 0);

  if (sensorTaskResult != pdPASS || logicTaskResult != pdPASS || outputTaskResult != pdPASS || networkTaskResult != pdPASS) {
    Serial.println("Salah satu task FreeRTOS gagal dibuat.");
    while (true) {
      digitalWrite(PIN_LED_RED, !digitalRead(PIN_LED_RED));
      delay(300);
    }
  }

  Serial.println("Smart Helmet FreeRTOS siap.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}