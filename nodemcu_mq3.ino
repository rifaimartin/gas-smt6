#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

const char* ssid = "ramadhan";
const char* password = "bogorbersatu123@";

// Backend pada Railways
const char* backendHost = "gas-smt6-production.up.railway.app";
const int backendPort = 443; // HTTPS
const String apiPath = "/api/sensor-data";

// Pin yang terhubung ke sensor MQ-3
const int sensorPin = A0;

// Konstanta kalibrasi sensor
const float R0 = 10.0; // Nilai R0 dalam kOhm (nilai kalibrasi)

// NTP Client Setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Global variables for tracking performance
int totalScans = 0;
int successfulTransmissions = 0;
int failedTransmissions = 0;
unsigned long totalLatency = 0;
unsigned long minLatency = 4294967295; // Max value for unsigned long
unsigned long maxLatency = 0;

// Track failure causes
int wifiFailures = 0;
int httpFailures = 0;
int sensorFailures = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n----- MQ-3 Alcohol Gas Sensor 1000-Scan Test -----");
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize and setup NTP Client
  timeClient.begin();
  timeClient.setTimeOffset(25200); // GMT+7 for Indonesia (7 hours * 3600 seconds)
  
  Serial.println("\nInitializing NTP client...");
  timeClient.update();
  Serial.print("Current time: ");
  Serial.println(timeClient.getFormattedTime());
  
  Serial.println("\nStarting 1000-scan test sequence...");
  Serial.println("This test will perform 1000 sensor readings and transmissions");
  Serial.println("to evaluate system reliability and performance.");
  Serial.println("------------------------------------------");
  delay(100);
}

void loop() {
  // Only proceed if we haven't reached 1000 scans
  if (totalScans < 1000) {
    totalScans++;
    Serial.print("\n===== Scan #");
    Serial.print(totalScans);
    Serial.println(" of 1000 =====");
    
    // Record start time for latency measurement
    unsigned long startTime = millis();
    
    // Attempt to read sensor
    int rawValue = 0;
    float voltage = 0.0;
    float rs = 0.0;
    float ratio = 0.0;
    float ppm = 0.0;
    
    // Read sensor and check for failures
    bool sensorReadSuccess = true;
    
    // Baca nilai analog dari sensor MQ-3
    rawValue = analogRead(sensorPin);
    
    // Check for obviously invalid reading (using NodeMCU's 10-bit ADC range)
    if (rawValue < 0 || rawValue > 1023) {
      sensorReadSuccess = false;
      sensorFailures++;
      Serial.println("SENSOR ERROR: Invalid sensor reading");
    } else {
      // Konversi nilai analog ke tegangan (NodeMCU menggunakan ADC 10-bit: 0-1023)
      voltage = rawValue * (3.3 / 1023.0);
      
      // Hitung resistansi sensor (Rs)
      rs = ((3.3 * 10.0) / voltage) - 10.0; // Asumsi resistor beban 10K
      
      // Hitung rasio Rs/R0
      ratio = rs / R0;
      
      // Konversi ke ppm (parts per million) menggunakan formula dari datasheet MQ-3
      ppm = 0.4 * pow(ratio, -0.6);
      
      // Check for reasonable range (basic validation)
      if (ppm < 0 || ppm > 10.0) {  // Assume above 10.0 ppm is unlikely/error
        sensorReadSuccess = false;
        sensorFailures++;
        Serial.println("SENSOR ERROR: PPM value out of reasonable range");
      }
    }
    
    if (sensorReadSuccess) {
      // Tampilkan semua nilai ke serial monitor
      Serial.println("----- Sensor Reading -----");
      Serial.print("Raw ADC Value: ");
      Serial.println(rawValue);
      Serial.print("Voltage: ");
      Serial.print(voltage, 3);
      Serial.println(" V");
      Serial.print("Sensor Resistance (Rs): ");
      Serial.print(rs, 2);
      Serial.println(" kOhm");
      Serial.print("Rs/R0 Ratio: ");
      Serial.println(ratio, 3);
      Serial.print("Alcohol Concentration: ");
      Serial.print(ppm, 2);
      Serial.println(" ppm");
      
      // Interpretasi hasil
      Serial.print("Status: ");
      if (ppm < 0.5) {
        Serial.println("Normal (Safe)");
      } else if (ppm < 1.0) {
        Serial.println("Slight Alcohol Detected");
      } else {
        Serial.println("Elevated Alcohol Level");
      }
      Serial.println("---------------------------");
      
      // Kirim data ke backend
      if (WiFi.status() == WL_CONNECTED) {
        // Update time from NTP server before sending data
        timeClient.update();
        
        Serial.println("Sending data to backend...");
        bool sendSuccess = sendToBackend(rawValue, voltage, rs, ratio, ppm);
        
        if (sendSuccess) {
          successfulTransmissions++;
          
          // Calculate and record latency
          unsigned long latency = millis() - startTime;
          totalLatency += latency;
          
          if (latency < minLatency) minLatency = latency;
          if (latency > maxLatency) maxLatency = latency;
          
          Serial.print("Transmission successful, Total Latency: ");
          Serial.print(latency);
          Serial.println("ms");
        } else {
          failedTransmissions++;
          httpFailures++;
          Serial.println("Transmission failed: HTTP error");
        }
      } else {
        failedTransmissions++;
        wifiFailures++;
        Serial.println("Transmission failed: WiFi Disconnected");
        
        // Try to reconnect for next attempt
        Serial.println("Attempting to reconnect WiFi...");
        WiFi.begin(ssid, password);
        int reconnectAttempts = 0;
        while (WiFi.status() != WL_CONNECTED && reconnectAttempts < 10) {
          delay(500);
          Serial.print(".");
          reconnectAttempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nWiFi reconnected!");
        } else {
          Serial.println("\nWiFi reconnection failed");
        }
      }
    } else {
      // If sensor reading failed, count as failed transmission
      failedTransmissions++;
    }
    
    // Print stats every 100 scans
    if (totalScans % 100 == 0 || totalScans == 1) {
      printStats();
    }
    
    // Wait between readings
    // Serial.println("\nWaiting 1 seconds before next scan...");
    // delay(1000);
  } else if (totalScans == 1000) {
    // After 1000 scans, display final results
    Serial.println("\n\n================================================");
    Serial.println("            TEST SEQUENCE COMPLETED");
    Serial.println("================================================");
    printStats();
    Serial.println("\nFinal Report Details:");
    Serial.println("------------------------------------------------");
    Serial.print("WiFi failures: ");
    Serial.print(wifiFailures);
    if (failedTransmissions > 0) {
      Serial.print(" (");
      Serial.print((float)wifiFailures/failedTransmissions * 100, 1);
      Serial.println("% of all failures)");
    } else {
      Serial.println(" (0% of all failures)");
    }
    
    Serial.print("HTTP request failures: ");
    Serial.print(httpFailures);
    if (failedTransmissions > 0) {
      Serial.print(" (");
      Serial.print((float)httpFailures/failedTransmissions * 100, 1);
      Serial.println("% of all failures)");
    } else {
      Serial.println(" (0% of all failures)");
    }
    
    Serial.print("Sensor reading failures: ");
    Serial.print(sensorFailures);
    if (failedTransmissions > 0) {
      Serial.print(" (");
      Serial.print((float)sensorFailures/failedTransmissions * 100, 1);
      Serial.println("% of all failures)");
    } else {
      Serial.println(" (0% of all failures)");
    }
    Serial.println("================================================");
    
    totalScans++; // Increment so we don't print this again
    
    // Flash the built-in LED to indicate completion
    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_BUILTIN, LOW); // LED on
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH); // LED off
      delay(200);
    }
  } else {
    // Just wait in an idle state
    // delay(1000);
  }
}

void printStats() {
  Serial.println("\n----- PERFORMANCE METRICS -----");
  Serial.print("Scans completed: ");
  Serial.println(totalScans);
  Serial.print("Successful transmissions: ");
  Serial.print(successfulTransmissions);
  Serial.print(" (");
  Serial.print((float)successfulTransmissions/totalScans * 100, 1);
  Serial.println("%)");
  Serial.print("Failed transmissions: ");
  Serial.print(failedTransmissions);
  Serial.print(" (");
  Serial.print((float)failedTransmissions/totalScans * 100, 1);
  Serial.println("%)");
  
  if (successfulTransmissions > 0) {
    Serial.print("Average latency: ");
    Serial.print(totalLatency / successfulTransmissions);
    Serial.println("ms");
    Serial.print("Min latency: ");
    Serial.print(minLatency);
    Serial.println("ms");
    Serial.print("Max latency: ");
    Serial.print(maxLatency);
    Serial.println("ms");
  }
  
  Serial.println("-------------------------------");
}

bool sendToBackend(int rawValue, float voltage, float rs, float ratio, float ppm) {
  WiFiClientSecure client;
  client.setInsecure(); // Untuk menyederhanakan koneksi HTTPS, skip verifikasi sertifikat
  
  unsigned long sendStartTime = millis();
  
  Serial.print("Connecting to backend on Railways: ");
  Serial.println(backendHost);

  // Get current epoch time (seconds since Jan 1, 1970)
  unsigned long epochTime = timeClient.getEpochTime();
  
  // Convert epochTime to a formatted ISO 8601 timestamp string
  char formattedTime[30];
  time_t rawtime = epochTime;
  struct tm * timeinfo;
  timeinfo = gmtime(&rawtime);
  
  // Format the time to ISO 8601 format with timezone adjustment already handled by NTP client
  strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%dT%H:%M:%S.000Z", timeinfo);
  
  // Buat payload JSON
  StaticJsonDocument<300> doc;
  doc["device_id"] = "nodemcu_sensor_01";
  doc["raw_value"] = rawValue;
  doc["voltage"] = voltage;
  doc["resistance"] = rs;
  doc["ratio"] = ratio;
  doc["alcohol_ppm"] = ppm;
  doc["timestamp"] = formattedTime; // Use formatted ISO timestamp string
  doc["scan_number"] = totalScans; // Add scan number for tracking

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  Serial.print("Payload: ");
  Serial.println(jsonPayload);

  HTTPClient https;
  
  // URL untuk backend API
  String url = "https://";
  url += backendHost;
  url += apiPath;
  
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(5000); // Increase timeout
    
    // Kirim request dan measure time
    unsigned long httpStartTime = millis();
    int httpResponseCode = https.POST(jsonPayload);
    unsigned long httpDuration = millis() - httpStartTime;
    
    Serial.print("HTTP request duration: ");
    Serial.print(httpDuration);
    Serial.println("ms");
    
    if (httpResponseCode > 0) {
      String response = https.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("Response: ");
      Serial.println(response);
      
      https.end();
      return true;
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      
      https.end();
      return false;
    }
  } else {
    Serial.println("Unable to connect to backend");
    return false;
  }
}