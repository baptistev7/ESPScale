#pragma once
#include "Arduino.h"
enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct IPAddress {
    String toString() const { return "192.168.4.1"; }
};
struct WiFiStub {
    int16_t scanNetworks(bool = false, bool = false) { return 0; }
    String SSID(int) { return ""; }
    int32_t RSSI(int) { return 0; }
    int encryptionType(int) { return 0; }
    int32_t channel(int) { return 0; }
    void scanDelete() {}
    void mode(int) {}
    void persistent(bool) {}
    void setHostname(const char*) {}
    bool softAP(const char*, const char* = nullptr) { return true; }
    bool softAPdisconnect(bool) { return true; }
    IPAddress softAPIP() { return {}; }
    IPAddress localIP() { return {}; }
    void begin(const char*, const char*) {}
    void disconnect(bool) {}
    int status() { return WL_DISCONNECTED; }
};
extern WiFiStub WiFi;
