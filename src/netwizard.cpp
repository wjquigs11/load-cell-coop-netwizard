
#ifdef NETWIZARD
#ifndef WIFI
#include "include.h"

// Initialize NetWizard
NetWizard NW(&server);

// Setup configuration parameters
//NetWizardParameter nw_header(&NW, NW_HEADER, "MQTT");
//NetWizardParameter nw_divider1(&NW, NW_DIVIDER);

bool setupWifi(void) {
  Serial.println("Starting wifi...");

  // ----------------------------
  // Configure NetWizard Strategy
  // ----------------------------
  // BLOCKING - Connect to WiFi and wait till portal is active
  // (blocks execution after autoConnect)
  // 
  // NON_BLOCKING - Connect to WiFi and proceed while portal is active in background
  // (does not block execution after autoConnect)
  NW.setStrategy(NetWizardStrategy::BLOCKING);

  // Listen for connection status changes
  NW.onConnectionStatus([](NetWizardConnectionStatus status) {
    String status_str = "";

    switch (status) {
      case NetWizardConnectionStatus::DISCONNECTED:
        status_str = "Disconnected";
        break;
      case NetWizardConnectionStatus::CONNECTING:
        status_str = "Connecting";
        break;
      case NetWizardConnectionStatus::CONNECTED:
        status_str = "Connected";
        break;
      case NetWizardConnectionStatus::CONNECTION_FAILED:
        status_str = "Connection Failed";
        break;
      case NetWizardConnectionStatus::CONNECTION_LOST:
        status_str = "Connection Lost";
        break;
      case NetWizardConnectionStatus::NOT_FOUND:
        status_str = "Not Found";
        break;
      default:
        status_str = "Unknown";
    }

    Serial.printf("NW connection status changed: %s\n", status_str.c_str());
    if (status == NetWizardConnectionStatus::CONNECTED) {
      // Local IP
      Serial.printf("Local IP: %s\n", NW.localIP().toString().c_str());
      // Gateway IP
      Serial.printf("Gateway IP: %s\n", NW.gatewayIP().toString().c_str());
      // Subnet mask
      Serial.printf("Subnet mask: %s\n", NW.subnetMask().toString().c_str());
    }
  });

  // Listen for portal state changes
  NW.onPortalState([](NetWizardPortalState state) {
    String state_str = "";

    switch (state) {
      case NetWizardPortalState::IDLE:
        state_str = "Idle";
        break;
      case NetWizardPortalState::CONNECTING_WIFI:
        state_str = "Connecting to WiFi";
        break;
      case NetWizardPortalState::WAITING_FOR_CONNECTION:
        state_str = "Waiting for Connection";
        break;
      case NetWizardPortalState::SUCCESS:
        state_str = "Success";
        break;
      case NetWizardPortalState::FAILED:
        state_str = "Failed";
        break;
      case NetWizardPortalState::TIMEOUT:
        state_str = "Timeout";
        break;
      default:
        state_str = "Unknown";
    }

    Serial.printf("NW portal state changed: %s\n", state_str.c_str());
  });

  NW.onConfig([&]() {
    Serial.println("NW onConfig Received");
    return true; // <-- return true to approve request, false to reject
  });

  // Start NetWizard
  Serial.println("starting autoConnect");
  NW.autoConnect("COOPFEEDER", "");
  
  // Check if configured
  if (NW.isConfigured()) {
    Serial.println("Device is configured");
  } else {
    Serial.println("Device is not configured");
  }

  // Start WebServer
  server.begin();
  return true;
}

// reset wifi (after double reset detected)
void resetWifi() {
  // Clear WiFi settings from preferences directly instead of using NW.reset()
  // This avoids the "STA not started" error that occurs when NW.reset() is called
  // before WiFi is properly initialized
  NW.erase();
  NW.autoConnect("COOPFEEDER", "");
  preferences.putBool("DRD", false);

  // Log the action
  log::toAll("WiFi settings cleared, restarting device...");
  
  // Small delay to ensure log message is sent
  //delay(1000);
  
  // Restart the ESP32
  //ESP.restart();
}
#endif
#endif