// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 01: Sensor + GPIO Control
// Publishes a dummy temperature sensor every 5 seconds.
// Receives a boolean control "gpio" to drive GPIO 2.

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
const char *DEVICE_ID = "your-devid-here";          // Physical Device ID (device setup panel)
const char *CONNECTION_KEY = "your-key-here"; // Connection Key (device setup panel)
// Slots are shown next to each variable on the dashboard.
const char *TEMPERATURE_SLOT = "temperature-1"; // sensor variable  — direction: input
const char *GPIO_SLOT         = "light-1";        // control variable — direction: output, type: boolean
                                                  // Dashboard: Add Variable (key "light-1", boolean, output)
                                                  //            Add Widget   → Toggle or Switch, metric_key "light-1"
// ---------------------------------------------------------------------------

#define GPIO_PIN 2

WiFiClientSecure net;
CircuitDigestCloud cd(net);

void resetTransport() {
  net.stop();
  net.setInsecure();
}

// Control callback — v.asBool() / v.asInt() / v.asFloat() / v.asString() / v.type()
void handleGpio(const char *var, CDValue v) {
  bool state = v.asBool();
  digitalWrite(GPIO_PIN, state ? HIGH : LOW);
  Serial.print("GPIO → ");
  Serial.println(state ? "ON" : "OFF");
  // CD_ACK_AUTO reports the value back automatically — no manual ack needed.
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(GPIO_PIN, OUTPUT);
  digitalWrite(GPIO_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println(" connected");

  net.setInsecure(); // dev only — pin the Anedya CA for production
  cd.setTransportResetCallback(resetTransport);

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial

  // Sensor: publishes readings when you call cd.publishVariable("temperature", temp)
  cd.registerVariable("temperature", CD_FLOAT, TEMPERATURE_SLOT);

  // Control: dashboard writes fire handleGpio. CD_ACK_AUTO publishes the new
  // value back so the dashboard widget confirms (and the slider/toggle stops blinking).
  cd.onChange("gpio", handleGpio, CD_ACK_AUTO, CD_BOOL, GPIO_SLOT);

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin(); // validates credentials; connection starts on first loop()
}

void loop() {
  cd.loop(); // drives connection + MQTT pump — call every iteration

  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();

    static float temp = 20.0f;
    temp += 0.5f;
    if (temp > 30.0f)
      temp = 20.0f;

    cd.publishVariable("temperature", temp);
  }
}
