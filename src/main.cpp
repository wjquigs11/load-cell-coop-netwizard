/*
Basic chicken coop feeder weight monitor based on ESP32
Uses a load cell and HX711 ADC
To use, first go to PlatformIO->Build Filesystem Image, then Upload Filesystem Image, then build and upload to your ESP32
At first upload, it will start the NetWizard captive portal to scan and select wifi
After reboot, it will connect to your wifi
To reset wifi connection (go back to captive portal), press reset on the ESP32 twice within 10 seconds (double reset detector)
Usage:
First, put an empty feeder on the load cell, then tare the load cell
There are two ways to tare:
  Either browse to "http://coopfeeder.local/config?tare"
  or use "http://coopfeeder.local/webserial", which is a basic command-line interface, and enter "tare"
You can also change the hostname and the response time for the web interface (default 1 second) using /config
A console log is stored on spiffs; access it via http://coopfeeder.local/console.log
This repo uses the excellent WebSerial and NetWizard libraries from ayusharma, as well as the ElegantOTA library for development
*/

#include "include.h"
#include <HX711_ADC.h>

File consLog;
Preferences preferences;

#ifdef WIFI
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
#endif

bool wifiEnabled = false;
int timerDelay = 1000;
unsigned long lastTime = 0;

// HX711 circuit wiring
const int HX711_dout = 27; //mcu > HX711 dout pin
const int HX711_sck = 14; //mcu > HX711 sck pin

HX711_ADC LoadCell(HX711_dout, HX711_sck);
unsigned long t = 0;
float calibrationValue=1.0; // calibration value (see example file "Calibration.ino")
float loadcell=0.0;
long empty_offset = 0; // offset value for empty feeder
long full_offset = -420000; // offset value for full feeder (remember it's in tension so "reverse")

// Configure tare offset values, calculate and save to preferences:
void configTare(const String& type) {
  long _offset = 0;
  log::toAll("Calculating " + type + " offset value...");
  LoadCell.tare(); // calculate the new tare / zero offset value (blocking)
  _offset = LoadCell.getTareOffset(); // get the new tare / zero offset value
  
  if (type == "empty") {
    empty_offset = _offset;
    preferences.putLong("empty_offset", _offset);
    log::toAll("New empty offset value: " + String(_offset));
  } else if (type == "full") {
    full_offset = _offset;
    preferences.putLong("full_offset", _offset);
    log::toAll("New full offset value: " + String(_offset));
  }
  
  LoadCell.setTareOffset(_offset); // set value as library parameter
}

void setup() {
  Serial.begin(115200); delay(300);

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
  LoadCell.begin();
  //LoadCell.setReverseOutput();

#ifdef WIFI
  bool doubleReset = preferences.getBool("DRD", false);
  preferences.putBool("DRD", true);

  if (wifiEnabled) {
    if (doubleReset) {
      log::toAll("double reset detected");
      resetWifi();
    } else {
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
          log::toAll("mdns service: " + MDNS.hostname(i) + " (" + String(MDNS.address(i)) + ":" + String(MDNS.port(i)) + ")");
        }
      }
    }
  }
#endif // WIFI

  //restore the offset values from preferences:
  empty_offset = preferences.getLong("empty_offset", 0);
  full_offset = preferences.getLong("full_offset", -420000);
  log::toAll("loaded empty offset value " + String(empty_offset));
  log::toAll("loaded full offset value " + String(full_offset));
  LoadCell.setTareOffset(empty_offset);
  // set this to false if the value has been resored from preferences
  // set to true if you want to tare
  boolean _tare = false; 
  unsigned long stabilizingtime = 2000; // precision right after power-up can be improved by adding a few seconds of stabilizing time
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    log::toAll("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    log::toAll("load cell startup is complete");
  }
  LoadCell.setSamplesInUse(25);
  log::toAll("load cell samples: " + String(LoadCell.getSamplesInUse()));

  consLog.flush();
}

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
    if (int lc = LoadCell.update()) {
      newDataReady = true;
    }
    if (newDataReady) {
      float raw = LoadCell.getData();
      loadcell = map((long)raw, empty_offset, full_offset, 0, 100);
      newDataReady = false;
      log::toAll("[" + String(now) + "] raw: " + String(raw) + " scaled: " + String(loadcell,0));
#if WIFI
      readings["loadcell"] = String(loadcell);
      // check time every hour
      if (now - lastTimeTime > 3600000 || lastTimeTime == 0) {
        if (WiFi.status() == WL_CONNECTED) {
          lastTimeTime = now;
          //log::toAll(WiFi.SSID());
          timeClient.update();
          log::toAll(timeClient.getFormattedTime());      
        }
      }
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
}
