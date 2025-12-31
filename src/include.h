#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>

#if defined(WIFI) || defined(NETWIZARD)
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#ifdef ELEGANTOTA_USE_ASYNC_WEBSERVER
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
extern AsyncWebServer server;
extern AsyncEventSource events;
#else
// not possible because there is no EventSource in non-async ESP32 WebServer library
#include <WebServer.h>
extern WebServer server;
//extern EventSource events;
#endif // ELEGANTOTA_USE_ASYNC_WEBSERVER
extern bool serverStarted;
extern String host;
extern JsonDocument readings;
extern int timerDelay;
#define HTTP_PORT 80
#define DRD_TIMEOUT 10
bool setupWifi();
void resetWifi();
void startWebServer();
String getSensorReadings();
#endif

#ifdef WEBSERIAL
#include <WebSerialPro.h>
void WebSerialonMessage(uint8_t *data, size_t len);
#endif
#ifdef ELEGANTOTA
#include <ElegantOTA.h>
#endif
#ifdef NETWIZARD
#include <NetWizard.h>
extern NetWizard NW;
#endif

#include "logto.h"

extern Preferences preferences;
extern File consLog;

// weight value from load cell
extern long loadcell;
extern long empty_offset;
extern long full_raw;

void configTare(const String& type);

// Timer variables
#define DEFDELAY 1000
extern unsigned long lastTime;
extern int timerDelay;
extern int minReadRate;
// store last update based on clock time from client browser
extern unsigned long lastUpdate, updateTime;

#include <HX711.h>
extern HX711 LoadCell;