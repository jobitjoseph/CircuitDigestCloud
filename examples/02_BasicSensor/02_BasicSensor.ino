// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 02: Basic Sensor
// Publishes a dummy temperature float to the dashboard every 5 seconds.

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
// Slot for each variable is shown next to it on the dashboard (a catalog key, e.g. "temperature-1").
const char *TEMPERATURE_SLOT = "temperature-1";
// ---------------------------------------------------------------------------

// Anedya MQTT requires TLS (port 8883) — use a secure client.
WiFiClientSecure net;
CircuitDigestCloud cd(net);

// Re-apply TLS config after the library stops the transport between connects —
// WiFiClientSecure loses setInsecure()/setCACert() on stop(), so without this the
// next TLS handshake fails (PubSubClient state=-2).
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

  // Dev convenience: skip TLS certificate validation. For production, pin the
  // Anedya root CA with net.setCACert(...) instead.
  net.setInsecure();
  cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial
  // cd.setRegion("ap-in-1");      // default region; change if your project differs
  // cd.setBufferSize(512);        // PubSubClient buffer (default 512, min 256)

  // registerVariable(name, type, slot) — maps your friendly name to the dashboard slot.
  // Types: CD_AUTO | CD_INT | CD_FLOAT | CD_BOOL | CD_STRING | CD_ENUM
  cd.registerVariable("temperature", CD_FLOAT, TEMPERATURE_SLOT);

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin(); // validates credentials; connection starts on first loop()
}

void loop() {
  cd.loop(); // drives connection + MQTT pump — call every iteration

  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    float t = 24.0f + (millis() % 1000) / 1000.0f; // dummy reading

    // publishVariable(name, value, retain) — published to the variable's slot.
    cd.publishVariable("temperature", t);
  }
}
