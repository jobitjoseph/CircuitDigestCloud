// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 03: Basic Control
// Receives a boolean control "light_1" from the dashboard and drives LED_BUILTIN.

#if defined(ESP32)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#else
#error                                                                         \
    "This example targets ESP32 or ESP8266. The library supports any Arduino-core board with a TLS Client."
#endif
#include <CircuitDigestCloud.h>

// ---- FILL ME IN ------------------------------------------------------------
const char *WIFI_SSID = "your_ssid";
const char *WIFI_PASS = "your_password";
const char *DEVICE_ID = "your-device-id-here";          // Physical Device ID (device setup panel)
const char *CONNECTION_KEY = "your-connection-key"; // Connection Key (device setup panel)
const char *LIGHT_SLOT  = "light-1";  // control variable slot (boolean catalog key)
// ---------------------------------------------------------------------------

WiFiClientSecure net;
CircuitDigestCloud cd(net);

// Re-apply TLS config after the library stops the transport between connects —
// WiFiClientSecure loses setInsecure()/setCACert() on stop(), so without this the
// next TLS handshake fails (PubSubClient state=-2).
void resetTransport() {
  net.stop();
  net.setInsecure(); // dev only — pin the Anedya CA for production
}

// Control callback — v.asBool() / v.asInt() / v.asFloat() / v.asString() / v.type()
void handleLight(const char *var, CDValue v) {
  digitalWrite(LED_BUILTIN, v.asBool() ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(LED_BUILTIN, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  net.setInsecure(); // dev only — pin the Anedya CA for production
  cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial

  // onChange(name, cb, ackMode, type, slot)
  cd.onChange("light_1", handleLight, CD_ACK_AUTO, CD_BOOL, LIGHT_SLOT);

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin();
}

void loop() { cd.loop(); }
