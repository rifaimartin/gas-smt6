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

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n----- MQ-3 Alcohol Gas Sensor Test -----");
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
  
  Serial.println("\nPreparing to read sensor values...");
  Serial.println("------------------------------------------");
  delay(100);
}

void loop() {
  // Baca nilai analog dari sensor MQ-3
  int rawValue = analogRead(sensorPin);
  
  // Konversi nilai analog ke tegangan (NodeMCU menggunakan ADC 10-bit: 0-1023)
  float voltage = rawValue * (3.3 / 1023.0);
  
  // Hitung resistansi sensor (Rs)
  float rs = ((3.3 * 10.0) / voltage) - 10.0; // Asumsi resistor beban 10K
  
  // Hitung rasio Rs/R0
  float ratio = rs / R0;
  
  // Konversi ke ppm (parts per million) menggunakan formula dari datasheet MQ-3
  float ppm = 0.4 * pow(ratio, -0.6);
  
  // Tampilkan semua nilai ke serial monitor
  Serial.println("\n----- Sensor Reading -----");
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
    sendToBackend(rawValue, voltage, rs, ratio, ppm);
  } else {
    Serial.println("WiFi Disconnected");
  }
  
  // Tunggu beberapa detik sebelum pembacaan berikutnya
  delay(5000); 
}

void sendToBackend(int rawValue, float voltage, float rs, float ratio, float ppm) {
  WiFiClientSecure client;
  client.setInsecure(); // Untuk menyederhanakan koneksi HTTPS, skip verifikasi sertifikat
  
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
    https.setTimeout(15000); // Increase timeout
    
    // Kirim request
    int httpResponseCode = https.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      String response = https.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("Response: ");
      Serial.println(response);
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    
    https.end();
  } else {
    Serial.println("Unable to connect to backend");
  }
}