/*
Basic chicken coop feeder weight monitor based on ESP32
Uses a load cell and HX711 ADC
To use, first go to PlatformIO->Build Filesystem Image, then Upload Filesystem Image, then build and upload to your ESP32
At first upload, it will start the NetWizard captive portal to scan and select wifi
After reboot, it will connect to your wifi
To reset wifi connection (go back to captive portal), press reset on the ESP32 twice within 10 seconds (double reset detector)
Usage:
First, put an empty feeder on the load cell, then tare the load cell
There are several ways to tare:
  Either browse to "http://coopfeeder.local/config?tare"
  or use "http://coopfeeder.local/webserial", which is a basic command-line interface, and enter "tare"
  or click the "calibrate empty" button on the web interface
You can also change the hostname and the response time for the web interface (default 1 second) using /config
A console log is stored on spiffs; access it via http://coopfeeder.local/console.log
This repo uses the WebSerial and NetWizard libraries from ayusharma, as well as the ElegantOTA library for development
*/

#include "include.h"

File consLog;
Preferences preferences;

bool wifiEnabled = false;
int timerDelay = 1000;
unsigned long lastTime = 0;

// HX711 circuit wiring
const int HX711_dout = 27; //mcu > HX711 dout pin
const int HX711_sck = 14; //mcu > HX711 sck pin
HX711 LoadCell;

unsigned long t = 0;
float calibrationValue=1.0; // calibration value (see example file "Calibration.ino")
long loadcell=0;
long empty_offset = 0; // offset value for empty feeder
long full_raw = -420000; // offset value for full feeder (remember it's in tension so "reverse")

// Configure tare offset values, calculate and save to preferences:
void configTare(const String& type) {
  log::toAll("Calculating " + type + " offset value...");
  if (type == "empty") {
    empty_offset = LoadCell.read();
    preferences.putLong("empty_offset", empty_offset);
    log::toAll("New empty offset value: " + String(empty_offset));
  } else if (type == "full") {
    static long totalRaw = 0;
    int i = 0;
    while (i<10) {
      // take 10 samples
      if (LoadCell.is_ready()) {
        long raw = LoadCell.read();   
        log::toAll("raw[" + String(i) + "]: " + String(raw));
        totalRaw += raw;
        i++;
        delay(10);
      }
    }
    full_raw = totalRaw / 10.0;
    preferences.putLong("full_raw", full_raw);
    log::toAll("total: " + String(totalRaw) + ", New full offset value: " + String(full_raw));
  }
}

void setup() {
  Serial.begin(115200); delay(300);
  //pinMode(HX711_dout, INPUT);
  //pinMode(HX711_sck, OUTPUT);

  if (SPIFFS.begin())
    Serial.println("opened SPIFFS");
  else
    Serial.println("failed to open SPIFFS");

  consLog = SPIFFS.open("/console.log", "a", true);
  if (!consLog) {
    log::toAll("failed to open console log");
  }
  if (consLog.println("ESP console log.")) {
    log::toAll("console log written");
  } else {
    log::toAll("console log write failed");
  }
  preferences.begin("ESPprefs", false);
  //restore the offset values from preferences:
  empty_offset = preferences.getLong("empty_offset", 0);
  full_raw = preferences.getLong("full_raw", 0);
  log::toAll("loaded empty offset value " + String(empty_offset));
  log::toAll("loaded full offset value " + String(full_raw));
  timerDelay = preferences.getInt("timerdelay", 10000);
  if (timerDelay<200) {
    timerDelay = 200;
    preferences.putInt("timerdelay", 1000);
  }
  log::toAll("timerDelay " + String(timerDelay));
#ifdef WIFI
  host = preferences.getString("hostname", host);
  log::toAll("hostname: " + host);
#endif
  wifiEnabled = preferences.getBool("wifi", true);

  // start the load cell
  LoadCell.begin(HX711_dout,HX711_sck);

#ifdef WIFI
  bool doubleReset = preferences.getBool("DRD", false);

  if (wifiEnabled) {
    if (doubleReset) {
      // DRD currently not working without Netwizard
      log::toAll("double reset detected");
      //resetWifi();
      //NW.erase();
      preferences.putBool("DRD", false);
      //ESP.restart();
    }
    //} else {
      preferences.putBool("DRD", true);
      bool wifiConnected = setupWifi();
      // Start the web server regardless of WiFi connection status
      // In fallback mode, it will serve from AP mode
      startWebServer();
      
#ifdef ELEGANTOTA
      ElegantOTA.begin(&server);
#endif
#ifdef WEBSERIAL
      WebSerial.begin(&server);
      // Attach a callback function to handle incoming messages
      WebSerial.onMessage(WebSerialonMessage);
#endif
      serverStarted = true;
      
      if (wifiConnected) {
        log::toAll("HTTP server started @" + WiFi.localIP().toString());
      } else {
        log::toAll("HTTP server started in AP mode @" + WiFi.softAPIP().toString());
      }
      if (!MDNS.begin(host.c_str()))
        log::toAll(F("Error starting MDNS responder"));
      else {
        log::toAll("MDNS started " + host);
      }
      // Add service to MDNS-SD
      if (!MDNS.addService("http", "tcp", HTTP_PORT))
        log::toAll("MDNS add service failed");
      int n = MDNS.queryService("http", "tcp");
      if (n == 0) {
        log::toAll("No services found");
      } else {
        for (int i = 0; i < n; i++) {
          log::toAll("mdns service: " + MDNS.hostname(i) + " (" + String(MDNS.IP(i)) + ":" + String(MDNS.port(i)) + ")");
        }
      }
    //}
  }
#endif // WIFI
  consLog.flush();
} // setup()

void loop() {
#ifdef ELEGANTOTA
  ElegantOTA.loop();
#endif
#ifdef WEBSERIAL
  WebSerial.loop();
#endif
#ifdef NETWIZARD
  NW.loop();
#endif
  unsigned long now = millis();
  static unsigned long lastEventTime, lastTimeTime, startTime;
#ifdef WIFI
  // update web page
  static bool drdCleared = false;
  
  // Clear DRD flag after DRD_TIMEOUT seconds for double reset detection
  // unless reset occurs within the timeout period
  if (!drdCleared && (now > (DRD_TIMEOUT * 1000))) {
    preferences.putBool("DRD", false);
    drdCleared = true;
    log::toAll("DRD timeout - cleared double reset flag");
  }
#endif
  if (now - lastEventTime > timerDelay || lastEventTime == 0) {
    lastEventTime = now;
    // check load cell
    static boolean newDataReady = 0;
    // check for new data/start next conversion:
    if (LoadCell.is_ready()) {
      newDataReady = true;
    }
    if (newDataReady) {
      long raw = LoadCell.read();
      loadcell = map(raw, empty_offset, full_raw, 0, 100);
      if (loadcell > 100) loadcell = 100;
      if (loadcell < 0) loadcell = 0;
      newDataReady = false;
      log::toAll("[" + String(now) + "] raw: " + String(raw) + " scaled: " + String(loadcell));
#if WIFI
      readings["loadcell"] = String(loadcell);
      readings["units"] = "%";
      
      // Calculate current time based on browser timestamp plus elapsed time
      if (updateTime > 0) {
        // Use the browser's timestamp as base and add elapsed milliseconds
        // This gives us a proper Unix timestamp in milliseconds
        lastUpdate = updateTime + (now - lastEventTime);
      } else {
        // If updateTime is not set yet, use the current time as a fallback
        // But this will be incorrect since ESP32 millis() is not a Unix timestamp
        lastUpdate = now;
      }
      
      // Send the timestamp in milliseconds since epoch (Unix timestamp)
      readings["lastUpdate"] = String(lastUpdate);
      
      events.send(getSensorReadings().c_str(),"new_readings" ,millis());
#endif
      consLog.flush();
    }
  }
  if (Serial.available() > 0) {
    String input = "";
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        // Ignore newline and carriage return characters
        break;
      }
      input += c;
    }
    if (input.length() > 0) {
      Serial.print("Received: ");
      Serial.println(input);
      if (input == "empty")
        configTare("empty");
      else if (input == "full")
        configTare("full");
    }
  }
} // loop()
