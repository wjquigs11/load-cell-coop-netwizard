#if defined(WIFI) || defined(NETWIZARD)
#include "include.h"

AsyncWebServer server(HTTP_PORT);
AsyncEventSource events("/events");
AsyncWebSocket ws("/ws");
bool serverStarted = false;
JsonDocument readings;
JsonDocument browserTimeData;
String host = "coopfeederBETA";
time_t lastUpdate, updateTime;
int wsClientCount = 0;
int esClientCount = 0;

// Function to check if any web clients are connected
bool hasAnyWebClients() {
  // Check WebSocket clients
  if (wsClientCount > 0) {
    return true;
  }
  
  // Check EventSource clients
  if (esClientCount > 0) {
    return true;
  }
  
  // WebSerial clients can't be directly checked with the current API
  // but we can assume they're connected if they're sending messages
  
  return false;
}

String getSensorReadings() {
  //readings["sensor"] = "0";
  String jsonString;
  serializeJson(readings,jsonString);
  return jsonString;
}

String processor(const String& var) {
  Serial.println(var);
  if(var == "TIMERDELAY") {
    return String(timerDelay);
  }
  return String();
}

void startWebServer() {
  log::toAll("starting web server");

  // Log IP address information
  if (WiFi.status() == WL_CONNECTED) {
    log::toAll("Server will be available at: " + WiFi.localIP().toString());
  } else {
    log::toAll("WiFi not connected in STA mode");
  }
  
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    log::toAll("AP IP: " + WiFi.softAPIP().toString());
  }

  // start serving from SPIFFS
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  // for .js .css etc
  server.serveStatic("/", SPIFFS, "/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    log::toAll("index.html");
    request->send(SPIFFS, "/index.html", "text/html", false, processor);
  });

  // Request latest sensor readings
  server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", getSensorReadings());
    // Add CORS headers
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
  });

  server.on("/host", HTTP_GET, [](AsyncWebServerRequest *request) {
    String buf = "hostname: " + host;
    buf += ", ESP local MAC addr: " + String(WiFi.macAddress());
    log::toAll(buf);
    request->send(200, "text/plain", buf.c_str());
    buf = String();
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    log::toAll("config:");
    String response = "none";
    if (request->hasParam("hostname")) {
      Serial.printf("hostname %s", request->getParam("hostname")->value().c_str());
      host = request->getParam("hostname")->value();
      response = "change hostname to " + host;
      log::toAll(response);
      preferences.putString("hostname",host);
      log::toAll("preferences " + preferences.getString("hostname", "unknown") + "\n");
    } else if (request->hasParam("webtimer")) {
      timerDelay = atoi(request->getParam("webtimer")->value().c_str());
      if (timerDelay < 0) timerDelay = DEFDELAY;
      if (timerDelay > 10000) timerDelay = 10000;
      response = "change web timer to " + String(timerDelay);
      log::toAll(response);
      preferences.putInt("timerdelay",timerDelay);
    } else if (request->hasParam("empty")) {
      configTare("empty");
      response = "empty calibration successful, empty raw offset is " + String(empty_offset);
      log::toAll(response);
    } else if (request->hasParam("full")) {
      configTare("full");
      response = "full calibration successful, full raw offset is " + String(full_raw);
      log::toAll(response);
    }
    request->send(200, "text/plain", response.c_str());
    response = String();
  });

  // Weight endpoint for feed weight monitoring
  server.on("/weight", HTTP_GET, [](AsyncWebServerRequest *request) {
    String loadcellStr = String(loadcell);
    log::toAll("Weight request: " + loadcellStr);
    request->send(200, "text/plain", loadcellStr);
    loadcellStr = String();
  });
  
  // Endpoint to receive browser time

  // Endpoint to receive browser time
  server.on("/browsertime", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Just acknowledge the request
    request->send(200, "text/plain", "Time received");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Handle the POST data
    if (len) {
      data[len] = 0; // Null terminate the data
      String jsonData = String((char*)data);
      
      // Parse the JSON data
      DeserializationError error = deserializeJson(browserTimeData, jsonData);
      
      if (!error) {
        // Log the received time data
        String localTime = browserTimeData["localTime"].as<String>();
        String timezone = browserTimeData["timezone"].as<String>();
        int offset = browserTimeData["offset"].as<int>();
        
        log::toAll("Browser time received: " + localTime);
        log::toAll("Browser timezone: " + timezone);
        log::toAll("Browser UTC offset (minutes): " + String(offset));
        
        // Store the browser's timestamp (milliseconds since epoch)
        updateTime = browserTimeData["timestamp"].as<unsigned long>();
        log::toAll("Browser timestamp (ms): " + String(updateTime));
        
        // Initialize lastUpdate with the current browser time
        lastUpdate = updateTime;
        
        //preferences.putString("browserTimezone", timezone);
      } else {
        log::toAll("Error parsing browser time JSON");
      }
    }
  });

  // Set up EventSource connection handler
  events.onConnect([](AsyncEventSourceClient *client){
    esClientCount++;
    if(client->lastId()){
      log::toAll("EventSource client reconnected! Last message ID: " + String(client->lastId()));
    } else {
      log::toAll("New EventSource client connected. Total clients: " + String(esClientCount));
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 1000);
  });
  
  // Set up WebSocket event handler
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
      case WS_EVT_CONNECT:
        wsClientCount++;
        log::toAll("WebSocket client connected. IP: " + client->remoteIP().toString() + ", ID: " + String(client->id()) + ". Total clients: " + String(wsClientCount));
        break;
      case WS_EVT_DISCONNECT:
        wsClientCount--;
        log::toAll("WebSocket client disconnected. ID: " + String(client->id()) + ". Remaining clients: " + String(wsClientCount));
        break;
      case WS_EVT_ERROR:
        log::toAll("WebSocket error: client ID " + String(client->id()));
        break;
      case WS_EVT_DATA:
        // Handle data if needed
        break;
    }
  });
  
  server.addHandler(&events);
  server.addHandler(&ws);
  
  // Begin server
  server.begin();
  log::toAll("Web server started successfully");
  serverStarted = true;

  // Handle OPTIONS preflight requests for CORS
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(204);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      response->addHeader("Access-Control-Max-Age", "3600");
      request->send(response);
      return;
    }
    // Handle not found for other requests
    request->send(404);
  });
}
#endif