// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// CircuitDigestCloud — Example 07: Color Picker (RGB LED)
// Receives a color from the dashboard's Color Picker widget and drives a common
// RGB LED. The widget sends a packed 24-bit RGB integer: (r << 16) | (g << 8) | b
// (i.e. 0xRRGGBB, range 0–16777215). Brightness is already baked into the value.
//
// Because it's a plain number it arrives as CD_INT on a *float* slot — read it
// with v.asInt() and unpack the channels. (Older firmware that sent a "#RRGGBB"
// hex string used a status slot; this is the current integer format.)

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
const char *DEVICE_ID = "your-device-id-here";       // Physical Device ID (device setup panel)
const char *CONNECTION_KEY = "your-connection-key";  // Connection Key (device setup panel)
const char *COLOR_SLOT  = "color-1";  // Color Picker control slot (color-1 … color-10)
// ---------------------------------------------------------------------------

// Common-anode/-cathode RGB LED pins (PWM-capable). Adjust to your wiring.
// analogWrite() needs ESP32 Arduino core 3.0+ (all ESP8266 cores have it); for a
// WS2812/NeoPixel strip, feed r/g/b into your LED library instead.
#define RED_PIN 25
#define GREEN_PIN 26
#define BLUE_PIN 27

WiFiClientSecure net;
CircuitDigestCloud cd(net);

// Re-apply TLS config after the library stops the transport between connects.
// WiFiClientSecure loses setInsecure()/setCACert() on stop(), so without this the
// next TLS handshake fails (PubSubClient state=-2). Required for reliable reconnect.
void resetTransport() {
  net.stop();
  net.setInsecure(); // dev only — pin the Anedya CA for production
}

// Control callback — the Color Picker sends a packed 24-bit RGB integer.
void handleColor(const char *var, CDValue v) {
  long c = v.asInt();           // 0xRRGGBB
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >> 8) & 0xFF;
  uint8_t b = c & 0xFF;

  analogWrite(RED_PIN, r);
  analogWrite(GREEN_PIN, g);
  analogWrite(BLUE_PIN, b);

  Serial.printf("color → #%02X%02X%02X (R%u G%u B%u)\n", r, g, b, r, g, b);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  net.setInsecure(); // dev only — pin the Anedya CA for production
  cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

  cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
  cd.setDebug(&Serial); // prints debug messages to Serial

  // onChange(name, cb, ackMode, type, slot) — CD_INT on a float slot.
  cd.onChange("color", handleColor, CD_ACK_AUTO, CD_INT, COLOR_SLOT);

  // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
  // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
  cd.begin();
}

void loop() { cd.loop(); }
