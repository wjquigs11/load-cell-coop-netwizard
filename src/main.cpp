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
#define FULL -420000 // load cell reading at full feeder (remember it's in tension so "reverse")
#define EMPTY 0 // should be close to zero because I tare'd with empty feeder hanging

// zero offset value (tare), calculate and save to preferences:
void refreshOffsetValueAndSaveToPrefs() {
  long _offset = 0;
  log::toAll("Calculating tare offset value...");
  LoadCell.tare(); // calculate the new tare / zero offset value (blocking)
  _offset = LoadCell.getTareOffset(); // get the new tare / zero offset value
  preferences.putLong("tareoffset",_offset);
  LoadCell.setTareOffset(_offset); // set value as library parameter (next restart it will be read from preferences)
  log::toAll("New tare offset value:" + String(_offset));
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

  //restore the zero offset value from preferences:
  long tare_offset = 0;
  tare_offset = preferences.getLong("tareoffset",0);
  Serial.printf(", loaded tare offset value %0.2ld\n", tare_offset);
  LoadCell.setTareOffset(tare_offset);
  // set this to false if the value has been resored from preferences
  // set to true if you want to tare
  boolean _tare = false; 
  unsigned long stabilizingtime = 2000; // precision right after power-up can be improved by adding a few seconds of stabilizing time
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }
  LoadCell.setSamplesInUse(25);
  log::toAll("load cell samples: " + String(LoadCell.getSamplesInUse()));
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
      loadcell = map((long)raw,EMPTY,FULL,0,100);
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
      if (input == "tare")
        refreshOffsetValueAndSaveToPrefs();
    }
  }
}
