#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_sleep.h"
#include "time.h"

// SDS011 hardware serial pins
#define SDS_RX 16  // RX pin (change according to your wiring)
#define SDS_TX 17  // TX pin (change according to your wiring)

HardwareSerial sds(1);

const char* ssid = "Elconics";
const char* password = "Elconics@123";

const char* serverName = "http://app.antzsystems.com/api/v1/iot/enclosure/metric/update"; // Use HTTP (insecure)

// Set deep sleep time in microseconds (15 minutes)
const uint64_t deepSleepTime = 15 * 60 * 1000000;

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 5 * 3600 + 1800; // GMT offset for IST (5 hours and 30 minutes)
const int   daylightOffset_sec = 0; // Daylight offset

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
}

String getFormattedTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "";
  }
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void setup() {
  // Initialize Serial communication with the computer
  Serial.begin(115200);
  delay(100);  // Short delay after initializing Serial
  Serial.println("Initializing SDS011 Air Quality Monitor...");

  // Initialize HardwareSerial communication with SDS011
  sds.begin(9600, SERIAL_8N1, SDS_RX, SDS_TX);

  // Initialize WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Reading data from SDS011...");

    // Look for the starting byte of the SDS011 data frame
    while (sds.available() && sds.read() != 0xAA) { }

    if (sds.available()) {
      Serial.println("Data available from SDS011...");
    }

    // Once we have the starting byte, attempt to read the next 9 bytes
    byte buffer[10];
    buffer[0] = 0xAA;  // The starting byte we already found
    if (sds.available() >= 9) {
      sds.readBytes(&buffer[1], 9);

      // Check if the last byte is the correct ending byte
      if (buffer[9] == 0xAB) {
        int pm25int = (buffer[3] << 8) | buffer[2];
        int pm10int = (buffer[5] << 8) | buffer[4];
        float pm2_5 = pm25int / 10.0;
        float pm10 = pm10int / 10.0;

        // WHO recommended limits
        float pm2_5_limit = 5.0;
        float pm10_limit = 40.0;

        // Print the values
        Serial.print("PM2.5: ");
        Serial.print(pm2_5, 2);  // 2 decimal places
        Serial.print(" µg/m³   ");
        Serial.print("PM10: ");
        Serial.print(pm10, 2);  // 2 decimal places
        Serial.println(" µg/m³   ");

        // Check if values exceed WHO limits
        if (pm2_5 > pm2_5_limit) {
          Serial.print("Exceeds WHO PM2.5 limit by: ");
          Serial.print(pm2_5 - pm2_5_limit, 2);
          Serial.println(" µg/m³");
        } else {
          Serial.println("\nPM2.5 is within safe limits.");
        }
        if (pm10 > pm10_limit) {
          Serial.print("\nExceeds WHO PM10 limit by: ");
          Serial.print(pm10 - pm10_limit, 2);
          Serial.println(" µg/m³");
        } else {
          Serial.println("\nPM10 is within safe limits.");
        }

        // Get current time
        String currentTime = getFormattedTime();
        Serial.println("Current Time: " + currentTime);

        // Sending data to the server
        Serial.println("Sending HTTP request...");

        // Your Domain name with URL path
        Serial.println("Server URL: " + String(serverName));

        // Specify content-type header as application/json
        HTTPClient http;

        // Use WiFiClient for HTTP (insecure)
        WiFiClient client;

        http.begin(client, serverName);

        // Create JSON data
        DynamicJsonDocument jsonDoc(512); // Adjust the buffer size as needed
        JsonObject root = jsonDoc.to<JsonObject>();
        root["enclosure_id"] = 111;

        JsonArray values = root.createNestedArray("values");

        JsonObject pm25Obj = values.createNestedObject();
        pm25Obj["key"] = "PM2.5";
        pm25Obj["value"] = pm2_5;
        pm25Obj["uom"] = "µg/m³";
        pm25Obj["event_date"] = currentTime;

        JsonObject pm10Obj = values.createNestedObject();
        pm10Obj["key"] = "PM10";
        pm10Obj["value"] = pm10;
        pm10Obj["uom"] = "µg/m³";
        pm10Obj["event_date"] = currentTime;

        // Serialize JSON to a string
        String jsonString;
        serializeJson(root, jsonString);
        Serial.println("JSON Data: " + jsonString);

        // Send HTTP POST request with JSON data
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(jsonString);

        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);

        // Check for a successful HTTP response (200 OK)
        if (httpResponseCode == 200) {
          // Parse and print the response
          String response = http.getString();
          Serial.println("Server response: " + response);
        } else {
          Serial.println("HTTP request failed");
        }

        // Free resources
        http.end();

      } else {
        Serial.println("Invalid ending byte from SDS011.");
      }
    } else {
      Serial.println("Not enough data from SDS011.");
    }
  } else {
    Serial.println("WiFi Disconnected");
  }

  // Enter deep sleep mode
  Serial.println("Entering deep sleep mode for 15 minutes...");
  esp_sleep_enable_timer_wakeup(deepSleepTime);
  esp_deep_sleep_start();
}
