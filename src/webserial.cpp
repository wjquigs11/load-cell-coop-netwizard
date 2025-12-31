#ifdef WEBSERIAL
#include "include.h"

#define PRBUF 128
char prbuf[PRBUF];

String formatMacAddress(const String& macAddress) {
  String result = "{";
  int len = macAddress.length();
  
  for (int i = 0; i < len; i += 3) {
    if (i > 0) {
      result += ", ";
    }
    result += "0x" + macAddress.substring(i, i + 2);
  }
  result += "};";
  return result;
}

String commandList[] = {"format", "restart", "ls", "hostname", "status", "wificonfig", "conslog", "log (on/off)", "note", "conslog (close/rm/open)", "timer (seconds)", "empty", "full"};
#define ASIZE(arr) (sizeof(arr) / sizeof(arr[0]))
String words[10]; // Assuming a maximum of 10 words

void WebSerialonMessage(uint8_t *data, size_t len) {
  Serial.printf("Received %lu bytes from WebSerial: ", len);
  Serial.write(data, len);
  Serial.println();
  //WebSerial.print("Received: ");
  String dataS = String((char*)data);
  // Split the String into an array of Strings using spaces as delimiters
  String words[10]; // Assuming a maximum of 10 words
  int wordCount = 0;
  int startIndex = 0;
  int endIndex = 0;
  while (endIndex != -1) {
    endIndex = dataS.indexOf(' ', startIndex);
    if (endIndex == -1) {
      words[wordCount++] = dataS.substring(startIndex);
    } else {
      words[wordCount++] = dataS.substring(startIndex, endIndex);
      startIndex = endIndex + 1;
    }
  }
  for (int i = 0; i < wordCount; i++) {
    int j;
    log::toAll(words[i]);
    if (words[i].equals("?")) {
      for (j = 1; j < ASIZE(commandList); j++) {
        log::toAll(String(j) + ":" + commandList[j]);
      }
      return;
    }
    if (words[i].equals("format")) {
      SPIFFS.format();
      log::toAll("SPIFFS formatted");
      return;
    }
    if (words[i].startsWith("conslog")) {
      consLog = SPIFFS.open("/console.log", "w", true);
      if (!consLog) {
        log::toAll("failed to open console log");
      }
      if (consLog.println("ESP console log.")) {
        log::toAll("console log restarted");
      } else {
        log::toAll("console log write failed");
      }
      return;
    }
    if (words[i].equals("restart")) {
      log::toAll("restarting...");
      ESP.restart();
    }
    if (words[i].equals("ls")) {
      File root = SPIFFS.open("/");
      File file = root.openNextFile();
      while (file) {
        log::toAll(file.name());
        file.close(); // Close the file after reading its name
        file = root.openNextFile();
      }
      root.close();
      return;
    }
    if (words[i].startsWith("host")) {
      if (!words[++i].isEmpty()) {
        host = words[i];
        preferences.putString("hostname", host);
        log::toAll("hostname set to " + host);
        log::toAll("restart to change hostname");
        log::toAll("preferences " + preferences.getString("hostname"));
      } else {
        log::toAll("hostname: " + host);
      }
      return;
    }
    if (words[i].equals("status")) {
      String buf = "";
      unsigned long uptime = millis() / 1000;
      log::toAll("      uptime: " + String(uptime));
      log::toAll(" current raw: " + String(LoadCell.read()));
      log::toAll("empty offset: " + String(empty_offset));
      log::toAll(" full offset: " + String(full_raw));
      buf = String();
      return;
    }
    if (words[i].startsWith("wifi")) {
      String buf = "hostname: " + host;
      buf += " wifi: " + WiFi.SSID();
      buf += " ip: " + WiFi.localIP().toString();
      buf += "  MAC addr: " + formatMacAddress(WiFi.macAddress());
      log::toAll(buf);
      buf = String();
      return;
    }
    if (words[i].startsWith("log")) {
      log::logToSerial = !log::logToSerial;
      log::toAll("serial log: " + String(log::logToSerial ? "on" : "off"));
      return;
    }
    if (words[i].startsWith("note")) {
      if (wordCount > 1)
        log::toAll("note: " + words[++i]);
      return;
    }    
    if (words[i].startsWith("timer")) {
      if (wordCount > 1) {
        // argument is seconds, timerDelay is msec
        timerDelay = atoi(words[++i].c_str())*1000;
        if (timerDelay < 200) timerDelay = 200;
        preferences.putInt("timerdelay", timerDelay);
      }
      log::toAll("timer: " + String(timerDelay) + " msecs");
      return;
    }
    /*
    empty/full command processing:
    empty ? shows current tare offset value
    empty (number) sets current tare offset value
    empty <CR> sets current tare offset value based on loadcell.tare()
    */
    if (words[i].startsWith("empty")) {
      if (wordCount > 1) {
        if (!words[++i].equals("?")) {
          empty_offset = atol(words[i].c_str());
          preferences.putLong("empty_offset", empty_offset);
        }
        log::toAll("empty calibration = " + String(empty_offset));
      } else {
        configTare("empty");
        log::toAll("empty calibration set");
      }
      return;
    }
    if (words[i].startsWith("full")) {
      if (wordCount > 1) {
        if (!words[++i].equals("?")) {
          long l;
          if ((l = atol(words[i].c_str())) > 0) {
            full_raw = l;
            preferences.putLong("full_raw", full_raw);
          }
        }
        log::toAll("full offset = " + String(full_raw));
      } else {
        configTare("full");
        log::toAll("full calibration set");
      }
      return;
    }
    log::toAll("Unknown command: " + words[i]);
  }
  for (int i=0; i<wordCount; i++) words[i] = String();
  dataS = String();
}
#endif