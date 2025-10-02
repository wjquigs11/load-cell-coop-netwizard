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
This repo uses the excellent WebSerial and NetWizard libraries from ayusharma, as well as the ElegantOTA library for development
*/

#include "include.h"
#include <HX711_ADC.h>

File consLog;
Preferences preferences;

bool wifiEnabled = false;
int timerDelay = 1000;
unsigned long lastTime = 0;
struct tm *ptm;
char prbuf[PRBUF];

#ifdef DEEPSLEEP
// Deep sleep variables
int awakeTimer = 600;  // stay awake for X seconds each time you wake up
// RTC memory variables (persist across deep sleep)
RTC_DATA_ATTR int bootCount = 0;
#else
// No deep sleep, but we still need some variables
int awakeTimer = 0;  // Not used without deep sleep
#endif
unsigned long startTime; // Time when the device started

// HX711 circuit wiring
const int HX711_dout = 27; //mcu > HX711 dout pin
const int HX711_sck = 14; //mcu > HX711 sck pin

HX711_ADC LoadCell(HX711_dout, HX711_sck);
unsigned long t = 0;
float calibrationValue=1.0; // calibration value (see example file "Calibration.ino")
long loadcell=0;
long empty_offset = 0; // offset value for empty feeder
long full_raw = -420000; // offset value for full feeder (it's in tension so "reverse")

// Configure tare offset values, calculate and save to preferences:
void configTare(const String& type) {
  long _offset = 0;
  log::toAll("Calculating " + type + " offset value...");
  if (type == "empty") {
    LoadCell.tare(); // calculate the new tare / zero offset value (blocking)
    _offset = LoadCell.getTareOffset(); // get the new tare / zero offset value    empty_offset = _offset;
    preferences.putLong("empty_offset", _offset);
    LoadCell.setTareOffset(_offset);
    log::toAll("New empty offset value: " + String(_offset));
  } else if (type == "full") {
    static float totalRaw = 0;
    int i = 0;
    while (i<10) {
      // take 10 samples
      if (LoadCell.update()) {
        float raw = -LoadCell.getData();   
        log::toAll("raw[" + String(i) + "]: " + String(raw,2));
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

#ifdef DEEPSLEEP
// Function to print the reason by which ESP32 has been awaken from sleep
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: log::toAll("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: log::toAll("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: log::toAll("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: log::toAll("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: log::toAll("Wakeup caused by ULP program"); break;
    default: log::toAll("Wakeup was not caused by deep sleep: " + String((int)wakeup_reason)); break;
  }
}
#else
// Empty function when deep sleep is disabled
void print_wakeup_reason() {
  log::toAll("Deep sleep is disabled");
}
#endif

void setup() {
  Serial.begin(115200); delay(300);
  startTime = millis();
#ifdef DEEPSLEEP
  // Deep sleep initialization
#else
  // No deep sleep initialization needed
#endif

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

  //restore the offset values from preferences:
  empty_offset = preferences.getLong("empty_offset", 0);
  full_raw = preferences.getLong("full_raw", -420000);
  log::toAll("loaded empty offset value " + String(empty_offset));
  log::toAll("loaded full offset value " + String(full_raw));
  LoadCell.setTareOffset(empty_offset);
  // set this to false if the value has been resored from preferences
  // set to true if you want to tare
  boolean _tare = false; 
  unsigned long stabilizingtime = 10000; // precision right after power-up can be improved by adding a few seconds of stabilizing time
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    log::toAll("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    log::toAll("load cell startup is complete");
  }

#ifdef DEEPSLEEP
  // Increment boot number and print it every reboot
  ++bootCount;
  log::toAll("Boot number: " + String(bootCount));

  // Print the wakeup reason for ESP32
  print_wakeup_reason();

  // Configure the wake up source - set ESP32 to wake up every TIME_TO_SLEEP seconds
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  log::toAll("Setup ESP32 to sleep for " + String(TIME_TO_SLEEP) + " Seconds every " + String(awakeTimer) + " seconds");
#else
  // Deep sleep is disabled
  print_wakeup_reason();
#endif

  preferences.begin("ESPprefs", false);
  timerDelay = preferences.getInt("timerdelay", 10000);
  if (timerDelay<200) {
    timerDelay = 200;
    preferences.putInt("timerdelay", 1000);
  }
  log::toAll("timerDelay " + String(timerDelay));

  LoadCell.begin();
  //LoadCell.setReverseOutput();
  LoadCell.setSamplesInUse(25);
  log::toAll("load cell samples: " + String(LoadCell.getSamplesInUse()));

#if defined(WIFI) || defined(NETWIZARD)
  host = preferences.getString("hostname", host);
  log::toAll("hostname: " + host);
  wifiEnabled = preferences.getBool("wifi", true);
  bool doubleReset = preferences.getBool("DRD", false);
  if (wifiEnabled) {
    if (doubleReset) {
      log::toAll("double reset detected");
      // DRD not implemented without Netwizard
#ifdef NETWIZARD
      NW.erase();
      preferences.putBool("DRD", false);
      delay(1000);
      ESP.restart();
#endif
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
          log::toAll("mdns service: " + MDNS.hostname(i) + " (" + String(MDNS.address(i)) + ":" + String(MDNS.port(i)) + ")");
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
  static unsigned long lastEventTime, lastTimeTime, lastClientCheckTime;
#ifndef DEEPSLEEP
  static unsigned long startTime; // Define startTime here when not using deep sleep
#endif
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
#ifndef DEEPSLEEP
  // When deep sleep is disabled, we need to periodically check if we should "reset" startTime
  // to prevent the static startTime from causing issues
  if (now > 3600000) { // Reset every hour (3600000 ms)
    startTime = now;
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
      float raw = -LoadCell.getData();
      // tare() sets return value from empty reading to 0, so map from 0 not empty_offset
      loadcell = map((long)raw, 0, full_raw, 0, 100);
      if (loadcell > 100) loadcell = 100;
      if (loadcell < 0) loadcell = 0;
      newDataReady = false;
#if defined(WIFI) || defined(NETWIZARD)
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
      ptm = localtime(&lastUpdate);
      sprintf(prbuf,"[%02d/%02d %02d:%02d:%02d] ",ptm->tm_mon,ptm->tm_mday,ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
      log::toAll("[" + String(prbuf) + "] raw: " + String(raw) + " scaled: " + String(loadcell));
      consLog.flush();
    }
  }
  
  // Periodically check and log client connection status (every 30 seconds)
  #if defined(WIFI) || defined(NETWIZARD)
  if (now - lastClientCheckTime > 30000 || lastClientCheckTime == 0) {
    lastClientCheckTime = now;
    if (hasAnyWebClients()) {
      log::toAll("Client connection status: " + String(wsClientCount) + " WebSocket clients, " +
                String(esClientCount) + " EventSource clients connected");
    }
  }
  #endif
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
  
#ifdef DEEPSLEEP
  // Check if it's time to go to sleep
  if ((millis() - startTime) > (awakeTimer * 1000)) {
    // Check if any clients are connected before going to sleep
    #if defined(WIFI) || defined(NETWIZARD)
    if (hasAnyWebClients()) {
      // Clients are connected, extend awake time
      log::toAll("Web clients connected, extending awake time");
      startTime = millis(); // Reset the start time to stay awake longer
    } else {
    #endif
      log::toAll("Going to sleep in 5 seconds...");
      
      // Flush any pending data
      consLog.flush();
    #ifdef WEBSERIAL
      WebSerial.flush();
    #endif
      
      // Give time for final communications
      delay(5000);
      
      // Enter deep sleep
      log::toAll("Entering deep sleep for " + String(TIME_TO_SLEEP) + " seconds");
      esp_deep_sleep_start();
      // Code after this point will not be executed
    #if defined(WIFI) || defined(NETWIZARD)
    }
    #endif
  }
#endif // DEEPSLEEP
} // loop()
