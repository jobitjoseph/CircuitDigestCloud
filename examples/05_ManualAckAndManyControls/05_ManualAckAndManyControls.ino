// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 05: Manual Ack & Many Controls
// relay_1 and relay_2 use CD_ACK_MANUAL — ack is sent with the actual GPIO
// read-back. fan uses CD_ACK_AUTO. Global fallback handles any unknown controls.
// Slots are predefined catalog keys (boolean keys here) from the device setup panel.

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

#define RELAY1_PIN 26
#define RELAY2_PIN 27

WiFiClientSecure net;
CircuitDigestCloud cd(net);

// Re-apply TLS config after the library stops the transport between connects —
// WiFiClientSecure loses setInsecure()/setCACert() on stop(), so without this the
// next TLS handshake fails (PubSubClient state=-2).
void resetTransport() {
  net.stop();
  net.setInsecure(); // dev only — pin the Anedya CA for production
}

// CD_ACK_MANUAL: you must call cd.ackChange() yourself.
// Best practice: read back actual GPIO state so the dashboard reflects reality.
void handleRelay1(const char *var, CDValue v) {
  digitalWrite(RELAY1_PIN, v.asBool() ? HIGH : LOW);
  bool actual = digitalRead(RELAY1_PIN) == HIGH;
  cd.ackChange("relay_1", actual); // ack with real state
}

void handleRelay2(const char *var, CDValue v) {
  digitalWrite(RELAY2_PIN, v.asBool() ? HIGH : LOW);
  bool actual = digitalRead(RELAY2_PIN) == HIGH;
  cd.ackChange("relay_2", actual);
}

// CD_ACK_AUTO: library reports the value automatically after callback.
void handleFan(const char *var, CDValue v) {
  Serial.print("fan → ");
  Serial.println(v.asBool() ? "ON" : "OFF");
}

// Global fallback — fires for any control not registered with onChange().
void handleUnknown(const char *var, CDValue v) {
  Serial.print("[fallback] unknown control: ");
  Serial.println(var);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  net.setInsecure(); // dev only — pin the Anedya CA for production
  cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial

  // onChange(name, cb, ackMode, type, slot) — catalog keys from the dashboard.
  cd.onChange("relay_1", handleRelay1, CD_ACK_MANUAL, CD_BOOL, "relay-1");
  cd.onChange("relay_2", handleRelay2, CD_ACK_MANUAL, CD_BOOL, "relay-2");
  cd.onChange("fan", handleFan, CD_ACK_AUTO, CD_BOOL, "fan-1");

  // Global fallback — one allowed; pass nullptr to clear.
  cd.onChange(handleUnknown);

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin();
}

void loop() { cd.loop(); }
