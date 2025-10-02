// Fallback implementation when NETWIZARD is not defined
#ifdef WIFI
#ifndef NETWIZARD

#include "include.h"

bool setupWifi() {
  Serial.println("Starting WiFi (fallback mode)...");
  
  // Default credentials in case file can't be read
  String ssid = "";
  String password = "";
  
  // Read WiFi credentials from JSON file
  if (SPIFFS.exists("/wifi.json")) {
    File configFile = SPIFFS.open("/wifi.json", "r");
    if (configFile) {
      Serial.println("Reading WiFi credentials from config file");
      
      // Parse JSON
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, configFile);
      configFile.close();
      
      if (!error) {
        ssid = doc["ssid"].as<String>();
        password = doc["password"].as<String>();
        Serial.println("WiFi credentials loaded successfully");
      } else {
        Serial.println("Failed to parse WiFi config file");
      }
    } else {
      Serial.println("Failed to open WiFi config file");
    }
  } else {
    Serial.println("WiFi config file not found");
  }
  
  if (ssid.length() == 0) {
    Serial.println("No SSID configured, cannot connect to WiFi");
    return false;
  }
  
  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);
  
  // Try to connect to saved WiFi credentials
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // Wait for connection with timeout
  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("Connected to WiFi. IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("");
    Serial.println("Failed to connect to WiFi. Please configure WiFi manually.");
    Serial.println("Format of the wifi.json file to put in spiffs (/data):");
    Serial.println("{\n\t\"ssid\": \"your_SSID\",\n\t\"password\": \"your_password\"\n}");
    return false;
  }
}

void resetWifi() {
  Serial.println("Resetting WiFi settings (fallback mode)...");
  WiFi.disconnect(true); // Disconnect and delete credentials
  delay(1000);
  ESP.restart();
}

#endif // ifndef NETWIZARD
#endif