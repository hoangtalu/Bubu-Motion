#pragma once
#include <Arduino.h>

enum class WifiState {
    OFF,
    PROVISIONING,
    CONNECTING,
    CONNECTED,
    FAILED
};

void wifiInit();                     // initialize internal state (does NOT connect)
void wifiStart(bool allowProvisionFallback = true);  // begin provisioning (AP + portal)
void wifiAutoConnectKnown();         // boot-time: scan + connect to known SSID if visible
void wifiStop();                     // disconnect Wi-Fi
void wifiUpdate();                   // non-blocking state update (call in loop)
bool wifiIsProvisioning();           // captive portal is active
WifiState wifiGetState();             // current Wi-Fi state
const char* wifiGetIp();              // returns IP string or ""
