#include "include.h"
#include "logto.h"

extern bool serverStarted;

bool log::logToSerial = true;  // Definition

void log::toAll(String s) {
  if (logToSerial) {
    if (s.endsWith("\n")) s.remove(s.length() - 1);
    Serial.println(s);
    consLog.println(s);
#ifdef WEBSERIAL
    if (serverStarted) {
      WebSerial.print(s);
    }
#endif
    s = String();
  }
}
