// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 04: Variable Types
// Sensors : temperature (CD_FLOAT), count (CD_INT), presence (CD_BOOL)
// Controls: setpoint (CD_FLOAT, auto-ack), relay (CD_BOOL, auto-ack)
//
// Slots are predefined catalog keys (e.g. "temperature-1", "light-1") shown on the
// dashboard's device setup panel. Float/int map to float keys; bool maps to a
// boolean key. (String/enum controls need a status-type slot, which the catalog
// does not expose yet — the Color Picker example sends color as an integer instead.)

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

float g_setpoint = 25.0f;
bool g_relay = false;

void handleSetpoint(const char *var, CDValue v) {
  g_setpoint = v.asFloat();
  Serial.print("setpoint → ");
  Serial.println(g_setpoint);
}

void handleRelay(const char *var, CDValue v) {
  g_relay = v.asBool();
  Serial.print("relay → ");
  Serial.println(g_relay ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  net.setInsecure(); // dev only — pin the Anedya CA for production
  cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial

  // registerVariable(name, type, slot) — catalog keys from the dashboard.
  cd.registerVariable("temperature", CD_FLOAT, "temperature-1");
  cd.registerVariable("count", CD_INT, "count-1");
  cd.registerVariable("presence", CD_BOOL, "motion-1");

  // onChange(name, cb, ackMode, type, slot)
  cd.onChange("setpoint", handleSetpoint, CD_ACK_AUTO, CD_FLOAT, "setpoint-1");
  cd.onChange("relay", handleRelay, CD_ACK_AUTO, CD_BOOL, "relay-1");

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin();
}

void loop() {
  cd.loop();

  static uint32_t last = 0;
  static int count = 0;
  if (millis() - last > 5000) {
    last = millis();

    cd.publishVariable("temperature", 22.5f + count * 0.1f);
    cd.publishVariable("count", count++);
    cd.publishVariable("presence", (count % 2 == 0));
  }
}
