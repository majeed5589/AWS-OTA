#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>

// Device Configuration (CHANGE THIS FOR EACH DEVICE)
#define DEVICE_ID "esp32_004"  // Make unique: esp32_002, esp32_003, etc.

// Global MQTT topics for OTA notifications and confirmations
#define OTA_NOTIFICATION_TOPIC "all_devices/ota_notification"  // Topic for all devices to receive OTA notifications
#define OTA_CONFIRMATION_TOPIC "device_updates/confirmation"   // Topic for devices to confirm update request

// Device-specific topics
#define AWS_IOT_PUBLISH_TOPIC   DEVICE_ID "/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC DEVICE_ID "/sub"

// OTA Update Status
bool updateInProgress = false;
bool updateAvailable = false;
String pendingUpdateUrl = "";
String pendingUpdateVersion = "";

// Simulated sensor data
float h;
float t;

// Create clients
WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);
WiFiClientSecure* httpClient = NULL;

// Function declarations
void connectAWS();
void messageHandler(char* topic, byte* payload, unsigned int length);
void publishMessage();
void checkSerialInput();
void confirmOtaUpdate();
void startOTAUpdate(const char* firmwareUrl);
void publishUpdateStatus(const char* status, const char* message);

void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Wi-Fi connected successfully!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Set NTP time (required for SSL validation)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current UTC time: ");
  Serial.println(asctime(&timeinfo));

  // Configure AWS IoT connection
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.setServer(AWS_IOT_ENDPOINT, 8883);
  client.setCallback(messageHandler);

  Serial.println("Connecting to AWS IoT");
  
  // Important: Use DEVICE_ID as client ID
  while (!client.connect(DEVICE_ID)) {
    Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }

  // Subscribe to device-specific topics
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
  
  // Subscribe to the global OTA notification topic
  if (client.subscribe(OTA_NOTIFICATION_TOPIC)) {
    Serial.println("Subscribed to OTA notification topic");
  } else {
    Serial.println("Failed to subscribe to OTA notification topic");
  }
  
  Serial.println("AWS IoT Connected!");
  
  // Initialize HTTP client for OTA updates
  if (httpClient == NULL) {
    httpClient = new WiFiClientSecure;
    if (httpClient != NULL) {
      httpClient->setCACert(AWS_CERT_CA);
    }
  }
}

void publishMessage() {
  StaticJsonDocument<200> doc;
  doc["device_id"] = DEVICE_ID;
  doc["humidity"] = h;
  doc["temperature"] = t;
  doc["firmware_version"] = FIRMWARE_VERSION;  // Include firmware version
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}

void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received message on topic: ");
  Serial.println(topic);
  
  // Print payload for debugging
  Serial.print("Payload: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Convert topic to string for easier comparison
  String topicStr = String(topic);
  
  // Parse the JSON payload
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Handle OTA notification on the global topic
  if (topicStr.equals(OTA_NOTIFICATION_TOPIC)) {
    Serial.println("Received OTA notification!");
    
    // Check if it's an update notification
    if (doc.containsKey("action") && strcmp(doc["action"].as<const char*>(), "update_available") == 0) {
      // Store the update URL and version for later
      if (doc.containsKey("firmwareUrl") && doc.containsKey("firmwareVersion")) {
        pendingUpdateUrl = doc["firmwareUrl"].as<String>();
        pendingUpdateVersion = doc["firmwareVersion"].as<String>();
        updateAvailable = true;
        
        // Display notification to user
        Serial.println("\n========================================");
        Serial.println("   FIRMWARE UPDATE AVAILABLE");
        Serial.println("========================================");
        Serial.print("Current version: ");
        Serial.println(FIRMWARE_VERSION);
        Serial.print("New version: ");
        Serial.println(pendingUpdateVersion);
        Serial.println("Type 'download' to start the update process");
        Serial.println("========================================\n");
      }
    }
  }
  // Handle device-specific commands
  else if (topicStr.equals(AWS_IOT_SUBSCRIBE_TOPIC)) {
    if (doc.containsKey("command")) {
      const char* command = doc["command"].as<const char*>();
      Serial.print("Device-specific command: ");
      Serial.println(command);
    }
  }
}

void checkSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    // Check for download command
    if (input.equalsIgnoreCase("download")) {
      if (updateAvailable) {
        Serial.println("User confirmed firmware download. Sending confirmation...");
        confirmOtaUpdate();
      } else {
        Serial.println("No firmware update is currently available.");
      }
    }
  }
}

void confirmOtaUpdate() {
  if (!updateAvailable || pendingUpdateUrl.length() == 0) {
    Serial.println("No valid update is available to confirm");
    return;
  }
  
  // Create confirmation message
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["action"] = "confirm_update";
  doc["current_version"] = FIRMWARE_VERSION;
  doc["target_version"] = pendingUpdateVersion;
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  // Publish confirmation
  if (client.publish(OTA_CONFIRMATION_TOPIC, jsonBuffer)) {
    Serial.println("Published update confirmation");
    // Begin update process immediately
    startOTAUpdate(pendingUpdateUrl.c_str());
  } else {
    Serial.println("Failed to publish update confirmation");
  }
}

void setup() {
  Serial.begin(115200);
  
  // Wait a moment for serial to connect
  delay(2000);
  
  Serial.println("\n=== ESP32 AWS IoT with Interactive OTA Updates ===");
  Serial.print("Current firmware version: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("Device ID: " + String(DEVICE_ID));
  
  connectAWS();
  randomSeed(analogRead(0));
  
  // Send a startup message
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["status"] = "online";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["message"] = "Device started";
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  String statusTopic = DEVICE_ID;
  statusTopic += "/status";
  
  client.publish(statusTopic.c_str(), jsonBuffer);
  Serial.println("Published startup message");
}

void loop() {
  // Maintain MQTT connection
  if (!client.connected()) {
    connectAWS();
  }
  client.loop();
  
  // Check for user input on Serial
  checkSerialInput();
  
  // Generate fake sensor data
  h = random(200, 801) / 10.0;  // 20.0-80.0%
  t = random(0, 501) / 10.0;    // 0.0-50.0°C

  // Print info to serial
  Serial.print("Device: ");
  Serial.print(DEVICE_ID);
  Serial.print(" | Humidity: ");
  Serial.print(h);
  Serial.print("% | Temperature: ");
  Serial.print(t);
  Serial.println("°C");

  // Publish data to AWS IoT
  publishMessage();

  // Wait before next reading
  delay(3000);
}

/**
 * Publish update status to AWS IoT
 */
void publishUpdateStatus(const char* status, const char* message) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["status"] = status;
  doc["message"] = message;
  doc["timestamp"] = millis();
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  String updateStatusTopic = DEVICE_ID;
  updateStatusTopic += "/update_status";
  
  client.publish(updateStatusTopic.c_str(), jsonBuffer);
}

/**
 * Start OTA update process
 * Downloads and installs new firmware from the provided URL
 */
void startOTAUpdate(const char* firmwareUrl) {
  // Set the update flag
  updateInProgress = true;
  
  // Send status update
  publishUpdateStatus("started", "Beginning OTA update");
  
  Serial.print("Starting OTA update from: ");
  Serial.println(firmwareUrl);
  
  // Check if HTTP client is ready
  if (httpClient == NULL) {
    Serial.println("ERROR: HTTP client not initialized!");
    publishUpdateStatus("error", "HTTP client initialization failed");
    updateInProgress = false;
    return;
  }
  
  // Set up progress callback
  Update.onProgress([](size_t progress, size_t total) {
    static int lastPercent = 0;
    int percent = (progress * 100) / total;
    
    // Only report when percentage changes by 5%
    if (percent % 5 == 0 && percent != lastPercent) {
      lastPercent = percent;
      Serial.printf("Update progress: %d%%\n", percent);
      
      char progressMsg[32];
      sprintf(progressMsg, "Update progress: %d%%", percent);
      publishUpdateStatus("progress", progressMsg);
    }
  });
  
  // Start the HTTP update
  HTTPClient http;
  http.begin(*httpClient, firmwareUrl);
  
  int httpCode = http.GET();
  if (httpCode <= 0) {
    Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
    publishUpdateStatus("error", http.errorToString(httpCode).c_str());
    updateInProgress = false;
    updateAvailable = false;
    pendingUpdateUrl = "";
    pendingUpdateVersion = "";
    http.end();
    return;
  }
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error code: %d\n", httpCode);
    publishUpdateStatus("error", String("HTTP error: " + String(httpCode)).c_str());
    updateInProgress = false;
    updateAvailable = false;
    pendingUpdateUrl = "";
    pendingUpdateVersion = "";
    http.end();
    return;
  }
  
  // Get content length
  int contentLength = http.getSize();
  Serial.printf("Content-Length: %d bytes\n", contentLength);
  
  // Check if there's enough space
  if (contentLength <= 0) {
    Serial.println("Invalid content length");
    publishUpdateStatus("error", "Invalid content length");
    updateInProgress = false;
    updateAvailable = false;
    pendingUpdateUrl = "";
    pendingUpdateVersion = "";
    http.end();
    return;
  }
  
  if (!Update.begin(contentLength)) {
    Serial.printf("Not enough space for update: %d bytes needed\n", contentLength);
    publishUpdateStatus("error", "Not enough space for update");
    updateInProgress = false;
    updateAvailable = false;
    pendingUpdateUrl = "";
    pendingUpdateVersion = "";
    http.end();
    return;
  }
  
  // Download and write firmware
  Serial.println("Starting download and update...");
  
  // Create buffer for content
  uint8_t buff[1024] = { 0 };
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  
  // Download and write
  while (http.connected() && (written < contentLength)) {
    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
      Update.write(buff, c);
      written += c;
    }
    delay(1);
  }
  
  Serial.printf("Download complete: %d bytes written\n", written);
  
  // Finish update
  if (Update.end(true)) {
    Serial.println("Update successfully completed. Rebooting...");
    publishUpdateStatus("success", "Update complete, rebooting");
    delay(1000);
    ESP.restart();
  } else {
    Serial.printf("Update failed with error: %d\n", Update.getError());
    char errorMsg[64];
    sprintf(errorMsg, "Update failed with error code: %d", Update.getError());
    publishUpdateStatus("error", errorMsg);
    updateInProgress = false;
    updateAvailable = false;
    pendingUpdateUrl = "";
    pendingUpdateVersion = "";
  }
  
  http.end();
}