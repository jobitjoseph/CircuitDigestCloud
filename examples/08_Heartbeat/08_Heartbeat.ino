// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 08: Heartbeat
// A heartbeat is a tiny, dataless "I'm alive" signal that keeps the device shown
// as ONLINE on the dashboard. It is distinct from the MQTT keepalive ping.
//
// The library sends one automatically on connect and then every
// setHeartbeatInterval() seconds (default 60s) — so for most sketches you do
// NOTHING. This example just shows the cadence and an extra manual beat.

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
const char *DEVICE_ID = "your-device-id-here";      // Physical Device ID (device setup panel)
const char *CONNECTION_KEY = "your-connection-key"; // Connection Key (device setup panel)
// ---------------------------------------------------------------------------

WiFiClientSecure net;
CircuitDigestCloud cd(net);

void resetTransport() {
  net.stop();
  net.setInsecure();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  net.setInsecure();
  cd.setTransportResetCallback(resetTransport);

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial);

  // Auto heartbeat cadence (also the MQTT keepalive). Default is 60s.
  // Floor is 5s (Anedya counts heartbeats per 5s window). Pass 0 to disable auto.
  cd.setHeartbeatInterval(30);

  cd.begin(); // auto-heartbeat begins once connected
}

void loop() {
  cd.loop(); // drives the connection + sends the automatic heartbeat

  // Optional: an extra on-demand beat (e.g. right after finishing a task).
  static uint32_t last = 0;
  if (cd.connected() && millis() - last > 120000) {
    last = millis();
    cd.heartbeat(); // MQTT beat over the existing connection

    // For a sketch NOT running cd.loop() (no MQTT), use the HTTP form with a
    // separate TLS client instead:
    //   WiFiClientSecure https; https.setInsecure();
    //   cd.heartbeat(https);
  }
}
