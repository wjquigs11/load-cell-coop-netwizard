#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>

#ifdef DEEPSLEEP
// ESP32 deep sleep includes
#include "esp_system.h"
#include "esp_sleep.h"
#endif

#if defined(WIFI) || defined(NETWIZARD)
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
extern AsyncWebServer server;
extern AsyncEventSource events;
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
bool hasAnyWebClients();
extern int wsClientCount;
extern int esClientCount;
#endif

#ifdef WEBSERIAL
#include <WebSerial.h>
void WebSerialonMessage(uint8_t *data, size_t len);
#endif
#ifdef ELEGANTOTA
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>
#endif
#ifdef NETWIZARD
// for some reason this doesn't work; need to define in platformio.ini
#define NETWIZARD_USE_ASYNC_WEBSERVER 1
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
#include <time.h>
#define DEFDELAY 1000
extern unsigned long lastTime;
extern int timerDelay;
extern int minReadRate;
// store last update based on clock time from client browser
extern time_t lastUpdate, updateTime;
#define PRBUF 128
extern char prbuf[];

#ifdef DEEPSLEEP
// Deep sleep variables
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60       /* Time ESP32 will go to sleep (in seconds) */
extern int awakeTimer;          /* Time to stay awake before going to sleep (in seconds) */
extern unsigned long startTime; /* Time when the device started */

// Function to print the wakeup reason
void print_wakeup_reason();
#else
// No deep sleep, but we still need some of these variables for compatibility
extern unsigned long startTime; /* Time when the device started */
#endif